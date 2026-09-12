#include "winchisel/platform/usb_identity.hpp"

#include <windows.h>
#include <cfgmgr32.h>
#include <initguid.h>
#include <devpkey.h>
#include <setupapi.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cwctype>

#pragma comment(lib, "cfgmgr32.lib")
#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "ole32.lib")

namespace winchisel::platform::cadence {
namespace {

std::string narrow(std::wstring_view value) {
    if (value.empty()) return {};
    const int length = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                                           nullptr, 0, nullptr, nullptr);
    if (length <= 0) return {};
    std::string result(static_cast<std::size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), length,
                        nullptr, nullptr);
    return result;
}

std::string upper(std::string value) {
    std::ranges::transform(value, value.begin(),
                           [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return value;
}

void parse_vid_pid(std::string const& id, std::string& vid, std::string& pid) {
    auto find_hex = [&](char const* marker) -> std::string {
        const auto pos = upper(id).find(marker);
        if (pos == std::string::npos || pos + 8 > id.size()) return {};
        std::string hex = id.substr(pos + 4, 4);
        if (!std::ranges::all_of(hex, [](unsigned char c) { return std::isxdigit(c) != 0; })) return {};
        return upper(hex);
    };
    vid = find_hex("VID_");
    pid = find_hex("PID_");
}

std::wstring devnode_id(DEVINST node) {
    std::array<wchar_t, MAX_DEVICE_ID_LEN> value{};
    return CM_Get_Device_IDW(node, value.data(), static_cast<ULONG>(value.size()), 0) == CR_SUCCESS
               ? std::wstring(value.data())
               : std::wstring{};
}

std::wstring devnode_property(DEVINST node, DEVPROPKEY const& key) {
    std::array<wchar_t, 512> value{};
    ULONG bytes = sizeof(value);
    DEVPROPTYPE type{};
    return CM_Get_DevNode_PropertyW(node, &key, &type, reinterpret_cast<PBYTE>(value.data()), &bytes, 0) ==
                   CR_SUCCESS
               ? std::wstring(value.data())
               : std::wstring{};
}

} // namespace

std::vector<UsbInstance> enumerate_usb_instances() {
    std::vector<UsbInstance> result;
    const auto devices = SetupDiGetClassDevsW(nullptr, nullptr, nullptr, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (devices == INVALID_HANDLE_VALUE) return result;
    for (DWORD index{};; ++index) {
        SP_DEVINFO_DATA info{sizeof(info)};
        if (!SetupDiEnumDeviceInfo(devices, index, &info)) break;
        const std::string id = narrow(devnode_id(info.DevInst));
        if (id.size() < 4 || upper(id).rfind("USB\\", 0) != 0) continue; // nur USB-Stack
        UsbInstance instance;
        instance.instance_id = id;
        parse_vid_pid(id, instance.vid, instance.pid);
        instance.bus_desc = narrow(devnode_property(info.DevInst, DEVPKEY_Device_BusReportedDeviceDesc));
        if (instance.bus_desc.empty())
            instance.bus_desc = narrow(devnode_property(info.DevInst, DEVPKEY_Device_DeviceDesc));
        // Container-ID (GUID -> String), fail-open bei Fehlen.
        GUID container{};
        ULONG bytes = sizeof(container);
        DEVPROPTYPE type{};
        if (CM_Get_DevNode_PropertyW(info.DevInst, &DEVPKEY_Device_ContainerId, &type,
                                     reinterpret_cast<PBYTE>(&container), &bytes, 0) == CR_SUCCESS &&
            type == DEVPROP_TYPE_GUID) {
            wchar_t text[64]{};
            if (StringFromGUID2(container, text, static_cast<int>(std::size(text))) > 0)
                instance.container_id = narrow(text);
        }
        DEVINST parent{};
        if (CM_Get_Parent(&parent, info.DevInst, 0) == CR_SUCCESS)
            instance.parent_id = narrow(devnode_id(parent));
        result.push_back(std::move(instance));
    }
    SetupDiDestroyDeviceInfoList(devices);
    return result;
}

bool interface_path_matches(std::string const& interface_path, std::string const& instance_id) {
    if (interface_path.empty() || instance_id.empty()) return false;
    std::string path = upper(interface_path);
    std::ranges::replace(path, '#', '\\');
    const std::string needle = upper(instance_id);
    if (needle.size() < 5) return false;
    return path.find(needle) != std::string::npos;
}

std::optional<std::string> resolve_container_id(std::string const& interface_path,
                                                std::vector<UsbInstance> const& instances) {
    std::string const* best_container = nullptr;
    std::size_t best_length = 0;
    for (auto const& instance : instances) {
        if (instance.container_id.empty()) continue;
        if (interface_path_matches(interface_path, instance.instance_id) &&
            instance.instance_id.size() > best_length) {
            best_length = instance.instance_id.size();
            best_container = &instance.container_id;
        }
    }
    if (best_container == nullptr) return std::nullopt;
    return *best_container;
}

} // namespace winchisel::platform::cadence
