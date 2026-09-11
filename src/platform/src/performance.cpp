#include "winchisel/platform/performance.hpp"
#include "winchisel/core/risk.hpp"
#include "winchisel/core/tweak.hpp"
#include "winchisel/platform/registry.hpp"
#include "winchisel/platform/revert.hpp"
#include "winchisel/platform/system.hpp"
#include "process_wait.hpp"
#include "com_apartment.hpp"
#include <Windows.h>
#include <taskschd.h>
#include <comdef.h>
#include <Wbemidl.h>
#include <powrprof.h>
#include <array>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <mutex>
#include <vector>
#include <utility>
#pragma comment(lib, "taskschd.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "wbemuuid.lib")
#pragma comment(lib, "powrprof.lib")
namespace winchisel::platform { namespace {
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

// Classic Windows 10 context menu (WinUtil parity). Presence of the
// InprocServer32 key with an empty default value restores the full menu;
// deleting the CLSID key restores the Windows 11 menu. Explorer is restarted
// afterwards (it relaunches automatically), otherwise nothing visibly changes.
constexpr wchar_t kClassicMenuClsid[] = L"Software\\Classes\\CLSID\\{86ca1aa0-34aa-4e8b-a509-50c905bae2a2}";
constexpr wchar_t kClassicMenuServer[] = L"Software\\Classes\\CLSID\\{86ca1aa0-34aa-4e8b-a509-50c905bae2a2}\\InprocServer32";
winchisel::core::Result<bool> read_classic_context_menu() {
    HKEY key{};
    const auto status = RegOpenKeyExW(HKEY_CURRENT_USER, kClassicMenuServer, 0, KEY_READ, &key);
    if (status == ERROR_FILE_NOT_FOUND) return false;
    if (status != ERROR_SUCCESS) return std::unexpected(error("classic context menu state unreadable: " + std::to_string(status)));
    RegCloseKey(key);
    return true;
}
void restart_explorer() {
    // Best effort: Explorer relaunches on its own after termination, which is
    // the same mechanism WinUtil relies on. Open Explorer windows are closed.
    auto [waited, output] = detail::run_captured(L"taskkill.exe /f /im explorer.exe", 60 * 1000);
    (void)waited.exit_code;
    (void)output;
}
winchisel::core::Result<void> write_classic_context_menu(bool enabled) {
    if (enabled) {
        HKEY key{};
        auto status = RegCreateKeyExW(HKEY_CURRENT_USER, kClassicMenuServer, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
        if (status != ERROR_SUCCESS) return std::unexpected(error("classic context menu change failed: " + std::to_string(status)));
        status = RegSetValueExW(key, nullptr, 0, REG_SZ, reinterpret_cast<BYTE const*>(L""), sizeof(wchar_t));
        RegCloseKey(key);
        if (status != ERROR_SUCCESS) return std::unexpected(error("classic context menu change failed: " + std::to_string(status)));
    } else {
        const auto status = RegDeleteTreeW(HKEY_CURRENT_USER, kClassicMenuClsid);
        if (status != ERROR_SUCCESS && status != ERROR_FILE_NOT_FOUND)
            return std::unexpected(error("classic context menu change failed: " + std::to_string(status)));
    }
    restart_explorer();
    return {};
}

// NIC power saving. Advanced NIC properties live under per-adapter Net-class
// subkeys with vendor-specific value names, so only values that already exist
// are ever touched (never created). Every listed value means "power saving
// enabled" when "1", so the toggle forces "0" (off) and restores "1".
// Virtual adapters (WAN miniports, VPN, Hyper-V, Bluetooth, debug) are
// skipped by DriverDesc keywords.
constexpr wchar_t kNetClassKey[] = L"SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e972-e325-11ce-bfc1-08002be10318}";
constexpr wchar_t const* kNicOffNames[] = {L"*EEE", L"EEE", L"*GreenEthernet", L"GreenEthernet",
    L"*EnergyEfficientEthernet", L"*InterruptModeration", L"EnablePowerManagement", L"PowerSavingMode",
    L"*WakeOnMagicPacket", L"*WakeOnPattern", L"WakeOnPattern"};
constexpr wchar_t const* kNicSkipKeywords[] = {L"wan miniport", L"miniport", L"virtual", L"vpn", L"bluetooth",
    L"debug", L"kernel", L"monitor", L"loopback", L"tap", L"hyper-v", L"vmware", L"virtualbox",
    L"wireguard", L"tailscale", L"filter", L"protocol"};
bool nic_description_eligible(std::wstring_view description) {
    std::wstring lowered(description);
    std::ranges::transform(lowered, lowered.begin(), towlower);
    for (auto keyword : kNicSkipKeywords) {
        if (lowered.find(keyword) != std::wstring::npos) return false;
    }
    return true;
}
struct NicPowerTarget {
    winchisel::core::RegistryTarget target;
    std::string on_value;
    std::string off_value;
};
// Every (adapter, known value) pair that exists on an eligible physical
// adapter. Empty when no supported adapter is present.
std::vector<NicPowerTarget> nic_power_targets() {
    std::vector<NicPowerTarget> targets;
    HKEY parent{};
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, kNetClassKey, 0, KEY_ENUMERATE_SUB_KEYS, &parent) != ERROR_SUCCESS) return targets;
    for (DWORD index{};; ++index) {
        wchar_t name[16]{};
        DWORD length = static_cast<DWORD>(std::size(name));
        if (RegEnumKeyExW(parent, index, name, &length, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
        if (length != 4) continue;
        bool digits = true;
        for (DWORD i{}; i < length; ++i) {
            if (name[i] < L'0' || name[i] > L'9') { digits = false; break; }
        }
        if (!digits) continue;
        const std::wstring sub = std::wstring(kNetClassKey) + L"\\" + std::wstring(name, length);
        wchar_t description[256]{};
        DWORD size = sizeof(description);
        if (RegGetValueW(HKEY_LOCAL_MACHINE, sub.c_str(), L"DriverDesc", RRF_RT_REG_SZ, nullptr, description, &size) != ERROR_SUCCESS) continue;
        if (!nic_description_eligible(description)) continue;
        DWORD instance_size{};
        if (RegGetValueW(HKEY_LOCAL_MACHINE, sub.c_str(), L"NetCfgInstanceId", RRF_RT_REG_SZ, nullptr, nullptr, &instance_size) != ERROR_SUCCESS) continue;
        std::string key_path;
        key_path.reserve(sub.size());
        for (wchar_t c : sub) key_path.push_back(static_cast<char>(c));
        auto known = [&](wchar_t const* value_name) {
            DWORD value_size{};
            if (RegGetValueW(HKEY_LOCAL_MACHINE, sub.c_str(), value_name, RRF_RT_REG_SZ, nullptr, nullptr, &value_size) != ERROR_SUCCESS) return;
            std::string narrow_name;
            for (wchar_t const* p = value_name; *p; ++p) narrow_name.push_back(static_cast<char>(*p));
            targets.push_back({{winchisel::core::RegistryHive::local_machine, key_path, narrow_name,
                winchisel::core::RegistryValueType::string},
                std::string{"0"}, std::string{"1"}});
        };
        for (auto value_name : kNicOffNames) known(value_name);
    }
    RegCloseKey(parent);
    return targets;
}
winchisel::core::Result<bool> read_nic_power_saving() {
    const auto targets = nic_power_targets();
    if (targets.empty()) return false;
    for (auto const& entry : targets) {
        auto current = read_registry_value(entry.target);
        if (!current) return std::unexpected(current.error());
        const auto text = std::get_if<std::string>(&*current);
        if (!text || *text != entry.on_value) return false;
    }
    return true;
}
winchisel::core::Result<void> write_nic_power_saving(bool enabled) {
    const auto targets = nic_power_targets();
    if (targets.empty()) return std::unexpected(error("no supported physical network adapter found"));
    std::vector<std::pair<winchisel::core::RegistryTarget, winchisel::core::RegistryValue>> changes;
    for (auto const& entry : targets) {
        changes.emplace_back(entry.target, winchisel::core::RegistryValue{enabled ? entry.on_value : entry.off_value});
    }
    // Snapshot + rollback on partial failure, same as any catalog batch.
    return write_registry_values_atomic(changes);
}

// PCIe Link State Power Management (ASPM) off while plugged in. GUIDs are
// stable across languages; state is read from the active scheme's registry
// values (locale-independent) and written through the native power APIs.
constexpr GUID kPcieSubgroup{0x501a4d13, 0x42af, 0x4429, {0x9f, 0xd1, 0xa8, 0x21, 0x8c, 0x26, 0x8e, 0x20}};
constexpr GUID kAspmSetting{0xee12f906, 0xd277, 0x404b, {0xb6, 0xda, 0xe5, 0xfa, 0x1a, 0x57, 0x6d, 0xf5}};
winchisel::core::Result<bool> read_pcie_link_state() {
    GUID* active{};
    if (PowerGetActiveScheme(nullptr, &active) != ERROR_SUCCESS || !active) {
        return std::unexpected(error("PCIe power state unreadable"));
    }
    wchar_t scheme_text[64]{};
    StringFromGUID2(*active, scheme_text, static_cast<int>(std::size(scheme_text)));
    LocalFree(active);
    wchar_t subgroup_text[64]{}, setting_text[64]{};
    StringFromGUID2(kPcieSubgroup, subgroup_text, static_cast<int>(std::size(subgroup_text)));
    StringFromGUID2(kAspmSetting, setting_text, static_cast<int>(std::size(setting_text)));
    const std::wstring key = std::wstring(L"SYSTEM\\CurrentControlSet\\Control\\Power\\User\\PowerSchemes\\") +
        scheme_text + L"\\" + subgroup_text + L"\\" + setting_text;
    DWORD index{}, size = sizeof(index), type{};
    const auto status = RegGetValueW(HKEY_LOCAL_MACHINE, key.c_str(), L"ACSettingIndex", RRF_RT_REG_DWORD, &type, &index, &size);
    if (status == ERROR_FILE_NOT_FOUND) return false;
    if (status != ERROR_SUCCESS) return std::unexpected(error("PCIe power state unreadable: " + std::to_string(status)));
    return index == 0;
}
winchisel::core::Result<void> write_pcie_link_state(bool enabled) {
    GUID* active{};
    if (PowerGetActiveScheme(nullptr, &active) != ERROR_SUCCESS || !active) {
        return std::unexpected(error("PCIe power change failed: active scheme unknown"));
    }
    const DWORD desired = enabled ? 0 : 1;
    DWORD status = PowerWriteACValueIndex(nullptr, active, &kPcieSubgroup, &kAspmSetting, desired);
    if (status == ERROR_SUCCESS) status = PowerSetActiveScheme(nullptr, active);
    LocalFree(active);
    if (status != ERROR_SUCCESS) return std::unexpected(error("PCIe power change failed: " + std::to_string(status)));
    return {};
}
// NVIDIA Control Panel values through the driver settings API (what
// nvidiaProfileInspector uses). Setting IDs and values come from NVIDIA's own
// MIT-licensed headers (github.com/NVIDIA/nvapi: NvApiDriverSettings.h) and
// NVIDIA's Driver Settings Programming Guide (PG-5116-001):
//   PREFERRED_PSTATE (0x1057EB71): 1 = Prefer Maximum Performance, 5 = Optimal Power (driver default)
//   PRERENDERLIMIT (0x007BA09E): 1 = one queued frame (Control Panel "On"), 0 = application controlled (default)
//   SHADERDISKCACHE_MAX_SIZE (0x00AC8497, MB): 0x2800 = 10GB, 0x4000 = 16GB (current driver default)
// nvapi64.dll is loaded dynamically and every call is status-checked, so
// machines without an NVIDIA driver get a clear error instead of a crash.
namespace nvidia_drs {
using NvU32 = std::uint32_t;
using NvStatus = std::int32_t;
constexpr NvStatus kOk = 0;
#pragma pack(push, 8)
struct Setting {
    NvU32 version{};
    char16_t name[2048]{};
    NvU32 id{}, type{}, location{}, is_current_predefined{}, is_predefined_valid{};
    union {
        NvU32 u32{};
        struct {
            NvU32 length;
            std::uint8_t data[4096];
        } binary;
        char16_t text[2048];
        std::uint64_t u64;
    } predefined{}, current{};
};
#pragma pack(pop)
static_assert(sizeof(Setting) == 12328, "NVDRS_SETTING_V1 layout mismatch");
static_assert(offsetof(Setting, id) == 4100, "NVDRS_SETTING_V1 id offset mismatch");
static_assert(offsetof(Setting, current) == 8224, "NVDRS_SETTING_V1 current offset mismatch");
constexpr NvU32 kSettingVersion = static_cast<NvU32>(sizeof(Setting) | (1u << 16));
constexpr NvU32 kDwordType = 0;
constexpr NvU32 kInitializeId = 0x0150E828;
constexpr NvU32 kCreateSessionId = 0x0694D52E;
constexpr NvU32 kDestroySessionId = 0xDAD9CFF8;
constexpr NvU32 kLoadSettingsId = 0x375DBD6B;
constexpr NvU32 kSaveSettingsId = 0xFCBC7E14;
constexpr NvU32 kGetBaseProfileId = 0xDA8466A0;
constexpr NvU32 kGetSettingId = 0xEA99498D;
constexpr NvU32 kGetSettingFallbackId = 0x73BF8338;
constexpr NvU32 kSetSettingId = 0x8A2CF5F5;
constexpr NvU32 kSetSettingFallbackId = 0x577DD202;
constexpr NvU32 kGetErrorMessageId = 0x6C2D048C;
using QueryFn = void*(__cdecl*)(NvU32);
using SimpleFn = NvStatus(__cdecl*)();
using CreateSessionFn = NvStatus(__cdecl*)(void**);
using SessionFn = NvStatus(__cdecl*)(void*);
using GetBaseProfileFn = NvStatus(__cdecl*)(void*, void**);
using GetSettingFn = NvStatus(__cdecl*)(void*, void*, NvU32, Setting*, NvU32*);
using SetSettingFn = NvStatus(__cdecl*)(void*, void*, Setting*, NvU32, NvU32);
using GetErrorMessageFn = NvStatus(__cdecl*)(NvStatus, char*);
struct Api {
    HMODULE dll{};
    SimpleFn initialize{};
    CreateSessionFn create_session{};
    SessionFn destroy_session{}, load_settings{}, save_settings{};
    GetBaseProfileFn get_base_profile{};
    GetSettingFn get_setting{};
    SetSettingFn set_setting{};
    GetErrorMessageFn get_error_message{};
};
std::string status_text(Api const& api, NvStatus status) {
    if (api.get_error_message) {
        char text[64]{};
        if (api.get_error_message(status, text) == kOk && text[0]) return text;
    }
    return "nvapi status " + std::to_string(status);
}
winchisel::core::Result<Api> open() {
    Api api;
    api.dll = LoadLibraryExW(L"nvapi64.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!api.dll) return std::unexpected(error("NVIDIA driver API not found (nvapi64.dll missing)"));
    auto query = reinterpret_cast<QueryFn>(GetProcAddress(api.dll, "nvapi_QueryInterface"));
    if (!query) {
        FreeLibrary(api.dll);
        return std::unexpected(error("NVIDIA driver API entry point missing"));
    }
    auto resolve = [&](NvU32 id) { return query(id); };
    api.initialize = reinterpret_cast<SimpleFn>(resolve(kInitializeId));
    api.create_session = reinterpret_cast<CreateSessionFn>(resolve(kCreateSessionId));
    api.destroy_session = reinterpret_cast<SessionFn>(resolve(kDestroySessionId));
    api.load_settings = reinterpret_cast<SessionFn>(resolve(kLoadSettingsId));
    api.save_settings = reinterpret_cast<SessionFn>(resolve(kSaveSettingsId));
    api.get_base_profile = reinterpret_cast<GetBaseProfileFn>(resolve(kGetBaseProfileId));
    api.get_setting = reinterpret_cast<GetSettingFn>(resolve(kGetSettingId));
    if (!api.get_setting) api.get_setting = reinterpret_cast<GetSettingFn>(resolve(kGetSettingFallbackId));
    api.set_setting = reinterpret_cast<SetSettingFn>(resolve(kSetSettingId));
    if (!api.set_setting) api.set_setting = reinterpret_cast<SetSettingFn>(resolve(kSetSettingFallbackId));
    api.get_error_message = reinterpret_cast<GetErrorMessageFn>(resolve(kGetErrorMessageId));
    if (!api.initialize || !api.create_session || !api.destroy_session || !api.load_settings || !api.save_settings ||
        !api.get_base_profile || !api.get_setting || !api.set_setting) {
        FreeLibrary(api.dll);
        return std::unexpected(error("NVIDIA driver API incomplete (driver too old?)"));
    }
    if (const auto status = api.initialize(); status != kOk) {
        const auto detail = status_text(api, status);
        FreeLibrary(api.dll);
        return std::unexpected(error("NVIDIA driver init failed: " + detail));
    }
    return api;
}
// Runs work with the global (base) driver profile, which enforces the setting
// for all processes. Session lifetime and errors follow NVIDIA's programming
// guide sequence: create, load, work, save (writes only), destroy.
template <typename Work>
auto with_base_profile(Api& api, Work work, bool save) -> decltype(work(static_cast<void*>(nullptr), static_cast<void*>(nullptr))) {
    using Result = decltype(work(static_cast<void*>(nullptr), static_cast<void*>(nullptr)));
    void* session{};
    if (const auto status = api.create_session(&session); status != kOk || !session) {
        return Result{std::unexpected(error("NVIDIA session failed: " + status_text(api, status)))};
    }
    auto result = [&]() -> Result {
        if (const auto status = api.load_settings(session); status != kOk) {
            return Result{std::unexpected(error("NVIDIA settings load failed: " + status_text(api, status)))};
        }
        void* profile{};
        if (const auto status = api.get_base_profile(session, &profile); status != kOk || !profile) {
            return Result{std::unexpected(error("NVIDIA global profile unavailable: " + status_text(api, status)))};
        }
        if (auto applied = work(session, profile); !applied) return applied;
        if (save) {
            if (const auto status = api.save_settings(session); status != kOk) {
                return Result{std::unexpected(error("NVIDIA settings save failed: " + status_text(api, status)))};
            }
        }
        if constexpr (std::is_void_v<typename Result::value_type>) return Result{};
        else return Result{typename Result::value_type{}};
    }();
    api.destroy_session(session);
    return result;
}
winchisel::core::Result<NvU32> read_dword(NvU32 setting_id) {
    static std::mutex mutex;
    std::scoped_lock lock(mutex);
    auto api = open();
    if (!api) return std::unexpected(api.error());
    auto result = with_base_profile(*api, [&](void* session, void* profile) -> winchisel::core::Result<NvU32> {
        Setting setting;
        setting.version = kSettingVersion;
        setting.id = setting_id;
        setting.type = kDwordType;
        NvU32 extra{};
        if (const auto status = api->get_setting(session, profile, setting_id, &setting, &extra);
            status != kOk) {
            return std::unexpected(error("NVIDIA setting read failed: " + status_text(*api, status)));
        }
        if (setting.type != kDwordType) return std::unexpected(error("NVIDIA setting has an unexpected type"));
        return setting.current.u32;
    }, false);
    FreeLibrary(api->dll);
    return result;
}
winchisel::core::Result<void> write_dword(NvU32 setting_id, NvU32 value) {
    static std::mutex mutex;
    std::scoped_lock lock(mutex);
    auto api = open();
    if (!api) return std::unexpected(api.error());
    auto result = with_base_profile(*api, [&](void* session, void* profile) -> winchisel::core::Result<void> {
        Setting setting;
        setting.version = kSettingVersion;
        setting.id = setting_id;
        setting.type = kDwordType;
        setting.current.u32 = value;
        if (const auto status = api->set_setting(session, profile, &setting, 0, 0); status != kOk) {
            return std::unexpected(error("NVIDIA setting write failed: " + status_text(*api, status)));
        }
        return {};
    }, true);
    FreeLibrary(api->dll);
    return result;
}
constexpr NvU32 kShaderCacheId = 0x00AC8497;
constexpr NvU32 kShaderCacheOn = 0x2800;
constexpr NvU32 kShaderCacheOff = 0x4000;
constexpr NvU32 kPowerId = 0x1057EB71;
constexpr NvU32 kPowerOn = 1;
constexpr NvU32 kPowerOff = 5;
constexpr NvU32 kLatencyId = 0x007BA09E;
constexpr NvU32 kLatencyOn = 1;
constexpr NvU32 kLatencyOff = 0;
}  // namespace nvidia_drs
bool is_nvidia_drs_toggle(std::string_view id) {
    return id == "graphics-nvidia-shader-cache" || id == "graphics-nvidia-power-max" || id == "graphics-nvidia-low-latency";
}
winchisel::core::Result<bool> read_nvidia_drs_toggle(std::string_view id) {
    nvidia_drs::NvU32 setting{}, on_value{};
    if (id == "graphics-nvidia-shader-cache") {
        setting = nvidia_drs::kShaderCacheId;
        on_value = nvidia_drs::kShaderCacheOn;
    } else if (id == "graphics-nvidia-power-max") {
        setting = nvidia_drs::kPowerId;
        on_value = nvidia_drs::kPowerOn;
    } else if (id == "graphics-nvidia-low-latency") {
        setting = nvidia_drs::kLatencyId;
        on_value = nvidia_drs::kLatencyOn;
    } else {
        return std::unexpected(error("unknown NVIDIA toggle"));
    }
    auto current = nvidia_drs::read_dword(setting);
    if (!current) return std::unexpected(current.error());
    return *current == on_value;
}
winchisel::core::Result<void> write_nvidia_drs_toggle(std::string_view id, bool enabled) {
    nvidia_drs::NvU32 setting{}, on_value{}, off_value{};
    if (id == "graphics-nvidia-shader-cache") {
        setting = nvidia_drs::kShaderCacheId;
        on_value = nvidia_drs::kShaderCacheOn;
        off_value = nvidia_drs::kShaderCacheOff;
    } else if (id == "graphics-nvidia-power-max") {
        setting = nvidia_drs::kPowerId;
        on_value = nvidia_drs::kPowerOn;
        off_value = nvidia_drs::kPowerOff;
    } else if (id == "graphics-nvidia-low-latency") {
        setting = nvidia_drs::kLatencyId;
        on_value = nvidia_drs::kLatencyOn;
        off_value = nvidia_drs::kLatencyOff;
    } else {
        return std::unexpected(error("unknown NVIDIA toggle"));
    }
    return nvidia_drs::write_dword(setting, enabled ? on_value : off_value);
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
    // A custom mix (7) cannot be replayed through the profile writer, so only
    // known previous profiles are journaled. Unchanged selections stay silent.
    if (!revert_recording_suppressed() && previous && *previous != 7 && *previous != index) {
        auto entry = make_revert_entry("performance", "Performance: DNS servers");
        entry.ints.push_back({"dns", "dns", *previous});
        if (auto recorded = record_revert(std::move(entry)); !recorded) {
            boot_log(("revert journal record failed: " + recorded.error().detail).c_str());
        }
    }
    return {};
}

// Windows Update policy selection. All policy keys are plain registry values
// (HKCU+HKLM ...\WindowsUpdate\AU, HKLM ...\WindowsUpdate\UX\Settings), so the
// whole selection is one atomic registry batch. -1 on read means a custom mix.
namespace {
constexpr char const* k_update_au = "SOFTWARE\\Policies\\Microsoft\\Windows\\WindowsUpdate\\AU";
constexpr char const* k_update_ux = "SOFTWARE\\Microsoft\\WindowsUpdate\\UX\\Settings";
constexpr std::array<char const*, 6> k_update_au_dwords{"NoAutoUpdate", "AUOptions", "NoAUShutdownOption",
    "AlwaysAutoRebootAtScheduledTime", "AutoInstallMinorUpdates", "UseWUServer"};
constexpr std::array<char const*, 8> k_update_ux_dwords{"BranchReadinessLevel", "DeferFeatureUpdates",
    "DeferFeatureUpdatesPeriodInDays", "DeferQualityUpdates", "DeferQualityUpdatesPeriodInDays",
    "FlightSettingsMaxPauseDays", "PausedFeatureStatus", "PausedQualityStatus"};
constexpr std::array<char const*, 8> k_update_ux_strings{"PauseFeatureUpdatesStartTime", "PauseFeatureUpdatesEndTime",
    "PauseQualityUpdatesStartTime", "PauseQualityUpdatesEndTime", "PauseUpdatesStartTime", "PauseUpdatesExpiryTime",
    "PausedQualityDate", "PausedFeatureDate"};
using winchisel::core::RegistryHive;
using winchisel::core::RegistryTarget;
using winchisel::core::RegistryValue;
using winchisel::core::RegistryValueType;
RegistryTarget au_target(bool machine, char const* name) {
    return {machine ? RegistryHive::local_machine : RegistryHive::current_user, k_update_au, name, RegistryValueType::dword};
}
RegistryTarget ux_target(char const* name, RegistryValueType type) {
    return {RegistryHive::local_machine, k_update_ux, name, type};
}
std::optional<std::uint32_t> update_au_dword(char const* name) {
    // Set in either hive counts as set; HKCU wins on conflict.
    if (auto user = read_registry_value(au_target(false, name))) {
        if (auto dword = std::get_if<std::uint32_t>(&*user)) return *dword;
        if (!std::holds_alternative<std::monostate>(*user)) return std::nullopt;
    }
    if (auto machine = read_registry_value(au_target(true, name))) {
        if (auto dword = std::get_if<std::uint32_t>(&*machine)) return *dword;
    }
    return std::nullopt;
}
std::optional<std::uint32_t> update_ux_dword(char const* name) {
    auto value = read_registry_value(ux_target(name, RegistryValueType::dword));
    if (!value) return std::nullopt;
    if (auto dword = std::get_if<std::uint32_t>(&*value)) return *dword;
    return std::nullopt;
}
std::optional<std::string> update_ux_string(char const* name) {
    auto value = read_registry_value(ux_target(name, RegistryValueType::string));
    if (!value) return std::nullopt;
    if (auto text = std::get_if<std::string>(&*value)) return *text;
    return std::nullopt;
}
bool update_any_set() {
    for (auto name : k_update_au_dwords) {
        for (bool machine : {false, true}) {
            auto value = read_registry_value(au_target(machine, name));
            if (value && !std::holds_alternative<std::monostate>(*value)) return true;
        }
    }
    for (auto name : k_update_ux_dwords) {
        auto value = read_registry_value(ux_target(name, RegistryValueType::dword));
        if (value && !std::holds_alternative<std::monostate>(*value)) return true;
    }
    for (auto name : k_update_ux_strings) {
        auto value = read_registry_value(ux_target(name, RegistryValueType::string));
        if (value && !std::holds_alternative<std::monostate>(*value)) return true;
    }
    return false;
}
void update_erase(std::vector<std::pair<RegistryTarget, RegistryValue>>& changes) {
    for (auto name : k_update_au_dwords)
        for (bool machine : {false, true}) changes.emplace_back(au_target(machine, name), RegistryValue{std::monostate{}});
    for (auto name : k_update_ux_dwords) changes.emplace_back(ux_target(name, RegistryValueType::dword), RegistryValue{std::monostate{}});
    for (auto name : k_update_ux_strings) changes.emplace_back(ux_target(name, RegistryValueType::string), RegistryValue{std::monostate{}});
}
void update_set_au(std::vector<std::pair<RegistryTarget, RegistryValue>>& changes, char const* name, std::uint32_t value) {
    for (bool machine : {false, true}) changes.emplace_back(au_target(machine, name), RegistryValue{value});
}
void update_set_ux_dword(std::vector<std::pair<RegistryTarget, RegistryValue>>& changes, char const* name, std::uint32_t value) {
    changes.emplace_back(ux_target(name, RegistryValueType::dword), RegistryValue{value});
}
void update_set_ux_string(std::vector<std::pair<RegistryTarget, RegistryValue>>& changes, char const* name, char const* value) {
    changes.emplace_back(ux_target(name, RegistryValueType::string), RegistryValue{std::string{value}});
}
}  // namespace
winchisel::core::Result<int> read_update_policy() {
    auto is = [](std::optional<std::uint32_t> value, std::uint32_t expected) { return value && *value == expected; };
    auto ux_start = update_ux_string("PauseUpdatesExpiryTime");
    const bool paused = is(update_ux_dword("PausedFeatureStatus"), 1) && is(update_ux_dword("PausedQualityStatus"), 1) &&
        ux_start && ux_start->rfind("2051", 0) == 0;
    if (paused) return 2;
    if (is(update_au_dword("NoAutoUpdate"), 1) && is(update_au_dword("AUOptions"), 1)) return 3;
    if (is(update_au_dword("AUOptions"), 2) && is(update_ux_dword("DeferFeatureUpdates"), 1) &&
        is(update_ux_dword("DeferFeatureUpdatesPeriodInDays"), 365) && is(update_ux_dword("DeferQualityUpdates"), 1) &&
        is(update_ux_dword("DeferQualityUpdatesPeriodInDays"), 7)) return 1;
    if (!update_any_set()) return 0;
    return -1;
}
winchisel::core::Result<void> write_update_policy(int index) {
    if (index == 4) return {};
    if (index < 0 || index > 3) return std::unexpected(error("invalid update policy"));
    std::vector<std::pair<RegistryTarget, RegistryValue>> changes;
    if (index == 1) {
        update_set_au(changes, "AUOptions", 2);
        update_set_ux_dword(changes, "BranchReadinessLevel", 20);
        update_set_ux_dword(changes, "DeferFeatureUpdates", 1);
        update_set_ux_dword(changes, "DeferFeatureUpdatesPeriodInDays", 365);
        update_set_ux_dword(changes, "DeferQualityUpdates", 1);
        update_set_ux_dword(changes, "DeferQualityUpdatesPeriodInDays", 7);
        // AUOptions is set above; every other policy key is cleared.
        for (auto name : k_update_au_dwords) {
            if (std::strcmp(name, "AUOptions") == 0) continue;
            for (bool machine : {false, true}) changes.emplace_back(au_target(machine, name), RegistryValue{std::monostate{}});
        }
        for (auto name : k_update_ux_dwords) {
            if (std::strcmp(name, "BranchReadinessLevel") == 0 || std::strcmp(name, "DeferFeatureUpdates") == 0 ||
                std::strcmp(name, "DeferFeatureUpdatesPeriodInDays") == 0 || std::strcmp(name, "DeferQualityUpdates") == 0 ||
                std::strcmp(name, "DeferQualityUpdatesPeriodInDays") == 0) continue;
            changes.emplace_back(ux_target(name, RegistryValueType::dword), RegistryValue{std::monostate{}});
        }
        for (auto name : k_update_ux_strings) changes.emplace_back(ux_target(name, RegistryValueType::string), RegistryValue{std::monostate{}});
    } else if (index == 2) {
        update_set_au(changes, "NoAutoUpdate", 1);
        update_set_au(changes, "AUOptions", 1);
        update_set_au(changes, "NoAUShutdownOption", 1);
        update_set_au(changes, "AlwaysAutoRebootAtScheduledTime", 0);
        update_set_au(changes, "AutoInstallMinorUpdates", 0);
        update_set_au(changes, "UseWUServer", 0);
        update_set_ux_dword(changes, "FlightSettingsMaxPauseDays", 10023);
        update_set_ux_dword(changes, "PausedFeatureStatus", 1);
        update_set_ux_dword(changes, "PausedQualityStatus", 1);
        update_set_ux_string(changes, "PauseFeatureUpdatesStartTime", "2025-01-01T00:00:00Z");
        update_set_ux_string(changes, "PauseFeatureUpdatesEndTime", "2051-12-31T00:00:00Z");
        update_set_ux_string(changes, "PauseQualityUpdatesStartTime", "2025-01-01T00:00:00Z");
        update_set_ux_string(changes, "PauseQualityUpdatesEndTime", "2051-12-31T00:00:00Z");
        update_set_ux_string(changes, "PauseUpdatesStartTime", "2025-01-01T00:00:00Z");
        update_set_ux_string(changes, "PauseUpdatesExpiryTime", "2051-12-31T00:00:00Z");
        update_set_ux_string(changes, "PausedQualityDate", "2025-01-01T00:00:00Z");
        update_set_ux_string(changes, "PausedFeatureDate", "2025-01-01T00:00:00Z");
        for (auto name : k_update_ux_dwords) {
            if (std::strcmp(name, "FlightSettingsMaxPauseDays") == 0 || std::strcmp(name, "PausedFeatureStatus") == 0 ||
                std::strcmp(name, "PausedQualityStatus") == 0) continue;
            changes.emplace_back(ux_target(name, RegistryValueType::dword), RegistryValue{std::monostate{}});
        }
    } else if (index == 3) {
        update_set_au(changes, "NoAutoUpdate", 1);
        update_set_au(changes, "AUOptions", 1);
        update_set_au(changes, "UseWUServer", 0);
        for (auto name : k_update_au_dwords) {
            if (std::strcmp(name, "NoAutoUpdate") == 0 || std::strcmp(name, "AUOptions") == 0 || std::strcmp(name, "UseWUServer") == 0) continue;
            for (bool machine : {false, true}) changes.emplace_back(au_target(machine, name), RegistryValue{std::monostate{}});
        }
        for (auto name : k_update_ux_dwords) changes.emplace_back(ux_target(name, RegistryValueType::dword), RegistryValue{std::monostate{}});
        for (auto name : k_update_ux_strings) changes.emplace_back(ux_target(name, RegistryValueType::string), RegistryValue{std::monostate{}});
    } else {
        update_erase(changes);
    }
    return write_registry_values_atomic(changes, "performance", "Performance: Update policy");
}
// System Protection (restore points) has no registry switch: state is the
// presence of protected volumes below SPP\Clients, toggling goes through the
// native WMI SystemRestore class (what Enable/Disable-ComputerRestore wrap).
winchisel::core::Result<bool> read_system_protection() {
    HKEY clients{};
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\SPP\\Clients",
            0, KEY_ENUMERATE_SUB_KEYS, &clients) != ERROR_SUCCESS) return false;
    DWORD subkeys{};
    const auto status = RegQueryInfoKeyW(clients, nullptr, nullptr, nullptr, &subkeys, nullptr, nullptr,
        nullptr, nullptr, nullptr, nullptr, nullptr);
    RegCloseKey(clients);
    if (status != ERROR_SUCCESS) return std::unexpected(error("system protection state unreadable: " + std::to_string(status)));
    return subkeys > 0;
}
winchisel::core::Result<void> write_system_protection(bool enabled) {
    detail::ComApartment com;
    if (!com) return std::unexpected(error("system protection change failed: COM unavailable"));
    IWbemLocator* locator{};
    HRESULT hr = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_IWbemLocator,
        reinterpret_cast<void**>(&locator));
    if (FAILED(hr) || !locator) return std::unexpected(error("system protection change failed: " + std::to_string(hr)));
    BSTR server = SysAllocString(L"ROOT\\DEFAULT");
    IWbemServices* services{};
    hr = locator->ConnectServer(server, nullptr, nullptr, nullptr, 0, nullptr, nullptr, &services);
    SysFreeString(server);
    locator->Release();
    if (FAILED(hr) || !services) return std::unexpected(error("system protection change failed: " + std::to_string(hr)));
    hr = CoSetProxyBlanket(services, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr, RPC_C_AUTHN_LEVEL_CALL,
        RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
    if (FAILED(hr)) {
        services->Release();
        return std::unexpected(error("system protection change failed: " + std::to_string(hr)));
    }
    const wchar_t* method = enabled ? L"Enable" : L"Disable";
    BSTR path = SysAllocString(L"SystemRestore");
    IWbemClassObject* cls{};
    hr = services->GetObject(path, 0, nullptr, &cls, nullptr);
    SysFreeString(path);
    if (FAILED(hr) || !cls) {
        services->Release();
        return std::unexpected(error("system protection unavailable: " + std::to_string(hr)));
    }
    BSTR method_name = SysAllocString(method);
    IWbemClassObject* signature{};
    hr = cls->GetMethod(method_name, 0, &signature, nullptr);
    SysFreeString(method_name);
    cls->Release();
    if (FAILED(hr) || !signature) {
        services->Release();
        return std::unexpected(error("system protection change failed: " + std::to_string(hr)));
    }
    IWbemClassObject* args{};
    hr = signature->SpawnInstance(0, &args);
    signature->Release();
    if (FAILED(hr) || !args) {
        services->Release();
        return std::unexpected(error("system protection change failed: " + std::to_string(hr)));
    }
    VARIANT drive;
    VariantInit(&drive);
    drive.vt = VT_BSTR;
    drive.bstrVal = SysAllocString(L"C:\\");
    BSTR drive_name = SysAllocString(L"Drive");
    hr = args->Put(drive_name, 0, &drive, 0);
    SysFreeString(drive_name);
    VariantClear(&drive);
    if (FAILED(hr)) {
        args->Release();
        services->Release();
        return std::unexpected(error("system protection change failed: " + std::to_string(hr)));
    }
    BSTR object_path = SysAllocString(L"SystemRestore");
    BSTR call = SysAllocString(method);
    IWbemClassObject* out{};
    hr = services->ExecMethod(object_path, call, 0, nullptr, args, &out, nullptr);
    SysFreeString(object_path);
    SysFreeString(call);
    args->Release();
    services->Release();
    if (FAILED(hr)) return std::unexpected(error("system protection change failed: " + std::to_string(hr)));
    DWORD code = 1;
    if (out) {
        VARIANT result;
        VariantInit(&result);
        if (SUCCEEDED(out->Get(L"ReturnValue", 0, &result, nullptr, nullptr))) {
            if (result.vt == VT_I4) code = static_cast<DWORD>(result.lVal);
            else if (result.vt == VT_UI4) code = result.ulVal;
        }
        VariantClear(&result);
        out->Release();
    }
    if (code != 0) return std::unexpected(error("system protection change rejected by Windows (" + std::to_string(code) + ")"));
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
winchisel::core::Result<bool> read_scheduled_task(std::string_view id){auto full=winchisel::core::task_path_for_id(id);if(full.empty())return std::unexpected(error("unknown scheduled task"));return with_registered_task(full,[](IRegisteredTask* task)->winchisel::core::Result<bool>{VARIANT_BOOL enabled{};auto hr=task->get_Enabled(&enabled);if(FAILED(hr))return std::unexpected(error(std::to_string(hr)));return enabled==VARIANT_TRUE;});}
winchisel::core::Result<void> write_scheduled_task(std::string_view id,bool enabled){auto full=winchisel::core::task_path_for_id(id);if(full.empty())return std::unexpected(error("unknown scheduled task"));return with_registered_task(full,[enabled](IRegisteredTask* task)->winchisel::core::Result<void>{auto hr=task->put_Enabled(enabled?VARIANT_TRUE:VARIANT_FALSE);if(FAILED(hr))return std::unexpected(error(std::to_string(hr)));return{};});}

winchisel::core::Result<void> apply_registry_and_tasks(
    std::vector<std::pair<winchisel::core::RegistryTarget, winchisel::core::RegistryValue>> const& registry,
    std::vector<std::pair<std::string, bool>> const& tasks,
    std::optional<int> dns_profile,
    std::optional<int> update_policy,
    std::string_view page, std::string_view label) {
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
    std::optional<int> previous_update;
    if (update_policy) {
        auto policy = read_update_policy();
        if (!policy) return std::unexpected(policy.error());
        previous_update = *policy;
    }
    auto restore = [&] {
        // Rollback must never pollute the revert journal: the writers below
        // (tasks, DNS, update policy) journal on their own, and replaying a
        // failed apply would otherwise record its own undo.
        RevertSuppressGuard suppress;
        bool ok = true;
        for (auto it = previous_registry.rbegin(); it != previous_registry.rend(); ++it)
            ok = static_cast<bool>(write_registry_native(it->first, it->second)) && ok;
        for (auto it = previous_tasks.rbegin(); it != previous_tasks.rend(); ++it)
            ok = static_cast<bool>(write_scheduled_task(it->first, it->second)) && ok;
        if (previous_dns) ok = static_cast<bool>(write_dns_profile(*previous_dns)) && ok;
        if (previous_update && *previous_update >= 0) ok = static_cast<bool>(write_update_policy(*previous_update)) && ok;
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
    // DNS/policy replay through the same writers the single toggles use, but
    // journaled below as part of this one entry: a bulk apply is a single
    // user action and must undo with a single Undo (not three). The inner
    // writers' own journaling stays suppressed while this scope is held
    // (nesting-safe via the guard's depth counter; restore() below holds
    // the guard again).
    std::optional<int> journal_dns;
    std::optional<int> journal_policy;
    {
        RevertSuppressGuard suppress_inner;
        if (dns_profile && *dns_profile != 7) {
            auto result = write_dns_profile(*dns_profile);
            if (!result) {
                auto original = result.error();
                original.detail += restore() ? "; rolled back" : "; rollback incomplete";
                return std::unexpected(std::move(original));
            }
            if (previous_dns && *previous_dns != 7 && *previous_dns != *dns_profile) journal_dns = *previous_dns;
        }
        if (update_policy) {
            auto result = write_update_policy(*update_policy);
            if (!result) {
                auto original = result.error();
                original.detail += restore() ? "; rolled back" : "; rollback incomplete";
                return std::unexpected(std::move(original));
            }
            if (previous_update && *previous_update >= 0 && *previous_update != *update_policy)
                journal_policy = *previous_update;
        }
    }
    // One entry per user action: the registry batch plus the task states
    // plus the folded DNS/policy before-values, changed pairs only so
    // matching re-applies stay silent.
    if (!label.empty() && !revert_recording_suppressed()) {
        auto entry = make_revert_entry(std::string(page), std::string(label));
        for (std::size_t index{}; index < registry.size(); ++index) {
            if (registry_native_matches(previous_registry[index].second, previous_registry[index].first,
                    registry[index].second))
                continue;
            winchisel::core::RevertRegistryStep step;
            step.target = previous_registry[index].first;
            step.before.missing = previous_registry[index].second.missing;
            step.before.type = previous_registry[index].second.type;
            step.before.data = previous_registry[index].second.data;
            entry.registry.push_back(std::move(step));
        }
        for (auto const& [id, was] : previous_tasks) {
            const auto desired = std::ranges::find_if(
                tasks, [&](auto const& item) { return item.first == id; });
            if (desired != tasks.end() && desired->second != was) entry.tasks.push_back({id, was});
        }
        if (journal_dns) entry.ints.push_back({"dns", "dns", *journal_dns});
        if (journal_policy) entry.ints.push_back({"update-policy", "update-policy", *journal_policy});
        if (auto recorded = record_revert(std::move(entry)); !recorded) {
            boot_log(("revert journal record failed: " + recorded.error().detail).c_str());
        }
    }
    return {};
}
bool is_special_performance_toggle(std::string_view id) {
    return gpu_vendor_for(id).has_value() || id == "gaming-usb-selective-suspend" ||
        id == "gaming-hibernate-fast-startup" || id == "updates-system-protection" ||
        id == "explorer-classic-context-menu" || id == "network-nic-power-saving" ||
        id == "power-pcie-link-state" || is_nvidia_drs_toggle(id);
}
winchisel::core::Result<bool> read_special_performance_toggle(std::string_view id) {
    if (auto vendor = gpu_vendor_for(id)) return read_gpu_vendor_tweak(*vendor);
    if (id == "gaming-usb-selective-suspend") return read_usb_selective_suspend();
    if (id == "gaming-hibernate-fast-startup") return read_hibernate();
    if (id == "updates-system-protection") return read_system_protection();
    if (id == "explorer-classic-context-menu") return read_classic_context_menu();
    if (id == "network-nic-power-saving") return read_nic_power_saving();
    if (id == "power-pcie-link-state") return read_pcie_link_state();
    if (is_nvidia_drs_toggle(id)) return read_nvidia_drs_toggle(id);
    return std::unexpected(error("unknown special performance toggle"));
}
winchisel::core::Result<void> write_special_performance_toggle(std::string_view id, bool enabled) {
    const auto before = read_special_performance_toggle(id);
    winchisel::core::Result<void> result{std::unexpected(error("unknown special performance toggle"))};
    if (auto vendor = gpu_vendor_for(id)) result = write_gpu_vendor_tweak(*vendor, enabled);
    else if (id == "gaming-usb-selective-suspend") result = write_usb_selective_suspend(enabled);
    else if (id == "gaming-hibernate-fast-startup") result = write_hibernate(enabled);
    else if (id == "updates-system-protection") result = write_system_protection(enabled);
    else if (id == "explorer-classic-context-menu") result = write_classic_context_menu(enabled);
    else if (id == "network-nic-power-saving") result = write_nic_power_saving(enabled);
    else if (id == "power-pcie-link-state") result = write_pcie_link_state(enabled);
    else if (is_nvidia_drs_toggle(id)) result = write_nvidia_drs_toggle(id, enabled);
    if (!result) return result;
    // Unchanged toggles stay silent; the read path is the display path, so a
    // matching read means the write was a no-op.
    if (!revert_recording_suppressed() && before && *before != enabled) {
        std::string label("Performance: ");
        bool named{};
        for (auto const& item : winchisel::core::get_performance_catalog()) {
            if (item.id == id) {
                label += item.name;
                named = true;
                break;
            }
        }
        if (!named) label += std::string(id);
        auto entry = make_revert_entry("performance", label);
        entry.toggles.push_back({"special", std::string(id), *before});
        if (auto recorded = record_revert(std::move(entry)); !recorded) {
            boot_log(("revert journal record failed: " + recorded.error().detail).c_str());
        }
    }
    return {};
}
winchisel::core::Result<bool> is_special_available(std::string_view id) {
    if (auto vendor = gpu_vendor_for(id)) return !gpu_adapter_subkeys(*vendor).empty();
    if (id == "gaming-usb-selective-suspend" || id == "gaming-hibernate-fast-startup" || id == "updates-system-protection" ||
        id == "explorer-classic-context-menu" || id == "power-pcie-link-state") return true;
    if (id == "network-nic-power-saving") return !nic_power_targets().empty();
    if (is_nvidia_drs_toggle(id)) return !gpu_adapter_subkeys(GpuVendor::nvidia).empty();
    return std::unexpected(error("unknown special performance toggle"));
}
void restart_shell() {
    restart_explorer();
}
}
