#include "winchisel/platform/performance.hpp"
#include "winchisel/platform/registry.hpp"
#include "winchisel/platform/system.hpp"
#include "process_wait.hpp"
#include "com_apartment.hpp"
#include <Windows.h>
#include <taskschd.h>
#include <comdef.h>
#include <array>
#include <algorithm>
#include <cctype>
#include <string>
#include <unordered_map>
#include <mutex>
#include <vector>
#include <utility>
#pragma comment(lib, "taskschd.lib")
#pragma comment(lib, "ole32.lib")
namespace winchisel::platform { namespace {
std::string_view task_path(std::string_view id){static constexpr std::pair<std::string_view,std::string_view> tasks[]{
{"CompatibilityAppraiserTask","\\Microsoft\\Windows\\Application Experience\\Microsoft Compatibility Appraiser"},{"ProgramDataUpdaterTask","\\Microsoft\\Windows\\Application Experience\\ProgramDataUpdater"},{"CEIPConsolidatorTask","\\Microsoft\\Windows\\Customer Experience Improvement Program\\Consolidator"},{"UsbCeipTask","\\Microsoft\\Windows\\Customer Experience Improvement Program\\UsbCeip"},{"DiskDiagnosticTask","\\Microsoft\\Windows\\DiskDiagnostic\\Microsoft-Windows-DiskDiagnosticDataCollector"},{"FeedbackDmClientTask","\\Microsoft\\Windows\\Feedback\\Siuf\\DmClient"},{"FeedbackDmClientDownloadTask","\\Microsoft\\Windows\\Feedback\\Siuf\\DmClientOnScenarioDownload"},{"ErrorReportingQueueTask","\\Microsoft\\Windows\\Windows Error Reporting\\QueueReporting"},{"SqmTask","\\Microsoft\\Windows\\PI\\Sqm-Tasks"},{"MareBackupTask","\\Microsoft\\Windows\\Application Experience\\MareBackup"},{"StartupAppTask","\\Microsoft\\Windows\\Application Experience\\StartupAppTask"},{"MapsUpdateTask","\\Microsoft\\Windows\\Maps\\MapsUpdateTask"},{"AutochkProxyTask","\\Microsoft\\Windows\\Autochk\\Proxy"},{"FamilySafetyTask","\\Microsoft\\Windows\\Shell\\FamilySafetyMonitor"},{"PowerEfficiencyTask","\\Microsoft\\Windows\\Power Efficiency Diagnostics\\AnalyzeSystem"},{"WindowsAIRecallConfig","\\Microsoft\\Windows\\WindowsAI\\RecallConfiguration"},{"WindowsAIRecallPipeline","\\Microsoft\\Windows\\WindowsAI\\RecallPipeline"},{"OfficeActionsServer","\\Microsoft\\Office\\Office Actions Server"}};for(auto const&[key,path]:tasks)if(key==id)return path;return{};}
std::pair<DWORD,std::string> run(std::wstring command){auto [waited,out]=detail::run_captured(std::move(command));return{waited.exit_code,std::move(out)};}
winchisel::core::Error error(std::string detail){boot_log(("performance command failed: "+detail).c_str());return{std::move(detail)};}
std::string trim_copy(std::string value){while(!value.empty()&&std::isspace(static_cast<unsigned char>(value.back())))value.pop_back();auto first=std::ranges::find_if(value,[](unsigned char c){return !std::isspace(c);});value.erase(value.begin(),first);return value;}
std::wstring quoted(std::string const& name){std::wstring wide(name.begin(),name.end());return L"\""+wide+L"\"";}
std::vector<std::string> ipv4_adapters(){
    auto[code,out]=run(L"netsh.exe interface ipv4 show interfaces");
    if(code) return {};
    std::vector<std::string> names;
    std::size_t start{};
    while(start<out.size()){
        auto end=out.find_first_of("\r\n",start); if(end==std::string::npos)end=out.size();
        auto line=out.substr(start,end-start);
        start=out.find_first_not_of("\r\n",end); if(start==std::string::npos)start=out.size();
        std::size_t pos=std::string::npos, skip{};
        if(auto found=line.find("disconnected"); found!=std::string::npos){pos=found;skip=12;}
        else if(auto found=line.find("connected"); found!=std::string::npos){pos=found;skip=9;}
        if(pos==std::string::npos) continue;
        auto name=trim_copy(line.substr(pos+skip));
        auto lower=name; std::ranges::transform(lower,lower.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
        if(name.empty()||lower.find("loopback")!=std::string::npos) continue;
        names.push_back(std::move(name));
    }
    return names;
}
int profile_from_dns_text(std::string text){
    text.erase(std::remove_if(text.begin(),text.end(),[](char c){return c=='\r'||c=='\n'||c==' ';}),text.end());
    constexpr std::array values{"","1.1.1.11.0.0.1","1.1.1.21.0.0.2","1.1.1.31.0.0.3","8.8.8.88.8.4.4","9.9.9.9149.112.112.112","208.67.222.222208.67.220.220"};
    for(std::size_t i=1;i<values.size();++i)if(text.find(values[i])!=std::string::npos)return static_cast<int>(i);
    if(text.find("DHCP")!=std::string::npos||text.find("dhcp")!=std::string::npos)return 0;
    return 7;
}
}
winchisel::core::Result<int> read_dns_profile(){
    auto adapters=ipv4_adapters();
    if(adapters.empty()){auto[code,out]=run(L"netsh.exe interface ipv4 show dnsservers");if(code)return std::unexpected(error(std::to_string(code)));return profile_from_dns_text(std::move(out));}
    int common=-1;
    for(auto const& name:adapters){
        auto[code,out]=run(L"netsh.exe interface ipv4 show dnsservers name="+quoted(name));
        if(code)return std::unexpected(error(std::to_string(code)));
        const auto profile=profile_from_dns_text(std::move(out));
        if(common<0)common=profile; else if(common!=profile)return 7;
    }
    return common<0?7:common;
}
winchisel::core::Result<void> write_dns_profile(int index){
    if(index==7)return {};
    constexpr std::array<std::pair<wchar_t const*,wchar_t const*>,7> servers{{
        {L"",L""},{L"1.1.1.1",L"1.0.0.1"},{L"1.1.1.2",L"1.0.0.2"},{L"1.1.1.3",L"1.0.0.3"},
        {L"8.8.8.8",L"8.8.4.4"},{L"9.9.9.9",L"149.112.112.112"},{L"208.67.222.222",L"208.67.220.220"}}};
    if(index<0||index>=static_cast<int>(servers.size()))return std::unexpected(error("invalid dns profile"));
    auto adapters=ipv4_adapters();
    if(adapters.empty())return std::unexpected(error("no ipv4 adapters"));
    for(auto const& name:adapters){
        const auto quoted_name=quoted(name);
        if(index==0){
            auto[code,out]=run(L"netsh.exe interface ipv4 set dnsservers name="+quoted_name+L" source=dhcp");
            if(code)return std::unexpected(error(std::to_string(code)));
            continue;
        }
        auto[code,out]=run(L"netsh.exe interface ipv4 set dnsservers name="+quoted_name+L" static address="+servers[index].first+L" register=none validate=no");
        if(code)return std::unexpected(error(std::to_string(code)));
        auto[code2,out2]=run(L"netsh.exe interface ipv4 add dnsservers name="+quoted_name+L" address="+servers[index].second+L" index=2 validate=no");
        if(code2)return std::unexpected(error(std::to_string(code2)));
    }
    return {};
}

template<typename Work>
auto with_registered_task(std::string_view full, Work work) {
    detail::ComApartment com;
    if (!com) return decltype(work(static_cast<IRegisteredTask*>(nullptr)))(std::unexpected(error(std::to_string(com.hr))));
    ITaskService* service{};
    HRESULT hr = CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&service));
    if (FAILED(hr)) return decltype(work(static_cast<IRegisteredTask*>(nullptr)))(std::unexpected(error(std::to_string(hr))));
    VARIANT empty; VariantInit(&empty);
    hr = service->Connect(empty, empty, empty, empty);
    if (FAILED(hr)) { service->Release(); return decltype(work(static_cast<IRegisteredTask*>(nullptr)))(std::unexpected(error(std::to_string(hr)))); }
    BSTR root_path = SysAllocString(L"\\");
    ITaskFolder* root{};
    hr = service->GetFolder(root_path, &root);
    SysFreeString(root_path);
    if (FAILED(hr)) { service->Release(); return decltype(work(static_cast<IRegisteredTask*>(nullptr)))(std::unexpected(error(std::to_string(hr)))); }
    std::wstring path(full.begin(), full.end());
    BSTR value = SysAllocString(path.c_str());
    IRegisteredTask* task{};
    hr = root->GetTask(value, &task);
    SysFreeString(value);
    root->Release();
    service->Release();
    if (FAILED(hr)) return decltype(work(static_cast<IRegisteredTask*>(nullptr)))(std::unexpected(error(std::to_string(hr))));
    auto result = work(task);
    task->Release();
    return result;
}
winchisel::core::Result<bool> read_scheduled_task(std::string_view id){auto full=task_path(id);if(full.empty())return std::unexpected(error("unknown scheduled task"));return with_registered_task(full,[](IRegisteredTask* task)->winchisel::core::Result<bool>{VARIANT_BOOL enabled{};auto hr=task->get_Enabled(&enabled);if(FAILED(hr))return std::unexpected(error(std::to_string(hr)));return enabled==VARIANT_TRUE;});}
winchisel::core::Result<void> write_scheduled_task(std::string_view id,bool enabled){auto full=task_path(id);if(full.empty())return std::unexpected(error("unknown scheduled task"));return with_registered_task(full,[enabled](IRegisteredTask* task)->winchisel::core::Result<void>{auto hr=task->put_Enabled(enabled?VARIANT_TRUE:VARIANT_FALSE);if(FAILED(hr))return std::unexpected(error(std::to_string(hr)));return{};});}

winchisel::core::Result<void> apply_registry_and_tasks(
    std::vector<std::pair<winchisel::core::RegistryTarget, winchisel::core::RegistryValue>> const& registry,
    std::vector<std::pair<std::string, bool>> const& tasks,
    std::optional<int> dns_profile) {
    std::vector<std::pair<winchisel::core::RegistryTarget, RegistryNativeValue>> previous_registry;
    previous_registry.reserve(registry.size());
    for (auto const& [target, _] : registry) {
        auto value = read_registry_native(target);
        if (!value) return std::unexpected(value.error());
        previous_registry.emplace_back(target, std::move(*value));
    }
    std::vector<std::pair<std::string, bool>> previous_tasks;
    previous_tasks.reserve(tasks.size());
    for (auto const& [id, _] : tasks) {
        auto state = read_scheduled_task(id);
        if (!state) return std::unexpected(state.error());
        previous_tasks.emplace_back(id, *state);
    }
    std::optional<int> previous_dns;
    if (dns_profile && *dns_profile != 7) {
        auto dns = read_dns_profile();
        if (!dns) return std::unexpected(dns.error());
        previous_dns = *dns;
    }
    auto restore = [&] {
        bool ok = true;
        for (auto it = previous_registry.rbegin(); it != previous_registry.rend(); ++it)
            ok = static_cast<bool>(write_registry_native(it->first, it->second)) && ok;
        for (auto it = previous_tasks.rbegin(); it != previous_tasks.rend(); ++it)
            ok = static_cast<bool>(write_scheduled_task(it->first, it->second)) && ok;
        if (previous_dns) ok = static_cast<bool>(write_dns_profile(*previous_dns)) && ok;
        return ok;
    };
    if (auto written = write_registry_values_atomic(registry); !written) return written;
    for (std::size_t index{}; index < tasks.size(); ++index) {
        auto result = write_scheduled_task(tasks[index].first, tasks[index].second);
        if (result) continue;
        auto original = result.error();
        original.detail += restore() ? "; rolled back" : "; rollback incomplete";
        return std::unexpected(std::move(original));
    }
    if (dns_profile && *dns_profile != 7) {
        auto result = write_dns_profile(*dns_profile);
        if (!result) {
            auto original = result.error();
            original.detail += restore() ? "; rolled back" : "; rollback incomplete";
            return std::unexpected(std::move(original));
        }
    }
    return {};
}
}
