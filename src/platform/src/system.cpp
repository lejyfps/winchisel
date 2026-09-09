#include "winchisel/platform/system.hpp"
#include "process_wait.hpp"

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <SrRestorePtApi.h>

#include <algorithm>
#include <fstream>
#include <array>
#include <optional>
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

// CommandLineToArgvW-compatible quoting: wraps in quotes, doubles
// backslashes before quotes and at the end so paths with spaces, trailing
// backslashes, or embedded quotes round-trip exactly.
std::wstring quote_arg(std::wstring_view arg) {
    std::wstring out;
    out.push_back(L'"');
    std::size_t backslashes{};
    for (const wchar_t c : arg) {
        if (c == L'\\') {
            ++backslashes;
            continue;
        }
        if (c == L'"') out.append(backslashes * 2 + 1, L'\\');
        else out.append(backslashes, L'\\');
        backslashes = 0;
        out.push_back(c);
    }
    out.append(backslashes * 2, L'\\');
    out.push_back(L'"');
    return out;
}

bool restart_elevated() {
    const auto path = exe_path();
    if (path.empty()) {
        return false;
    }
    const auto dir = path.parent_path();
    std::wstring arguments;
    const auto host_size = GetEnvironmentVariableW(L"WINCHISEL_PORTABLE_HOST", nullptr, 0);
    if (host_size > 1) {
        std::wstring host(host_size, L'\0');
        if (GetEnvironmentVariableW(L"WINCHISEL_PORTABLE_HOST", host.data(), host_size)) {
            if (!host.empty() && host.back() == L'\0') host.pop_back();
            if (!host.empty()) arguments = L"--portable-host " + quote_arg(host);
        }
    }
    const INT_PTR rc = reinterpret_cast<INT_PTR>(ShellExecuteW(
        nullptr, L"runas", path.c_str(), arguments.empty() ? nullptr : arguments.c_str(), dir.c_str(), SW_SHOWNORMAL));
    return rc > 32;
}

std::optional<std::wstring> autostart_host() {
    const auto size = GetEnvironmentVariableW(L"WINCHISEL_PORTABLE_HOST", nullptr, 0);
    if (size > 1) {
        std::wstring host(size, L'\0');
        if (GetEnvironmentVariableW(L"WINCHISEL_PORTABLE_HOST", host.data(), size)) {
            if (!host.empty() && host.back() == L'\0') host.pop_back();
            if (!host.empty()) return host;
        }
    }
    int count{};
    auto arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!arguments) return std::nullopt;
    std::optional<std::wstring> host;
    for (int index = 1; index + 1 < count; ++index) {
        if (std::wstring_view(arguments[index]) == L"--portable-host") {
            host = arguments[index + 1];
            break;
        }
    }
    LocalFree(arguments);
    return host;
}

std::wstring autostart_command() {
    // Portable launches keep a self-referential --portable-host marker so the
    // updater still recognizes the installation after a reboot. The bootstrap
    // stub ignores unknown arguments, so this is safe to pass.
    if (auto host = autostart_host(); host && !host->empty()) {
        return quote_arg(*host) + L" --portable-host " + quote_arg(*host);
    }
    return quote_arg(exe_path().wstring());
}

bool command_matches_autostart(std::wstring const& value) {
    int count{};
    auto argv = CommandLineToArgvW(value.c_str(), &count);
    if (!argv || count < 1) {
        if (argv) LocalFree(argv);
        return false;
    }
    const std::wstring program(argv[0]);
    LocalFree(argv);
    if (program.empty()) return false;
    const auto stored = std::filesystem::path(program).lexically_normal();
    if (stored == exe_path().lexically_normal()) return true;
    if (auto host = autostart_host()) return stored == std::filesystem::path(*host).lexically_normal();
    return false;
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
    return command_matches_autostart(value.data());
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
        const auto path = autostart_command();
        status = path.size() <= 2
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

winchisel::core::Language system_ui_language() {
    wchar_t locale[LOCALE_NAME_MAX_LENGTH]{};
    if (!GetUserDefaultLocaleName(locale, LOCALE_NAME_MAX_LENGTH)) return winchisel::core::Language::english;
    if (_wcsnicmp(locale, L"de", 2) == 0) return winchisel::core::Language::german;
    if (_wcsnicmp(locale, L"es", 2) == 0) return winchisel::core::Language::spanish;
    if (_wcsnicmp(locale, L"fr", 2) == 0) return winchisel::core::Language::french;
    if (_wcsnicmp(locale, L"ru", 2) == 0) return winchisel::core::Language::russian;
    if (_wcsicmp(locale, L"zh-CN") == 0 || _wcsicmp(locale, L"zh-SG") == 0 ||
        _wcsnicmp(locale, L"zh-Hans", 7) == 0) return winchisel::core::Language::simplified_chinese;
    if (_wcsicmp(locale, L"pt-BR") == 0) return winchisel::core::Language::portuguese_brazil;
    if (_wcsnicmp(locale, L"pl", 2) == 0) return winchisel::core::Language::polish;
    if (_wcsnicmp(locale, L"tr", 2) == 0) return winchisel::core::Language::turkish;
    if (_wcsnicmp(locale, L"ja", 2) == 0) return winchisel::core::Language::japanese;
    if (_wcsnicmp(locale, L"ko", 2) == 0) return winchisel::core::Language::korean;
    if (_wcsnicmp(locale, L"it", 2) == 0) return winchisel::core::Language::italian;
    if (_wcsnicmp(locale, L"nl", 2) == 0) return winchisel::core::Language::dutch;
    if (_wcsnicmp(locale, L"uk", 2) == 0) return winchisel::core::Language::ukrainian;
    if (_wcsnicmp(locale, L"cs", 2) == 0) return winchisel::core::Language::czech;
    if (_wcsnicmp(locale, L"id", 2) == 0) return winchisel::core::Language::indonesian;
    if (_wcsnicmp(locale, L"vi", 2) == 0) return winchisel::core::Language::vietnamese;
    if (_wcsnicmp(locale, L"ar", 2) == 0) return winchisel::core::Language::arabic;
    if (_wcsicmp(locale, L"zh-TW") == 0 || _wcsicmp(locale, L"zh-HK") == 0 ||
        _wcsicmp(locale, L"zh-MO") == 0 || _wcsnicmp(locale, L"zh-Hant", 7) == 0)
        return winchisel::core::Language::traditional_chinese;
    if (_wcsnicmp(locale, L"th", 2) == 0) return winchisel::core::Language::thai;
    return winchisel::core::Language::english;
}

winchisel::core::Result<winchisel::core::Settings> load_settings() {
    const auto path = settings_path();
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return winchisel::core::settings_defaults();
    }
    std::error_code size_error;
    const auto size = std::filesystem::file_size(path, size_error);
    if (size_error || size > 65536) {
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
    if (std::filesystem::file_size(log, error) > 1024 * 1024 && !error) {
        const auto previous = directory / L"winchisel.log.1";
        std::filesystem::remove(previous, error);
        error.clear();
        std::filesystem::rename(log, previous, error);
    }
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
    // Offset-based scan with a single compaction per call. Erasing the
    // consumed prefix per line would memmove the remainder every time,
    // which is quadratic on large process output.
    std::size_t consumed{};
    while (true) {
        const auto pos = pending.find_first_of("\r\n", consumed);
        if (pos == std::string::npos) {
            break;
        }
        auto line = pending.substr(consumed, pos - consumed);
        std::size_t skip = 1;
        if (pending[pos] == '\r' && pos + 1 < pending.size() && pending[pos + 1] == '\n') {
            skip = 2;
        }
        consumed = pos + skip;
        while (!line.empty() && (line.back() == ' ' || line.back() == '\t')) {
            line.pop_back();
        }
        if (!line.empty() && progress) {
            progress(false, line);
        }
    }
    if (consumed) {
        pending.erase(0, consumed);
    }
}

winchisel::core::Result<void> run_hidden(std::wstring command, char const* message_key, DWORD timeout_ms = 10 * 60 * 1000) {
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
        return fail(message_key, "Failed to start process");
    }
    const auto waited = detail::wait_process(process.hProcess, timeout_ms);
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
        if (pending.size() > detail::k_max_captured_output)
            pending.erase(0, pending.size() - detail::k_max_captured_output);
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
    return run_hidden(L"cleanmgr.exe /d C: /VERYLOWDISK", "settings_disk_cleanup_failed", 15 * 60 * 1000);
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
    // existing plan with the same name. The temp copy uses a unique name so
    // concurrent runs cannot collide on a fixed file name.
    std::array<wchar_t, MAX_PATH> temp_dir{}, temp_seed{};
    if (!GetTempPathW(static_cast<DWORD>(temp_dir.size()), temp_dir.data()) ||
        !GetTempFileNameW(temp_dir.data(), L"wci", 0, temp_seed.data())) {
        return fail("power_plan_failed", "Unable to create a temporary file");
    }
    const std::filesystem::path temp = std::wstring(temp_seed.data()) + L".pow";
    std::filesystem::copy_file(plan, temp, std::filesystem::copy_options::overwrite_existing, ec);
    const auto copy_error = ec;
    std::filesystem::remove(temp_seed.data(), ec);
    ec.clear();
    if (copy_error) return fail("power_plan_failed", "Unable to write temporary power plan: " + copy_error.message());

    auto [waited, output] = detail::run_captured(L"powercfg.exe /import \"" + temp.wstring() + L"\"", 2 * 60 * 1000);
    std::filesystem::remove(temp, ec);
    if (waited.exit_code != 0) return fail("power_plan_failed", command_error("powercfg /import", waited, std::move(output)));

    static const std::regex guid_pattern(R"(([0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}))");
    std::smatch match;
    if (!std::regex_search(output, match, guid_pattern)) return fail("power_plan_failed", "powercfg /import succeeded, but returned no power plan GUID. Output: " + output);
    const auto guid = match[1].str();
    auto [activated, activation_output] = detail::run_captured(L"powercfg.exe /setactive " + std::wstring(guid.begin(), guid.end()), 2 * 60 * 1000);
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
    return run_hidden(command, "widgets_failed", enabled ? 10 * 60 * 1000 : 30 * 60 * 1000);
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
    auto command = run_hidden(enabled ? L"netsh.exe interface teredo set state disabled" : L"netsh.exe interface teredo set state default", "teredo_failed", 2 * 60 * 1000);
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

// Tri-state BCD read: nullopt = the query itself failed, an empty inner
// value = no useplatformclock entry present, otherwise the parsed state.
// bcdedit localizes boolean words, so anything unrecognized is reported as
// unknown instead of being claimed as enabled or disabled.
std::optional<std::optional<bool>> query_hpet_state() {
    auto [waited, output] = detail::run_captured(L"bcdedit.exe /enum {current}", 2 * 60 * 1000);
    if (waited.exit_code != 0) return std::nullopt;
    static const std::regex pattern(R"(useplatformclock\s+(\S+))", std::regex::icase);
    std::smatch match;
    if (!std::regex_search(output, match, pattern)) return std::optional<std::optional<bool>>{std::nullopt};
    auto value = match[1].str();
    std::ranges::transform(value, value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    static constexpr std::string_view disabled_words[] = {
        "false", "no", "0", "nein", "aus", "off", "non", "falso", "nee", "niet", "nem", "ei"};
    static constexpr std::string_view enabled_words[] = {
        "true", "yes", "1", "ja", "an", "on", "oui", "si", "sim", "evet"};
    for (auto word : disabled_words) if (value == word) return std::optional<std::optional<bool>>{true};
    for (auto word : enabled_words) if (value == word) return std::optional<std::optional<bool>>{false};
    return std::optional<std::optional<bool>>{std::nullopt};
}

winchisel::core::Result<void> set_hpet_disabled(bool enabled) {
    auto result = enabled
        ? run_hidden(L"bcdedit.exe /set useplatformclock false", "hpet_failed", 2 * 60 * 1000)
        : run_hidden(L"bcdedit.exe /deletevalue useplatformclock", "hpet_failed", 2 * 60 * 1000);
    const auto state = query_hpet_state();
    if (!state) return std::unexpected(winchisel::core::Error{.detail = "HPET state could not be verified"});
    if (!*state) {
        // No entry present: the expected outcome of deletevalue, but a
        // failure when an explicit value was just written.
        if (!enabled) return {};
        if (!result) return result;
        return std::unexpected(winchisel::core::Error{.detail = "HPET state did not change"});
    }
    if (**state != enabled) {
        if (!result) return result;
        return std::unexpected(winchisel::core::Error{.detail = "HPET state did not change"});
    }
    return {};
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
    state.power_plan_active = false;
    if (power_code == 0 && !expected.empty()) {
        auto haystack = power;
        std::ranges::transform(haystack, haystack.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        auto needle = expected;
        std::ranges::transform(needle, needle.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        state.power_plan_active = haystack.find(needle) != std::string::npos;
    }
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
    if (const auto hpet = query_hpet_state()) {
        // Absent or unrecognized entries stay unknown instead of being
        // claimed as enabled; callers already handle nullopt.
        state.hpet_disabled = *hpet;
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
