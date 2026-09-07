#include "winchisel/platform/system.hpp"

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <regex>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "advapi32.lib")

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
    return current_windows_build() >= winchisel::core::k_min_windows_build;
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
        return std::unexpected(winchisel::core::Error{
            .code = winchisel::core::ErrorCode::io,
            .message_key = "autostart_update_failed",
            .detail = std::to_string(open_status),
        });
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
        return std::unexpected(winchisel::core::Error{
            .code = winchisel::core::ErrorCode::io,
            .message_key = "autostart_update_failed",
            .detail = std::to_string(status),
        });
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
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return std::unexpected(winchisel::core::Error{
            .code = winchisel::core::ErrorCode::io,
            .message_key = "settings_save_failed",
            .detail = path.string(),
        });
    }
    out << winchisel::core::serialize_settings_json(settings);
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
    wchar_t temp[MAX_PATH]{};
    if (GetTempPathW(MAX_PATH, temp) == 0) {
        return;
    }
    std::filesystem::path log = temp;
    log /= L"winchisel-boot.log";
    std::ofstream out(log, std::ios::app);
    if (!out) {
        return;
    }
    out << message << '\n';
    out.flush();
}

void set_current_directory_to_exe() {
    const auto path = exe_path();
    if (path.empty()) {
        return;
    }
    SetCurrentDirectoryW(path.parent_path().c_str());
}

winchisel::core::Result<void> fail(char const* message_key, std::string detail) {
    return std::unexpected(winchisel::core::Error{
        .code = winchisel::core::ErrorCode::platform,
        .message_key = message_key,
        .detail = std::move(detail),
    });
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
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD code{};
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    if (code != 0) {
        return fail(message_key, std::to_string(code));
    }
    return {};
}

winchisel::core::Result<void> run_logged(std::wstring inner, char const* message_key, ProtectionProgress const& progress) {
    std::wstring command =
        L"powershell.exe -NoProfile -NonInteractive -Command \"$ErrorActionPreference='Continue'; & { " + inner +
        L" } 2>&1 | ForEach-Object { $_.ToString() }\"";
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
    char buffer[512]{};
    DWORD read_count{};
    while (ReadFile(read, buffer, sizeof(buffer), &read_count, nullptr) && read_count > 0) {
        pending.append(buffer, read_count);
        emit_lines(pending, progress);
    }
    emit_lines(pending, progress);
    if (!pending.empty() && progress) {
        progress(false, pending);
    }
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD code{};
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(read);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    if (code != 0) {
        return fail(message_key, std::to_string(code));
    }
    return {};
}

winchisel::core::Result<void> create_restore_point() {
    return run_hidden(
        L"powershell.exe -NoProfile -NonInteractive -Command \"$ErrorActionPreference='Stop'; Enable-ComputerRestore -Drive 'C:\\' | Out-Null; Checkpoint-Computer -Description 'Winchisel Restore Point' -RestorePointType 'MODIFY_SETTINGS' | Out-Null\"",
        "restore_point_failed");
}

winchisel::core::Result<void> run_system_repair(ProtectionProgress const& progress) {
    if (progress) {
        progress(true, "Running DISM /Online /Cleanup-Image /RestoreHealth");
    }
    if (auto result = run_logged(L"DISM /Online /Cleanup-Image /RestoreHealth", "repair_failed", progress); !result) {
        return result;
    }
    if (progress) {
        progress(true, "Running sfc /scannow");
    }
    return run_logged(L"sfc /scannow", "repair_failed", progress);
}

winchisel::core::Result<void> run_disk_cleanup() {
    if (auto result = run_hidden(L"cleanmgr.exe /d C: /VERYLOWDISK", "settings_disk_cleanup_failed"); !result) {
        return result;
    }
    return run_hidden(L"dism.exe /online /Cleanup-Image /StartComponentCleanup /ResetBase", "settings_disk_cleanup_failed");
}

winchisel::core::Result<void> remove_temp_files(ProtectionProgress const& progress) {
    if (progress) {
        progress(true, "Temporary Files - Remove");
    }
    return run_logged(
        L"Remove-Item -Path $Env:Temp\\* -Recurse -Force -ErrorAction SilentlyContinue; Remove-Item -Path $Env:SystemRoot\\Temp\\* -Recurse -Force -ErrorAction SilentlyContinue",
        "settings_temp_files_failed",
        progress);
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

    const auto command = L"powercfg.exe /import \"" + temp.wstring() + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    HANDLE read{}, write{};
    if (!CreatePipe(&read, &write, &security, 0)) {
        std::filesystem::remove(temp, ec);
        return fail("power_plan_failed", "Unable to create output pipe");
    }
    SetHandleInformation(read, HANDLE_FLAG_INHERIT, 0);
    startup.hStdOutput = write;
    startup.hStdError = write;
    PROCESS_INFORMATION process{};
    std::wstring mutable_command = command;
    if (!CreateProcessW(nullptr, mutable_command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
        CloseHandle(read); CloseHandle(write); std::filesystem::remove(temp, ec);
        return fail("power_plan_failed", "powercfg /import could not be started");
    }
    CloseHandle(write);
    std::string output;
    char buffer[256]{};
    DWORD count{};
    while (ReadFile(read, buffer, sizeof(buffer), &count, nullptr) && count) output.append(buffer, count);
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD code{}; GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(read); CloseHandle(process.hThread); CloseHandle(process.hProcess);
    std::filesystem::remove(temp, ec);
    if (code != 0) return fail("power_plan_failed", "powercfg /import exited with " + std::to_string(code));

    static const std::regex guid_pattern(R"(([0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}))");
    std::smatch match;
    if (!std::regex_search(output, match, guid_pattern)) return fail("power_plan_failed", "Unable to read imported power plan GUID");
    const auto guid = match[1].str();
    return run_hidden(L"powercfg.exe /setactive " + std::wstring(guid.begin(), guid.end()), "power_plan_failed");
}

winchisel::core::Result<void> set_widgets_removed(bool enabled) {
    const wchar_t* script = enabled
        ? LR"($ErrorActionPreference = 'Stop'; Get-Process *Widget* -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue; Get-AppxPackage Microsoft.WidgetsPlatformRuntime -AllUsers | Remove-AppxPackage -AllUsers; Get-AppxPackage MicrosoftWindows.Client.WebExperience -AllUsers | Remove-AppxPackage -AllUsers; Stop-Process -Name explorer -Force -ErrorAction SilentlyContinue; Start-Process explorer.exe)"
        : LR"($ErrorActionPreference = 'Stop'; Get-ChildItem 'C:\Program Files\WindowsApps\Microsoft.WidgetsPlatformRuntime*\AppxManifest.xml' | ForEach-Object { Add-AppxPackage -Register $_.FullName -DisableDevelopmentMode }; Get-ChildItem 'C:\Program Files\WindowsApps\MicrosoftWindows.Client.WebExperience*\AppxManifest.xml' | ForEach-Object { Add-AppxPackage -Register $_.FullName -DisableDevelopmentMode }; Stop-Process -Name explorer -Force -ErrorAction SilentlyContinue; Start-Process explorer.exe)";
    return run_hidden(L"powershell.exe -NoLogo -NoProfile -NonInteractive -WindowStyle Hidden -Command \"" + std::wstring(script) + L"\"", "widgets_failed");
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
    return run_hidden(enabled ? L"netsh.exe interface teredo set state disabled" : L"netsh.exe interface teredo set state default", "teredo_failed");
}

winchisel::core::Result<void> set_hpet_disabled(bool enabled) {
    return run_hidden(enabled ? L"bcdedit.exe /set useplatformclock false" : L"bcdedit.exe /set useplatformclock true", "hpet_failed");
}

ExtrasCommandState read_extras_command_state() {
    auto capture = [](std::wstring command) -> std::pair<DWORD, std::string> {
        SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
        HANDLE read{}, write{};
        if (!CreatePipe(&read, &write, &security, 0)) return {ERROR_NOT_ENOUGH_MEMORY, {}};
        SetHandleInformation(read, HANDLE_FLAG_INHERIT, 0);
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
        startup.wShowWindow = SW_HIDE;
        startup.hStdOutput = write;
        startup.hStdError = write;
        PROCESS_INFORMATION process{};
        if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
            CloseHandle(read); CloseHandle(write); return {GetLastError(), {}};
        }
        CloseHandle(write);
        std::string output;
        char buffer[256]{};
        DWORD count{};
        while (ReadFile(read, buffer, sizeof(buffer), &count, nullptr) && count) output.append(buffer, count);
        WaitForSingleObject(process.hProcess, INFINITE);
        DWORD code{}; GetExitCodeProcess(process.hProcess, &code);
        CloseHandle(read); CloseHandle(process.hThread); CloseHandle(process.hProcess);
        return {code, std::move(output)};
    };

    ExtrasCommandState state;
    const auto [power_code, power] = capture(L"powercfg.exe /list");
    state.power_plan_active = power_code == 0 && power.find("(Winchisel)") != std::string::npos && power.find('*') != std::string::npos;
    const auto [widgets_code, widgets] = capture(L"powershell.exe -NoLogo -NoProfile -NonInteractive -Command \"$a=Get-AppxPackage Microsoft.WidgetsPlatformRuntime -AllUsers; $b=Get-AppxPackage MicrosoftWindows.Client.WebExperience -AllUsers; if($null -eq $a -and $null -eq $b){exit 0}else{exit 1}\"");
    state.widgets_removed = widgets_code == 0;
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
