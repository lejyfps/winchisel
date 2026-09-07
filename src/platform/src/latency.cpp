#include "winchisel/platform/latency.hpp"
#include <windows.h>
#include <array>
#include <cstdio>
#include <memory>

namespace winchisel::platform {
winchisel::core::Result<std::string> analyze_usb_topology() {
    std::array<wchar_t, MAX_PATH> executable{};
    GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    std::wstring script(executable.data());
    script = script.substr(0, script.find_last_of(L"\\/")) + L"\\cpu-direct-usb.ps1";
    const auto required = WideCharToMultiByte(CP_UTF8, 0, script.data(), static_cast<int>(script.size()), nullptr, 0, nullptr, nullptr);
    std::string utf8(required, '\0');
    WideCharToMultiByte(CP_UTF8, 0, script.data(), static_cast<int>(script.size()), utf8.data(), required, nullptr, nullptr);
    const auto command = "powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File \"" + utf8 + "\" -NonInteractive -Embedded";
    std::array<char, 4096> buffer{}; std::string output;
    std::unique_ptr<FILE, decltype(&_pclose)> pipe(_popen(command.c_str(), "r"), _pclose);
    if (!pipe) return std::unexpected(winchisel::core::Error{.message_key="latency_start_failed", .detail="Unable to start USB analysis."});
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe.get())) output += buffer.data();
    if (_pclose(pipe.release()) != 0 || output.empty()) return std::unexpected(winchisel::core::Error{.message_key="latency_failed", .detail="USB analysis failed."});
    return output;
}
}
