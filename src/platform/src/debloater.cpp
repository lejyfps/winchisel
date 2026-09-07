#include "winchisel/platform/debloater.hpp"

#include <windows.h>
#include <algorithm>
#include <array>
#include <future>
#include <string>
#include <unordered_set>

namespace winchisel::platform {
namespace {

std::pair<DWORD, std::string> run_hidden(std::string command) {
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    HANDLE read_handle{}, write_handle{};
    if (!CreatePipe(&read_handle, &write_handle, &security, 0)) return {GetLastError(), {}};
    SetHandleInformation(read_handle, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOA startup{}; startup.cb = sizeof(startup); startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE; startup.hStdOutput = write_handle; startup.hStdError = write_handle;
    PROCESS_INFORMATION process{};
    if (!CreateProcessA(nullptr, command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
        const auto error = GetLastError(); CloseHandle(read_handle); CloseHandle(write_handle); return {error, {}};
    }
    CloseHandle(write_handle); std::string output; std::array<char, 4096> buffer{}; DWORD count{};
    while (ReadFile(read_handle, buffer.data(), static_cast<DWORD>(buffer.size()), &count, nullptr) && count) output.append(buffer.data(), count);
    WaitForSingleObject(process.hProcess, INFINITE); DWORD exit_code{}; GetExitCodeProcess(process.hProcess, &exit_code);
    CloseHandle(read_handle); CloseHandle(process.hThread); CloseHandle(process.hProcess); return {exit_code, std::move(output)};
}

std::unordered_set<std::string> lines(std::string command, bool strip_capability_version = false) {
    auto [exit_code, output] = run_hidden(std::move(command)); std::unordered_set<std::string> result; if (exit_code != 0) return result;
    std::size_t start{};
    while (start < output.size()) {
        auto end = output.find_first_of("\r\n", start); if (end == std::string::npos) end = output.size();
        auto value = output.substr(start, end - start); if (strip_capability_version) if (auto marker = value.find("~~~~"); marker != std::string::npos) value.resize(marker);
        std::ranges::transform(value, value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (!value.empty()) result.insert(std::move(value)); start = output.find_first_not_of("\r\n", end);
        if (start == std::string::npos) break;
    }
    return result;
}

bool contains_match(std::unordered_set<std::string> const& installed, std::string_view package) {
    std::string needle(package); std::ranges::transform(needle, needle.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return std::ranges::any_of(installed, [&](auto const& value) { return value.find(needle) != std::string::npos || needle.find(value) != std::string::npos; });
}

bool is_installed(std::unordered_set<std::string> const& installed, winchisel::core::DebloatCatalogEntry const& item) {
    if (contains_match(installed, item.package_name)) return true;
    std::size_t start{};
    while (start < item.package_aliases.size()) {
        auto end = item.package_aliases.find('|', start);
        if (end == std::string_view::npos) end = item.package_aliases.size();
        if (contains_match(installed, item.package_aliases.substr(start, end - start))) return true;
        start = end + 1;
    }
    return false;
}

std::unordered_set<std::string> appx_packages() {
    std::unordered_set<std::string> result;
    HKEY key{}; if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\AppModel\\StateRepository\\Cache\\Package\\Data", 0, KEY_READ, &key) != ERROR_SUCCESS) return result;
    for (DWORD i{};; ++i) { std::array<wchar_t, 512> name{}; DWORD length=static_cast<DWORD>(name.size()); auto status=RegEnumKeyExW(key,i,name.data(),&length,nullptr,nullptr,nullptr,nullptr); if(status==ERROR_NO_MORE_ITEMS)break; if(status!=ERROR_SUCCESS)continue; std::string value; const int count=WideCharToMultiByte(CP_UTF8,0,name.data(),static_cast<int>(length),nullptr,0,nullptr,nullptr); value.resize(count); WideCharToMultiByte(CP_UTF8,0,name.data(),static_cast<int>(length),value.data(),count,nullptr,nullptr); std::ranges::transform(value,value.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));}); result.insert(std::move(value)); } RegCloseKey(key);
    return result;
}

}  // namespace

winchisel::core::Result<std::vector<bool>> scan_debloater_installed(std::span<winchisel::core::DebloatCatalogEntry const> catalog) {
    auto apps = std::async(std::launch::async, [] { return appx_packages(); });
    auto capabilities = std::async(std::launch::async, [] { return lines("dism.exe /Online /Get-Capabilities /Format:Table", true); });
    auto features = std::async(std::launch::async, [] { return lines("dism.exe /Online /Get-Features /Format:Table"); });
    const auto app_set = apps.get(); const auto capability_set = capabilities.get(); const auto feature_set = features.get();
    std::vector<bool> result; result.reserve(catalog.size());
    for (auto const& item : catalog) {
        auto const& installed = item.category == winchisel::core::DebloatCategory::windows_apps ? app_set : item.category == winchisel::core::DebloatCategory::capabilities ? capability_set : feature_set;
        result.push_back(is_installed(installed, item));
    }
    return result;
}

winchisel::core::Result<DebloatActionResult> apply_debloater_action(std::span<winchisel::core::DebloatCatalogEntry const* const> items, bool install) {
    DebloatActionResult result;
    for (auto const* item : items) {
        bool ok{};
        if (item->category == winchisel::core::DebloatCategory::windows_apps) {
            const auto command = std::string("winget.exe ") + (install ? "install --name \"" : "uninstall --name \"") + std::string(item->package_name) + "\" --exact --disable-interactivity";
            ok = run_hidden(command).first == 0;
        } else {
            const auto quoted = "\"" + std::string(item->package_name) + "\"";
            const auto command = item->category == winchisel::core::DebloatCategory::capabilities
                ? std::string("dism.exe /Online ") + (install ? "/Add-Capability /CapabilityName:" : "/Remove-Capability /CapabilityName:") + quoted
                : std::string("dism.exe /Online ") + (install ? "/Enable-Feature /FeatureName:" : "/Disable-Feature /FeatureName:") + quoted + " /NoRestart";
            ok = run_hidden(command).first == 0;
        }
        ok ? ++result.succeeded : ++result.failed;
    }
    return result;
}

}  // namespace winchisel::platform
