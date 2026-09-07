#include "winchisel/platform/system.hpp"

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>

#include <fstream>
#include <sstream>
#include <vector>

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
