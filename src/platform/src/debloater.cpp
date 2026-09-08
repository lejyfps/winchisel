#include "winchisel/platform/debloater.hpp"
#include "process_wait.hpp"

#include <windows.h>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Management.Deployment.h>

#include <algorithm>
#include <string>
#include <unordered_set>
#include <vector>

#pragma comment(lib, "runtimeobject.lib")

namespace winchisel::platform {
namespace {
using StringSet=std::unordered_set<std::string>;
struct CommandResult{DWORD code{};bool timed_out{};std::string output;};
winchisel::core::Error error(std::string detail){return{.code=winchisel::core::ErrorCode::platform,.message_key="debloater_command_failed",.detail=std::move(detail)};}
std::string lower(std::string value){std::ranges::transform(value,value.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});return value;}
std::string trim(std::string value){while(!value.empty()&&std::isspace(static_cast<unsigned char>(value.back())))value.pop_back();auto first=std::ranges::find_if(value,[](unsigned char c){return !std::isspace(c);});value.erase(value.begin(),first);return value;}
std::wstring wide(std::string_view value){const auto count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0);std::wstring result(count,L'\0');if(count)MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),result.data(),count);return result;}

winchisel::core::Result<CommandResult> run_native(std::wstring command){SECURITY_ATTRIBUTES security{sizeof(security),nullptr,TRUE};HANDLE read{},write{};if(!CreatePipe(&read,&write,&security,0))return std::unexpected(error("CreatePipe: "+std::to_string(GetLastError())));SetHandleInformation(read,HANDLE_FLAG_INHERIT,0);STARTUPINFOW startup{.cb=sizeof(startup),.dwFlags=STARTF_USESTDHANDLES|STARTF_USESHOWWINDOW,.wShowWindow=SW_HIDE,.hStdOutput=write,.hStdError=write};PROCESS_INFORMATION process{};if(!CreateProcessW(nullptr,command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process)){auto code=GetLastError();CloseHandle(read);CloseHandle(write);return std::unexpected(error("CreateProcessW: "+std::to_string(code)));}CloseHandle(write);std::string output;auto waited=detail::wait_process_with_pipe(process.hProcess,read,30*60*1000,[&](char const* data,DWORD size){output.append(data,size);});CloseHandle(read);CloseHandle(process.hThread);CloseHandle(process.hProcess);return CommandResult{waited.exit_code,waited.timed_out,std::move(output)};}

bool contains_match(StringSet const& installed,std::string_view package){if(package.empty())return false;auto needle=lower(std::string(package));return std::ranges::any_of(installed,[&](auto const& value){return value.find(needle)!=std::string::npos||needle.find(value)!=std::string::npos;});}
bool is_installed(StringSet const& installed,winchisel::core::DebloatCatalogEntry const& item){std::size_t start{};while(start<item.package_aliases.size()){auto end=item.package_aliases.find('|',start);if(end==std::string_view::npos)end=item.package_aliases.size();if(contains_match(installed,item.package_aliases.substr(start,end-start)))return true;start=end+1;}return contains_match(installed,item.package_name);}
bool item_matches(std::string name,winchisel::core::DebloatCatalogEntry const& item){return is_installed(StringSet{lower(std::move(name))},item);}

winchisel::core::Result<StringSet> appx_packages(){try{winrt::init_apartment(winrt::apartment_type::multi_threaded);winrt::Windows::Management::Deployment::PackageManager manager;StringSet result;for(auto const& package:manager.FindPackagesForUser(L""))result.insert(lower(winrt::to_string(package.Id().Name())));return result;}catch(winrt::hresult_error const& value){return std::unexpected(error("PackageManager scan: "+winrt::to_string(value.message())));}}

winchisel::core::Result<StringSet> dism_packages(bool capabilities){auto command=run_native(capabilities?L"dism.exe /Online /Get-Capabilities /English /Format:Table":L"dism.exe /Online /Get-Features /English /Format:Table");if(!command)return std::unexpected(command.error());if(command->code)return std::unexpected(error(command->timed_out?"DISM scan timed out":"DISM scan exited with "+std::to_string(command->code)));StringSet result;std::size_t start{};while(start<command->output.size()){auto end=command->output.find_first_of("\r\n",start);if(end==std::string::npos)end=command->output.size();auto line=command->output.substr(start,end-start);if(auto separator=line.find('|');separator!=std::string::npos){auto name=lower(trim(line.substr(0,separator))),state=lower(trim(line.substr(separator+1)));if(state=="installed"||state=="enabled"){if(capabilities)if(auto marker=name.find("~~~~");marker!=std::string::npos)name.resize(marker);result.insert(std::move(name));}}start=command->output.find_first_not_of("\r\n",end);if(start==std::string::npos)break;}return result;}

winchisel::core::Result<void> change_dism(winchisel::core::DebloatCatalogEntry const& item,bool install){const auto name=wide(item.package_name);std::wstring command=L"dism.exe /Online /English /NoRestart ";if(item.category==winchisel::core::DebloatCategory::capabilities)command+=(install?L"/Add-Capability /CapabilityName:\"":L"/Remove-Capability /CapabilityName:\"")+name+L"\"";else command+=(install?L"/Enable-Feature /FeatureName:\"":L"/Disable-Feature /FeatureName:\"")+name+L"\"";auto result=run_native(std::move(command));if(!result)return std::unexpected(result.error());if(result->code)return std::unexpected(error(result->timed_out?"DISM action timed out":"DISM action exited with "+std::to_string(result->code)));return{};}

winchisel::core::Result<void> change_appx(winchisel::core::DebloatCatalogEntry const& item,bool install){try{winrt::init_apartment(winrt::apartment_type::multi_threaded);winrt::Windows::Management::Deployment::PackageManager manager;bool matched{};for(auto const& package:manager.FindPackages()){if(!item_matches(winrt::to_string(package.Id().Name()),item))continue;matched=true;if(install){auto location=package.InstalledLocation();if(location)manager.RegisterPackageAsync(winrt::Windows::Foundation::Uri(location.Path()+L"\\AppxManifest.xml"),nullptr,winrt::Windows::Management::Deployment::DeploymentOptions::None).get();}else manager.RemovePackageAsync(package.Id().FullName(),winrt::Windows::Management::Deployment::RemovalOptions::RemoveForAllUsers).get();}if(install&&!matched)return std::unexpected(error("No installed AppX payload was found for registration"));return{};}catch(winrt::hresult_error const& value){return std::unexpected(error("PackageManager action: "+winrt::to_string(value.message())));}}
}

winchisel::core::Result<std::vector<bool>> scan_debloater_installed(std::span<winchisel::core::DebloatCatalogEntry const> catalog){auto apps=appx_packages();if(!apps)return std::unexpected(apps.error());auto capabilities=dism_packages(true);if(!capabilities)return std::unexpected(capabilities.error());auto features=dism_packages(false);if(!features)return std::unexpected(features.error());std::vector<bool> result;result.reserve(catalog.size());for(auto const& item:catalog){auto const& installed=item.category==winchisel::core::DebloatCategory::windows_apps?*apps:item.category==winchisel::core::DebloatCategory::capabilities?*capabilities:*features;result.push_back(is_installed(installed,item));}return result;}
winchisel::core::Result<DebloatActionResult> apply_debloater_action(std::span<winchisel::core::DebloatCatalogEntry const* const> items,bool install){DebloatActionResult result;for(auto const* item:items){auto changed=item->category==winchisel::core::DebloatCategory::windows_apps?change_appx(*item,install):change_dism(*item,install);if(changed)++result.succeeded;else ++result.failed;}return result;}
}
