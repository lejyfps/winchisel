#include "winchisel/platform/home.hpp"

#include <dxgi.h>
#include <windows.h>
#include <TlHelp32.h>

#include <cstdio>
#include <string>
#include <string_view>

#pragma comment(lib, "dxgi.lib")

namespace winchisel::platform {
namespace {

std::string narrow(std::wstring_view w) {
    if (w.empty()) {
        return {};
    }
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

std::string format_gb(double gb, const char* suffix) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.1f %s", gb, suffix);
    return buf;
}

DWORD reg_dword(HKEY root, const wchar_t* path, const wchar_t* value) {
    HKEY key{};
    if (RegOpenKeyExW(root, path, 0, KEY_READ, &key) != ERROR_SUCCESS) return 0;
    DWORD data{}, size = sizeof(data), type = 0;
    const auto st = RegQueryValueExW(key, value, nullptr, &type, reinterpret_cast<LPBYTE>(&data), &size);
    RegCloseKey(key);
    return st == ERROR_SUCCESS && type == REG_DWORD ? data : 0;
}

std::wstring reg_sz(HKEY root, const wchar_t* path, const wchar_t* value) {
    // No cache: a handful of reads per refresh is cheap, and cached values
    // would go stale (e.g. after a feature update) without any invalidation.
    HKEY key{};
    if (RegOpenKeyExW(root, path, 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return {};
    }
    DWORD type{}, size{};
    if (RegQueryValueExW(key, value, nullptr, &type, nullptr, &size) != ERROR_SUCCESS ||
        (type != REG_SZ && type != REG_EXPAND_SZ) || !size || size > 32768) {
        RegCloseKey(key);
        return {};
    }
    std::wstring text(size / sizeof(wchar_t), L'\0');
    const auto status = RegQueryValueExW(key, value, nullptr, &type, reinterpret_cast<LPBYTE>(text.data()), &size);
    RegCloseKey(key);
    if (status != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) {
        return {};
    }
    while (!text.empty() && !text.back()) text.pop_back();
    return text;
}

float cpu_usage() {
    FILETIME idle{}, kernel{}, user{};
    if (!GetSystemTimes(&idle, &kernel, &user)) {
        return 0.f;
    }
    auto u64 = [](FILETIME ft) {
        ULARGE_INTEGER v;
        v.LowPart = ft.dwLowDateTime;
        v.HighPart = ft.dwHighDateTime;
        return v.QuadPart;
    };
    static ULONGLONG prev_idle{};
    static ULONGLONG prev_kernel{};
    static ULONGLONG prev_user{};
    static bool first_call = true;
    const auto i = u64(idle);
    const auto k = u64(kernel);
    const auto u = u64(user);
    if (first_call) {
        first_call = false;
        prev_idle = i;
        prev_kernel = k;
        prev_user = u;
        return 0.f;
    }
    const auto di = i - prev_idle;
    const auto dt = (k - prev_kernel) + (u - prev_user);
    prev_idle = i;
    prev_kernel = k;
    prev_user = u;
    if (dt == 0) {
        return 0.f;
    }
    const float used = 1.f - static_cast<float>(di) / static_cast<float>(dt);
    return used < 0.f ? 0.f : (used > 1.f ? 1.f : used);
}

void gpu(std::string& name, std::string& vram) {
    static const auto cached = [] {
        std::pair<std::string, std::string> result;
        auto& [cached_name, cached_vram] = result;
    IDXGIFactory1* factory{};
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) || factory == nullptr) {
        cached_name = "Unknown GPU";
        return result;
    }
    IDXGIAdapter1* adapter{};
    if (SUCCEEDED(factory->EnumAdapters1(0, &adapter)) && adapter != nullptr) {
        DXGI_ADAPTER_DESC1 desc{};
        adapter->GetDesc1(&desc);
        cached_name = narrow(desc.Description);
        cached_vram = format_gb(static_cast<double>(desc.DedicatedVideoMemory) / (1024.0 * 1024.0 * 1024.0), "GB VRAM");
        adapter->Release();
    }
    factory->Release();
    if (cached_name.empty()) {
        cached_name = "Unknown GPU";
    }
        return result;
    }();
    name = cached.first;
    vram = cached.second;
}

std::string display() {
    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    if (!EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &mode)) {
        return {};
    }
    char buf[80];
    std::snprintf(buf, sizeof(buf), "%lux%lu @ %lu Hz", mode.dmPelsWidth, mode.dmPelsHeight, mode.dmDisplayFrequency);
    return buf;
}

std::string uptime() {
    const auto ms = GetTickCount64();
    const auto s = ms / 1000;
    const auto d = s / 86400;
    const auto h = (s % 86400) / 3600;
    const auto m = (s % 3600) / 60;
    char buf[64];
    if (d > 0) {
        std::snprintf(buf, sizeof(buf), "%llud %lluh %llum", d, h, m);
    } else {
        std::snprintf(buf, sizeof(buf), "%lluh %llum", h, m);
    }
    return buf;
}

std::uint32_t process_count() {
    static std::uint32_t cached{};
    static ULONGLONG last_tick{};
    const auto now = GetTickCount64();
    if (cached && now - last_tick < 15000) return cached;
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return cached;
    PROCESSENTRY32W entry{.dwSize = sizeof(PROCESSENTRY32W)};
    std::uint32_t count{};
    if (Process32FirstW(snapshot, &entry)) do { ++count; } while (Process32NextW(snapshot, &entry));
    CloseHandle(snapshot);
    cached = count;
    last_tick = now;
    return count;
}

}  // namespace

winchisel::core::HomeInfo query_home_info() {
    winchisel::core::HomeInfo info;

    info.cpu_brand = narrow(reg_sz(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"ProcessorNameString"));
    if (info.cpu_brand.empty()) {
        info.cpu_brand = "Unknown CPU";
    }
    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    char cores[32];
    std::snprintf(cores, sizeof(cores), "%u", si.dwNumberOfProcessors);
    info.cpu_cores = cores;
    const DWORD mhz = reg_dword(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"~MHz");
    if (mhz > 0) {
        char sp[32];
        std::snprintf(sp, sizeof(sp), "%.2f GHz", mhz / 1000.0);
        info.cpu_speed = sp;
    }
    info.cpu_usage_percent = cpu_usage() * 100.f;
    char usage[16];
    std::snprintf(usage, sizeof(usage), "%.0f%%", info.cpu_usage_percent);
    info.cpu_usage = usage;

    gpu(info.gpu_name, info.gpu_vram);
    info.gpu_driver = narrow(reg_sz(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"DisplayVersion"));

    MEMORYSTATUSEX mem{};
    mem.dwLength = sizeof(mem);
    GlobalMemoryStatusEx(&mem);
    const double total_gb = mem.ullTotalPhys / (1024.0 * 1024.0 * 1024.0);
    const double used_gb = (mem.ullTotalPhys - mem.ullAvailPhys) / (1024.0 * 1024.0 * 1024.0);
    info.memory_total = format_gb(total_gb, "GB total");
    info.memory_used = format_gb(used_gb, "GB used");
    info.ram_details = format_gb(mem.ullAvailPhys / (1024.0 * 1024.0 * 1024.0), "GB available");
    info.memory_fraction = total_gb > 0 ? static_cast<float>(used_gb / total_gb) : 0.f;

    ULARGE_INTEGER free_b{}, total_b{}, dummy{};
    double disk_total = 0, disk_used = 0;
    wchar_t drives[256]{};
    GetLogicalDriveStringsW(static_cast<DWORD>(std::size(drives)), drives);
    for (wchar_t* p = drives; *p; p += wcslen(p) + 1) {
        if (GetDriveTypeW(p) != DRIVE_FIXED) {
            continue;
        }
        if (GetDiskFreeSpaceExW(p, &dummy, &total_b, &free_b)) {
            disk_total += total_b.QuadPart / (1024.0 * 1024.0 * 1024.0);
            disk_used += (total_b.QuadPart - free_b.QuadPart) / (1024.0 * 1024.0 * 1024.0);
        }
    }
    info.storage_total = format_gb(disk_total, "GB total");
    info.storage_used = format_gb(disk_used, "GB used");
    info.storage_fraction = disk_total > 0 ? static_cast<float>(disk_used / disk_total) : 0.f;

    info.os_version = "Windows " + narrow(reg_sz(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"DisplayVersion"));
    info.windows_build = "Build " + narrow(reg_sz(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"CurrentBuildNumber"));
    wchar_t host[MAX_COMPUTERNAME_LENGTH + 1]{};
    DWORD host_n = MAX_COMPUTERNAME_LENGTH + 1;
    GetComputerNameW(host, &host_n);
    info.computer_name = narrow(host);
    info.kernel_version = narrow(reg_sz(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"CurrentVersion"));

    const auto man = narrow(reg_sz(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\BIOS", L"SystemManufacturer"));
    const auto prod = narrow(reg_sz(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\BIOS", L"SystemProductName"));
    info.motherboard = (man.empty() ? "" : man + " ") + prod;
    const auto bios = narrow(reg_sz(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\BIOS", L"BIOSVersion"));
    const auto bios_date = narrow(reg_sz(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\BIOS", L"BIOSReleaseDate"));
    info.bios = bios_date.empty() ? bios : bios + " — " + bios_date;

    info.display = display();
    info.uptime = uptime();
    info.process_count = process_count();
    return info;
}

}  // namespace winchisel::platform
