#pragma once
#include <windows.h>
#include <filesystem>
#include <string>

namespace winchisel::release {
inline bool safe(std::wstring const& value) {
    const std::filesystem::path path(value);
    if (path.empty() || path.has_root_path() || value.find_first_of(L":*?\"<>|") != std::wstring::npos ||
        value.find(L'\0') != std::wstring::npos) return false;
    for (auto const& part : path) {
        const auto name = part.native();
        if (name.empty() || name == L"." || name == L".." || name.back() == L'.' || name.back() == L' ') return false;
        const auto stem = part.stem().native();
        if (_wcsicmp(stem.c_str(), L"CON") == 0 || _wcsicmp(stem.c_str(), L"PRN") == 0 ||
            _wcsicmp(stem.c_str(), L"AUX") == 0 || _wcsicmp(stem.c_str(), L"NUL") == 0) return false;
        if (stem.size() == 4 && (_wcsnicmp(stem.c_str(), L"COM", 3) == 0 || _wcsnicmp(stem.c_str(), L"LPT", 3) == 0) &&
            stem[3] >= L'1' && stem[3] <= L'9') return false;
    }
    return true;
}

// Reject existing junctions/symlinks, including the destination file itself.
inline bool no_reparse_points(std::filesystem::path const& path) {
    std::error_code error;
    const auto absolute = std::filesystem::absolute(path, error).lexically_normal();
    if (error) return false;
    std::filesystem::path current;
    for (auto const& part : absolute) {
        current /= part;
        if (!current.is_absolute()) continue;
        const auto attributes = GetFileAttributesW(current.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            const auto code = GetLastError();
            if (code != ERROR_FILE_NOT_FOUND && code != ERROR_PATH_NOT_FOUND) return false;
        } else if (attributes & FILE_ATTRIBUTE_REPARSE_POINT) return false;
    }
    return true;
}

inline bool safe_destination(std::filesystem::path const& root, std::wstring const& relative) {
    if (!safe(relative)) return false;
    const auto destination = (root / relative).lexically_normal();
    const auto within = destination.lexically_relative(root.lexically_normal());
    return safe(within.native()) && no_reparse_points(destination);
}
}
