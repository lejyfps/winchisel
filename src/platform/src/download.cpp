#include "winchisel/platform/download.hpp"

#include <Windows.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <future>
#include <mutex>
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

std::unordered_set<std::string> query(std::string script) {
    auto [code, output] = run_hidden("powershell.exe -NoLogo -NoProfile -NonInteractive -WindowStyle Hidden -Command \"" + script + "\"");
    std::unordered_set<std::string> result; if (code != 0) return result;
    std::size_t start{}; while (start < output.size()) {
        auto end = output.find_first_of("\r\n", start); if (end == std::string::npos) end = output.size();
        auto value = output.substr(start, end - start); std::ranges::transform(value, value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (!value.empty()) result.insert(std::move(value)); start = output.find_first_not_of("\r\n", end); if (start == std::string::npos) break;
    } return result;
}

ScanData perform_scan() {
    auto ids = std::async(std::launch::async, [] { return query("$t=[IO.Path]::GetTempFileName()+'.json';winget export -o $t --accept-source-agreements --nowarn --disable-interactivity 2>$null|Out-Null;if(Test-Path $t){try{(Get-Content $t -Raw|ConvertFrom-Json).Sources.Packages.PackageIdentifier}catch{};Remove-Item $t -Force -ErrorAction SilentlyContinue}"); });
    auto names = std::async(std::launch::async, [] { return query("winget list --accept-source-agreements --disable-interactivity|Select-Object -Skip 2|%{$l=$_.ToString().Trim();if($l){($l -split '\\s{2,}')[0]}}"); });
    auto registry = std::async(std::launch::async, [] { return query("$p=[Collections.Generic.List[string]]@('HKLM:\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall','HKLM:\\SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall');if(!(Get-PSDrive HKU -ErrorAction SilentlyContinue)){New-PSDrive HKU Registry HKEY_USERS|Out-Null};Get-ChildItem HKU:\\|?{$_.PSChildName -notmatch '_Classes$'}|%{$p.Add('HKU:\\'+$_.PSChildName+'\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall');$p.Add('HKU:\\'+$_.PSChildName+'\\SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall')};$p|%{if(Test-Path $_){Get-ChildItem $_ -ErrorAction SilentlyContinue|%{(Get-ItemProperty $_.PSPath -Name DisplayName -ErrorAction SilentlyContinue).DisplayName}}}"); });
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
