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
}
winchisel::core::Result<int> read_dns_profile(){auto[code,out]=run(L"netsh.exe interface ipv4 show dnsservers");if(code)return std::unexpected(error(std::to_string(code)));out.erase(std::remove_if(out.begin(),out.end(),[](char c){return c=='\r'||c=='\n'||c==' ';}),out.end());constexpr std::array values{"","1.1.1.11.0.0.1","1.1.1.21.0.0.2","1.1.1.31.0.0.3","8.8.8.88.8.4.4","9.9.9.9149.112.112.112","208.67.222.222208.67.220.220"};for(std::size_t i=1;i<values.size();++i)if(out.find(values[i])!=std::string::npos)return static_cast<int>(i);return out.find("DHCP")!=std::string::npos||out.find("dhcp")!=std::string::npos?0:7;}
winchisel::core::Result<void> write_dns_profile(int index){constexpr std::array servers{L"",L"1.1.1.1",L"1.1.1.2",L"1.1.1.3",L"8.8.8.8",L"9.9.9.9",L"208.67.222.222"};auto[code,out]=run(L"netsh.exe interface ipv4 set dnsservers name=all source="+std::wstring(index>0&&index<static_cast<int>(servers.size())?L"static address="+std::wstring(servers[index]):L"dhcp"));if(code)return std::unexpected(error(std::to_string(code)));return{};}

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
    std::vector<std::pair<std::string, bool>> const& tasks) {
    std::vector<std::pair<winchisel::core::RegistryTarget, winchisel::core::RegistryValue>> previous_registry;
    previous_registry.reserve(registry.size());
    for (auto const& [target, _] : registry) {
        auto value = read_registry_value(target);
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
    if (auto written = write_registry_values_atomic(registry); !written) return written;
    for (std::size_t index{}; index < tasks.size(); ++index) {
        auto result = write_scheduled_task(tasks[index].first, tasks[index].second);
        if (result) continue;
        auto original = result.error();
        while (index > 0) {
            --index;
            (void)write_scheduled_task(previous_tasks[index].first, previous_tasks[index].second);
        }
        (void)rollback_registry_values(previous_registry);
        original.detail += "; profile rollback attempted";
        return std::unexpected(std::move(original));
    }
    return {};
}
}
