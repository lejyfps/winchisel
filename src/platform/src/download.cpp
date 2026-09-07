#include "winchisel/platform/download.hpp"

#include <Windows.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <future>
#include <fstream>
#include <mutex>
#include <regex>
#include <string>
#include <unordered_set>

namespace winchisel::platform {
namespace {
using Clock = std::chrono::steady_clock;
struct ScanData { std::unordered_set<std::string> ids, names, registry; Clock::time_point at{}; };
std::mutex cache_mutex;
ScanData cache;
bool cache_valid{};

std::pair<DWORD, std::string> run_hidden(std::string command) {
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE}; HANDLE read{}, write{};
    if (!CreatePipe(&read, &write, &security, 0)) return {GetLastError(), {}};
    SetHandleInformation(read, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOA startup{}; startup.cb = sizeof(startup); startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE; startup.hStdOutput = write; startup.hStdError = write;
    PROCESS_INFORMATION process{};
    if (!CreateProcessA(nullptr, command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
        auto error = GetLastError(); CloseHandle(read); CloseHandle(write); return {error, {}};
    }
    CloseHandle(write); std::string output; std::array<char, 4096> buffer{}; DWORD count{};
    while (ReadFile(read, buffer.data(), static_cast<DWORD>(buffer.size()), &count, nullptr) && count) output.append(buffer.data(), count);
    WaitForSingleObject(process.hProcess, INFINITE); DWORD code{}; GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(read); CloseHandle(process.hThread); CloseHandle(process.hProcess); return {code, std::move(output)};
}

std::unordered_set<std::string> lines(std::string output) {
    std::unordered_set<std::string> result;
    std::size_t start{}; while (start < output.size()) {
        auto end = output.find_first_of("\r\n", start); if (end == std::string::npos) end = output.size();
        auto value = output.substr(start, end - start); std::ranges::transform(value, value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (!value.empty()) result.insert(std::move(value)); start = output.find_first_not_of("\r\n", end); if (start == std::string::npos) break;
    } return result;
}

std::unordered_set<std::string> registry_display_names(HKEY root, std::wstring const& path) {
    std::unordered_set<std::string> result;
    HKEY key{};
    if (RegOpenKeyExW(root, path.c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS) return result;
    for (DWORD index{};; ++index) {
        std::array<wchar_t, 256> name{}; DWORD length = static_cast<DWORD>(name.size());
        const auto status = RegEnumKeyExW(key, index, name.data(), &length, nullptr, nullptr, nullptr, nullptr);
        if (status == ERROR_NO_MORE_ITEMS) break;
        if (status != ERROR_SUCCESS) continue;
        HKEY entry{};
        if (RegOpenKeyExW(key, name.data(), 0, KEY_READ, &entry) != ERROR_SUCCESS) continue;
        DWORD type{}, bytes{};
        if (RegQueryValueExW(entry, L"DisplayName", nullptr, &type, nullptr, &bytes) == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ) && bytes >= sizeof(wchar_t)) {
            std::wstring value(bytes / sizeof(wchar_t), L'\0');
            if (RegQueryValueExW(entry, L"DisplayName", nullptr, nullptr, reinterpret_cast<BYTE*>(value.data()), &bytes) == ERROR_SUCCESS) {
                while (!value.empty() && value.back() == L'\0') value.pop_back();
                const int chars = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
                std::string utf8(chars, '\0'); WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), utf8.data(), chars, nullptr, nullptr);
                std::ranges::transform(utf8, utf8.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); }); result.insert(std::move(utf8));
            }
        }
        RegCloseKey(entry);
    }
    RegCloseKey(key); return result;
}

ScanData perform_scan() {
    auto ids = std::async(std::launch::async, [] {
        char path[MAX_PATH]{}; if (!GetTempFileNameA(nullptr, "wci", 0, path)) return std::unordered_set<std::string>{};
        const auto [code, ignored] = run_hidden("winget.exe export --output \"" + std::string(path) + "\" --accept-source-agreements --nowarn --disable-interactivity");
        std::ifstream input(path, std::ios::binary); std::string json((std::istreambuf_iterator<char>(input)), {}); DeleteFileA(path); if (code != 0) return std::unordered_set<std::string>{};
        std::unordered_set<std::string> result; static const std::regex id(R"json("PackageIdentifier"\s*:\s*"([^"]+)")json"); for (std::sregex_iterator it(json.begin(), json.end(), id), end; it != end; ++it) { auto value = (*it)[1].str(); std::ranges::transform(value, value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); }); result.insert(std::move(value)); } return result;
    });
    auto names = std::async(std::launch::async, [] { auto [code, output] = run_hidden("winget.exe list --accept-source-agreements --disable-interactivity"); return code == 0 ? lines(std::move(output)) : std::unordered_set<std::string>{}; });
    auto registry = std::async(std::launch::async, [] {
        std::unordered_set<std::string> result; auto append = [&](auto values) { result.insert(values.begin(), values.end()); };
        append(registry_display_names(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall")); append(registry_display_names(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall"));
        HKEY users{}; if (RegOpenKeyExW(HKEY_USERS, nullptr, 0, KEY_READ, &users) == ERROR_SUCCESS) { for (DWORD i{};; ++i) { std::array<wchar_t, 256> sid{}; DWORD length = static_cast<DWORD>(sid.size()); if (RegEnumKeyExW(users, i, sid.data(), &length, nullptr, nullptr, nullptr, nullptr) == ERROR_NO_MORE_ITEMS) break; if (wcsstr(sid.data(), L"_Classes")) continue; const std::wstring base = std::wstring(sid.data(), length) + L"\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall"; append(registry_display_names(HKEY_USERS, base)); } RegCloseKey(users); } return result;
    });
    return {ids.get(), names.get(), registry.get(), Clock::now()};
}

bool contains(std::unordered_set<std::string> const& values, std::string_view needle) {
    std::string lower(needle); std::ranges::transform(lower, lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return std::ranges::any_of(values, [&](auto const& value) { return value == lower || value.find(lower) != std::string::npos || lower.find(value) != std::string::npos; });
}
}

winchisel::core::Result<std::vector<bool>> scan_downloads_installed(std::span<winchisel::core::DownloadCatalogEntry const> catalog, bool force_refresh) {
    ScanData data;
    { std::scoped_lock lock(cache_mutex); if (!force_refresh && cache_valid && Clock::now() - cache.at < std::chrono::minutes(10)) data = cache; }
    if (data.at == Clock::time_point{}) { data = perform_scan(); std::scoped_lock lock(cache_mutex); cache = data; cache_valid = true; }
    std::vector<bool> result; result.reserve(catalog.size());
    for (auto const& item : catalog) {
        bool installed = contains(data.names, item.name) || contains(data.registry, item.name); std::size_t start{};
        while (!installed && start < item.winget_ids.size()) { auto end = item.winget_ids.find('|', start); if (end == std::string_view::npos) end = item.winget_ids.size(); installed = contains(data.ids, item.winget_ids.substr(start, end - start)); start = end + 1; }
        result.push_back(installed);
    } return result;
}

winchisel::core::Result<DownloadInstallResult> install_downloads(std::span<winchisel::core::DownloadCatalogEntry const* const> items) {
    DownloadInstallResult result;
    for (auto item : items) {
        auto end = item->winget_ids.find('|'); auto id = item->winget_ids.substr(0, end); if (id.empty()) { ++result.failed; continue; }
        auto [code, output] = run_hidden("winget.exe install --id \"" + std::string(id) + "\" --exact --accept-package-agreements --accept-source-agreements --disable-interactivity");
        code == 0 ? ++result.succeeded : ++result.failed;
    }
    { std::scoped_lock lock(cache_mutex); cache_valid = false; }
    return result;
}

}  // namespace winchisel::platform
