#pragma once

#include <cstdint>
#include <array>
#include <optional>
#include <span>
#include <string_view>

namespace winchisel::core {

// These types deliberately contain no Win32 or WinUI details. Platform maps a
// definition to registry/services/tasks; UI only renders the resulting state.
enum class TweakInput : std::uint8_t {
    toggle,
    selection,
};

enum class TweakGroup : std::uint8_t {
    performance,
    privacy_security,
};

struct TweakProfile {
    std::optional<bool> recommended_toggle;
    std::optional<bool> default_toggle;
    std::optional<std::int32_t> recommended_selection;
    std::optional<std::int32_t> default_selection;
};

struct TweakDefinition {
    std::int32_t numeric_id{};
    std::string_view id;
    TweakGroup group{};
    TweakInput input{TweakInput::toggle};
    std::string_view title_key;
    std::string_view description_key;
    TweakProfile profile;
    bool is_new{};
};

struct TweakProfileMatch {
    bool recommended{};
    bool windows_default{};

    constexpr bool custom() const noexcept {
        return !recommended && !windows_default;
    }
};

struct TweakGroupDefinition {
    std::string_view id;
    std::string_view title;
};

struct PerformanceCatalogEntry {
    std::string_view id;
    std::string_view name;
    std::string_view description;
    std::int32_t group{};
    std::int32_t input{};
    std::string_view options;
};

struct PerformanceRegistryRule {
    std::string_view id, path, name, enabled_values, disabled_values;
    std::int32_t root{}, kind{}, byte_index{};
    std::uint8_t bit_mask{};
};

std::span<PerformanceCatalogEntry const> get_performance_catalog() noexcept;
std::span<PerformanceRegistryRule const> get_performance_registry_rules() noexcept;

inline constexpr std::array k_performance_groups{
    TweakGroupDefinition{"gaming", "Gaming"},
    TweakGroupDefinition{"processor", "Processor"},
    TweakGroupDefinition{"graphics", "Graphics"},
    TweakGroupDefinition{"network", "Network"},
    TweakGroupDefinition{"security", "Security"},
    TweakGroupDefinition{"xbox", "Xbox"},
    TweakGroupDefinition{"system_services", "System Services"},
    TweakGroupDefinition{"scheduled_tasks", "Scheduled Tasks"},
    TweakGroupDefinition{"visual_effects", "Visual Effects"},
    TweakGroupDefinition{"accessibility", "Accessibility"},
};

inline constexpr std::array k_privacy_security_groups{
    TweakGroupDefinition{"security", "Security"},
    TweakGroupDefinition{"ads", "Ads"},
    TweakGroupDefinition{"lock_screen", "Lock Screen"},
    TweakGroupDefinition{"general", "General"},
    TweakGroupDefinition{"speech", "Speech"},
    TweakGroupDefinition{"inking", "Inking"},
    TweakGroupDefinition{"diagnostics", "Diagnostics"},
    TweakGroupDefinition{"activity_history", "Activity History"},
    TweakGroupDefinition{"search", "Search"},
    TweakGroupDefinition{"app_permissions", "App Permissions"},
    TweakGroupDefinition{"windows_ai", "Windows AI"},
    TweakGroupDefinition{"edge_ai", "Edge AI"},
    TweakGroupDefinition{"office_ai", "Office AI"},
};

constexpr TweakProfileMatch match_toggle_profile(TweakProfile const& profile, bool current) noexcept {
    return {
        .recommended = profile.recommended_toggle && current == *profile.recommended_toggle,
        .windows_default = profile.default_toggle && current == *profile.default_toggle,
    };
}

constexpr TweakProfileMatch match_selection_profile(TweakProfile const& profile, std::int32_t current) noexcept {
    return {
        .recommended = profile.recommended_selection && current == *profile.recommended_selection,
        .windows_default = profile.default_selection && current == *profile.default_selection,
    };
}

}  // namespace winchisel::core
