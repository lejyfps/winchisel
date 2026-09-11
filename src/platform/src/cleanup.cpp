#include "winchisel/platform/cleanup.hpp"
#include "winchisel/platform/update.hpp"
#include "process_wait.hpp"

#include <windows.h>
#include <shlobj.h>

#include <array>
#include <chrono>
#include <filesystem>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")

namespace winchisel::platform {
namespace {

using winchisel::core::CleanupCategory;
using winchisel::core::Result;

template <typename T>
Result<T> fail(std::string detail) {
    return std::unexpected(winchisel::core::Error{.detail = std::move(detail)});
}

std::filesystem::path windows_dir() {
    std::array<wchar_t, MAX_PATH> buffer{};
    const auto length = GetWindowsDirectoryW(buffer.data(), static_cast<UINT>(buffer.size()));
    if (!length || length >= buffer.size()) return {};
    return std::filesystem::path(buffer.data());
}

std::filesystem::path user_temp_dir() {
    std::array<wchar_t, MAX_PATH> buffer{};
    const auto length = GetTempPathW(static_cast<DWORD>(buffer.size()), buffer.data());
    if (!length || length >= buffer.size()) return {};
    return std::filesystem::path(buffer.data());
}

std::filesystem::path local_appdata_dir() {
    PWSTR raw{};
    if (SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &raw) != S_OK) return {};
    std::filesystem::path result(raw);
    CoTaskMemFree(raw);
    return result;
}

std::filesystem::path system_drive_root() {
    const auto windows = windows_dir();
    if (windows.empty()) return {};
    return std::filesystem::path(windows.root_name().wstring() + L"\\");
}

// ERROR_ACCESS_DENIED / sharing violations mean "in use or protected":
// honest to report as skipped, not as failure. Missing paths are already
// gone and count as success without bytes.
bool is_skip_error(std::error_code const& ec) {
    const auto code = static_cast<DWORD>(ec.value());
    return code == ERROR_ACCESS_DENIED || code == ERROR_SHARING_VIOLATION || code == ERROR_LOCK_VIOLATION;
}

bool is_gone_error(std::error_code const& ec) {
    const auto code = static_cast<DWORD>(ec.value());
    return code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND;
}

struct DirSize {
    std::uint64_t bytes{};
    std::uint64_t files{};
};

DirSize directory_size(std::filesystem::path const& root) {
    DirSize total;
    std::error_code ec;
    if (root.empty() || !std::filesystem::exists(root, ec)) return total;
    std::filesystem::recursive_directory_iterator it(
        root, std::filesystem::directory_options::skip_permission_denied, ec);
    const std::filesystem::recursive_directory_iterator end;
    if (ec) return total;
    for (; it != end; it.increment(ec)) {
        if (ec) {
            ec.clear();
            continue;
        }
        std::error_code type_ec;
        if (!it->is_regular_file(type_ec) || type_ec) continue;
        std::error_code size_ec;
        const auto size = it->file_size(size_ec);
        if (size_ec) continue;
        total.bytes += static_cast<std::uint64_t>(size);
        ++total.files;
    }
    return total;
}

struct CleanStats {
    std::uint64_t bytes_freed{};
    std::uint64_t files_removed{};
    std::uint64_t skipped{};
    std::uint64_t errors{};
    std::string first_error{};
};

void note_error(CleanStats& stats, std::filesystem::path const& path, std::error_code const& ec) {
    ++stats.errors;
    if (stats.first_error.empty()) stats.first_error = path.string() + ": " + ec.message();
}

// Removes every top-level entry below `root` (files, dirs, links) but never
// `root` itself, so a stray path bug cannot delete the folder. Reparse
// points are removed as links, never traversed.
void clean_directory_contents(std::filesystem::path const& root, CleanupCategory category,
    CleanupProgress const& progress, std::atomic<bool> const& cancel, CleanStats& stats) {
    std::error_code ec;
    if (root.empty() || cancel.load()) return;
    std::filesystem::directory_iterator it(root, std::filesystem::directory_options::skip_permission_denied, ec), end;
    if (ec) {
        if (!is_skip_error(ec) && !is_gone_error(ec)) note_error(stats, root, ec);
        else if (is_skip_error(ec)) ++stats.skipped;
        return;
    }
    for (; it != end; it.increment(ec)) {
        if (cancel.load()) return;
        if (ec) {
            ec.clear();
            ++stats.skipped;
            break;
        }
        const auto path = it->path();
        std::error_code type_ec;
        const bool link = it->is_symlink(type_ec);
        if (type_ec) {
            ++stats.skipped;
            continue;
        }
        std::error_code file_ec;
        const bool file = !link && it->is_regular_file(file_ec);
        if (file_ec) {
            ++stats.skipped;
            continue;
        }
        if (link || file) {
            std::uint64_t size{};
            if (file) {
                std::error_code size_ec;
                const auto measured = it->file_size(size_ec);
                if (!size_ec) size = static_cast<std::uint64_t>(measured);
            }
            std::error_code remove_ec;
            if (std::filesystem::remove(path, remove_ec) && !remove_ec) {
                stats.bytes_freed += size;
                ++stats.files_removed;
                if (progress) progress(category, path.filename().string());
            } else if (is_gone_error(remove_ec)) {
                continue;
            } else if (is_skip_error(remove_ec)) {
                ++stats.skipped;
            } else {
                note_error(stats, path, remove_ec);
            }
            continue;
        }
        const auto before = directory_size(path);
        std::error_code tree_ec;
        const auto removed = std::filesystem::remove_all(path, tree_ec);
        if (tree_ec) {
            if (is_gone_error(tree_ec)) continue;
            if (is_skip_error(tree_ec)) ++stats.skipped;
            else note_error(stats, path, tree_ec);
            continue;
        }
        stats.bytes_freed += before.bytes;
        stats.files_removed += before.files ? before.files : static_cast<std::uint64_t>(removed);
        if (progress) {
            const auto name = path.filename().string();
            progress(category, name);
        }
    }
}

bool matches_pattern(std::filesystem::path const& path, std::wstring_view prefix, std::wstring_view suffix) {
    const auto name = path.filename().wstring();
    if (name.size() < prefix.size() + suffix.size()) return false;
    if (name.compare(0, prefix.size(), prefix) != 0) return false;
    if (name.compare(name.size() - suffix.size(), suffix.size(), suffix) != 0) return false;
    return true;
}

// Same as clean_directory_contents but only deletes files matching a
// name filter (prefetch *.pf, thumbcache_*.db) and never recurses.
void clean_matching_files(std::filesystem::path const& root,
    std::function<bool(std::filesystem::path const&)> const& accept, CleanupCategory category,
    CleanupProgress const& progress, std::atomic<bool> const& cancel, CleanStats& stats) {
    std::error_code ec;
    if (root.empty() || cancel.load()) return;
    std::filesystem::directory_iterator it(root, std::filesystem::directory_options::skip_permission_denied, ec), end;
    if (ec) {
        if (is_skip_error(ec)) ++stats.skipped;
        else if (!is_gone_error(ec)) note_error(stats, root, ec);
        return;
    }
    for (; it != end; it.increment(ec)) {
        if (cancel.load()) return;
        if (ec) {
            ec.clear();
            ++stats.skipped;
            break;
        }
        const auto path = it->path();
        std::error_code type_ec;
        if (!it->is_regular_file(type_ec) || type_ec || !accept(path)) continue;
        std::error_code size_ec;
        const auto size = static_cast<std::uint64_t>(it->file_size(size_ec));
        std::error_code remove_ec;
        if (std::filesystem::remove(path, remove_ec) && !remove_ec) {
            if (!size_ec) stats.bytes_freed += size;
            ++stats.files_removed;
            if (progress) progress(category, path.filename().string());
        } else if (is_skip_error(remove_ec)) {
            ++stats.skipped;
        } else if (!is_gone_error(remove_ec)) {
            note_error(stats, path, remove_ec);
        }
    }
}

DirSize matching_files_size(std::filesystem::path const& root,
    std::function<bool(std::filesystem::path const&)> const& accept) {
    DirSize total;
    std::error_code ec;
    if (root.empty()) return total;
    std::filesystem::directory_iterator it(root, std::filesystem::directory_options::skip_permission_denied, ec), end;
    if (ec) return total;
    for (; it != end; it.increment(ec)) {
        if (ec) {
            ec.clear();
            continue;
        }
        std::error_code type_ec;
        if (!it->is_regular_file(type_ec) || type_ec || !accept(it->path())) continue;
        std::error_code size_ec;
        const auto size = it->file_size(size_ec);
        if (size_ec) continue;
        total.bytes += static_cast<std::uint64_t>(size);
        ++total.files;
    }
    return total;
}

std::vector<std::filesystem::path> shader_cache_dirs(std::filesystem::path const& local_appdata) {
    std::vector<std::filesystem::path> dirs;
    if (local_appdata.empty()) return dirs;
    for (auto const* tail : {L"D3DSCache", L"NVIDIA\\DXCache", L"NVIDIA\\GLCache", L"AMD\\DxCache"}) {
        std::error_code ec;
        auto dir = local_appdata / tail;
        if (std::filesystem::exists(dir, ec) && !ec) dirs.push_back(std::move(dir));
    }
    return dirs;
}

std::vector<std::filesystem::path> previous_install_dirs(std::filesystem::path const& drive_root) {
    std::vector<std::filesystem::path> dirs;
    if (drive_root.empty()) return dirs;
    for (auto const* tail : {L"Windows.old", L"$Windows.~BT", L"$Windows.~WS"}) {
        std::error_code ec;
        auto dir = drive_root / tail;
        if (std::filesystem::exists(dir, ec) && !ec) dirs.push_back(std::move(dir));
    }
    return dirs;
}

// Cancellable process run for the DISM phase: polls every 250 ms so Cancel
// terminates the child promptly. Kill-on-close job handles grandchildren.
Result<void> run_cancellable(std::wstring command, std::atomic<bool> const& cancel, DWORD timeout_ms) {
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
            &startup, &process)) {
        return fail<void>("Could not start cleanup helper (error " + std::to_string(GetLastError()) + ").");
    }
    const auto job = detail::attach_kill_job(process.hProcess);
    const auto started = GetTickCount64();
    DWORD exit_code{};
    bool done{};
    for (;;) {
        if (cancel.load()) {
            if (job) TerminateJobObject(job, ERROR_CANCELLED);
            else TerminateProcess(process.hProcess, ERROR_CANCELLED);
            WaitForSingleObject(process.hProcess, 5000);
            if (job) CloseHandle(job);
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
            return fail<void>("Cleanup was cancelled.");
        }
        const auto status = WaitForSingleObject(process.hProcess, 250);
        if (status == WAIT_OBJECT_0) {
            if (!GetExitCodeProcess(process.hProcess, &exit_code)) exit_code = GetLastError();
            done = true;
            break;
        }
        if (status == WAIT_FAILED) {
            exit_code = GetLastError();
            break;
        }
        if (GetTickCount64() - started >= timeout_ms) {
            if (job) TerminateJobObject(job, ERROR_TIMEOUT);
            else TerminateProcess(process.hProcess, ERROR_TIMEOUT);
            WaitForSingleObject(process.hProcess, 5000);
            if (job) CloseHandle(job);
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
            return fail<void>("Cleanup helper timed out and was stopped.");
        }
    }
    if (job) CloseHandle(job);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    if (!done || exit_code != 0) {
        return fail<void>("Cleanup helper failed with exit code " + std::to_string(exit_code) + ".");
    }
    return {};
}

}  // namespace

bool cleanup_phase_b_visible() {
    // Packaged Store builds cannot elevate via runas and must not offer
    // system-file deletion (certification + no UAC path from the package).
    return !is_packaged_install();
}

bool cleanup_process_elevated() {
    HANDLE token{};
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    TOKEN_ELEVATION elevation{};
    DWORD size{};
    const bool elevated =
        GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size) && elevation.TokenIsElevated;
    CloseHandle(token);
    return elevated;
}

Result<CleanupScan> scan_cleanup() {
    CleanupScan scan;
    const auto windows = windows_dir();
    const auto local_appdata = local_appdata_dir();
    const auto drive = system_drive_root();
    auto push = [&](CleanupCategory category, DirSize size, bool unknown = false) {
        scan.push_back(CleanupScanEntry{category, size.bytes, size.files, unknown});
    };
    push(CleanupCategory::user_temp, directory_size(user_temp_dir()));
    push(CleanupCategory::windows_temp, windows.empty() ? DirSize{} : directory_size(windows / L"Temp"));
    {
        SHQUERYRBINFO info{};
        info.cbSize = sizeof(info);
        DirSize bin;
        if (SUCCEEDED(SHQueryRecycleBinW(nullptr, &info))) {
            bin.bytes = static_cast<std::uint64_t>(info.i64Size);
            bin.files = static_cast<std::uint64_t>(info.i64NumItems);
        }
        push(CleanupCategory::recycle_bin, bin);
    }
    {
        DirSize thumbs;
        if (!local_appdata.empty()) {
            const auto explorer = local_appdata / L"Microsoft" / L"Windows" / L"Explorer";
            auto accept = [](std::filesystem::path const& path) {
                return matches_pattern(path, L"thumbcache_", L".db") || matches_pattern(path, L"iconcache_", L".db");
            };
            thumbs = matching_files_size(explorer, accept);
        }
        push(CleanupCategory::thumbnails, thumbs);
    }
    push(CleanupCategory::delivery_optimization,
        windows.empty() ? DirSize{} : directory_size(windows / L"SoftwareDistribution" / L"DeliveryOptimization"));
    {
        DirSize shaders;
        for (auto const& dir : shader_cache_dirs(local_appdata)) {
            const auto size = directory_size(dir);
            shaders.bytes += size.bytes;
            shaders.files += size.files;
        }
        push(CleanupCategory::shader_cache, shaders);
    }
    if (cleanup_phase_b_visible()) {
        push(CleanupCategory::update_cleanup, DirSize{}, true);
        DirSize previous;
        for (auto const& dir : previous_install_dirs(drive)) {
            const auto size = directory_size(dir);
            previous.bytes += size.bytes;
            previous.files += size.files;
        }
        push(CleanupCategory::previous_installations, previous);
        push(CleanupCategory::prefetch,
            windows.empty()
                ? DirSize{}
                : matching_files_size(windows / L"Prefetch",
                      [](std::filesystem::path const& path) { return matches_pattern(path, L"", L".pf"); }));
    }
    return scan;
}

Result<CleanupSummary> clean_cleanup(std::vector<CleanupCategory> const& categories,
    CleanupProgress const& progress, std::atomic<bool> const& cancel) {
    CleanupSummary summary;
    CleanStats stats;
    const auto windows = windows_dir();
    const auto local_appdata = local_appdata_dir();
    const auto drive = system_drive_root();
    for (const auto category : categories) {
        if (cancel.load()) break;
        if (winchisel::core::cleanup_is_phase_b(category) && !cleanup_process_elevated()) {
            ++stats.errors;
            if (stats.first_error.empty())
                stats.first_error = "System categories need administrator rights. Restart Winchisel as administrator and try again.";
            continue;
        }
        switch (category) {
            case CleanupCategory::user_temp: clean_directory_contents(user_temp_dir(), category, progress, cancel, stats); break;
            case CleanupCategory::windows_temp:
                if (!windows.empty()) clean_directory_contents(windows / L"Temp", category, progress, cancel, stats);
                break;
            case CleanupCategory::recycle_bin: {
                const HRESULT empty =
                    SHEmptyRecycleBinW(nullptr, nullptr, SHERB_NOCONFIRMATION | SHERB_NOPROGRESSUI | SHERB_NOSOUND);
                if (FAILED(empty)) {
                    ++stats.errors;
                    if (stats.first_error.empty())
                        stats.first_error = "Could not empty the recycle bin (error " + std::to_string(empty) + ").";
                }
                break;
            }
            case CleanupCategory::thumbnails: {
                if (!local_appdata.empty()) {
                    auto accept = [](std::filesystem::path const& path) {
                        return matches_pattern(path, L"thumbcache_", L".db") ||
                            matches_pattern(path, L"iconcache_", L".db");
                    };
                    clean_matching_files(
                        local_appdata / L"Microsoft" / L"Windows" / L"Explorer", accept, category, progress, cancel, stats);
                }
                break;
            }
            case CleanupCategory::delivery_optimization:
                if (!windows.empty())
                    clean_directory_contents(windows / L"SoftwareDistribution" / L"DeliveryOptimization", category,
                        progress, cancel, stats);
                break;
            case CleanupCategory::shader_cache:
                for (auto const& dir : shader_cache_dirs(local_appdata)) {
                    if (cancel.load()) break;
                    clean_directory_contents(dir, category, progress, cancel, stats);
                }
                break;
            case CleanupCategory::update_cleanup: {
                // No /ResetBase: it permanently prevents uninstalling updates
                // and must never hide behind ordinary disk cleanup.
                if (progress) progress(category, "DISM StartComponentCleanup");
                auto result = run_cancellable(L"dism.exe /Online /Cleanup-Image /StartComponentCleanup", cancel,
                    60 * 60 * 1000);
                if (!result) {
                    ++stats.errors;
                    if (stats.first_error.empty()) stats.first_error = result.error().detail;
                }
                break;
            }
            case CleanupCategory::previous_installations:
                for (auto const& dir : previous_install_dirs(drive)) {
                    if (cancel.load()) break;
                    std::error_code ec;
                    const auto before = directory_size(dir);
                    const auto removed = std::filesystem::remove_all(dir, ec);
                    if (ec) {
                        if (is_skip_error(ec)) ++stats.skipped;
                        else note_error(stats, dir, ec);
                    } else {
                        stats.bytes_freed += before.bytes;
                        stats.files_removed += before.files ? before.files : static_cast<std::uint64_t>(removed);
                        if (progress) progress(category, dir.filename().string());
                    }
                }
                break;
            case CleanupCategory::prefetch: {
                if (!windows.empty()) {
                    auto accept = [](std::filesystem::path const& path) { return matches_pattern(path, L"", L".pf"); };
                    clean_matching_files(windows / L"Prefetch", accept, category, progress, cancel, stats);
                }
                break;
            }
        }
    }
    summary.bytes_freed = stats.bytes_freed;
    summary.files_removed = stats.files_removed;
    summary.skipped = stats.skipped;
    summary.errors = stats.errors;
    summary.first_error = std::move(stats.first_error);
    if (cancel.load() && summary.errors == 0 && summary.files_removed == 0) {
        return fail<CleanupSummary>("Cleanup was cancelled.");
    }
    return summary;
}

}  // namespace winchisel::platform
