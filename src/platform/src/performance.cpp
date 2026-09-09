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
#include <cstdint>
#include <cstring>
#include <span>
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
struct NetshResult { DWORD code{}; bool timed_out{}; std::string output; };
NetshResult run_netsh(std::wstring command, DWORD timeout_ms = 60 * 1000) {
    auto [waited, out] = detail::run_captured(std::move(command), timeout_ms);
    return {waited.exit_code, waited.timed_out, std::move(out)};
}
winchisel::core::Error error(std::string detail){boot_log(("performance command failed: "+detail).c_str());return{std::move(detail)};}
winchisel::core::Error netsh_error(NetshResult const& result, char const* action) {
    if (result.timed_out) return error(std::string(action) + " timed out");
    return error(std::string(action) + " exited with " + std::to_string(result.code));
}
std::wstring quoted(std::wstring const& name){return L"\""+name+L"\"";}
// Locale-independent adapter discovery. Parsing `netsh show interfaces`
// for English words like "connected" breaks on localized Windows. The
// connection names below are the same store `netsh.exe` itself reads, so no
// subprocess and no locale-dependent parsing is needed. Names arrive as wide
// strings directly (no lossy conversion). Mirrors the old behavior: every
// connection except loopback (which has no Connection key), regardless of
// media state.
std::vector<std::wstring> ipv4_adapters(){
    std::vector<std::wstring> names;
    HKEY adapters{};
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
            L"SYSTEM\\CurrentControlSet\\Control\\Network\\{4D36E972-E325-11CE-BFC1-08002BE10318}",
            0, KEY_ENUMERATE_SUB_KEYS, &adapters) != ERROR_SUCCESS) {
        return names;
    }
    for (DWORD index{};; ++index) {
        wchar_t guid[64]{};
        DWORD length = 64;
        if (RegEnumKeyExW(adapters, index, guid, &length, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
        std::wstring connection = L"SYSTEM\\CurrentControlSet\\Control\\Network\\{4D36E972-E325-11CE-BFC1-08002BE10318}\\";
        connection.append(guid, length);
        connection += L"\\Connection";
        DWORD size{}, type{};
        if (RegGetValueW(HKEY_LOCAL_MACHINE, connection.c_str(), L"Name", RRF_RT_REG_SZ, &type, nullptr, &size) != ERROR_SUCCESS ||
            !size || size > 8192) {
            continue;
        }
        std::wstring adapter(size / sizeof(wchar_t), L'\0');
        if (RegGetValueW(HKEY_LOCAL_MACHINE, connection.c_str(), L"Name", RRF_RT_REG_SZ, &type, adapter.data(), &size) != ERROR_SUCCESS) {
            continue;
        }
        while (!adapter.empty() && !adapter.back()) adapter.pop_back();
        if (adapter.empty() || adapter.find(L'"') != std::wstring::npos) continue;
        names.push_back(std::move(adapter));
    }
    RegCloseKey(adapters);
    return names;
}
std::vector<std::string> dns_tokens(std::string const& text){
    std::vector<std::string> tokens;
    std::size_t pos{};
    while (pos < text.size()) {
        while (pos < text.size() && !(text[pos] >= '0' && text[pos] <= '9')) ++pos;
        std::size_t end = pos;
        while (end < text.size() && ((text[end] >= '0' && text[end] <= '9') || text[end] == '.')) ++end;
        if (end > pos) {
            unsigned parts[4]{};
            int count{};
            std::size_t at = pos;
            bool valid = true;
            for (int p{}; p < 4; ++p) {
                unsigned value{};
                int digits{};
                while (at < end && text[at] >= '0' && text[at] <= '9') { value = value * 10 + (text[at] - '0'); ++digits; ++at; }
                if (!digits || value > 255) { valid = false; break; }
                parts[p] = value;
                ++count;
                if (p < 3) {
                    if (at >= end || text[at] != '.') { valid = false; break; }
                    ++at;
                }
            }
            if (valid && count == 4 && at == end) {
                tokens.push_back(text.substr(pos, end - pos));
            }
        }
        pos = end ? end : pos + 1;
    }
    return tokens;
}
bool contains_dhcp(std::string text){
    std::ranges::transform(text, text.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    return text.find("dhcp") != std::string::npos;
}
int profile_from_dns_text(std::string const& text){
    constexpr std::array<std::pair<char const*, char const*>, 7> profiles{{
        {"", ""}, {"1.1.1.1", "1.0.0.1"}, {"1.1.1.2", "1.0.0.2"}, {"1.1.1.3", "1.0.0.3"},
        {"8.8.8.8", "8.8.4.4"}, {"9.9.9.9", "149.112.112.112"}, {"208.67.222.222", "208.67.220.220"}}};
    const auto tokens = dns_tokens(text);
    if (tokens.size() >= 2) {
        for (std::size_t i = 1; i < profiles.size(); ++i) {
            if (tokens[0] == profiles[i].first && tokens[1] == profiles[i].second) return static_cast<int>(i);
        }
    }
    if (contains_dhcp(text)) return 0;
    return 7;
}
// Vendor GPU power tweaks live under per-adapter Display-class subkeys, so
// they cannot be static catalog rules. Adapters are matched by DriverDesc
// instead of WMI DeviceId order, which does not align with registry indices.
constexpr wchar_t kDisplayClassKey[] = L"SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e968-e325-11ce-bfc1-08002be10318}";
struct GpuRegistryValue { wchar_t const* name; DWORD on; DWORD off; };
std::span<GpuRegistryValue const> gpu_values(GpuVendor vendor) {
    static constexpr GpuRegistryValue amd[] = {
        {L"EnableULPS", 0, 1},
        {L"EnableAspmL0s", 0, 1},
        {L"EnableAspmL1", 0, 1},
        {L"DisablePowerGating", 1, 0},
        {L"PP_GPUPowerDownEnabled", 0, 1},
        {L"DisableDynamicPstate", 1, 0},
        {L"DisableVCEPowerGating", 1, 0},
        {L"DisableVceClockGating", 1, 0},
        {L"EnableUvdClockGating", 0, 1},
        {L"EnableVceSwClockGating", 0, 1},
    };
    static constexpr GpuRegistryValue nvidia[] = {
        {L"DisableDynamicPstate", 1, 0},
        {L"DisableASyncPstates", 1, 0},
    };
    static constexpr GpuRegistryValue intel[] = {
        {L"Display1_DisableAsyncFlips", 1, 0},
        {L"AdaptiveVsyncEnable", 0, 1},
    };
    switch (vendor) {
        case GpuVendor::amd: return amd;
        case GpuVendor::nvidia: return nvidia;
        case GpuVendor::intel: return intel;
    }
    return {};
}
bool gpu_description_matches(std::wstring_view description, GpuVendor vendor) {
    std::wstring lowered(description);
    std::ranges::transform(lowered, lowered.begin(), towlower);
    switch (vendor) {
        case GpuVendor::amd: return lowered.find(L"amd") != std::wstring::npos || lowered.find(L"radeon") != std::wstring::npos;
        case GpuVendor::nvidia: return lowered.find(L"nvidia") != std::wstring::npos || lowered.find(L"geforce") != std::wstring::npos;
        case GpuVendor::intel: return lowered.find(L"intel") != std::wstring::npos;
    }
    return false;
}
std::string narrow_ascii(std::wstring_view wide) {
    std::string out;
    out.reserve(wide.size());
    for (wchar_t c : wide) out.push_back(static_cast<char>(c));
    return out;
}
// Narrow adapter subkey names ("0000") of display adapters matching the vendor.
std::vector<std::string> gpu_adapter_subkeys(GpuVendor vendor) {
    std::vector<std::string> subkeys;
    HKEY parent{};
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, kDisplayClassKey, 0, KEY_ENUMERATE_SUB_KEYS, &parent) != ERROR_SUCCESS) return subkeys;
    for (DWORD index{};; ++index) {
        wchar_t name[16]{};
        DWORD length = static_cast<DWORD>(std::size(name));
        if (RegEnumKeyExW(parent, index, name, &length, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
        std::wstring sub = std::wstring(kDisplayClassKey) + L"\\" + std::wstring(name, length);
        wchar_t description[256]{};
        DWORD size = sizeof(description);
        if (RegGetValueW(HKEY_LOCAL_MACHINE, sub.c_str(), L"DriverDesc", RRF_RT_REG_SZ, nullptr, description, &size) != ERROR_SUCCESS) continue;
        if (gpu_description_matches(description, vendor)) subkeys.push_back(narrow_ascii({name, length}));
    }
    RegCloseKey(parent);
    return subkeys;
}
std::string gpu_key_path(std::string_view subkey) {
    return "SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e968-e325-11ce-bfc1-08002be10318}\\" + std::string(subkey);
}
winchisel::core::RegistryTarget gpu_target(std::string const& key_path, std::wstring_view value_name) {
    return {.hive = winchisel::core::RegistryHive::local_machine, .key_path = key_path, .value_name = narrow_ascii(value_name), .type = winchisel::core::RegistryValueType::dword};
}
winchisel::core::Result<bool> read_gpu_vendor_tweak(GpuVendor vendor) {
    const auto subkeys = gpu_adapter_subkeys(vendor);
    if (subkeys.empty()) return false;
    for (auto const& subkey : subkeys) {
        for (auto const& value : gpu_values(vendor)) {
            auto current = read_registry_native(gpu_target(gpu_key_path(subkey), value.name));
            if (!current) return std::unexpected(current.error());
            if (current->missing || current->type != REG_DWORD || current->data.size() != sizeof(DWORD)) return false;
            DWORD actual{};
            std::memcpy(&actual, current->data.data(), sizeof(actual));
            if (actual != value.on) return false;
        }
    }
    return true;
}
winchisel::core::Result<void> write_gpu_vendor_tweak(GpuVendor vendor, bool enabled) {
    struct Pending { winchisel::core::RegistryTarget target; RegistryNativeValue previous; DWORD desired; };
    std::vector<Pending> pending;
    for (auto const& subkey : gpu_adapter_subkeys(vendor)) {
        for (auto const& value : gpu_values(vendor)) {
            auto target = gpu_target(gpu_key_path(subkey), value.name);
            auto current = read_registry_native(target);
            if (!current) return std::unexpected(current.error());
            pending.push_back({std::move(target), std::move(*current), enabled ? value.on : value.off});
        }
    }
    for (std::size_t index{}; index < pending.size(); ++index) {
        RegistryNativeValue next{false, REG_DWORD, {}};
        next.data.resize(sizeof(DWORD));
        std::memcpy(next.data.data(), &pending[index].desired, sizeof(DWORD));
        if (auto written = write_registry_native(pending[index].target, next); !written) {
            auto original = written.error();
            bool restored = true;
            for (std::size_t back = index; back > 0; --back) {
                restored = static_cast<bool>(write_registry_native(pending[back - 1].target, pending[back - 1].previous)) && restored;
            }
            if (!restored) original.detail += "; rollback incomplete";
            return std::unexpected(std::move(original));
        }
    }
    return {};
}
winchisel::core::RegistryTarget usb_selective_suspend_target() {
    return {.hive = winchisel::core::RegistryHive::local_machine, .key_path = "SYSTEM\\CurrentControlSet\\Services\\USB", .value_name = "DisableSelectiveSuspend", .type = winchisel::core::RegistryValueType::dword};
}
void apply_usb_powercfg(DWORD value) {
    // Best effort: the registry value is the source of truth, the active
    // power plan is updated so the change applies without a reboot.
    const std::wstring setting = value ? L"1" : L"0";
    for (auto const* scope : {L"/SETACVALUEINDEX", L"/SETDCVALUEINDEX"}) {
        auto [waited, output] = detail::run_captured(
            std::wstring(L"powercfg.exe ") + scope +
                L" SCHEME_CURRENT 2a737441-1930-4402-8d77-b2bebba308a3 48e6b7a6-50f5-4782-a5d4-53bb8f07e226 " + setting,
            60 * 1000);
        (void)waited.exit_code;
        (void)output;
    }
}
winchisel::core::Result<bool> read_usb_selective_suspend() {
    auto value = read_registry_value(usb_selective_suspend_target());
    if (!value) return std::unexpected(value.error());
    if (std::holds_alternative<std::monostate>(*value)) return true;
    if (auto dword = std::get_if<std::uint32_t>(&*value)) return *dword == 0;
    return std::unexpected(error("unexpected USB selective suspend value type"));
}
winchisel::core::Result<void> write_usb_selective_suspend(bool enabled) {
    auto result = enabled ? write_registry_value(usb_selective_suspend_target(), winchisel::core::RegistryValue{std::monostate{}})
                          : write_registry_value(usb_selective_suspend_target(), winchisel::core::RegistryValue{std::uint32_t{1}});
    if (!result) return result;
    apply_usb_powercfg(enabled ? 1 : 0);
    return {};
}
winchisel::core::RegistryTarget hibernate_target() {
    return {.hive = winchisel::core::RegistryHive::local_machine, .key_path = "SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Power", .value_name = "HibernateEnabled", .type = winchisel::core::RegistryValueType::dword};
}
winchisel::core::Result<bool> read_hibernate() {
    auto value = read_registry_value(hibernate_target());
    if (!value) return std::unexpected(value.error());
    if (std::holds_alternative<std::monostate>(*value)) return true;
    if (auto dword = std::get_if<std::uint32_t>(&*value)) return *dword != 0;
    return std::unexpected(error("unexpected hibernate value type"));
}
winchisel::core::Result<void> write_hibernate(bool enabled) {
    auto [waited, output] = detail::run_captured(enabled ? L"powercfg.exe /h on" : L"powercfg.exe /h off", 2 * 60 * 1000);
    if (waited.timed_out) return std::unexpected(error("powercfg /h timed out"));
    if (waited.exit_code != 0) return std::unexpected(error("powercfg /h exited with " + std::to_string(waited.exit_code)));
    return {};
}
std::optional<GpuVendor> gpu_vendor_for(std::string_view id) {
    if (id == "gaming-gpu-amd-power") return GpuVendor::amd;
    if (id == "gaming-gpu-nvidia-power") return GpuVendor::nvidia;
    if (id == "gaming-gpu-intel-display") return GpuVendor::intel;
    return std::nullopt;
}
}
winchisel::core::Result<int> read_dns_profile(){
    auto adapters=ipv4_adapters();
    if(adapters.empty()){auto result=run_netsh(L"netsh.exe interface ipv4 show dnsservers");if(result.code)return std::unexpected(netsh_error(result,"show dnsservers"));return profile_from_dns_text(result.output);}
    int common=-1;
    for(auto const& name:adapters){
        auto result=run_netsh(L"netsh.exe interface ipv4 show dnsservers name="+quoted(name));
        if(result.code)return std::unexpected(netsh_error(result,"show dnsservers"));
        const auto profile=profile_from_dns_text(result.output);
        if(common<0)common=profile; else if(common!=profile)return 7;
    }
    return common<0?7:common;
}
winchisel::core::Result<void> apply_dns_profile(int index, std::vector<std::wstring> const& adapters){
    constexpr std::array<std::pair<wchar_t const*,wchar_t const*>,7> servers{{
        {L"",L""},{L"1.1.1.1",L"1.0.0.1"},{L"1.1.1.2",L"1.0.0.2"},{L"1.1.1.3",L"1.0.0.3"},
        {L"8.8.8.8",L"8.8.4.4"},{L"9.9.9.9",L"149.112.112.112"},{L"208.67.222.222",L"208.67.220.220"}}};
    for(auto const& name:adapters){
        const auto quoted_name=quoted(name);
        if(index==0){
            auto result=run_netsh(L"netsh.exe interface ipv4 set dnsservers name="+quoted_name+L" source=dhcp");
            if(result.code)return std::unexpected(netsh_error(result,"set dnsservers"));
            continue;
        }
        auto result=run_netsh(L"netsh.exe interface ipv4 set dnsservers name="+quoted_name+L" static address="+servers[index].first+L" register=none validate=no");
        if(result.code)return std::unexpected(netsh_error(result,"set dnsservers"));
        auto added=run_netsh(L"netsh.exe interface ipv4 add dnsservers name="+quoted_name+L" address="+servers[index].second+L" index=2 validate=no");
        if(added.code)return std::unexpected(netsh_error(added,"add dnsservers"));
    }
    return {};
}
winchisel::core::Result<void> write_dns_profile(int index){
    if(index==7)return {};
    if(index<0||index>6)return std::unexpected(error("invalid dns profile"));
    auto adapters=ipv4_adapters();
    if(adapters.empty())return std::unexpected(error("no ipv4 adapters"));
    const auto previous = read_dns_profile();
    if (auto applied = apply_dns_profile(index, adapters); !applied) {
        auto original = applied.error();
        bool restored = false;
        if (previous && *previous != 7) restored = static_cast<bool>(apply_dns_profile(*previous, adapters));
        original.detail += restored ? "; rolled back" : "; rollback incomplete";
        return std::unexpected(std::move(original));
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
bool is_special_performance_toggle(std::string_view id) {
    return gpu_vendor_for(id).has_value() || id == "gaming-usb-selective-suspend" || id == "gaming-hibernate-fast-startup";
}
winchisel::core::Result<bool> read_special_performance_toggle(std::string_view id) {
    if (auto vendor = gpu_vendor_for(id)) return read_gpu_vendor_tweak(*vendor);
    if (id == "gaming-usb-selective-suspend") return read_usb_selective_suspend();
    if (id == "gaming-hibernate-fast-startup") return read_hibernate();
    return std::unexpected(error("unknown special performance toggle"));
}
winchisel::core::Result<void> write_special_performance_toggle(std::string_view id, bool enabled) {
    if (auto vendor = gpu_vendor_for(id)) return write_gpu_vendor_tweak(*vendor, enabled);
    if (id == "gaming-usb-selective-suspend") return write_usb_selective_suspend(enabled);
    if (id == "gaming-hibernate-fast-startup") return write_hibernate(enabled);
    return std::unexpected(error("unknown special performance toggle"));
}
winchisel::core::Result<bool> is_special_available(std::string_view id) {
    if (auto vendor = gpu_vendor_for(id)) return !gpu_adapter_subkeys(*vendor).empty();
    if (id == "gaming-usb-selective-suspend" || id == "gaming-hibernate-fast-startup") return true;
    return std::unexpected(error("unknown special performance toggle"));
}
}
