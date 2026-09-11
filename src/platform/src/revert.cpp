#include "winchisel/platform/revert.hpp"
#include "winchisel/platform/performance.hpp"
#include "winchisel/platform/registry.hpp"
#include "winchisel/platform/startup.hpp"
#include "winchisel/platform/system.hpp"
#include "process_wait.hpp"

#include <windows.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <regex>
#include <sstream>

namespace winchisel::platform {
namespace {

constexpr std::size_t k_max_journal_entries = 50;
constexpr std::size_t k_max_journal_bytes = 1024 * 1024;

thread_local bool g_suppressed = false;
std::mutex g_journal_mutex;
std::atomic<unsigned> g_entry_counter{};

std::filesystem::path journal_path() {
    return appdata_dir() / L"revert.json";
}

std::string timestamp_now() {
    SYSTEMTIME time{};
    GetLocalTime(&time);
    char buffer[20]{};
    snprintf(buffer, sizeof(buffer), "%04u-%02u-%02u %02u:%02u", time.wYear, time.wMonth, time.wDay,
        time.wHour, time.wMinute);
    return buffer;
}

std::vector<winchisel::core::RevertEntry> load_locked() {
    std::error_code size_error;
    const auto path = journal_path();
    const auto size = std::filesystem::file_size(path, size_error);
    if (size_error || size > k_max_journal_bytes) {
        if (!size_error) boot_log("revert journal oversized, starting empty");
        return {};
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::ostringstream content;
    content << in.rdbuf();
    auto entries = winchisel::core::parse_revert_journal(content.str());
    if (entries.empty() && !content.str().empty() && content.str() != R"({"version":1,"entries":[]})") {
        boot_log("revert journal unreadable, starting empty");
    }
    return entries;
}

winchisel::core::Result<void> store_locked(std::vector<winchisel::core::RevertEntry> const& entries) {
    const auto path = journal_path();
    const auto temporary = path.wstring() + L".tmp";
    {
        std::ofstream out(std::filesystem::path(temporary), std::ios::binary | std::ios::trunc);
        if (!out) return std::unexpected(winchisel::core::Error{.detail = "revert journal unwritable"});
        out << winchisel::core::serialize_revert_journal(entries);
        out.flush();
        out.close();
        if (!out) return std::unexpected(winchisel::core::Error{.detail = "revert journal write failed"});
    }
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const auto error = GetLastError();
        DeleteFileW(temporary.c_str());
        return std::unexpected(winchisel::core::Error{.detail = "revert journal replace failed: " + std::to_string(error)});
    }
    return {};
}

bool valid_guid(std::string_view guid) {
    if (guid.size() != 36) return false;
    for (std::size_t i{}; i < guid.size(); ++i) {
        const char c = guid[i];
        const bool dash = i == 8 || i == 13 || i == 18 || i == 23;
        if (dash) {
            if (c != '-') return false;
        } else if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
            return false;
        }
    }
    return true;
}

winchisel::core::Result<void> apply_power_scheme(std::string const& guid) {
    if (!valid_guid(guid)) return std::unexpected(winchisel::core::Error{.detail = "invalid power scheme"});
    std::wstring command = L"powercfg.exe /setactive ";
    command += std::wstring(guid.begin(), guid.end());
    auto [waited, output] = detail::run_captured(std::move(command), 2 * 60 * 1000);
    (void)output;
    if (waited.timed_out) return std::unexpected(winchisel::core::Error{.detail = "power scheme activation timed out"});
    if (waited.exit_code != 0)
        return std::unexpected(
            winchisel::core::Error{.detail = "power scheme activation failed: " + std::to_string(waited.exit_code)});
    return {};
}

void note_step_error(RevertApplySummary& summary, std::string detail) {
    ++summary.failed;
    if (summary.first_error.empty()) summary.first_error = std::move(detail);
}

}  // namespace

RevertSuppressGuard::RevertSuppressGuard() {
    g_suppressed = true;
}
RevertSuppressGuard::~RevertSuppressGuard() {
    g_suppressed = false;
}

bool revert_recording_suppressed() {
    return g_suppressed;
}

winchisel::core::RevertEntry make_revert_entry(std::string const& page, std::string const& label) {
    winchisel::core::RevertEntry entry;
    entry.timestamp = timestamp_now();
    SYSTEMTIME time{};
    GetLocalTime(&time);
    char key[64]{};
    snprintf(key, sizeof(key), "%04u%02u%02u-%02u%02u%02u-%04u", time.wYear, time.wMonth, time.wDay, time.wHour,
        time.wMinute, time.wSecond, g_entry_counter.fetch_add(1) % 10000);
    entry.key = key;
    entry.page = page;
    entry.label = label;
    return entry;
}

winchisel::core::Result<void> record_revert(winchisel::core::RevertEntry entry) {
    if (g_suppressed || winchisel::core::revert_entry_empty(entry)) return {};
    if (entry.key.empty() || entry.label.empty()) return {};
    std::scoped_lock lock(g_journal_mutex);
    auto entries = load_locked();
    entries.push_back(std::move(entry));
    while (entries.size() > k_max_journal_entries) entries.erase(entries.begin());
    return store_locked(entries);
}

winchisel::core::Result<std::vector<winchisel::core::RevertEntry>> read_revert_journal() {
    std::scoped_lock lock(g_journal_mutex);
    return load_locked();
}

winchisel::core::Result<void> drop_revert_entry(std::string const& key) {
    std::scoped_lock lock(g_journal_mutex);
    auto entries = load_locked();
    const auto before = entries.size();
    entries.erase(
        std::remove_if(entries.begin(), entries.end(), [&](auto const& item) { return item.key == key; }),
        entries.end());
    if (entries.size() == before) return {};
    return store_locked(entries);
}

winchisel::core::Result<void> clear_revert_journal() {
    std::scoped_lock lock(g_journal_mutex);
    return store_locked({});
}

winchisel::core::Result<RevertApplySummary> apply_revert_entry(winchisel::core::RevertEntry const& entry) {
    RevertSuppressGuard suppress;
    RevertApplySummary summary;
    for (auto it = entry.registry.rbegin(); it != entry.registry.rend(); ++it) {
        winchisel::core::RegistryNativeValue native{it->before.missing, it->before.type, it->before.data};
        if (auto result = write_registry_native(it->target, native); !result) {
            note_step_error(summary, result.error().detail);
        } else {
            ++summary.applied;
        }
    }
    for (auto it = entry.tasks.rbegin(); it != entry.tasks.rend(); ++it) {
        if (auto result = write_scheduled_task(it->id, it->was_enabled); !result) {
            note_step_error(summary, it->id + ": " + result.error().detail);
        } else {
            ++summary.applied;
        }
    }
    for (auto it = entry.startups.rbegin(); it != entry.startups.rend(); ++it) {
        if (auto result = set_startup_entry_enabled(it->entry, it->was_enabled); !result) {
            note_step_error(summary, it->entry.name + ": " + result.error().detail);
        } else {
            ++summary.applied;
        }
    }
    for (auto it = entry.toggles.rbegin(); it != entry.toggles.rend(); ++it) {
        // `was_enabled` always stores the exact bool to replay into the
        // setter (for Extras commands that is the "disabled" value).
        winchisel::core::Result<void> result{std::unexpected(winchisel::core::Error{.detail = "unknown toggle"})};
        if (it->domain == "special") {
            result = write_special_performance_toggle(it->id, it->was_enabled);
        } else if (it->domain == "extras-teredo") {
            result = set_teredo_disabled(it->was_enabled);
        } else if (it->domain == "extras-hpet") {
            result = set_hpet_disabled(it->was_enabled);
        } else if (it->domain == "extras-tick") {
            result = set_dynamic_tick_disabled(it->was_enabled);
        } else {
            result = std::unexpected(winchisel::core::Error{.detail = "unknown toggle domain: " + it->domain});
        }
        if (!result) {
            note_step_error(summary, it->id + ": " + result.error().detail);
        } else {
            ++summary.applied;
        }
    }
    for (auto it = entry.ints.rbegin(); it != entry.ints.rend(); ++it) {
        winchisel::core::Result<void> result{std::unexpected(winchisel::core::Error{.detail = "unknown value"})};
        if (it->domain == "dns") {
            result = write_dns_profile(it->was_value);
        } else if (it->domain == "update-policy") {
            result = write_update_policy(it->was_value);
        } else {
            result = std::unexpected(winchisel::core::Error{.detail = "unknown value domain: " + it->domain});
        }
        if (!result) {
            note_step_error(summary, it->id + ": " + result.error().detail);
        } else {
            ++summary.applied;
        }
    }
    if (entry.power) {
        if (auto result = apply_power_scheme(entry.power->scheme_guid); !result) {
            note_step_error(summary, result.error().detail);
        } else {
            ++summary.applied;
        }
    }
    return summary;
}

}  // namespace winchisel::platform
