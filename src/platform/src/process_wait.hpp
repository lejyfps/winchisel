#pragma once

#include <windows.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

namespace winchisel::platform::detail {

inline constexpr std::size_t k_max_captured_output = 4 * 1024 * 1024;

struct ProcessWaitResult {
    DWORD exit_code{};
    bool timed_out{};
};

inline HANDLE attach_kill_job(HANDLE process) {
    const auto job = CreateJobObjectW(nullptr, nullptr);
    if (!job) return nullptr;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) ||
        !AssignProcessToJobObject(job, process)) {
        CloseHandle(job);
        return nullptr;
    }
    return job;
}

inline ProcessWaitResult wait_process(HANDLE process, DWORD timeout_ms) {
    const auto job = attach_kill_job(process);
    if (WaitForSingleObject(process, timeout_ms) == WAIT_TIMEOUT) {
        if (job) TerminateJobObject(job, ERROR_TIMEOUT); else TerminateProcess(process, ERROR_TIMEOUT);
        WaitForSingleObject(process, 5000);
        if (job) CloseHandle(job);
        return {ERROR_TIMEOUT, true};
    }
    DWORD code{};
    const auto result = GetExitCodeProcess(process, &code) ? ProcessWaitResult{code, false}
                                                            : ProcessWaitResult{GetLastError(), false};
    if (job) CloseHandle(job);
    return result;
}

template <typename Consumer>
ProcessWaitResult wait_process_with_pipe(HANDLE process, HANDLE pipe, DWORD timeout_ms, Consumer&& consume) {
    const auto job = attach_kill_job(process);
    const auto started = GetTickCount64();
    std::array<char, 4096> buffer{};
    const auto stop = [&](DWORD code, bool timed_out) {
        if (job) TerminateJobObject(job, code); else TerminateProcess(process, code);
        WaitForSingleObject(process, 5000);
        if (job) CloseHandle(job);
        return ProcessWaitResult{code, timed_out};
    };
    for (;;) {
        const auto status = WaitForSingleObject(process, 0);
        if (status == WAIT_FAILED) return stop(GetLastError(), false);
        const bool process_done = status == WAIT_OBJECT_0;
        if (GetTickCount64() - started >= timeout_ms) return stop(ERROR_TIMEOUT, true);
        DWORD available{};
        if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr)) {
            const auto code = GetLastError();
            if (code != ERROR_BROKEN_PIPE) return stop(code, false);
        }
        if (available) {
            DWORD read{};
            const auto requested = std::min<DWORD>(available, static_cast<DWORD>(buffer.size()));
            if (!ReadFile(pipe, buffer.data(), requested, &read, nullptr)) return stop(GetLastError(), false);
            if (read) consume(buffer.data(), read);
            continue;
        }
        if (process_done) break;
        WaitForSingleObject(process, 20);
    }
    DWORD code{};
    const auto result = GetExitCodeProcess(process, &code) ? ProcessWaitResult{code, false}
                                                            : ProcessWaitResult{GetLastError(), false};
    if (job) CloseHandle(job);
    return result;
}

inline std::wstring first_command_token(std::wstring const& command) {
    std::size_t i{};
    while (i < command.size() && (command[i] == L' ' || command[i] == L'\t')) ++i;
    if (i >= command.size()) return {};
    if (command[i] == L'"') {
        ++i;
        std::wstring out;
        while (i < command.size() && command[i] != L'"') out.push_back(command[i++]);
        return out;
    }
    std::wstring out;
    while (i < command.size() && command[i] != L' ' && command[i] != L'\t') out.push_back(command[i++]);
    return out;
}

inline std::wstring system32_join(std::wstring_view leaf) {
    wchar_t dir[MAX_PATH]{};
    const auto n = GetSystemDirectoryW(dir, static_cast<UINT>(std::size(dir)));
    if (!n || n >= std::size(dir)) return {};
    std::wstring path(dir, n);
    if (path.back() != L'\\') path.push_back(L'\\');
    path.append(leaf);
    return path;
}

inline std::wstring leaf_name(std::wstring const& token) {
    const auto slash = token.find_last_of(L"\\/");
    return slash == std::wstring::npos ? token : token.substr(slash + 1);
}

inline std::wstring newest_winget_under(std::wstring const& root) {
    const auto pattern = root + L"\\Microsoft.DesktopAppInstaller_*";
    WIN32_FIND_DATAW data{};
    const auto find = FindFirstFileW(pattern.c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) return {};
    std::wstring best;
    FILETIME best_write{};
    do {
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) continue;
        auto exe = root + L'\\' + data.cFileName + L"\\winget.exe";
        const auto attr = GetFileAttributesW(exe.c_str());
        if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (best.empty() || CompareFileTime(&data.ftLastWriteTime, &best_write) > 0) {
            best = std::move(exe);
            best_write = data.ftLastWriteTime;
        }
    } while (FindNextFileW(find, &data));
    FindClose(find);
    return best;
}

inline std::wstring resolve_winget() {
    wchar_t program_files[MAX_PATH]{};
    if (GetEnvironmentVariableW(L"ProgramFiles", program_files, static_cast<DWORD>(std::size(program_files)))) {
        if (auto found = newest_winget_under(std::wstring(program_files) + L"\\WindowsApps"); !found.empty())
            return found;
    }
    wchar_t local[MAX_PATH]{};
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", local, static_cast<DWORD>(std::size(local)))) {
        if (auto found = newest_winget_under(std::wstring(local) + L"\\Microsoft\\WindowsApps"); !found.empty())
            return found;
    }
    return {};
}

inline std::wstring resolve_application(std::wstring const& command) {
    const auto token = first_command_token(command);
    if (token.empty()) return {};
    const auto leaf = leaf_name(token);
    if (leaf.empty() || leaf.find(L'.') == std::wstring::npos) return {};
    if (_wcsicmp(leaf.c_str(), L"winget.exe") == 0) return resolve_winget();
    if (token.find_first_of(L"\\/") != std::wstring::npos) {
        std::error_code error;
        auto path = std::filesystem::weakly_canonical(std::filesystem::path(token), error);
        if (error || !path.is_absolute() || _wcsicmp(path.extension().c_str(), L".exe") != 0) return {};
        if (!std::filesystem::is_regular_file(path, error) || error) return {};
        return path.wstring();
    }
    auto full = system32_join(leaf);
    if (full.empty()) return {};
    const auto attr = GetFileAttributesW(full.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY)) return {};
    return full;
}

inline BOOL start_hidden_process(std::wstring& command, BOOL inherit, STARTUPINFOW& startup,
    PROCESS_INFORMATION& process) {
    const auto application = resolve_application(command);
    if (application.empty()) {
        SetLastError(ERROR_FILE_NOT_FOUND);
        return FALSE;
    }
    return CreateProcessW(application.c_str(), command.data(), nullptr, nullptr, inherit, CREATE_NO_WINDOW,
        nullptr, nullptr, &startup, &process);
}

inline std::pair<ProcessWaitResult, std::string> run_captured(std::wstring command, DWORD timeout_ms = 30 * 60 * 1000) {
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    HANDLE read{}, write{};
    if (!CreatePipe(&read, &write, &security, 0)) return {{GetLastError(), false}, {}};
    SetHandleInformation(read, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    startup.hStdOutput = write;
    startup.hStdError = write;
    PROCESS_INFORMATION process{};
    if (!start_hidden_process(command, TRUE, startup, process)) {
        const auto code = GetLastError();
        CloseHandle(read);
        CloseHandle(write);
        return {{code, false}, {}};
    }
    CloseHandle(write);
    std::string output;
    const auto waited = wait_process_with_pipe(process.hProcess, read, timeout_ms, [&](char const* data, DWORD size) {
        if (output.size() < k_max_captured_output)
            output.append(data, std::min<std::size_t>(size, k_max_captured_output - output.size()));
    });
    CloseHandle(read);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return {waited, std::move(output)};
}

} // namespace winchisel::platform::detail
