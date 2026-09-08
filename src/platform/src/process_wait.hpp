#pragma once

#include <windows.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
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
    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
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
