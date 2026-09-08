#include "winchisel/platform/debloater.hpp"

#include <windows.h>
#include <algorithm>
#include <array>
#include <future>
#include <string>
#include <unordered_set>

namespace winchisel::platform {
namespace {

struct CommandResult { DWORD exit_code{}; std::string output; };

winchisel::core::Error debloater_error(std::string detail) {
    return {.code = winchisel::core::ErrorCode::platform,
        .message_key = "debloater_command_failed", .detail = std::move(detail)};
}

std::wstring wide(std::string_view value) {
    if (value.empty()) return {};
    const auto count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0);
    if (count <= 0) return {};
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
            static_cast<int>(value.size()), result.data(), count)) return {};
    return result;
}

std::string base64_utf16(std::wstring_view value) {
    static constexpr char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const auto* bytes = reinterpret_cast<unsigned char const*>(value.data());
    const auto size = value.size() * sizeof(wchar_t);
    std::string result;
    result.reserve(((size + 2) / 3) * 4);
    for (std::size_t index{}; index < size; index += 3) {
        const auto remaining = size - index;
        const unsigned value24 = (static_cast<unsigned>(bytes[index]) << 16) |
            (remaining > 1 ? static_cast<unsigned>(bytes[index + 1]) << 8 : 0) |
            (remaining > 2 ? static_cast<unsigned>(bytes[index + 2]) : 0);
        result.push_back(alphabet[(value24 >> 18) & 63]);
        result.push_back(alphabet[(value24 >> 12) & 63]);
        result.push_back(remaining > 1 ? alphabet[(value24 >> 6) & 63] : '=');
        result.push_back(remaining > 2 ? alphabet[value24 & 63] : '=');
    }
    return result;
}

winchisel::core::Result<CommandResult> run_powershell(std::wstring_view script) {
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    HANDLE read_handle{}, write_handle{};
    if (!CreatePipe(&read_handle, &write_handle, &security, 0))
        return std::unexpected(debloater_error("CreatePipe failed: " + std::to_string(GetLastError())));
    if (!SetHandleInformation(read_handle, HANDLE_FLAG_INHERIT, 0)) {
        const auto error = GetLastError(); CloseHandle(read_handle); CloseHandle(write_handle);
        return std::unexpected(debloater_error("SetHandleInformation failed: " + std::to_string(error)));
    }
    STARTUPINFOW startup{}; startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW; startup.wShowWindow = SW_HIDE;
    startup.hStdOutput = write_handle; startup.hStdError = write_handle;
    PROCESS_INFORMATION process{};
    auto encoded = base64_utf16(script);
    std::wstring command = L"powershell.exe -NoLogo -NoProfile -NonInteractive -WindowStyle Hidden -EncodedCommand ";
    command.append(encoded.begin(), encoded.end());
    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
            nullptr, nullptr, &startup, &process)) {
        const auto error = GetLastError(); CloseHandle(read_handle); CloseHandle(write_handle);
        return std::unexpected(debloater_error("CreateProcessW failed: " + std::to_string(error)));
    }
    CloseHandle(write_handle);
    std::string output; std::array<char, 4096> buffer{}; DWORD count{};
    while (ReadFile(read_handle, buffer.data(), static_cast<DWORD>(buffer.size()), &count, nullptr) && count)
        output.append(buffer.data(), count);
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exit_code{}; GetExitCodeProcess(process.hProcess, &exit_code);
    CloseHandle(read_handle); CloseHandle(process.hThread); CloseHandle(process.hProcess);
    return CommandResult{exit_code, std::move(output)};
}

winchisel::core::Result<std::unordered_set<std::string>> powershell_lines(
    std::wstring_view script, bool strip_version = false) {
    auto command = run_powershell(script);
    if (!command) return std::unexpected(command.error());
    if (command->exit_code != 0)
        return std::unexpected(debloater_error("PowerShell exited with " +
            std::to_string(command->exit_code) + (command->output.empty() ? "" : ": " + command->output)));
    std::unordered_set<std::string> result;
    std::size_t start{};
    while (start < command->output.size()) {
        auto end = command->output.find_first_of("\r\n", start);
        if (end == std::string::npos) end = command->output.size();
        auto value = command->output.substr(start, end - start);
        if (strip_version) if (const auto marker = value.find("~~~~"); marker != std::string::npos) value.resize(marker);
        while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) value.pop_back();
        std::ranges::transform(value, value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (!value.empty()) result.insert(std::move(value));
        start = command->output.find_first_not_of("\r\n", end);
        if (start == std::string::npos) break;
    }
    return result;
}

bool contains_match(std::unordered_set<std::string> const& installed, std::string_view package) {
    if (package.empty()) return false;
    std::string needle(package);
    std::ranges::transform(needle, needle.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return std::ranges::any_of(installed, [&](auto const& value) {
        return value.find(needle) != std::string::npos || needle.find(value) != std::string::npos;
    });
}

bool is_installed(std::unordered_set<std::string> const& installed,
    winchisel::core::DebloatCatalogEntry const& item) {
    if (item.package_aliases.empty()) return contains_match(installed, item.package_name);
    std::size_t start{};
    while (start < item.package_aliases.size()) {
        auto end = item.package_aliases.find('|', start);
        if (end == std::string_view::npos) end = item.package_aliases.size();
        if (contains_match(installed, item.package_aliases.substr(start, end - start))) return true;
        start = end + 1;
    }
    return false;
}

std::wstring ps_escape(std::string_view value) {
    auto result = wide(value); std::size_t position{};
    while ((position = result.find(L'\'', position)) != std::wstring::npos) {
        result.insert(position, 1, L'\''); position += 2;
    }
    return result;
}

std::vector<std::wstring> package_names(winchisel::core::DebloatCatalogEntry const& item) {
    std::vector<std::wstring> result; std::size_t start{};
    while (start < item.package_aliases.size()) {
        auto end = item.package_aliases.find('|', start);
        if (end == std::string_view::npos) end = item.package_aliases.size();
        auto alias = item.package_aliases.substr(start, end - start);
        if (!alias.empty()) result.push_back(ps_escape(alias));
        start = end + 1;
    }
    result.push_back(ps_escape(item.package_name));
    return result;
}

std::wstring appx_script(winchisel::core::DebloatCatalogEntry const& item, bool install) {
    std::wstring script = L"$ErrorActionPreference='Stop'; $pkgs=@("; bool first = true;
    for (auto const& package : package_names(item)) {
        if (!first) script += L','; first = false; script += L'\'' + package + L'\'';
    }
    script += L"); foreach($pkg in $pkgs){";
    script += install
        ? L"Get-AppxPackage -AllUsers \"*$pkg*\" -ErrorAction SilentlyContinue | ForEach-Object { Add-AppxPackage -DisableDevelopmentMode -Register \"$($_.InstallLocation)\\AppxManifest.xml\" -ErrorAction Stop }"
        : L"Get-AppxPackage -Name $pkg -AllUsers -ErrorAction SilentlyContinue | Remove-AppxPackage -ErrorAction Stop";
    script += L'}'; return script;
}

}  // namespace

winchisel::core::Result<std::vector<bool>> scan_debloater_installed(
    std::span<winchisel::core::DebloatCatalogEntry const> catalog) {
    auto apps = std::async(std::launch::async, [] { return powershell_lines(
        L"$ErrorActionPreference='Stop'; Get-AppxPackage | Select-Object -ExpandProperty Name"); });
    auto capabilities = std::async(std::launch::async, [] { return powershell_lines(
        L"$ErrorActionPreference='Stop'; Get-WindowsCapability -Online | Where-Object State -eq Installed | Select-Object -ExpandProperty Name", true); });
    auto features = std::async(std::launch::async, [] { return powershell_lines(
        L"$ErrorActionPreference='Stop'; Get-WindowsOptionalFeature -Online | Where-Object State -eq Enabled | Select-Object -ExpandProperty FeatureName"); });
    auto app_set = apps.get(); auto capability_set = capabilities.get(); auto feature_set = features.get();
    if (!app_set) return std::unexpected(app_set.error());
    if (!capability_set) return std::unexpected(capability_set.error());
    if (!feature_set) return std::unexpected(feature_set.error());
    std::vector<bool> result; result.reserve(catalog.size());
    for (auto const& item : catalog) {
        auto const& installed = item.category == winchisel::core::DebloatCategory::windows_apps ? *app_set
            : item.category == winchisel::core::DebloatCategory::capabilities ? *capability_set : *feature_set;
        result.push_back(is_installed(installed, item));
    }
    return result;
}

winchisel::core::Result<DebloatActionResult> apply_debloater_action(
    std::span<winchisel::core::DebloatCatalogEntry const* const> items, bool install) {
    DebloatActionResult result;
    for (auto const* item : items) {
        std::wstring script;
        if (item->category == winchisel::core::DebloatCategory::windows_apps) script = appx_script(*item, install);
        else if (item->category == winchisel::core::DebloatCategory::capabilities) {
            script = L"$ErrorActionPreference='Stop'; "; script += install ? L"Add-WindowsCapability" : L"Remove-WindowsCapability";
            script += L" -Online -Name '" + ps_escape(item->package_name) + L"' | Out-Null";
        } else {
            script = L"$ErrorActionPreference='Stop'; "; script += install ? L"Enable-WindowsOptionalFeature" : L"Disable-WindowsOptionalFeature";
            script += L" -Online -FeatureName '" + ps_escape(item->package_name) + L"' -NoRestart | Out-Null";
        }
        auto command = run_powershell(script);
        if (command && command->exit_code == 0) ++result.succeeded; else ++result.failed;
    }
    return result;
}

}  // namespace winchisel::platform
