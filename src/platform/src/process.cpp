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
    const auto open_status = RegCreateKeyExW(HKEY_LOCAL_MACHINE, path.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
    if (open_status != ERROR_SUCCESS) {
        return std::unexpected(win32_error(std::to_string(open_status)));
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
    const auto open_status = RegOpenKeyExW(HKEY_LOCAL_MACHINE, path.c_str(), 0, KEY_QUERY_VALUE | KEY_SET_VALUE, &key);
    // Only a missing key means "nothing to remove". Any other failure
    // (e.g. access denied) must surface instead of pretending success.
    if (open_status == ERROR_FILE_NOT_FOUND) {
        return {};
    }
    if (open_status != ERROR_SUCCESS) {
        return std::unexpected(win32_error(std::to_string(open_status)));
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

constexpr int k_process_io_priority = 33;
using NtQueryInformationProcessFn = LONG(NTAPI*)(HANDLE, int, PVOID, ULONG, PULONG);
using NtSetInformationProcessFn = LONG(NTAPI*)(HANDLE, int, PVOID, ULONG);

FARPROC ntdll_proc(char const* name) {
    auto ntdll = GetModuleHandleW(L"ntdll.dll");
    return ntdll ? GetProcAddress(ntdll, name) : nullptr;
}

std::optional<std::uint32_t> read_process_io_priority(std::uint32_t pid) {
    auto fn = reinterpret_cast<NtQueryInformationProcessFn>(ntdll_proc("NtQueryInformationProcess"));
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    std::uint32_t value{};
    const bool ok = fn && process && fn(process, k_process_io_priority, &value, sizeof(value), nullptr) >= 0;
    if (process) CloseHandle(process);
    if (ok) return value;
    return std::nullopt;
}

winchisel::core::Result<void> set_process_io_priority(std::uint32_t pid, std::uint32_t value) {
    auto fn = reinterpret_cast<NtSetInformationProcessFn>(ntdll_proc("NtSetInformationProcess"));
    HANDLE process = OpenProcess(PROCESS_SET_INFORMATION | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    const DWORD open_error = process ? ERROR_SUCCESS : GetLastError();
    const LONG status = (fn && process) ? fn(process, k_process_io_priority, &value, sizeof(value)) : static_cast<LONG>(0xC0000001);
    if (process) CloseHandle(process);
    if (!fn) return std::unexpected(win32_error("NtSetInformationProcess unavailable"));
    if (!process) return std::unexpected(win32_error(std::to_string(open_error)));
    if (status < 0) return std::unexpected(win32_error(std::to_string(static_cast<unsigned long>(status))));
    return {};
}

}  // namespace winchisel::platform
