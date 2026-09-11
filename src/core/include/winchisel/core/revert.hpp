#pragma once

#include "registry.hpp"
#include "startup.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace winchisel::core {

// One undoable system change, persisted in the revert journal. Every step
// stores the state from BEFORE the change, so undo restores exactly what was
// there (not merely the Windows default). Steps the entry does not use stay
// empty; an entry with no steps at all is never recorded.
struct RevertRegistryStep {
    RegistryTarget target;
    RegistryNativeValue before;
};
struct RevertTaskStep {
    std::string id;
    bool was_enabled{};
};
struct RevertStartupStep {
    StartupEntry entry;
    bool was_enabled{};
};
// domain "special" replays through write_special_performance_toggle,
// domains "extras-teredo"/"extras-hpet"/"extras-tick" through the matching
// Extras command setter. `was_enabled` always stores the exact bool to
// replay (for Extras commands that is the "disabled" value).
struct RevertToggleStep {
    std::string domain;
    std::string id;
    bool was_enabled{};
};
// domain "dns" replays through write_dns_profile,
// domain "update-policy" through write_update_policy.
struct RevertIntStep {
    std::string domain;
    std::string id;
    int was_value{};
};
struct RevertPowerStep {
    std::string scheme_guid;
};
struct RevertEntry {
    std::string key;
    std::string timestamp;
    std::string page;
    std::string label;
    std::vector<RevertRegistryStep> registry;
    std::vector<RevertTaskStep> tasks;
    std::vector<RevertStartupStep> startups;
    std::vector<RevertToggleStep> toggles;
    std::vector<RevertIntStep> ints;
    std::optional<RevertPowerStep> power;
};

inline bool revert_entry_empty(RevertEntry const& entry) {
    return entry.registry.empty() && entry.tasks.empty() && entry.startups.empty() && entry.toggles.empty() &&
        entry.ints.empty() && !entry.power.has_value();
}

// Whole-journal serialization. Malformed entries are skipped on parse (the
// rest still loads); complete garbage yields an empty journal.
std::string serialize_revert_journal(std::vector<RevertEntry> const& entries);
std::vector<RevertEntry> parse_revert_journal(std::string_view text);

}  // namespace winchisel::core
