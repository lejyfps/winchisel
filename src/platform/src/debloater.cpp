#include "winchisel/platform/debloater.hpp"
#include "process_wait.hpp"
#include "com_apartment.hpp"
#include "scan_cache.hpp"

#include <windows.h>
#include <shellapi.h>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Management.Deployment.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <future>
#include <memory>
#include <atomic>
#include <mutex>
#include <thread>
#include <optional>
#include <ranges>
#include <string>
#include <unordered_set>
#include <vector>

#pragma comment(lib, "runtimeobject.lib")

namespace winchisel::platform {
namespace {
using StringSet=std::unordered_set<std::string>;
using Clock=std::chrono::steady_clock;
struct DebloatScan { StringSet apps, capabilities, features; Clock::time_point at{}; };
std::mutex scan_mutex;
DebloatScan scan_cache;
bool scan_cache_valid{};
// Change signals for the DISM state: every feature/capability change runs a
// servicing session, which rewrites Sessions.xml and touches the CBS Packages
// key; a Windows update changes the build/UBR.
std::string dism_fingerprint() {
    wchar_t windows[MAX_PATH]{};
    const auto length = GetWindowsDirectoryW(windows, MAX_PATH);
    const std::filesystem::path sessions = std::filesystem::path(std::wstring(windows, length)) / L"servicing" / L"Sessions" / L"Sessions.xml";
    return persistent_cache::windows_build_stamp()
        + "|" + persistent_cache::key_stamp(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Component Based Servicing\\Packages")
        + "|" + persistent_cache::file_stamp(sessions);
}
// A scan in progress, shared by every caller that arrives meanwhile (page
// open during the startup prewarm, quick page switches) so DISM runs once.
std::shared_ptr<std::shared_future<winchisel::core::Result<DebloatScan>>> scan_inflight;
struct CommandResult{DWORD code{};bool timed_out{};std::string output;};
winchisel::core::Error error(std::string detail){return{.detail=std::move(detail)};}
std::string lower(std::string value){std::ranges::transform(value,value.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});return value;}
std::string trim(std::string value){while(!value.empty()&&std::isspace(static_cast<unsigned char>(value.back())))value.pop_back();auto first=std::ranges::find_if(value,[](unsigned char c){return !std::isspace(c);});value.erase(value.begin(),first);return value;}
std::wstring wide(std::string_view value){const auto count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0);std::wstring result(count,L'\0');if(count)MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),result.data(),count);return result;}

winchisel::core::Result<CommandResult> run_native(std::wstring command, DWORD timeout_ms = 5 * 60 * 1000){auto [waited,output]=detail::run_captured(std::move(command),timeout_ms);return CommandResult{waited.exit_code,waited.timed_out,std::move(output)};}

bool name_equals(std::string_view left,std::string_view right){if(left.size()!=right.size())return false;for(std::size_t i{};i<left.size();++i)if(std::tolower(static_cast<unsigned char>(left[i]))!=std::tolower(static_cast<unsigned char>(right[i])))return false;return true;}
bool exact_match(StringSet const& installed,std::string_view package){if(package.empty())return false;return std::ranges::any_of(installed,[&](auto const& value){return name_equals(value,package);});}
bool is_installed(StringSet const& installed,winchisel::core::DebloatCatalogEntry const& item){std::size_t start{};while(start<item.package_aliases.size()){auto end=item.package_aliases.find('|',start);if(end==std::string::npos)end=item.package_aliases.size();if(exact_match(installed,item.package_aliases.substr(start,end-start)))return true;start=end+1;}return exact_match(installed,item.package_name);}
bool item_matches(std::string const& name,winchisel::core::DebloatCatalogEntry const& item){if(name_equals(name,item.package_name))return true;std::size_t start{};while(start<item.package_aliases.size()){auto end=item.package_aliases.find('|',start);if(end==std::string::npos)end=item.package_aliases.size();if(name_equals(name,item.package_aliases.substr(start,end-start)))return true;start=end+1;}return false;}

winchisel::core::Result<StringSet> appx_packages(){try{detail::ComApartment com;if(!com)return std::unexpected(error(std::to_string(com.hr)));winrt::Windows::Management::Deployment::PackageManager manager;StringSet result;for(auto const& package:manager.FindPackagesForUser(L""))result.insert(lower(winrt::to_string(package.Id().Name())));return result;}catch(winrt::hresult_error const& value){return std::unexpected(error("PackageManager scan: "+winrt::to_string(value.message())));}}

winchisel::core::Result<StringSet> dism_packages(bool capabilities){auto command=run_native(capabilities?L"dism.exe /Online /Get-Capabilities /English /Format:Table":L"dism.exe /Online /Get-Features /English /Format:Table");if(!command)return std::unexpected(command.error());if(command->code)return std::unexpected(error(command->timed_out?"DISM scan timed out":"DISM scan exited with "+std::to_string(command->code)));StringSet result;std::size_t start{};while(start<command->output.size()){auto end=command->output.find_first_of("\r\n",start);if(end==std::string::npos)end=command->output.size();auto line=command->output.substr(start,end-start);if(auto separator=line.find('|');separator!=std::string::npos){auto name=lower(trim(line.substr(0,separator))),state=lower(trim(line.substr(separator+1)));if(state=="installed"||state=="enabled"){if(capabilities)if(auto marker=name.find("~~~~");marker!=std::string::npos)name.resize(marker);result.insert(std::move(name));}}start=command->output.find_first_not_of("\r\n",end);if(start==std::string::npos)break;}return result;}

winchisel::core::Result<bool> change_dism(winchisel::core::DebloatCatalogEntry const& item,bool install){const auto name=wide(item.package_name);std::wstring command=L"dism.exe /Online /English /NoRestart ";if(item.category==winchisel::core::DebloatCategory::capabilities)command+=(install?L"/Add-Capability /CapabilityName:\"":L"/Remove-Capability /CapabilityName:\"")+name+L"\"";else command+=(install?L"/Enable-Feature /FeatureName:\"":L"/Disable-Feature /FeatureName:\"")+name+L"\"";auto result=run_native(std::move(command), 20 * 60 * 1000);if(!result)return std::unexpected(result.error());if(result->timed_out)return std::unexpected(error("DISM action timed out"));if(result->code==0)return false;if(result->code==ERROR_SUCCESS_REBOOT_REQUIRED)return true;return std::unexpected(error("DISM action exited with "+std::to_string(result->code)));}

// Refreshes a DISM capability/feature by removing and re-adding it so the
// latest payload is applied. A failed re-add leaves the item removed, which
// is reported explicitly in the failure detail.
winchisel::core::Result<bool> update_dism(winchisel::core::DebloatCatalogEntry const& item){auto removed=change_dism(item,false);if(!removed)return std::unexpected(removed.error());auto added=change_dism(item,true);if(!added)return std::unexpected(error(std::string(item.package_name)+": re-install failed after removal ("+added.error().detail+")"));return *removed||*added;}

winchisel::core::Result<bool> change_appx(
    winrt::Windows::Management::Deployment::PackageManager& manager,
    std::vector<std::pair<std::string, winrt::Windows::ApplicationModel::Package>> const& packages,
    winchisel::core::DebloatCatalogEntry const& item, bool install){try{if(install&&!item.can_reinstall)return std::unexpected(error(std::string(item.package_name)+": reinstall is not supported"));bool matched{};bool registered{};for(auto const& [name,package]:packages){if(!item_matches(name,item))continue;matched=true;if(install){auto location=package.InstalledLocation();if(location){manager.RegisterPackageAsync(winrt::Windows::Foundation::Uri(winrt::hstring(std::wstring(location.Path().c_str())+L"\\AppxManifest.xml")),nullptr,winrt::Windows::Management::Deployment::DeploymentOptions::None).get();registered=true;}}else manager.RemovePackageAsync(package.Id().FullName()).get();}if(install&&!registered){if(item.store_id.empty())return std::unexpected(error(std::string(item.package_name)+(matched?": local payload has no manifest":": no local payload and no Store ID")));auto uri=L"ms-windows-store://pdp/?ProductId="+wide(item.store_id);if(reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr,L"open",uri.c_str(),nullptr,nullptr,SW_SHOWNORMAL))<=32)return std::unexpected(error(std::string(item.package_name)+": Store listing could not be opened"));}return false;}catch(winrt::hresult_error const& value){return std::unexpected(error(std::string(item.package_name)+": "+winrt::to_string(value.message())));}}

// Updates an installed Windows app: with a Store ID the Store listing is
// opened so the update comes from the Store; without one the package is
// re-registered (repair) from its local payload.
winchisel::core::Result<bool> update_appx(
    winrt::Windows::Management::Deployment::PackageManager& manager,
    std::vector<std::pair<std::string, winrt::Windows::ApplicationModel::Package>> const& packages,
    winchisel::core::DebloatCatalogEntry const& item){if(!item.store_id.empty()){auto uri=L"ms-windows-store://pdp/?ProductId="+wide(item.store_id);if(reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr,L"open",uri.c_str(),nullptr,nullptr,SW_SHOWNORMAL))<=32)return std::unexpected(error(std::string(item.package_name)+": Store listing could not be opened"));return false;}return change_appx(manager,packages,item,true);}

// Every DISM scan (foreground or background) runs under this lock: two
// concurrent DISM sessions on the online image can block or fail each other.
std::mutex dism_scan_mutex;
std::atomic_bool dism_background_running{};

struct DismState { winchisel::core::Result<StringSet> capabilities, features; };
DismState scan_dism_now(std::string const& fingerprint) {
    std::scoped_lock lock(dism_scan_mutex);
    DismState state{dism_packages(true), {}};
    state.features = state.capabilities ? dism_packages(false) : winchisel::core::Result<StringSet>{};
    if (state.capabilities && state.features)
        persistent_cache::store("debloat-dism", fingerprint, {
            {"capabilities", {state.capabilities->begin(), state.capabilities->end()}},
            {"features", {state.features->begin(), state.features->end()}}});
    return state;
}

// Rescans DISM for a stale (but fingerprint-matching) cache entry without
// making anyone wait. If the result differs, the in-memory scan is dropped so
// the next page open shows it. One background refresh at a time.
void refresh_dism_in_background(std::string fingerprint, StringSet cached_capabilities, StringSet cached_features) {
    if (dism_background_running.exchange(true)) return;
    std::thread([fingerprint = std::move(fingerprint), cached_capabilities = std::move(cached_capabilities), cached_features = std::move(cached_features)] {
        try {
            auto fresh = scan_dism_now(fingerprint);
            if (fresh.capabilities && fresh.features && (*fresh.capabilities != cached_capabilities || *fresh.features != cached_features)) {
                std::scoped_lock lock(scan_mutex);
                scan_cache_valid = false;
            }
        } catch (...) {}
        dism_background_running = false;
    }).detach();
}
}

winchisel::core::Result<std::vector<bool>> scan_debloater_installed(
    std::span<winchisel::core::DebloatCatalogEntry const> catalog, bool force_refresh) {
    DebloatScan data;
    bool hit{};
    // Manual refresh: bypass the persistent DISM cache as well.
    if (force_refresh) persistent_cache::invalidate("debloat-dism");
    {
        std::scoped_lock lock(scan_mutex);
        if (!force_refresh && scan_cache_valid && Clock::now() - scan_cache.at < std::chrono::minutes(10)) {
            data = scan_cache;
            hit = true;
        }
    }
    if (!hit) {
        std::shared_ptr<std::shared_future<winchisel::core::Result<DebloatScan>>> flight;
        {
            std::scoped_lock lock(scan_mutex);
            if (!scan_inflight) {
                scan_inflight = std::make_shared<std::shared_future<winchisel::core::Result<DebloatScan>>>(
                    std::async(std::launch::async, []() -> winchisel::core::Result<DebloatScan> {
                        // The AppX enumeration (WinRT, in-process) runs while
                        // DISM works. The two DISM queries stay sequential:
                        // concurrent DISM sessions on the online image can
                        // block or fail each other.
                        auto apps = std::async(std::launch::async, appx_packages);
                        // DISM results come from the persistent cache while
                        // the servicing fingerprint is unchanged (computed
                        // before scanning, so a change during the scan only
                        // makes the next start rescan). Entries never expire;
                        // after 24 h a hit is still shown at once and DISM
                        // re-checks it in the background.
                        const auto fingerprint = dism_fingerprint();
                        winchisel::core::Result<StringSet> capabilities{std::unexpect, error("not scanned")};
                        winchisel::core::Result<StringSet> features{std::unexpect, error("not scanned")};
                        if (auto cached = persistent_cache::load("debloat-dism", fingerprint, std::chrono::hours(24))) {
                            capabilities = StringSet(cached->lists["capabilities"].begin(), cached->lists["capabilities"].end());
                            features = StringSet(cached->lists["features"].begin(), cached->lists["features"].end());
                            if (cached->stale) refresh_dism_in_background(fingerprint, *capabilities, *features);
                        } else {
                            auto state = scan_dism_now(fingerprint);
                            capabilities = std::move(state.capabilities);
                            features = std::move(state.features);
                        }
                        auto app_result = apps.get();
                        if (!app_result) return std::unexpected(app_result.error());
                        if (!capabilities) return std::unexpected(capabilities.error());
                        if (!features) return std::unexpected(features.error());
                        return DebloatScan{std::move(*app_result), std::move(*capabilities), std::move(*features), Clock::now()};
                    }).share());
            }
            flight = scan_inflight;
        }
        auto scanned = flight->get();
        std::scoped_lock lock(scan_mutex);
        if (scan_inflight == flight) scan_inflight.reset();
        if (!scanned) return std::unexpected(scanned.error());
        scan_cache = *scanned;
        scan_cache_valid = true;
        data = scan_cache;
    }
    std::vector<bool> result;
    result.reserve(catalog.size());
    for (auto const& item : catalog) {
        auto const& installed = item.category == winchisel::core::DebloatCategory::windows_apps ? data.apps
            : item.category == winchisel::core::DebloatCategory::capabilities ? data.capabilities : data.features;
        result.push_back(is_installed(installed, item));
    }
    return result;
}

void invalidate_debloat_scan() {
    {
        std::scoped_lock lock(scan_mutex);
        scan_cache_valid = false;
    }
    persistent_cache::invalidate("debloat-dism");
}
// One COM apartment, one PackageManager and one enumeration shared by every
// AppX item of the batch instead of re-initializing per item. Members are
// declared so that the manager is destroyed before the apartment uninitializes.
struct AppxBatch {
    detail::ComApartment com;
    winrt::Windows::Management::Deployment::PackageManager manager;
    std::vector<std::pair<std::string, winrt::Windows::ApplicationModel::Package>> packages;
    winchisel::core::Error error;
    bool ready{};
};
void ensure_appx_batch(AppxBatch& batch){
    if(batch.ready||!batch.error.detail.empty())return;
    if(!batch.com){batch.error=error(std::to_string(batch.com.hr));return;}
    try{for(auto const& package:batch.manager.FindPackagesForUser(L""))batch.packages.emplace_back(winrt::to_string(package.Id().Name()),package);}
    catch(winrt::hresult_error const& value){batch.error=error("PackageManager scan: "+winrt::to_string(value.message()));return;}
    batch.ready=true;
}
winchisel::core::Result<DebloatActionResult> apply_debloater_action(std::span<winchisel::core::DebloatCatalogEntry const* const> items,bool install){invalidate_debloat_scan();DebloatActionResult result;std::optional<AppxBatch> appx;const bool needs_appx=std::ranges::any_of(items,[](auto const* item){return item->category==winchisel::core::DebloatCategory::windows_apps;});if(needs_appx){appx.emplace();ensure_appx_batch(*appx);}for(auto const* item:items){winchisel::core::Result<bool> changed{std::unexpect,error("internal error")};if(item->category==winchisel::core::DebloatCategory::windows_apps){changed=(appx&&appx->ready)?change_appx(appx->manager,appx->packages,*item,install):std::unexpected(appx?appx->error:error("PackageManager unavailable"));}else changed=change_dism(*item,install);if(changed){++result.succeeded;if(item->requires_reboot||*changed)result.reboot_required=true;}else{++result.failed;result.failure_details.push_back(item->package_name.empty()?std::string(item->id):std::string(item->package_name)+": "+changed.error().detail);}}return result;}

winchisel::core::Result<DebloatActionResult> apply_debloater_update(std::span<winchisel::core::DebloatCatalogEntry const* const> items){invalidate_debloat_scan();DebloatActionResult result;std::optional<AppxBatch> appx;const bool needs_appx=std::ranges::any_of(items,[](auto const* item){return item->category==winchisel::core::DebloatCategory::windows_apps;});if(needs_appx){appx.emplace();ensure_appx_batch(*appx);}for(auto const* item:items){winchisel::core::Result<bool> changed{std::unexpect,error("internal error")};if(item->category==winchisel::core::DebloatCategory::windows_apps){changed=(appx&&appx->ready)?update_appx(appx->manager,appx->packages,*item):std::unexpected(appx?appx->error:error("PackageManager unavailable"));}else changed=update_dism(*item);if(changed){++result.succeeded;if(item->requires_reboot||*changed)result.reboot_required=true;}else{++result.failed;result.failure_details.push_back(item->package_name.empty()?std::string(item->id):std::string(item->package_name)+": "+changed.error().detail);}}return result;}
}
