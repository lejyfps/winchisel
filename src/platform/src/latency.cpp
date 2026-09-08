#include "winchisel/platform/latency.hpp"
#include "latency_database.generated.hpp"
#include "process_wait.hpp"

#include <windows.h>
#include <cfgmgr32.h>
#include <initguid.h>
#include <devpkey.h>
#include <setupapi.h>
#include <powrprof.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cwctype>
#include <memory>
#include <optional>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#pragma comment(lib, "cfgmgr32.lib")
#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "powrprof.lib")

namespace winchisel::platform {
namespace {

struct Controller {
    std::string vid, did, name, platform, usb, instance_id, bus_prefix;
    int chip_level{};
    std::string msi_status{"Unknown"};
    std::optional<bool> selective_suspend;
};
struct Device { std::string name, vid, pid, instance; int chip_count{}, hub_count{}; std::size_t controller{}; };
struct UsbNode { std::string device_key, instance, parent_prefix, name; std::vector<std::string> compatible_ids; };

struct RegKey {
    HKEY value{};
    ~RegKey() { if (value) RegCloseKey(value); }
    RegKey() = default;
    RegKey(RegKey const&) = delete;
    RegKey& operator=(RegKey const&) = delete;
    RegKey(RegKey&& other) noexcept : value(std::exchange(other.value, nullptr)) {}
    RegKey& operator=(RegKey&& other) noexcept { if (this != &other) { if (value) RegCloseKey(value); value = std::exchange(other.value, nullptr); } return *this; }
};

std::optional<RegKey> open_key(HKEY root, std::wstring const& path) {
    RegKey key;
    if (RegOpenKeyExW(root, path.c_str(), 0, KEY_READ, &key.value) != ERROR_SUCCESS) return std::nullopt;
    return std::optional<RegKey>{std::move(key)};
}

std::vector<std::wstring> enum_keys(HKEY root, std::wstring const& path) {
    auto key = open_key(root, path); std::vector<std::wstring> result; if (!key) return result;
    for (DWORD index{};; ++index) {
        std::array<wchar_t, 512> name{}; DWORD length = static_cast<DWORD>(name.size());
        const auto status = RegEnumKeyExW(key->value, index, name.data(), &length, nullptr, nullptr, nullptr, nullptr);
        if (status == ERROR_NO_MORE_ITEMS) break;
        if (status == ERROR_SUCCESS) result.emplace_back(name.data(), length);
    }
    return result;
}

std::optional<std::wstring> read_string(HKEY root, std::wstring const& path, wchar_t const* name) {
    auto key = open_key(root, path); if (!key) return std::nullopt;
    DWORD type{}, bytes{};
    if (RegQueryValueExW(key->value, name, nullptr, &type, nullptr, &bytes) != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) return std::nullopt;
    std::wstring result(bytes / sizeof(wchar_t), L'\0');
    if (RegQueryValueExW(key->value, name, nullptr, nullptr, reinterpret_cast<BYTE*>(result.data()), &bytes) != ERROR_SUCCESS) return std::nullopt;
    while (!result.empty() && result.back() == L'\0') result.pop_back(); return result;
}

std::vector<std::wstring> read_multi_string(HKEY root, std::wstring const& path, wchar_t const* name) {
    auto key = open_key(root, path); if (!key) return {};
    DWORD type{}, bytes{};
    if (RegQueryValueExW(key->value, name, nullptr, &type, nullptr, &bytes) != ERROR_SUCCESS || type != REG_MULTI_SZ) return {};
    std::vector<wchar_t> data(bytes / sizeof(wchar_t) + 1);
    if (RegQueryValueExW(key->value, name, nullptr, nullptr, reinterpret_cast<BYTE*>(data.data()), &bytes) != ERROR_SUCCESS) return {};
    std::vector<std::wstring> result; for (auto* item = data.data(); *item; item += wcslen(item) + 1) result.emplace_back(item); return result;
}

std::optional<DWORD> read_dword(HKEY root, std::wstring const& path, wchar_t const* name) {
    auto key = open_key(root, path); if (!key) return std::nullopt;
    DWORD value{}, type{}, bytes = sizeof(value);
    if (RegQueryValueExW(key->value, name, nullptr, &type, reinterpret_cast<BYTE*>(&value), &bytes) != ERROR_SUCCESS || type != REG_DWORD) return std::nullopt;
    return value;
}

std::string utf8(std::wstring const& value) {
    if (value.empty()) return {};
    const int length = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string result(length, '\0'); WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), length, nullptr, nullptr); return result;
}

std::wstring wide(std::string const& value) {
    if (value.empty()) return {};
    const int length = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    std::wstring result(length, L'\0'); MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), length); return result;
}

std::string lower(std::string value) { std::ranges::transform(value, value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); }); return value; }
std::optional<std::string> extract_hex(std::string const& value, std::string_view marker) {
    const auto start = value.find(marker); if (start == std::string::npos || start + marker.size() + 4 > value.size()) return std::nullopt;
    return lower(value.substr(start + marker.size(), 4));
}
std::optional<std::string> strip_last(std::string const& value) { const auto pos = value.rfind('&'); if (pos == std::string::npos) return std::nullopt; return value.substr(0, pos); }

Controller lookup(std::string vid, std::string did, std::string instance, std::string bus) {
    for (auto const& item : latency_db::entries) if (item.vid == vid && item.did == did)
        return {std::move(vid), std::move(did), std::string(item.name), std::string(item.platform), std::string(item.usb), std::move(instance), std::move(bus), item.chip_level};
    const bool intel = vid == "8086", amd = vid == "1022";
    return {std::move(vid), std::move(did), intel ? "Intel USB Controller" : amd ? "AMD USB Controller" : "Unknown USB Controller",
        intel ? "Unknown PCH" : amd ? "Unknown Chipset" : "PCIe Add-in", "USB 3.x", std::move(instance), std::move(bus), 1};
}

std::vector<Controller> scan_controllers() {
    constexpr auto pci = L"SYSTEM\\CurrentControlSet\\Enum\\PCI"; std::vector<Controller> result;
    for (auto const& device : enum_keys(HKEY_LOCAL_MACHINE, pci)) {
        const auto device_ascii = utf8(device); const auto vid = extract_hex(device_ascii, "VEN_"); const auto did = extract_hex(device_ascii, "DEV_"); if (!vid || !did) continue;
        const auto device_path = std::wstring(pci) + L"\\" + device;
        for (auto const& instance : enum_keys(HKEY_LOCAL_MACHINE, device_path)) {
            const auto path = device_path + L"\\" + instance; const auto guid = read_string(HKEY_LOCAL_MACHINE, path, L"ClassGUID");
            if (!guid || _wcsicmp(guid->c_str(), L"{36fc9e60-c465-11cf-8056-444553540000}") != 0) continue;
            const auto instance_ascii = utf8(instance); auto controller = lookup(*vid, *did, "PCI\\" + device_ascii + "\\" + instance_ascii, strip_last(instance_ascii).value_or(instance_ascii));
            const auto parameters = L"SYSTEM\\CurrentControlSet\\Enum\\" + wide(controller.instance_id) + L"\\Device Parameters";
            if (auto msi = read_dword(HKEY_LOCAL_MACHINE, parameters + L"\\Interrupt Management\\MessageSignaledInterruptProperties", L"MSISupported")) controller.msi_status = *msi == 1 ? "MSI" : "Line-Based";
            if (auto suspend = read_dword(HKEY_LOCAL_MACHINE, parameters, L"SelectiveSuspendEnabled")) controller.selective_suspend = *suspend == 1;
            result.push_back(std::move(controller));
        }
    }
    std::ranges::sort(result, {}, &Controller::chip_level); return result;
}

std::vector<UsbNode> scan_usb_tree() {
    constexpr auto usb = L"SYSTEM\\CurrentControlSet\\Enum\\USB"; std::vector<UsbNode> result;
    for (auto const& device : enum_keys(HKEY_LOCAL_MACHINE, usb)) {
        const auto device_path = std::wstring(usb) + L"\\" + device;
        for (auto const& instance : enum_keys(HKEY_LOCAL_MACHINE, device_path)) {
            const auto path = device_path + L"\\" + instance;
            auto name = read_string(HKEY_LOCAL_MACHINE, path, L"FriendlyName").value_or(read_string(HKEY_LOCAL_MACHINE, path, L"DeviceDesc").value_or(device));
            std::vector<std::string> ids; for (auto const& id : read_multi_string(HKEY_LOCAL_MACHINE, path, L"CompatibleIDs")) ids.push_back(utf8(id));
            result.push_back({utf8(device), utf8(instance), utf8(read_string(HKEY_LOCAL_MACHINE, path, L"ParentIdPrefix").value_or(L"")), utf8(name), std::move(ids)});
        }
    }
    return result;
}

std::optional<std::pair<std::size_t, int>> trace_chain(std::string current,
    std::unordered_map<std::string, std::size_t> const& prefixes, std::unordered_map<std::string, std::size_t> const& instances,
    std::vector<UsbNode> const& nodes, std::unordered_map<std::string, std::size_t> const& buses) {
    int hubs{};
    for (int step{}; step < 20; ++step) {
        auto stripped = strip_last(current); if (!stripped) return std::nullopt;
        if (auto found = prefixes.find(*stripped); found != prefixes.end()) {
            auto const& parent = nodes[found->second]; auto upper = parent.device_key;
            std::ranges::transform(upper, upper.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
            if (upper.find("ROOT_HUB") != std::string::npos) { auto bus = strip_last(parent.instance); if (!bus) return std::nullopt; auto controller = buses.find(*bus); if (controller == buses.end()) return std::nullopt; return {{controller->second, hubs}}; }
            ++hubs; current = parent.instance; continue;
        }
        if (auto found = instances.find(*stripped); found != instances.end()) { current = nodes[found->second].instance; continue; }
        break;
    }
    return std::nullopt;
}

std::string run_command(wchar_t const* command) {
    return detail::run_captured(std::wstring(command), 5 * 60 * 1000).second;
}

std::vector<Device> pnp_devices(std::vector<Controller> const& controllers) {
    std::vector<Device> result;
    const auto devices = SetupDiGetClassDevsW(nullptr, nullptr, nullptr, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (devices == INVALID_HANDLE_VALUE) return result;
    std::unordered_set<std::string> seen;
    auto instance_id = [](DEVINST node) {
        std::array<wchar_t, MAX_DEVICE_ID_LEN> value{};
        return CM_Get_Device_IDW(node, value.data(), static_cast<ULONG>(value.size()), 0) == CR_SUCCESS ? std::wstring(value.data()) : std::wstring{};
    };
    auto property = [devices](SP_DEVINFO_DATA& info, DWORD key) {
        std::array<wchar_t, 1024> value{}; DWORD type{}, bytes{};
        return SetupDiGetDeviceRegistryPropertyW(devices, &info, key, &type, reinterpret_cast<BYTE*>(value.data()), sizeof(value), &bytes)
            ? std::wstring(value.data()) : std::wstring{};
    };
    auto node_property = [](DEVINST node, DEVPROPKEY const& key) {
        std::array<wchar_t, 512> value{}; ULONG bytes = sizeof(value); DEVPROPTYPE type{};
        return CM_Get_DevNode_PropertyW(node, &key, &type, reinterpret_cast<PBYTE>(value.data()), &bytes, 0) == CR_SUCCESS
            ? std::wstring(value.data()) : std::wstring{};
    };
    for (DWORD index{};; ++index) {
        SP_DEVINFO_DATA info{sizeof(info)};
        if (!SetupDiEnumDeviceInfo(devices, index, &info)) break;
        auto id = instance_id(info.DevInst);
        auto compatible = property(info, SPDRP_COMPATIBLEIDS);
        auto device_class = property(info, SPDRP_CLASS);
        auto lowered = compatible + L" " + device_class;
        std::ranges::transform(lowered, lowered.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
        if (!lowered.contains(L"class_03") && !lowered.contains(L"xboxcomposite") &&
            !lowered.contains(L"xnacomposite") && !lowered.contains(L"xusbclass")) continue;

        DEVINST current = info.DevInst;
        int hubs{};
        std::wstring controller_id;
        for (int step{}; step < 15; ++step) {
            auto current_id = instance_id(current);
            auto upper = current_id;
            std::ranges::transform(upper, upper.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towupper(c)); });
            DEVINST parent{};
            if (CM_Get_Parent(&parent, current, 0) != CR_SUCCESS) break;
            if (upper.contains(L"ROOT_HUB")) { controller_id = instance_id(parent); break; }
            auto label = node_property(current, DEVPKEY_Device_FriendlyName);
            if (label.empty()) label = node_property(current, DEVPKEY_Device_DeviceDesc);
            if (label.contains(L"Hub") && !label.contains(L"Root")) ++hubs;
            current = parent;
        }
        if (controller_id.empty()) continue;
        const auto controller = std::ranges::find_if(controllers, [&](auto const& item) {
            return _wcsicmp(wide(item.instance_id).c_str(), controller_id.c_str()) == 0;
        });
        if (controller == controllers.end()) continue;
        auto ascii_id = utf8(id);
        auto vid = extract_hex(ascii_id, "VID_").value_or("????");
        auto pid = extract_hex(ascii_id, "PID_").value_or("????");
        if (!seen.insert(ascii_id).second) continue;
        auto name = node_property(info.DevInst, DEVPKEY_Device_BusReportedDeviceDesc);
        if (name.empty()) name = property(info, SPDRP_FRIENDLYNAME);
        if (name.empty()) name = property(info, SPDRP_DEVICEDESC);
        const auto controller_index = static_cast<std::size_t>(controller - controllers.begin());
        result.push_back({utf8(name), std::move(vid), std::move(pid), ascii_id, controller->chip_level + hubs, hubs, controller_index});
    }
    SetupDiDestroyDeviceInfoList(devices);
    return result;
}

std::optional<bool> system_suspend() {
    GUID usb_subgroup{0x2a737441, 0x1930, 0x4402, {0x8d, 0x77, 0xb2, 0xbe, 0xbb, 0xa3, 0x08, 0xa3}};
    GUID usb_suspend{0x48e6b7a6, 0x50f5, 0x4782, {0xa5, 0xd4, 0x53, 0xbb, 0x8f, 0x07, 0xe2, 0x26}};
    GUID* scheme{};
    if (PowerGetActiveScheme(nullptr, &scheme) != ERROR_SUCCESS || !scheme) return std::nullopt;
    DWORD value{};
    const auto status = PowerReadACValueIndex(nullptr, scheme, &usb_subgroup, &usb_suspend, &value);
    LocalFree(scheme);
    if (status != ERROR_SUCCESS) return std::nullopt;
    return value != 0;
}

void add(std::vector<LatencyLine>& lines, std::string text = {}, LatencyColor color = LatencyColor::normal, bool bold = false) { lines.push_back({std::move(text), color, bold}); }
LatencyColor chip_color(int level) { return level == 0 ? LatencyColor::success : level == 1 ? LatencyColor::warning : LatencyColor::critical; }
std::string chip_label(int level) { return level == 0 ? "CHIP 0 - INSIDE CPU" : level == 1 ? "CHIP 1 - CHIPSET" : "CHIP " + std::to_string(level) + " - HUB"; }

} // namespace

winchisel::core::Result<LatencyAnalysis> analyze_usb_topology(LatencyProgress progress) {
    auto notify = [&](int value, std::string_view status) { if (progress) progress(value, status); };
    notify(5, "Checking power settings..."); const auto suspend = system_suspend();
    notify(15, "Scanning USB controllers..."); auto controllers = scan_controllers();
    notify(35, "Scanning USB registry tree..."); const auto nodes = scan_usb_tree();
    notify(55, "Finding input devices..."); auto devices = pnp_devices(controllers);

    std::unordered_set<std::string> seen; for (auto const& device : devices) if (!device.instance.empty()) seen.insert(device.instance);
    std::unordered_map<std::string, std::size_t> prefixes, instances, buses;
    for (std::size_t i{}; i < nodes.size(); ++i) { if (!nodes[i].parent_prefix.empty()) prefixes[nodes[i].parent_prefix] = i; instances[nodes[i].instance] = i; }
    for (std::size_t i{}; i < controllers.size(); ++i) buses[controllers[i].bus_prefix] = i;
    notify(82, "Verifying fallback USB tree...");
    for (auto const& node : nodes) {
        const bool hid = std::ranges::any_of(node.compatible_ids, [](auto const& id) { return lower(id).find("class_03") != std::string::npos; }); if (!hid) continue;
        auto base = node.device_key; if (auto mi = base.find("&MI_"); mi != std::string::npos) base.resize(mi); if (!seen.insert(node.instance.empty() ? base : node.instance).second) continue;
        const bool composite_interface = node.device_key.find("&MI_") != std::string::npos;
        const auto trace_instance = composite_interface ? strip_last(node.instance).value_or(node.instance) : node.instance;
        auto trace = trace_chain(trace_instance, prefixes, instances, nodes, buses); if (!trace) continue;
        auto name = node.name;
        if (composite_interface) {
            const auto composite_instance = strip_last(node.instance).value_or("");
            if (auto parent = std::ranges::find_if(nodes, [&](auto const& candidate) {
                    return candidate.device_key.find("&MI_") == std::string::npos && candidate.device_key.starts_with(base) && candidate.instance == composite_instance;
                }); parent != nodes.end()) name = parent->name;
        }
        const auto [controller, hubs] = *trace; devices.push_back({std::move(name), "????", "????", node.instance, controllers[controller].chip_level + hubs, hubs, controller});
    }

    notify(95, "Building report..."); LatencyAnalysis result; auto& out = result.lines;
    add(out); add(out, "  USB LATENCY ANALYZER", LatencyColor::accent, true); add(out, "  =====================================================================", LatencyColor::separator); add(out);
    add(out, "  Count chips between your device and CPU. More chips = more latency.", LatencyColor::muted); add(out);
    add(out, "  0 CHIPS  device --- [CPU]", LatencyColor::success, true); add(out, "  1 CHIP   device -[CHIPSET]- [CPU]", LatencyColor::warning, true); add(out, "  2 CHIPS  device -[HUB]-[CHIPSET]- [CPU]", LatencyColor::critical, true); add(out);
    add(out, "  =============================================================", LatencyColor::separator); add(out);
    if (controllers.empty()) add(out, "  No USB controllers detected. Try running as Administrator.", LatencyColor::muted);
    else if (!std::ranges::any_of(controllers, [](auto const& controller) { return controller.chip_level == 0; })) {
        add(out, "  ! This system has no direct CPU USB", LatencyColor::warning);
        add(out, "    1 chip is your best option here", LatencyColor::muted);
        add(out);
    }
    for (int level : {0, 1, 2}) {
        std::vector<Device const*> group; for (auto const& device : devices) if ((level < 2 && device.chip_count == level) || (level == 2 && device.chip_count >= 2)) group.push_back(&device);
        if (group.empty()) continue; add(out, level == 0 ? "  0 chips - direct to CPU" : level == 1 ? "  1 chip - through chipset" : "  2+ chips - through hub", chip_color(level));
        for (std::size_t i{}; i < group.size(); ++i) add(out, std::string(i + 1 == group.size() ? "    '- " : "    |- ") + group[i]->name + (group[i]->hub_count ? " (+" + std::to_string(group[i]->hub_count) + " hub)" : ""), chip_color(level)); add(out);
    }
    if (devices.empty()) { add(out, "  No USB input devices detected", LatencyColor::muted); add(out); }
    add(out, "  =============================================================", LatencyColor::separator); add(out); add(out, "  Unplug and replug to test different ports", LatencyColor::muted); add(out);
    add(out, "  CONTROLLERS", LatencyColor::normal, true); add(out, "  ---------------------------------------------------------------------", LatencyColor::separator);
    for (std::size_t i{}; i < controllers.size(); ++i) { auto const& controller = controllers[i]; add(out); add(out, "  " + chip_label(controller.chip_level), chip_color(controller.chip_level), true); add(out, "      " + controller.name); add(out, "      VID:" + controller.vid + " DID:" + controller.did + " | " + controller.platform + " | " + controller.usb, LatencyColor::muted); add(out, "      IRQ: " + controller.msi_status + (controller.msi_status == "MSI" ? " (low latency interrupts)" : controller.msi_status == "Line-Based" ? " (higher latency)" : ""), controller.msi_status == "MSI" ? LatencyColor::success : controller.msi_status == "Line-Based" ? LatencyColor::critical : LatencyColor::muted); if (controller.selective_suspend == true) add(out, "      ! Selective Suspend ENABLED (causes latency spikes)", LatencyColor::warning); bool heading{}; for (auto const& device : devices) if (device.controller == i) { if (!heading) { add(out, "      Devices:", LatencyColor::muted); heading = true; } add(out, "        |- " + device.name + (device.hub_count ? " (+hub)" : "")); } }
    add(out); add(out, "  INPUT DEVICES", LatencyColor::normal, true); add(out, "  ---------------------------------------------------------------------", LatencyColor::separator);
    std::ranges::sort(devices, {}, &Device::chip_count); for (auto const& device : devices) { add(out); add(out, "  " + device.name); add(out, "      VID:" + device.vid + " PID:" + device.pid, LatencyColor::muted); add(out, "      " + chip_label(device.chip_count), chip_color(device.chip_count)); add(out, "      via " + controllers[device.controller].name + " (" + controllers[device.controller].platform + ")", LatencyColor::muted); }
    const bool has_optimizations = suspend == true || std::ranges::any_of(controllers, [](auto const& c) { return c.msi_status == "Line-Based" || c.selective_suspend == true; });
    if (has_optimizations) { add(out); add(out, "  OPTIMIZATIONS AVAILABLE", LatencyColor::normal, true); add(out, "  ---------------------------------------------------------------------", LatencyColor::separator); add(out); if (suspend == true) add(out, "  ! Disable USB Selective Suspend in current power plan", LatencyColor::warning); for (auto const& controller : controllers) { if (controller.msi_status == "Line-Based") add(out, "  ! Enable MSI interrupts on " + controller.name, LatencyColor::critical); if (controller.selective_suspend == true) add(out, "  ! Disable Selective Suspend on " + controller.name, LatencyColor::warning); } }
    add(out); add(out, "  =====================================================================", LatencyColor::separator); add(out); notify(100, "Ready"); return result;
}

} // namespace winchisel::platform
