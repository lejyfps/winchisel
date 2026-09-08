#include "winchisel/platform/system.hpp"
#include "process_wait.hpp"

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <SrRestorePtApi.h>

#include <fstream>
#include <array>
#include <sstream>
#include <string>
#include <vector>
#include <regex>
#include <chrono>
#include <cctype>
#include <iomanip>
#include <mutex>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "srclient.lib")

namespace winchisel::platform {
namespace {

std::filesystem::path exe_path() {
    std::wstring buf(MAX_PATH, L'\0');
    for (;;) {
        const DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (n == 0) {
            return {};
        }
        if (n < buf.size() - 1) {
            buf.resize(n);
            return buf;
        }
        buf.resize(buf.size() * 2);
    }
}

}  // namespace

std::uint32_t current_windows_build() {
    HKEY key{};
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", 0, KEY_READ, &key) !=
        ERROR_SUCCESS) {
        return 0;
    }
    wchar_t text[32]{};
    DWORD size = sizeof(text);
    DWORD type = 0;
    const auto status = RegQueryValueExW(key, L"CurrentBuildNumber", nullptr, &type, reinterpret_cast<LPBYTE>(text), &size);
    RegCloseKey(key);
    if (status != ERROR_SUCCESS) {
        return 0;
    }
    return static_cast<std::uint32_t>(_wtol(text));
}

bool is_supported_windows() {
    return current_windows_build() >= k_min_windows_build;
}

bool is_user_an_admin() {
    BOOL admin = FALSE;
    PSID group{};
    SID_IDENTIFIER_AUTHORITY nt = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&nt, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &group)) {
        CheckTokenMembership(nullptr, group, &admin);
        FreeSid(group);
    }
    return admin == TRUE;
}

bool restart_elevated() {
    const auto path = exe_path();
    if (path.empty()) {
        return false;
    }
    const auto dir = path.parent_path();
    const INT_PTR rc = reinterpret_cast<INT_PTR>(ShellExecuteW(
        nullptr, L"runas", path.c_str(), nullptr, dir.c_str(), SW_SHOWNORMAL));
    return rc > 32;
}

bool is_autostart_enabled() {
    HKEY key{};
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return false;
    }
    DWORD type{};
    DWORD bytes{};
    auto status = RegQueryValueExW(key, L"Winchisel", nullptr, &type, nullptr, &bytes);
    if (status != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ) || bytes < sizeof(wchar_t)) {
        RegCloseKey(key);
        return false;
    }
    std::vector<wchar_t> value(bytes / sizeof(wchar_t), L'\0');
    status = RegQueryValueExW(key, L"Winchisel", nullptr, &type, reinterpret_cast<LPBYTE>(value.data()), &bytes);
    RegCloseKey(key);
    if (status != ERROR_SUCCESS) {
        return false;
    }
    return std::filesystem::path(value.data()).lexically_normal() == exe_path().lexically_normal();
}

winchisel::core::Result<void> set_autostart_enabled(bool enabled) {
    HKEY key{};
    const auto open_status = RegCreateKeyExW(
        HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, nullptr, 0,
        KEY_READ | KEY_WRITE, nullptr, &key, nullptr);
    if (open_status != ERROR_SUCCESS) {
        return std::unexpected(winchisel::core::Error{.detail = std::to_string(open_status)});
    }

    LONG status{};
    if (enabled) {
        const auto path = exe_path().wstring();
        status = path.empty()
            ? ERROR_FILE_NOT_FOUND
            : RegSetValueExW(key, L"Winchisel", 0, REG_SZ, reinterpret_cast<const BYTE*>(path.c_str()),
                              static_cast<DWORD>((path.size() + 1) * sizeof(wchar_t)));
    } else {
        status = RegDeleteValueW(key, L"Winchisel");
        if (status == ERROR_FILE_NOT_FOUND) {
            status = ERROR_SUCCESS;
        }
    }
    RegCloseKey(key);
    if (status != ERROR_SUCCESS) {
        return std::unexpected(winchisel::core::Error{.detail = std::to_string(status)});
    }
    return {};
}

std::filesystem::path appdata_dir() {
    PWSTR raw{};
    if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &raw)) || raw == nullptr) {
        return {};
    }
    std::filesystem::path dir = raw;
    CoTaskMemFree(raw);
    dir /= L"Winchisel";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

std::filesystem::path settings_path() {
    return appdata_dir() / L"settings.json";
}

winchisel::core::Result<winchisel::core::Settings> load_settings() {
    const auto path = settings_path();
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return winchisel::core::settings_defaults();
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return winchisel::core::settings_defaults();
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return winchisel::core::parse_settings_json(ss.str());
}

winchisel::core::Result<void> save_settings(const winchisel::core::Settings& settings) {
    const auto path = settings_path();
    const auto temporary = path.wstring() + L".tmp";
    std::ofstream out(std::filesystem::path(temporary), std::ios::binary | std::ios::trunc);
    if (!out) {
        return std::unexpected(winchisel::core::Error{.detail = std::filesystem::path(temporary).string()});
    }
    out << winchisel::core::serialize_settings_json(settings);
    out.flush();
    out.close();
    if (!out || !MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const auto error = GetLastError();
        DeleteFileW(temporary.c_str());
        return std::unexpected(winchisel::core::Error{.detail = std::to_string(error)});
    }
    return {};
}

void show_unsupported_os_message() {
    MessageBoxW(
        nullptr,
        L"Winchisel requires Windows 11 24H2 or newer (build 26100+).",
        L"Winchisel",
        MB_OK | MB_ICONERROR);
}

void show_error_message(const wchar_t* text) {
    MessageBoxW(nullptr, text, L"Winchisel", MB_OK | MB_ICONERROR);
}

void boot_log(const char* message) {
    static std::mutex log_mutex;
    std::scoped_lock lock(log_mutex);
    auto directory = appdata_dir() / L"logs";
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) return;
    const auto log = directory / L"winchisel.log";
    std::ofstream out(log, std::ios::app);
    if (!out) return;
    const auto now = std::chrono::system_clock::now(); const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm local{}; localtime_s(&local, &time);
    out << std::put_time(&local, "%Y-%m-%d %H:%M:%S") << " [" << GetCurrentProcessId() << "] " << message << '\n';
    out.flush();
}

void set_current_directory_to_exe() {
    const auto path = exe_path();
    if (path.empty()) {
        return;
    }
    SetCurrentDirectoryW(path.parent_path().c_str());
}

winchisel::core::Result<void> fail(char const*, std::string detail) {
    return std::unexpected(winchisel::core::Error{.detail = std::move(detail)});
}

std::string command_error(std::string_view action, detail::ProcessWaitResult waited, std::string output) {
    while (!output.empty() && std::isspace(static_cast<unsigned char>(output.back()))) output.pop_back();
    while (!output.empty() && std::isspace(static_cast<unsigned char>(output.front()))) output.erase(output.begin());
    std::string utf8;
    if (!output.empty()) {
        const int wide_size = MultiByteToWideChar(CP_OEMCP, 0, output.data(), static_cast<int>(output.size()), nullptr, 0);
        std::wstring wide(static_cast<std::size_t>(wide_size), L'\0');
        MultiByteToWideChar(CP_OEMCP, 0, output.data(), static_cast<int>(output.size()), wide.data(), wide_size);
        const int utf8_size = WideCharToMultiByte(CP_UTF8, 0, wide.data(), wide_size, nullptr, 0, nullptr, nullptr);
        utf8.resize(static_cast<std::size_t>(utf8_size));
        WideCharToMultiByte(CP_UTF8, 0, wide.data(), wide_size, utf8.data(), utf8_size, nullptr, nullptr);
    }
    std::string message(action);
    message += waited.timed_out ? " timed out" : " failed with exit code " + std::to_string(waited.exit_code);
    if (!utf8.empty()) message += ": " + utf8;
    return message;
}

void emit_lines(std::string& pending, ProtectionProgress const& progress) {
    while (true) {
        auto pos = pending.find_first_of("\r\n");
        if (pos == std::string::npos) {
            return;
        }
        auto line = pending.substr(0, pos);
        auto skip = 1;
        if (pending[pos] == '\r' && pos + 1 < pending.size() && pending[pos + 1] == '\n') {
            skip = 2;
        }
        pending.erase(0, pos + skip);
        while (!line.empty() && (line.back() == ' ' || line.back() == '\t')) {
            line.pop_back();
        }
        if (!line.empty() && progress) {
            progress(false, line);
        }
    }
}

winchisel::core::Result<void> run_hidden(std::wstring command, char const* message_key) {
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
        return fail(message_key, "Failed to start process");
    }
    const auto waited = detail::wait_process(process.hProcess, 30 * 60 * 1000);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    if (waited.exit_code != 0) {
        return fail(message_key, waited.timed_out ? "process timed out" : std::to_string(waited.exit_code));
    }
    return {};
}

winchisel::core::Result<void> run_logged(std::wstring command, char const* message_key, ProtectionProgress const& progress) {
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    HANDLE read{};
    HANDLE write{};
    if (!CreatePipe(&read, &write, &security, 0)) {
        return fail(message_key, "Failed to start process");
    }
    SetHandleInformation(read, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    startup.hStdOutput = write;
    startup.hStdError = write;
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
        CloseHandle(read);
        CloseHandle(write);
        return fail(message_key, "Failed to start process");
    }
    CloseHandle(write);
    std::string pending;
    const auto waited = detail::wait_process_with_pipe(process.hProcess, read, 30 * 60 * 1000, [&](char const* buffer,DWORD read_count) {
        pending.append(buffer, read_count);
        emit_lines(pending, progress);
    });
    emit_lines(pending, progress);
    if (!pending.empty() && progress) {
        progress(false, pending);
    }
    CloseHandle(read);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    if (waited.exit_code != 0) {
        return fail(message_key, waited.timed_out ? "process timed out" : std::to_string(waited.exit_code));
    }
    return {};
}

winchisel::core::Result<void> create_restore_point() {
    RESTOREPOINTINFOW point{};
    point.dwEventType = BEGIN_SYSTEM_CHANGE;
    point.dwRestorePtType = MODIFY_SETTINGS;
    wcsncpy_s(point.szDescription, L"Winchisel Restore Point", _TRUNCATE);
    STATEMGRSTATUS status{};
    if (!SRSetRestorePointW(&point, &status) || status.nStatus != ERROR_SUCCESS) {
        const auto error = status.nStatus ? status.nStatus : GetLastError();
        boot_log(("restore-point BEGIN failed error=" + std::to_string(error)).c_str());
        return fail("restore_point_failed", "BEGIN error=" + std::to_string(error));
    }
    const auto sequence = status.llSequenceNumber;
    boot_log(("restore-point BEGIN sequence=" + std::to_string(sequence)).c_str());
    point.dwEventType = END_SYSTEM_CHANGE;
    point.llSequenceNumber = sequence;
    status = {};
    if (!SRSetRestorePointW(&point, &status) || status.nStatus != ERROR_SUCCESS) {
        const auto error = status.nStatus ? status.nStatus : GetLastError();
        boot_log(("restore-point END failed sequence=" + std::to_string(sequence) + " error=" + std::to_string(error)).c_str());
        return fail("restore_point_failed", "END sequence=" + std::to_string(sequence) + " error=" + std::to_string(error));
    }
    boot_log(("restore-point END sequence=" + std::to_string(sequence) + " status=0").c_str());
    return {};
}

winchisel::core::Result<void> run_system_repair(ProtectionProgress const& progress) {
    if (progress) {
        progress(true, "Running DISM /Online /Cleanup-Image /RestoreHealth");
    }
    if (auto result = run_logged(L"dism.exe /Online /Cleanup-Image /RestoreHealth", "repair_failed", progress); !result) {
        return result;
    }
    if (progress) {
        progress(true, "Running sfc /scannow");
    }
    return run_logged(L"sfc.exe /scannow", "repair_failed", progress);
}

winchisel::core::Result<void> run_disk_cleanup() {
    // Keep this action equivalent to the Rust version. Component-store
    // /ResetBase is destructive (installed updates can no longer be removed)
    // and must never be hidden behind ordinary disk cleanup.
    return run_hidden(L"cleanmgr.exe /d C: /VERYLOWDISK", "settings_disk_cleanup_failed");
}

winchisel::core::Result<void> remove_temp_files(ProtectionProgress const& progress) {
    if (progress) {
        progress(true, "Temporary Files - Remove");
    }
    std::array<wchar_t, MAX_PATH> temp{};
    const auto length = GetTempPathW(static_cast<DWORD>(temp.size()), temp.data());
    if (length == 0 || length >= temp.size()) return fail("settings_temp_files_failed", "Unable to resolve user temp directory");
    std::array<std::filesystem::path, 2> roots{std::filesystem::path(temp.data()), {}};
    std::array<wchar_t, MAX_PATH> windows{};
    const auto windows_length = GetWindowsDirectoryW(windows.data(), static_cast<UINT>(windows.size()));
    if (windows_length && windows_length < windows.size()) roots[1] = std::filesystem::path(windows.data()) / L"Temp";
    std::error_code ec;
    std::size_t removed{};
    std::vector<std::string> failures;
    for (auto const& root : roots) {
        if (root.empty()) continue;
        if (!std::filesystem::exists(root, ec)) { ec.clear(); continue; }
        std::filesystem::directory_iterator it(root,
            std::filesystem::directory_options::skip_permission_denied, ec), end;
        if (ec) { failures.push_back(root.string() + ": " + ec.message()); ec.clear(); continue; }
        while (it != end) {
            const auto path = it->path();
            std::error_code type_error;
            const bool reparse_point = it->is_symlink(type_error);
            if (type_error) {
                failures.push_back(path.string() + ": " + type_error.message());
            } else if (reparse_point) {
                std::filesystem::remove(path, ec);
                if (!ec) {
                    ++removed;
                    if (progress) progress(false, path.filename().string());
                } else {
                    failures.push_back(path.string() + ": " + ec.message());
                    ec.clear();
                }
            } else {
                std::filesystem::remove_all(path, ec);
                if (!ec) {
                    ++removed;
                    if (progress) progress(false, path.filename().string());
                } else {
                    failures.push_back(path.string() + ": " + ec.message());
                    ec.clear();
                }
            }
            it.increment(ec);
            if (ec) { failures.push_back(root.string() + ": " + ec.message()); ec.clear(); break; }
        }
    }
    if (!failures.empty()) {
        std::string detail = std::to_string(removed) + " entries removed; " +
            std::to_string(failures.size()) + " failed. First error: " + failures.front();
        return fail("settings_temp_files_failed", std::move(detail));
    }
    return {};
}

winchisel::core::Result<void> apply_winchisel_power_plan() {
    const auto plan = exe_path().parent_path() / L"assets" / L"Winchisel.pow";
    std::error_code ec;
    if (!std::filesystem::exists(plan, ec)) {
        return fail("power_plan_failed", "Embedded power plan is missing: " + plan.string());
    }

    // The imported scheme is named Winchisel.  powercfg prints its GUID as part
    // of the import result; activating that exact GUID avoids changing another
    // existing plan with the same name.
    const auto temp = std::filesystem::temp_directory_path(ec) / L"Winchisel.pow";
    if (ec) return fail("power_plan_failed", "Unable to resolve the temp directory");
    std::filesystem::copy_file(plan, temp, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) return fail("power_plan_failed", "Unable to write temporary power plan: " + ec.message());

    auto [waited, output] = detail::run_captured(L"powercfg.exe /import \"" + temp.wstring() + L"\"");
    std::filesystem::remove(temp, ec);
    if (waited.exit_code != 0) return fail("power_plan_failed", command_error("powercfg /import", waited, std::move(output)));

    static const std::regex guid_pattern(R"(([0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}))");
    std::smatch match;
    if (!std::regex_search(output, match, guid_pattern)) return fail("power_plan_failed", "powercfg /import succeeded, but returned no power plan GUID. Output: " + output);
    const auto guid = match[1].str();
    auto [activated, activation_output] = detail::run_captured(L"powercfg.exe /setactive " + std::wstring(guid.begin(), guid.end()));
    if (activated.exit_code != 0) return fail("power_plan_failed", command_error("powercfg /setactive", activated, std::move(activation_output)));
    HKEY key{};
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Winchisel", 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr) == ERROR_SUCCESS) {
        const std::wstring value(guid.begin(), guid.end());
        RegSetValueExW(key, L"PowerPlanGuid", 0, REG_SZ, reinterpret_cast<BYTE const*>(value.c_str()),
            static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
        RegCloseKey(key);
    }
    return {};
}

winchisel::core::Result<void> set_widgets_removed(bool enabled) {
    const auto command = enabled
        ? L"winget.exe uninstall --name \"Windows Web Experience Pack\" --exact --disable-interactivity"
        : L"winget.exe install --id 9MSSGKG348SP --source msstore --accept-package-agreements --accept-source-agreements --disable-interactivity";
    return run_hidden(command, "widgets_failed");
}

winchisel::core::Result<void> set_teredo_disabled(bool enabled) {
    HKEY key{};
    auto status = RegCreateKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\Tcpip6\\Parameters", 0, nullptr, 0, KEY_READ | KEY_WRITE, nullptr, &key, nullptr);
    if (status != ERROR_SUCCESS) return fail("teredo_failed", "Unable to open Tcpip6 parameters: " + std::to_string(status));
    DWORD current{}, bytes = sizeof(current), type{};
    if (RegQueryValueExW(key, L"DisabledComponents", nullptr, &type, reinterpret_cast<BYTE*>(&current), &bytes) != ERROR_SUCCESS || type != REG_DWORD) current = 0;
    const DWORD next = enabled ? current | 0x01 : current & ~0x01;
    status = RegSetValueExW(key, L"DisabledComponents", 0, REG_DWORD, reinterpret_cast<BYTE const*>(&next), sizeof(next));
    RegCloseKey(key);
    if (status != ERROR_SUCCESS) return fail("teredo_failed", "Unable to update DisabledComponents: " + std::to_string(status));
    auto command = run_hidden(enabled ? L"netsh.exe interface teredo set state disabled" : L"netsh.exe interface teredo set state default", "teredo_failed");
    if (!command) {
        HKEY rollback_key{};
        if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\Tcpip6\\Parameters", 0,
                nullptr, 0, KEY_WRITE, nullptr, &rollback_key, nullptr) == ERROR_SUCCESS) {
            RegSetValueExW(rollback_key, L"DisabledComponents", 0, REG_DWORD,
                reinterpret_cast<BYTE const*>(&current), sizeof(current));
            RegCloseKey(rollback_key);
        }
        return command;
    }
    return {};
}

winchisel::core::Result<void> set_hpet_disabled(bool enabled) {
    if (enabled) return run_hidden(L"bcdedit.exe /set useplatformclock false", "hpet_failed");
    // Absence of the value is the Windows-managed default. bcdedit returns a
    // non-zero code when it is already absent, which is also the desired state.
    auto result = run_hidden(L"bcdedit.exe /deletevalue useplatformclock", "hpet_failed");
    if (!result) {
        const auto state = read_extras_command_state();
        if (!state.hpet_disabled) return {};
    }
    return result;
}

ExtrasCommandState read_extras_command_state() {
    auto capture = [](std::wstring command) -> std::pair<DWORD, std::string> {
        auto [waited, output] = detail::run_captured(std::move(command), 2 * 60 * 1000);
        return {waited.exit_code, std::move(output)};
    };

    ExtrasCommandState state;
    const auto [power_code, power] = capture(L"powercfg.exe /getactivescheme");
    std::array<wchar_t, 64> saved_guid{}; DWORD saved_size=sizeof(saved_guid);
    const bool has_saved_guid=RegGetValueW(HKEY_LOCAL_MACHINE,L"SOFTWARE\\Winchisel",L"PowerPlanGuid",RRF_RT_REG_SZ,nullptr,saved_guid.data(),&saved_size)==ERROR_SUCCESS;
    std::string expected;
    if(has_saved_guid){const auto chars=WideCharToMultiByte(CP_UTF8,0,saved_guid.data(),-1,nullptr,0,nullptr,nullptr);if(chars>1){expected.resize(chars);WideCharToMultiByte(CP_UTF8,0,saved_guid.data(),-1,expected.data(),chars,nullptr,nullptr);expected.pop_back();}}
    state.power_plan_active = power_code == 0 && !expected.empty() && power.find(expected)!=std::string::npos;
    HKEY widgets{};
    state.widgets_removed = true;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\AppModel\\StateRepository\\Cache\\Package\\Data", 0, KEY_READ, &widgets) == ERROR_SUCCESS) {
        for (DWORD index{};; ++index) {
            std::array<wchar_t, 512> name{}; DWORD length = static_cast<DWORD>(name.size());
            const auto status = RegEnumKeyExW(widgets, index, name.data(), &length, nullptr, nullptr, nullptr, nullptr);
            if (status == ERROR_NO_MORE_ITEMS) break;
            if (status == ERROR_SUCCESS) {
                std::wstring package(name.data(), length);
                std::ranges::transform(package, package.begin(), towlower);
                if (package.find(L"microsoftwindows.client.webexperience") != std::wstring::npos) {
                    state.widgets_removed = false; break;
                }
            }
        }
        RegCloseKey(widgets);
    }
    const auto [hpet_code, hpet] = capture(L"bcdedit.exe /enum {current}");
    if (hpet_code == 0) {
        const std::regex hpet_pattern(R"(useplatformclock\s+(false|no|0))", std::regex::icase);
        state.hpet_disabled = std::regex_search(hpet, hpet_pattern);
    }
    return state;
}

void set_console_visible(bool visible) {
    HWND console = GetConsoleWindow();
    if (visible) {
        if (console == nullptr) {
            AllocConsole();
            console = GetConsoleWindow();
        }
        if (console != nullptr) {
            ShowWindow(console, SW_SHOW);
            DeleteMenu(GetSystemMenu(console, FALSE), SC_CLOSE, MF_BYCOMMAND);
        }
        return;
    }
    if (console != nullptr) {
        ShowWindow(console, SW_HIDE);
    }
}

}  // namespace winchisel::platform
