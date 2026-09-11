#pragma once

#include "winchisel/core/error.hpp"
#include "winchisel/core/revert.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace winchisel::platform {

// Persistent undo journal ("change history"). Every mutating tweak records
// the state from BEFORE the change; undo restores exactly that (never just
// the Windows default). Destructive one-way actions (app installs, file
// deletion, winget operations) are intentionally never recorded.
//
// All functions are safe to call from background workers. The journal file
// lives next to settings.json, is capped (count and bytes), and a corrupt
// file reads back as empty rather than failing.

// RAII: while held on the current thread, record_revert() is a no-op. Undo
// application holds it so replaying an entry never journals inverse entries.
struct RevertSuppressGuard {
    RevertSuppressGuard();
    ~RevertSuppressGuard();
    RevertSuppressGuard(RevertSuppressGuard const&) = delete;
    RevertSuppressGuard& operator=(RevertSuppressGuard const&) = delete;
};

bool revert_recording_suppressed();

// Builds a labeled entry shell (key + local timestamp). Steps are filled in
// by the caller before record_revert().
winchisel::core::RevertEntry make_revert_entry(std::string const& page, std::string const& label);

// Appends one entry (drops oldest beyond the cap, persists atomically).
// No-op for empty entries and while suppressed. A journal write must never
// fail the user's apply, so callers log-but-ignore a bad result.
winchisel::core::Result<void> record_revert(winchisel::core::RevertEntry entry);

winchisel::core::Result<std::vector<winchisel::core::RevertEntry>> read_revert_journal();
winchisel::core::Result<void> drop_revert_entry(std::string const& key);
winchisel::core::Result<void> clear_revert_journal();

struct RevertApplySummary {
    std::size_t applied{};
    std::size_t failed{};
    std::string first_error{};
};

// Replays one entry in reverse (holds the suppress guard). Continues past
// single-step failures and reports them; the caller decides whether to drop
// the entry.
winchisel::core::Result<RevertApplySummary> apply_revert_entry(winchisel::core::RevertEntry const& entry);

}  // namespace winchisel::platform
