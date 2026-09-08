#include "winchisel/platform/process.hpp"

#include <Windows.h>
#include <string>

namespace winchisel::platform {
namespace {

std::wstring exe_name(std::wstring name) {
    if (!name.ends_with(L".exe")) name += L".exe";
    return name;
}

std::wstring ifeo_path(std::wstring const& image) {
    return L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options\\" + exe_name(image) + L"\\PerfOptions";
}

winchisel::core::Error win32_error(std::string detail) {
    return {std::move(detail)};
}

}  // namespace

winchisel::core::Result<void> set_ifeo_dword(std::wstring const& image, std::wstring_view value_name, std::uint32_t value) {
    const auto path = ifeo_path(image);
    HKEY key{};
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, path.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
        return std::unexpected(win32_error(std::to_string(GetLastError())));
    }
    const auto status = RegSetValueExW(key, std::wstring(value_name).c_str(), 0, REG_DWORD, reinterpret_cast<BYTE const*>(&value), sizeof(value));
    RegCloseKey(key);
    if (status != ERROR_SUCCESS) return std::unexpected(win32_error(std::to_string(status)));
    return {};
}

winchisel::core::Result<void> remove_ifeo_dword(std::wstring const& image, std::wstring_view value_name) {
    const auto exe = exe_name(image);
    const auto base = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options\\" + exe;
    const auto path = base + L"\\PerfOptions";
    HKEY key{};
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, path.c_str(), 0, KEY_QUERY_VALUE | KEY_SET_VALUE, &key) != ERROR_SUCCESS) {
        return {};
    }
    const auto removed = RegDeleteValueW(key, std::wstring(value_name).c_str());
    DWORD values{}, subkeys{};
    const bool empty = RegQueryInfoKeyW(key, nullptr, nullptr, nullptr, &subkeys, nullptr, nullptr, &values, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS && !values && !subkeys;
    RegCloseKey(key);
    if (empty) RegDeleteKeyW(HKEY_LOCAL_MACHINE, path.c_str());
    HKEY parent{};
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, base.c_str(), 0, KEY_QUERY_VALUE, &parent) == ERROR_SUCCESS) {
        if (RegQueryInfoKeyW(parent, nullptr, nullptr, nullptr, &subkeys, nullptr, nullptr, &values, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS && !values && !subkeys) {
            RegCloseKey(parent);
            RegDeleteKeyW(HKEY_LOCAL_MACHINE, base.c_str());
        } else {
            RegCloseKey(parent);
        }
    }
    if (removed != ERROR_SUCCESS && removed != ERROR_FILE_NOT_FOUND) return std::unexpected(win32_error(std::to_string(removed)));
    return {};
}

std::optional<std::uint32_t> read_ifeo_dword(std::wstring const& image, std::wstring_view value_name) {
    const auto path = ifeo_path(image);
    DWORD value{}, size = sizeof(value), type{};
    if (RegGetValueW(HKEY_LOCAL_MACHINE, path.c_str(), std::wstring(value_name).c_str(), RRF_RT_REG_DWORD, &type, &value, &size) == ERROR_SUCCESS) {
        return value;
    }
    return std::nullopt;
}

}  // namespace winchisel::platform
