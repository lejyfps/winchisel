#pragma once

#include <cstdint>
#include <array>
#include <span>
#include <string_view>

namespace winchisel::core {

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
struct PerformanceProfileRule {
    std::string_view id;
    std::int8_t recommended_toggle{}, default_toggle{};
    std::int16_t recommended_selection{}, default_selection{};
};
struct PrivacyCatalogEntry { std::string_view id,name,description; std::int32_t group{}; };
struct PrivacyRegistryRule { std::string_view id,path,name,enabled_value,disabled_value; std::int32_t root{},kind{}; };

std::span<PerformanceCatalogEntry const> get_performance_catalog() noexcept;
std::span<PerformanceRegistryRule const> get_performance_registry_rules() noexcept;
std::span<PerformanceProfileRule const> get_performance_profile_rules() noexcept;
std::span<PrivacyCatalogEntry const> get_privacy_catalog() noexcept;
std::span<PrivacyRegistryRule const> get_privacy_registry_rules() noexcept;

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

}  // namespace winchisel::core
