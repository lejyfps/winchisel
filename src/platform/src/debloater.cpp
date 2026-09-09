#include "winchisel/platform/debloater.hpp"
#include "process_wait.hpp"
#include "com_apartment.hpp"

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
#include <optional>
#include <ranges>
#include <string>
#include <unordered_set>
#include <vector>

#pragma comment(lib, "runtimeobject.lib")

namespace winchisel::platform {
namespace {
using StringSet=std::unordered_set<std::string>;
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

winchisel::core::Result<bool> change_appx(
    winrt::Windows::Management::Deployment::PackageManager& manager,
    std::vector<std::pair<std::string, winrt::Windows::ApplicationModel::Package>> const& packages,
    winchisel::core::DebloatCatalogEntry const& item, bool install){try{if(install&&!item.can_reinstall)return std::unexpected(error(std::string(item.package_name)+": reinstall is not supported"));bool matched{};bool registered{};for(auto const& [name,package]:packages){if(!item_matches(name,item))continue;matched=true;if(install){auto location=package.InstalledLocation();if(location){manager.RegisterPackageAsync(winrt::Windows::Foundation::Uri(winrt::hstring(std::wstring(location.Path().c_str())+L"\\AppxManifest.xml")),nullptr,winrt::Windows::Management::Deployment::DeploymentOptions::None).get();registered=true;}}else manager.RemovePackageAsync(package.Id().FullName()).get();}if(install&&!registered){if(item.store_id.empty())return std::unexpected(error(std::string(item.package_name)+(matched?": local payload has no manifest":": no local payload and no Store ID")));auto uri=L"ms-windows-store://pdp/?ProductId="+wide(item.store_id);if(reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr,L"open",uri.c_str(),nullptr,nullptr,SW_SHOWNORMAL))<=32)return std::unexpected(error(std::string(item.package_name)+": Store listing could not be opened"));}return false;}catch(winrt::hresult_error const& value){return std::unexpected(error(std::string(item.package_name)+": "+winrt::to_string(value.message())));}}
}

winchisel::core::Result<std::vector<bool>> scan_debloater_installed(std::span<winchisel::core::DebloatCatalogEntry const> catalog){auto apps=appx_packages();if(!apps)return std::unexpected(apps.error());auto capabilities=dism_packages(true);if(!capabilities)return std::unexpected(capabilities.error());auto features=dism_packages(false);if(!features)return std::unexpected(features.error());std::vector<bool> result;result.reserve(catalog.size());for(auto const& item:catalog){auto const& installed=item.category==winchisel::core::DebloatCategory::windows_apps?*apps:item.category==winchisel::core::DebloatCategory::capabilities?*capabilities:*features;result.push_back(is_installed(installed,item));}return result;}
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
winchisel::core::Result<DebloatActionResult> apply_debloater_action(std::span<winchisel::core::DebloatCatalogEntry const* const> items,bool install){DebloatActionResult result;std::optional<AppxBatch> appx;const bool needs_appx=std::ranges::any_of(items,[](auto const* item){return item->category==winchisel::core::DebloatCategory::windows_apps;});if(needs_appx){appx.emplace();ensure_appx_batch(*appx);}for(auto const* item:items){winchisel::core::Result<bool> changed{std::unexpect,error("internal error")};if(item->category==winchisel::core::DebloatCategory::windows_apps){changed=(appx&&appx->ready)?change_appx(appx->manager,appx->packages,*item,install):std::unexpected(appx?appx->error:error("PackageManager unavailable"));}else changed=change_dism(*item,install);if(changed){++result.succeeded;if(item->requires_reboot||*changed)result.reboot_required=true;}else{++result.failed;result.failure_details.push_back(item->package_name.empty()?std::string(item->id):std::string(item->package_name)+": "+changed.error().detail);}}return result;}
}
