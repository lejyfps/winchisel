#include "winchisel/platform/process.hpp"
#include "winchisel/platform/registry.hpp"
#include "winchisel/platform/revert.hpp"
#include "winchisel/platform/system.hpp"

#include <Windows.h>
#include <cstdint>
#include <cstring>
#include <optional>
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

std::string to_utf8(std::wstring_view text) {
    if (text.empty()) return {};
    const int length =
        WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (length <= 0) return {};
    std::string result(static_cast<std::size_t>(length), '\0');
    if (WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), length, nullptr,
            nullptr) <= 0)
        return {};
    return result;
}

struct IfeoBefore {
    winchisel::core::RegistryTarget target;
    winchisel::core::RegistryNativeValue native;
    bool readable{};
};

IfeoBefore read_ifeo_before(std::wstring const& image, std::wstring_view value_name) {
    IfeoBefore result;
    result.target = winchisel::core::RegistryTarget{winchisel::core::RegistryHive::local_machine,
        to_utf8(ifeo_path(image)), to_utf8(std::wstring(value_name)), winchisel::core::RegistryValueType::dword};
    if (result.target.key_path.empty() || result.target.value_name.empty()) return result;
    auto before = read_registry_native(result.target);
    if (!before) return result;
    result.native.missing = before->missing;
    result.native.type = before->type;
    result.native.data = before->data;
    result.readable = true;
    return result;
}

void record_ifeo_change(std::wstring const& image, IfeoBefore const& before, std::optional<std::uint32_t> desired) {
    // desired == nullopt means removal. Unchanged pairs stay silent (this
    // also keeps the "remove missing" unit test out of the journal).
    if (!before.readable || revert_recording_suppressed()) return;
    bool changed{};
    if (desired) {
        if (before.native.missing || before.native.type != REG_DWORD ||
            before.native.data.size() != sizeof(std::uint32_t)) {
            changed = true;
        } else {
            std::uint32_t stored{};
            std::memcpy(&stored, before.native.data.data(), sizeof(stored));
            changed = stored != *desired;
        }
    } else {
        changed = !before.native.missing;
    }
    if (!changed) return;
    auto entry = make_revert_entry("processes", "Processes: " + to_utf8(exe_name(image)));
    winchisel::core::RevertRegistryStep step;
    step.target = before.target;
    step.before = before.native;
    entry.registry.push_back(std::move(step));
    if (auto recorded = record_revert(std::move(entry)); !recorded) {
        boot_log(("revert journal record failed: " + recorded.error().detail).c_str());
    }
}

winchisel::core::Error win32_error(std::string detail) {
    return {std::move(detail)};
}

}  // namespace

winchisel::core::Result<void> set_ifeo_dword(std::wstring const& image, std::wstring_view value_name, std::uint32_t value) {
    const auto before = read_ifeo_before(image, value_name);
    const auto path = ifeo_path(image);
    HKEY key{};
    const auto open_status = RegCreateKeyExW(HKEY_LOCAL_MACHINE, path.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
    if (open_status != ERROR_SUCCESS) {
        return std::unexpected(win32_error(std::to_string(open_status)));
    }
    const auto status = RegSetValueExW(key, std::wstring(value_name).c_str(), 0, REG_DWORD, reinterpret_cast<BYTE const*>(&value), sizeof(value));
    RegCloseKey(key);
    if (status != ERROR_SUCCESS) return std::unexpected(win32_error(std::to_string(status)));
    record_ifeo_change(image, before, value);
    return {};
}

winchisel::core::Result<void> remove_ifeo_dword(std::wstring const& image, std::wstring_view value_name) {
    const auto before = read_ifeo_before(image, value_name);
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
    record_ifeo_change(image, before, std::nullopt);
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
