#pragma once

#include "winchisel/core/cleanup.hpp"
#include "winchisel/core/error.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace winchisel::platform {

struct CleanupScanEntry {
    winchisel::core::CleanupCategory category{};
    std::uint64_t bytes{};
    std::uint64_t files{};
    // True when no meaningful size exists upfront (update cleanup runs DISM;
    // the UI shows a placeholder instead of a number).
    bool size_unknown{};
};

using CleanupScan = std::vector<CleanupScanEntry>;

// Sums reclaimable sizes per category. Never fails the whole scan for one
// unreadable location: denied or missing paths contribute zero. Call from a
// background worker; a full SoftwareDistribution walk can take seconds.
winchisel::core::Result<CleanupScan> scan_cleanup();

// Deletes the selected categories. Locked or access-denied files are counted
// as skipped (not errors) and reported; anything else records an error but
// never aborts the remaining categories. Checks `cancel` between entries;
// the DISM run for update cleanup polls it every 250 ms and terminates the
// child process when set. Call from a background worker.
using CleanupProgress = std::function<void(winchisel::core::CleanupCategory category, std::string_view file)>;

struct CleanupSummary {
    std::uint64_t bytes_freed{};
    std::uint64_t files_removed{};
    std::uint64_t skipped{};
    std::uint64_t errors{};
    std::string first_error{};
};

winchisel::core::Result<CleanupSummary> clean_cleanup(
    std::vector<winchisel::core::CleanupCategory> const& categories,
    CleanupProgress const& progress,
    std::atomic<bool> const& cancel);

// Phase B (update cleanup, previous installations, prefetch) needs real
// elevation and stays hidden on packaged Store builds, where elevation via
// runas is unavailable and system-file deletion risks certification.
bool cleanup_phase_b_visible();
// True elevation state (TokenElevation), not just admin group membership.
bool cleanup_process_elevated();

}  // namespace winchisel::platform
