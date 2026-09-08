#include "winchisel/platform/download.hpp"
#include "process_wait.hpp"

#include <Windows.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <future>
#include <fstream>
#include <mutex>
#include <regex>
#include <string>
#include <unordered_set>

namespace winchisel::platform {
namespace {
using Clock = std::chrono::steady_clock;
using StringSet = std::unordered_set<std::string>;

struct ScanData { StringSet ids, registry; Clock::time_point at{}; };
struct CommandResult { DWORD exit_code{}; std::string output; };
std::mutex cache_mutex;
ScanData cache;
bool cache_valid{};

winchisel::core::Error error(std::string detail) {
    return {.detail=std::move(detail)};
}

std::wstring wide(std::string_view text) {
    const auto count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0);
    if(count<=0)return{};std::wstring result(count,L'\0');
    return MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),result.data(),count)?result:std::wstring{};
}

winchisel::core::Result<CommandResult> run_hidden(std::wstring command) {
    auto [waited, output] = detail::run_captured(std::move(command));
    return CommandResult{waited.exit_code, std::move(output)};
}

StringSet registry_display_names(HKEY root,std::wstring const& path) {
    StringSet result;HKEY key{};if(RegOpenKeyExW(root,path.c_str(),0,KEY_READ,&key)!=ERROR_SUCCESS)return result;
    for(DWORD index{};;++index){std::array<wchar_t,256> name{};DWORD length=static_cast<DWORD>(name.size());const auto status=RegEnumKeyExW(key,index,name.data(),&length,nullptr,nullptr,nullptr,nullptr);if(status==ERROR_NO_MORE_ITEMS)break;if(status!=ERROR_SUCCESS)continue;HKEY entry{};if(RegOpenKeyExW(key,name.data(),0,KEY_READ,&entry)!=ERROR_SUCCESS)continue;DWORD type{},bytes{};if(RegQueryValueExW(entry,L"DisplayName",nullptr,&type,nullptr,&bytes)==ERROR_SUCCESS&&(type==REG_SZ||type==REG_EXPAND_SZ)&&bytes>=sizeof(wchar_t)){std::wstring value(bytes/sizeof(wchar_t),L'\0');if(RegQueryValueExW(entry,L"DisplayName",nullptr,nullptr,reinterpret_cast<BYTE*>(value.data()),&bytes)==ERROR_SUCCESS){while(!value.empty()&&!value.back())value.pop_back();const auto chars=WideCharToMultiByte(CP_UTF8,0,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);std::string utf8(chars,'\0');WideCharToMultiByte(CP_UTF8,0,value.data(),static_cast<int>(value.size()),utf8.data(),chars,nullptr,nullptr);std::ranges::transform(utf8,utf8.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});result.insert(std::move(utf8));}}RegCloseKey(entry);}RegCloseKey(key);return result;
}

winchisel::core::Result<StringSet> winget_ids() {
    std::array<wchar_t,MAX_PATH> temp{},path{};if(!GetTempPathW(static_cast<DWORD>(temp.size()),temp.data())||!GetTempFileNameW(temp.data(),L"wci",0,path.data()))return std::unexpected(error("Temporary export path unavailable"));
    auto command=run_hidden(L"winget.exe export --output \""+std::wstring(path.data())+L"\" --accept-source-agreements --nowarn --disable-interactivity");
    std::string json;
    {
        std::ifstream input(path.data(),std::ios::binary);
        json.assign(std::istreambuf_iterator<char>(input),{});
    }
    DeleteFileW(path.data());
    if(!command)return std::unexpected(command.error());if(command->exit_code!=0)return std::unexpected(error("winget export exited with "+std::to_string(command->exit_code)));
    StringSet result;static const std::regex id(R"json("PackageIdentifier"\s*:\s*"([^"]+)")json");for(std::sregex_iterator it(json.begin(),json.end(),id),end;it!=end;++it){auto value=(*it)[1].str();std::ranges::transform(value,value.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});result.insert(std::move(value));}return result;
}

winchisel::core::Result<ScanData> perform_scan() {
    auto ids=std::async(std::launch::async,winget_ids);
    auto registry=std::async(std::launch::async,[]{StringSet result;auto append=[&](auto values){result.insert(values.begin(),values.end());};append(registry_display_names(HKEY_LOCAL_MACHINE,L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall"));append(registry_display_names(HKEY_LOCAL_MACHINE,L"SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall"));HKEY users{};if(RegOpenKeyExW(HKEY_USERS,nullptr,0,KEY_READ,&users)==ERROR_SUCCESS){for(DWORD i{};;++i){std::array<wchar_t,256> sid{};DWORD length=static_cast<DWORD>(sid.size());const auto status=RegEnumKeyExW(users,i,sid.data(),&length,nullptr,nullptr,nullptr,nullptr);if(status==ERROR_NO_MORE_ITEMS)break;if(status!=ERROR_SUCCESS||wcsstr(sid.data(),L"_Classes"))continue;append(registry_display_names(HKEY_USERS,std::wstring(sid.data(),length)+L"\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall"));}RegCloseKey(users);}return result;});
    auto id_result=ids.get();auto registry_result=registry.get();
    if(!id_result)return std::unexpected(id_result.error());
    return ScanData{std::move(*id_result),std::move(registry_result),Clock::now()};
}

bool contains(StringSet const& values,std::string_view needle,bool exact=false){std::string lower(needle);std::ranges::transform(lower,lower.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});return std::ranges::any_of(values,[&](auto const& value){return exact?value==lower:value==lower||value.find(lower)!=std::string::npos||lower.find(value)!=std::string::npos;});}
}

winchisel::core::Result<std::vector<bool>> scan_downloads_installed(std::span<winchisel::core::DownloadCatalogEntry const> catalog,bool force_refresh){ScanData data;{std::scoped_lock lock(cache_mutex);if(!force_refresh&&cache_valid&&Clock::now()-cache.at<std::chrono::minutes(10))data=cache;}if(data.at==Clock::time_point{}){auto scanned=perform_scan();if(!scanned)return std::unexpected(scanned.error());data=std::move(*scanned);std::scoped_lock lock(cache_mutex);cache=data;cache_valid=true;}std::vector<bool> result;result.reserve(catalog.size());for(auto const& item:catalog){bool installed=contains(data.registry,item.name);std::size_t start{};while(!installed&&start<item.winget_ids.size()){auto end=item.winget_ids.find('|',start);if(end==std::string_view::npos)end=item.winget_ids.size();installed=contains(data.ids,item.winget_ids.substr(start,end-start),true);start=end+1;}result.push_back(installed);}return result;}

winchisel::core::Result<DownloadInstallResult> install_downloads(std::span<winchisel::core::DownloadCatalogEntry const* const> items){DownloadInstallResult result;for(auto item:items){auto end=item->winget_ids.find('|');auto id=item->winget_ids.substr(0,end);if(id.empty()){++result.failed;result.failure_details.push_back(std::string(item->name)+": no winget ID");continue;}auto command=run_hidden(L"winget.exe install --id \""+wide(id)+L"\" --exact --accept-package-agreements --accept-source-agreements --disable-interactivity");if(command&&command->exit_code==0){++result.succeeded;std::string installed(id);std::ranges::transform(installed,installed.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});std::scoped_lock lock(cache_mutex);if(cache_valid){cache.ids.insert(std::move(installed));cache.at=Clock::now();}}else{++result.failed;result.failure_details.push_back(std::string(item->name)+": "+(command?"winget exit "+std::to_string(command->exit_code):command.error().detail));}}return result;}

}  // namespace winchisel::platform
