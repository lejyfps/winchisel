#pragma once

#include "winchisel/core/error.hpp"
#include "winchisel/core/startup.hpp"

#include <string>
#include <vector>

namespace winchisel::platform {

// Enumerates startup entries from registry Run/RunOnce keys (including the
// 32-bit view), startup folders, and packaged-app StartupTasks. Read-only;
// never changes system state.
winchisel::core::Result<std::vector<winchisel::core::StartupEntry>> scan_startup_entries();

// Enumerates scheduled tasks with boot/logon triggers. Read-only; never
// changes system state.
winchisel::core::Result<std::vector<winchisel::core::StartupEntry>> scan_startup_tasks();

// Enables or disables a single entry previously returned by the scan:
// registry/folder entries via StartupApproved flags, packaged apps via their
// State value, scheduled tasks via the Task Scheduler API.
winchisel::core::Result<void> set_startup_entry_enabled(
    winchisel::core::StartupEntry const& entry, bool enabled);

}  // namespace winchisel::platform
