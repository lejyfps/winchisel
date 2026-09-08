#include <filesystem>
#include <iostream>
#include <string>

bool safe(std::wstring const& value) {
    const std::filesystem::path path(value);
    return !path.empty() && !path.is_absolute() && !value.contains(L"..") && value.find(L':') == std::wstring::npos;
}

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
    return failed ? 1 : 0;
}
