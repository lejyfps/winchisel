#include "winchisel/platform/update.hpp"

#include <iostream>
#include <string>

#pragma comment(lib, "shell32.lib")

int wmain() {
    using namespace winchisel::platform;
    int failed = 0;
    auto expect = [&](bool ok, char const* name) {
        if (!ok) {
            std::cerr << "FAIL " << name << '\n';
            ++failed;
        } else {
            std::cout << "ok " << name << '\n';
        }
    };

    expect(is_newer_version("1.2.4", "1.2.3"), "newer patch");
    expect(is_newer_version("1.3.0", "1.2.9"), "newer minor");
    expect(is_newer_version("2.0.0", "1.9.9"), "newer major");
    expect(!is_newer_version("1.2.3", "1.2.3"), "equal versions");
    expect(!is_newer_version("1.2.3", "1.2.4"), "older candidate");
    expect(!is_newer_version("1.2", "1.2.3"), "short candidate rejected");
    expect(!is_newer_version("1.2.3.4", "1.2.3"), "long candidate rejected");
    expect(!is_newer_version("v1.2.3", "1.2.3"), "prefix rejected");
    expect(!is_newer_version("", "1.2.3"), "empty candidate rejected");
    expect(!is_newer_version("99999999999999999999.0.0", "1.2.3"), "overflowing candidate rejected");

    // Fail-closed manifest handling: corrupt input must produce an error,
    // never an exception or a partially parsed manifest.
    expect(!verify_release_manifest("not json", "not a signature"), "garbage manifest rejected");
    expect(!verify_release_manifest("", ""), "empty manifest rejected");
    expect(!verify_release_manifest(
               R"({"version":"1.2.3","artifacts":[{"id":"setup-x64","file":"a.exe","sha256":")"
               + std::string(64, 'a') +
               R"(","size":99999999999999999999}]})",
               "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=="),
           "oversized artifact rejected");
    expect(!verify_release_manifest(
               R"({"version":"1.2","artifacts":[{"id":"setup-x64","file":"a.exe","sha256":")"
               + std::string(64, 'b') + R"(","size":10}]})",
               "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=="),
           "bad version rejected");

    return failed ? 1 : 0;
}
