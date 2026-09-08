#include <filesystem>
#include <iostream>
#include <string>
#include "release_paths.hpp"
#include "portable_cache.hpp"
#include <future>
#include <atomic>
using winchisel::release::safe;

int wmain() {
    int failed = 0;
    auto expect = [&](bool ok, char const* name) {
        if (!ok) { std::cerr << "FAIL " << name << '\n'; ++failed; }
        else std::cout << "ok " << name << '\n';
    };
    expect(safe(L"Winchisel.exe"), "relative product file");
    expect(safe(L"assets\\logo.ico"), "nested relative");
    expect(!safe(L"C:\\Windows\\notepad.exe"), "absolute rejected");
    expect(!safe(L"..\\evil.dll"), "parent traversal rejected");
    expect(!safe(L""), "empty rejected");
    expect(!safe(L"foo:bar"), "stream/drive rejected");
    expect(!safe(L"\\evil.dll"), "root directory rejected");
    expect(!safe(L"/evil.dll"), "forward root rejected");
    expect(!safe(L"C:evil.dll"), "drive relative rejected");
    expect(!safe(std::wstring(L"ok\0evil", 7)), "embedded NUL rejected");
    expect(!safe(L"assets/../evil.dll"), "nested traversal rejected");
    expect(!safe(L"NUL.txt"), "device path rejected");
    expect(!safe(L"assets /logo.ico"), "ambiguous component rejected");
    expect(winchisel::release::safe_destination(std::filesystem::current_path(), L"assets/logo.ico"), "contained destination");
    const auto root = std::filesystem::current_path() / L"out" / L"tests" / (L"portable-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    std::filesystem::create_directories(root);
    std::ofstream(root / L"a") << "package one";
    std::ofstream(root / L"b") << "package two";
    const auto a = winchisel::release::package_hash(root / L"a");
    const auto b = winchisel::release::package_hash(root / L"b");
    expect(a.size() == 64 && a != b, "same-sized packages have different identities");
    std::atomic<int> extractions{};
    auto extract = [&](auto const& staging) { ++extractions; std::ofstream(staging / L"Winchisel.exe") << "test"; return true; };
    auto first = std::async(std::launch::async, [&] { return winchisel::release::prepare_portable(root, a, extract); });
    auto second = std::async(std::launch::async, [&] { return winchisel::release::prepare_portable(root, a, extract); });
    const auto ready = first.get();
    expect(ready && second.get() == ready && extractions == 1, "concurrent starts publish only once");
    const auto open = CreateFileW((*ready / L"Winchisel.exe").c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    expect(open != INVALID_HANDLE_VALUE && winchisel::release::prepare_portable(root, a, extract) == ready && extractions == 1, "running image is reused without overwrite");
    if (open != INVALID_HANDLE_VALUE) CloseHandle(open);
    expect(!winchisel::release::prepare_portable(root, b, [](auto const& staging) { std::ofstream(staging / L"Winchisel.exe") << "incomplete"; return false; }), "failed extraction not published");
    expect(!std::filesystem::exists(root / std::wstring(b.begin(), b.end())), "failed target absent");
    expect(winchisel::release::prepare_portable(root, b, extract).has_value(), "retry after failed extraction succeeds");
    std::filesystem::remove_all(root);
    return failed ? 1 : 0;
}
