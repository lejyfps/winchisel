#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace winchisel::core {

// Where a startup entry comes from. Registry and folder entries are toggled
// through the StartupApproved flags Task Manager uses; packaged apps through
// their per-task State value; scheduled tasks through the Task Scheduler API.
enum class StartupLocation : std::uint8_t {
    registry_run_user,
    registry_run_machine,
    registry_runonce_user,
    registry_runonce_machine,
    registry_run32_machine,
    registry_runonce32_machine,
    folder_user,
    folder_machine,
    uwp_task,
    scheduled_task,
};

struct StartupEntry {
    std::string id;
    std::string name;
    std::string command;
    std::string detail;
    std::string key;
    StartupLocation location{};
    bool enabled{};
    // Optional extras with defaults so existing initializers keep working:
    // publisher from the executable's version info (empty when unknown),
    // file path backing the entry for "open location" (empty when none),
    // scheduler state for tasks (-1 when not a task, else TASK_STATE:
    // 0 unknown, 1 disabled, 2 queued, 3 ready, 4 running).
    std::string publisher{};
    int task_state{-1};
    std::string file_path{};
};

struct StartupScan {
    std::vector<StartupEntry> entries;
};

// Task Scheduler trigger types worth showing in a startup manager.
// Values match TASK_TRIGGER_TYPE2: BOOT (8) and LOGON (9). Registration (7)
// and session-state triggers are left out: they rarely affect boot time and
// disabling them by accident breaks background maintenance.
inline bool is_startup_trigger(int type) {
    return type == 8 || type == 9;
}

// StartupApproved binary format Task Manager writes: 12 bytes, first byte
// 02/06 = enabled, 03/07 = disabled, trailing 8 bytes = FILETIME of the
// change (shown as the disable date in Task Manager).
inline std::array<std::uint8_t, 12> build_approved_data(bool enabled, std::uint64_t filetime) {
    std::array<std::uint8_t, 12> data{};
    data[0] = enabled ? std::uint8_t{0x02} : std::uint8_t{0x03};
    for (std::size_t index{}; index < 8; ++index)
        data[4 + index] = static_cast<std::uint8_t>((filetime >> (index * 8)) & 0xFF);
    return data;
}

inline std::optional<bool> parse_approved_data(std::vector<std::uint8_t> const& data) {
    if (data.size() < 4) return std::nullopt;
    switch (data[0]) {
        case 0x02:
        case 0x06: return true;
        case 0x03:
        case 0x07: return false;
        default: return std::nullopt;
    }
}

// Packaged-app startup state from
// HKCU\...\AppModel\SystemAppData\{FamilyName}\{TaskId}\State:
// 2 = Enabled, 4 = EnabledByPolicy, 0 = Disabled, 1 = DisabledByUser.
inline std::optional<bool> parse_uwp_startup_state(std::uint32_t state) {
    switch (state) {
        case 2:
        case 4: return true;
        case 0:
        case 1: return false;
        default: return std::nullopt;
    }
}

// Heuristic for the "hide Microsoft entries" filter: matches first-party
// locations (task paths below \Microsoft, commands from System32/SysWOW64
// or below a Microsoft path). Only command and key are tested: detail holds
// source paths (Run key, startup folder, AppModel hive) that legitimately
// contain "Microsoft\Windows" for every entry. Third-party software rarely
// lives in the matched locations, and the filter only hides rows, never
// changes state.
inline bool is_microsoft_startup_entry(StartupEntry const& entry) {
    std::string haystack = entry.command + '|' + entry.key;
    for (auto& c : haystack) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return haystack.find("microsoft") != std::string::npos || haystack.find("system32") != std::string::npos ||
        haystack.find("syswow64") != std::string::npos;
}

}  // namespace winchisel::core
