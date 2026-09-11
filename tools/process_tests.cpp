#define NOMINMAX
#include "../src/platform/src/process_wait.hpp"
#include "../src/platform/src/com_apartment.hpp"
#include "winchisel/platform/process.hpp"
#include <iostream>
#include <future>
#include "../src/app/AsyncResult.hpp"
#include "winchisel/core/dialog_slot.hpp"
#pragma comment(lib, "windowsapp.lib")
#pragma comment(lib, "ole32.lib")

int wmain(int argc, wchar_t** argv) {
    if (argc > 1) {
        if (std::wstring_view(argv[1]) == L"--finite") { std::cout << "finished"; return 0; }
        const auto output = GetStdHandle(STD_OUTPUT_HANDLE);
        const std::string data(4096, 'x');
        for (;;) { DWORD written{}; if (!WriteFile(output, data.data(), static_cast<DWORD>(data.size()), &written, nullptr)) return 0; }
    }
    wchar_t self[32768]{};
    GetModuleFileNameW(nullptr, self, static_cast<DWORD>(std::size(self)));
    auto command = L"\"" + std::wstring(self) + L"\"";
    const auto start = GetTickCount64();
    {
        wchar_t sys[MAX_PATH]{};
        GetSystemDirectoryW(sys, MAX_PATH);
        const auto cmd = winchisel::platform::detail::resolve_application(L"cmd.exe /c echo");
        const auto expected = std::wstring(sys) + L"\\cmd.exe";
        if (_wcsicmp(cmd.c_str(), expected.c_str()) != 0) {
            std::cerr << "FAIL system32 resolution\n";
            return 1;
        }
        if (!winchisel::platform::detail::resolve_application(L".\\cmd.exe /c echo").empty() ||
            !winchisel::platform::detail::resolve_application(L"notanexe /c").empty()) {
            std::cerr << "FAIL relative planting rejected\n";
            return 1;
        }
        std::cout << "ok process path resolution\n";
    }
    auto [timeout, output] = winchisel::platform::detail::run_captured(command + L" --flood", 150);
    if (!timeout.timed_out || timeout.exit_code != ERROR_TIMEOUT || GetTickCount64() - start > 5000 || output.empty()) {
        std::cerr << "FAIL continuous output timeout\n"; return 1;
    }
    auto [finite, text] = winchisel::platform::detail::run_captured(command + L" --finite", 5000);
    if (finite.timed_out || finite.exit_code || text != "finished") { std::cerr << "FAIL finite output\n"; return 1; }
    std::cout << "ok continuous output deadline and finite output\n";
    auto throwing = std::async(std::launch::async, [] {
        return winchisel::ui::result_or_error([]() -> winchisel::core::Result<void> { throw std::runtime_error("injected worker failure"); });
    });
    auto error = throwing.get();
    if (error || error.error().detail != "injected worker failure") return 1;
    auto winrt_error = winchisel::ui::result_or_error([]() -> winchisel::core::Result<void> { throw winrt::hresult_error(E_ACCESSDENIED); });
    if (winrt_error || winrt_error.error().detail.empty()) return 1;
    {
        winchisel::core::DialogSlot first, second;
        if (!first || second) return 1;
    }
    { winchisel::core::DialogSlot reopened; if (!reopened) return 1; }
    try { winchisel::core::DialogSlot failed; throw std::runtime_error("dialog failed"); } catch (...) {}
    { winchisel::core::DialogSlot after_failure; if (!after_failure) return 1; }
    std::cout << "ok worker errors and dialog slot recovery\n";

    const auto pid = GetCurrentProcessId();
    const auto original_io = winchisel::platform::read_process_io_priority(pid);
    if (!original_io) { std::cerr << "FAIL read io priority\n"; return 1; }
    const auto next_io = *original_io == 1 ? 2u : 1u;
    if (!winchisel::platform::set_process_io_priority(pid, next_io)) { std::cerr << "FAIL set io priority\n"; return 1; }
    const auto updated_io = winchisel::platform::read_process_io_priority(pid);
    const auto restored = winchisel::platform::set_process_io_priority(pid, *original_io);
    if (!updated_io || *updated_io != next_io || !restored) { std::cerr << "FAIL io priority roundtrip\n"; return 1; }
    std::cout << "ok io priority set and read\n";

    if (!winchisel::platform::read_process_io_priority(0xFFFFFFFE)) {
        std::cout << "ok io priority bogus pid\n";
    } else {
        std::cerr << "FAIL io priority bogus pid\n";
        return 1;
    }
    const auto ifeo_missing = winchisel::platform::remove_ifeo_dword(L"WinchiselTestNonexistentHost_xyz", L"CpuPriorityClass");
    if (ifeo_missing) {
        std::cout << "ok ifeo remove missing\n";
    } else {
        std::cerr << "FAIL ifeo remove missing: " << ifeo_missing.error().detail << '\n';
        return 1;
    }

    {
        winchisel::platform::detail::ComApartment outer;
        if (!outer || !outer.owned) { std::cerr << "FAIL com outer\n"; return 1; }
        {
            winchisel::platform::detail::ComApartment nested;
            if (!nested || !nested.owned) { std::cerr << "FAIL com nested\n"; return 1; }
        }
    }
    APTTYPE type{}; APTTYPEQUALIFIER qualifier{};
    if (CoGetApartmentType(&type, &qualifier) != CO_E_NOTINITIALIZED) { std::cerr << "FAIL com released\n"; return 1; }
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) { std::cerr << "FAIL sta init\n"; return 1; }
    {
        winchisel::platform::detail::ComApartment mismatched;
        if (!mismatched || mismatched.owned) { std::cerr << "FAIL com changed mode\n"; CoUninitialize(); return 1; }
    }
    if (FAILED(CoGetApartmentType(&type, &qualifier)) || (type != APTTYPE_STA && type != APTTYPE_MAINSTA)) { std::cerr << "FAIL sta preserved\n"; CoUninitialize(); return 1; }
    CoUninitialize();
    std::cout << "ok com apartment balance\n";
}
