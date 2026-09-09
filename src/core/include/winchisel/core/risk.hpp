#pragma once

#include "winchisel/core/registry.hpp"

#include <cstdint>
#include <span>
#include <string_view>

namespace winchisel::core {

// Risk rating shown next to a tweak so users know what they are touching.
// Ratings are computed from catalog data, never hand-labeled per tweak:
// - safe: current-user scope only, or curated values with one-click rollback.
// - moderate: system-wide (HKLM) change with a documented trade-off.
// - risky: touches security boundaries (VBS, BitLocker, Defender, UAC...).
enum class TweakRisk : std::uint8_t { safe, moderate, risky };

// Engine over concrete registry targets (hive + key path decide).
TweakRisk assess_registry_targets(std::span<RegistryTarget const> targets);
// Service startup change. can_disable reflects whether the tweak offers Disabled.
TweakRisk assess_service(std::string_view service, bool can_disable);
// Scheduled-task toggle, judged by the task's full path.
TweakRisk assess_task_path(std::string_view full_path);
// Hand-built Extras toggles, keyed by their internal name.
TweakRisk assess_extras(std::string_view key);

// Full dispatch for catalog entries, including the small override table for
// cases the rules misjudge. Unknown IDs report moderate (fail-safe).
TweakRisk assess_performance(std::string_view id);
TweakRisk assess_privacy(std::string_view id);

// Single source of truth for which OS target a catalog ID controls.
// Consumed by the UI/Platform layers instead of local copies.
std::string_view service_name_for_id(std::string_view id);
std::string_view task_path_for_id(std::string_view id);

// English lookup key for the UI badge ("Safe", "Moderate", "Risky").
std::string_view risk_label_key(TweakRisk risk);

}  // namespace winchisel::core
