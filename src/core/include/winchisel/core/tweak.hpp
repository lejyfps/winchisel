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
    TweakGroupDefinition{"power", "Power"},
    TweakGroupDefinition{"updates", "Windows Update"},
    TweakGroupDefinition{"notifications", "Notifications"},
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

// Tweaks introduced in k_new_tweaks_version carry a "NEW" badge in the UI
// until a newer app version runs. The badges therefore stay visible for the
// whole lifetime of the introducing release (e.g. 1.0.8) and disappear with
// the next one (e.g. 1.0.9) without touching this list.
inline constexpr std::string_view k_new_tweaks_version{"1.0.8"};
inline constexpr std::array k_new_tweak_ids{
    std::string_view{"gaming-gpu-amd-power"},
    std::string_view{"gaming-gpu-nvidia-power"},
    std::string_view{"gaming-gpu-intel-display"},
    std::string_view{"gaming-keyboard-repeat"},
    std::string_view{"gaming-power-throttling-off"},
    std::string_view{"gaming-usb-selective-suspend"},
    std::string_view{"gaming-hibernate-fast-startup"},
    std::string_view{"privacy-disable-delivery-optimization"},
    std::string_view{"privacy-disable-autologger"},
    std::string_view{"updates-driver-controls"},
    std::string_view{"updates-restart-options"},
    std::string_view{"updates-notification-level"},
    std::string_view{"updates-driver-coinstallers"},
    std::string_view{"updates-latest-updates"},
    std::string_view{"updates-other-products"},
    std::string_view{"updates-restart-asap"},
    std::string_view{"updates-restart-notification"},
    std::string_view{"updates-metered-connection"},
    std::string_view{"updates-store-auto-download"},
    std::string_view{"updates-policy-mode"},
    std::string_view{"updates-delivery-optimization"},
    std::string_view{"updates-system-protection"},
    std::string_view{"power-show-sleep"},
    std::string_view{"power-show-hibernate"},
    std::string_view{"power-show-lock"},
    std::string_view{"notifications-push"},
    std::string_view{"notifications-sound"},
    std::string_view{"notifications-toast-above-lock"},
    std::string_view{"notifications-critical-toast-above-lock"},
    std::string_view{"notifications-show-bell-icon"},
    std::string_view{"notifications-welcome-experience"},
    std::string_view{"notifications-system-setting-engagement"},
    std::string_view{"notifications-tips-suggestions"},
    std::string_view{"notifications-system-pane-suggestions"},
    std::string_view{"notifications-capability-access"},
    std::string_view{"notifications-startup-app"},
    std::string_view{"notifications-app-location-request"},
    std::string_view{"notifications-clock-change"},
    std::string_view{"notifications-windows-security"},
    std::string_view{"notifications-security-maintenance"},
};

inline bool is_new_tweak(std::string_view id) {
    for (auto const candidate : k_new_tweak_ids)
        if (candidate == id) return true;
    return false;
}

}  // namespace winchisel::core
