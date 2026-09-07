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

std::string ps_escape(std::string_view value);

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

std::string powershell_package_array(winchisel::core::DebloatCatalogEntry const& item) {
    std::string result = "'" + ps_escape(item.package_name) + "'";
    std::size_t start{};
    while (start < item.package_aliases.size()) {
        auto end = item.package_aliases.find('|', start);
        if (end == std::string_view::npos) end = item.package_aliases.size();
        auto alias = item.package_aliases.substr(start, end - start);
        if (!alias.empty() && alias != item.package_name) result += ",'" + ps_escape(alias) + "'";
        start = end + 1;
    }
    return result;
}

std::string ps_escape(std::string_view value) { std::string result; for (char c : value) { result.push_back(c); if (c == '\'') result.push_back('\''); } return result; }

}  // namespace

winchisel::core::Result<std::vector<bool>> scan_debloater_installed(std::span<winchisel::core::DebloatCatalogEntry const> catalog) {
    auto apps = std::async(std::launch::async, [] { return lines("powershell.exe -NoLogo -NoProfile -NonInteractive -WindowStyle Hidden -Command \"Get-AppxPackage | Select-Object -ExpandProperty Name\""); });
    auto capabilities = std::async(std::launch::async, [] { return lines("powershell.exe -NoLogo -NoProfile -NonInteractive -WindowStyle Hidden -Command \"Get-WindowsCapability -Online | Where-Object State -eq Installed | Select-Object -ExpandProperty Name\"", true); });
    auto features = std::async(std::launch::async, [] { return lines("powershell.exe -NoLogo -NoProfile -NonInteractive -WindowStyle Hidden -Command \"Get-WindowsOptionalFeature -Online | Where-Object State -eq Enabled | Select-Object -ExpandProperty FeatureName\""); });
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
        const auto package = ps_escape(item->package_name); std::string script;
        if (item->category == winchisel::core::DebloatCategory::windows_apps)
            script = install
                ? "$pkgs=@(" + powershell_package_array(*item) + ");foreach($pkg in $pkgs){Get-AppxPackage -AllUsers \"*$pkg*\" -ErrorAction SilentlyContinue|%{Add-AppxPackage -DisableDevelopmentMode -Register ($_.InstallLocation+'\\AppxManifest.xml') -ErrorAction SilentlyContinue}}"
                : "$pkgs=@(" + powershell_package_array(*item) + ");foreach($pkg in $pkgs){Get-AppxPackage -Name $pkg -AllUsers|Remove-AppxPackage}";
        else if (item->category == winchisel::core::DebloatCategory::capabilities)
            script = (install ? "Add-WindowsCapability" : "Remove-WindowsCapability") + std::string(" -Online -Name '") + package + "'";
        else script = (install ? "Enable-WindowsOptionalFeature" : "Disable-WindowsOptionalFeature") + std::string(" -Online -FeatureName '") + package + "' -NoRestart";
        auto [exit_code, output] = run_hidden("powershell.exe -NoLogo -NoProfile -NonInteractive -WindowStyle Hidden -Command \"$ErrorActionPreference='Stop';" + script + "\"");
        exit_code == 0 ? ++result.succeeded : ++result.failed;
    }
    return result;
}

}  // namespace winchisel::platform
