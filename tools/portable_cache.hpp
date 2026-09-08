#pragma once
#include "release_paths.hpp"
#include <bcrypt.h>
#include <array>
#include <atomic>
#include <fstream>
#include <optional>
#include <vector>
#pragma comment(lib, "bcrypt.lib")

namespace winchisel::release {
inline std::atomic_uint64_t staging_sequence{};
inline std::string package_hash(std::filesystem::path const& file) {
    BCRYPT_ALG_HANDLE algorithm{};
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) return {};
    struct Algorithm { BCRYPT_ALG_HANDLE value; ~Algorithm() { BCryptCloseAlgorithmProvider(value, 0); } } algorithm_guard{algorithm};
    BCRYPT_HASH_HANDLE hash{};
    if (BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) < 0) return {};
    struct Hash { BCRYPT_HASH_HANDLE value; ~Hash() { BCryptDestroyHash(value); } } hash_guard{hash};
    std::ifstream input(file, std::ios::binary);
    std::array<char, 64 * 1024> buffer{};
    while (input.good()) {
        input.read(buffer.data(), buffer.size());
        if (input.gcount() && BCryptHashData(hash, reinterpret_cast<BYTE*>(buffer.data()), static_cast<ULONG>(input.gcount()), 0) < 0) return {};
    }
    std::array<BYTE, 32> digest{};
    if (!input.eof() || BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) < 0) return {};
    std::string result;
    for (auto byte : digest) { result += "0123456789abcdef"[byte >> 4]; result += "0123456789abcdef"[byte & 15]; }
    return result;
}

template<typename Extract>
std::optional<std::filesystem::path> prepare_portable(std::filesystem::path const& root,
    std::string const& hash, Extract&& extract) {
    if (hash.size() != 64 || hash.find_first_not_of("0123456789abcdef") != std::string::npos || !no_reparse_points(root)) return std::nullopt;
    const std::wstring id(hash.begin(), hash.end());
    const auto mutex = CreateMutexW(nullptr, FALSE, (L"Local\\Winchisel.Portable." + id).c_str());
    if (!mutex) return std::nullopt;
    struct Lock {
        HANDLE value; bool owned{};
        ~Lock() { if (owned) ReleaseMutex(value); CloseHandle(value); }
    } lock{mutex};
    const auto waited = WaitForSingleObject(mutex, 120'000);
    if (waited != WAIT_OBJECT_0 && waited != WAIT_ABANDONED) return std::nullopt;
    lock.owned = true;
    const auto target = root / id;
    if (!safe_destination(root, id + L"\\Winchisel.exe")) return std::nullopt;
    std::error_code error;
    if (std::filesystem::is_regular_file(target / L"Winchisel.exe", error)) return target;
    // A partial extraction is never published as a usable cache entry.
    if (std::filesystem::exists(target, error)) return std::nullopt;
    const auto staging = root / (id + L".partial-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(staging_sequence++));
    std::filesystem::create_directories(root, error);
    if (error || !std::filesystem::create_directory(staging, error) || error) return std::nullopt;
    if (!extract(staging) || !std::filesystem::is_regular_file(staging / L"Winchisel.exe", error) || error) return std::nullopt;
    if (!MoveFileExW(staging.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH)) return std::nullopt;
    return target;
}
}
