#pragma once
// Persistent cache for slow system scans (DISM, winget export).
//
// Each entry stores named string lists plus a fingerprint: cheap change
// signals (registry key last-write times, file stamps, Windows build) computed
// by the caller. A stored entry is used only while its fingerprint still
// matches and it is younger than max_age, so the slow scan runs only when the
// system actually changed - or at least once per max_age as a safety net.
//
// Storage: %LocalAppData%\Winchisel\cache\scan-cache.json, written atomically.
// A missing, corrupt or foreign-format file just means "no cache".
// Internal to the platform library (uses Win32 types).

#include <windows.h>

#include <chrono>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace winchisel::platform::persistent_cache {

using Lists = std::map<std::string, std::vector<std::string>, std::less<>>;

std::optional<Lists> load(std::string_view name, std::string_view fingerprint, std::chrono::seconds max_age);
void store(std::string_view name, std::string_view fingerprint, Lists const& lists);
void invalidate(std::string_view name);

// "<last-write FILETIME>:<subkey count>:<value count>" of a key, or "-" when
// it cannot be opened. Changes when the key's own values or its direct
// subkey list change (not on changes deeper below).
std::string key_stamp(HKEY root, wchar_t const* path);
// "<last-write FILETIME>:<size>" of a file, or "-".
std::string file_stamp(std::filesystem::path const& path);
// Windows build and update revision, e.g. "26100.4061".
std::string windows_build_stamp();

}  // namespace winchisel::platform::persistent_cache
