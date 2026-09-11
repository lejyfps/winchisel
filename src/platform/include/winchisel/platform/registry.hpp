#pragma once

#include "winchisel/core/error.hpp"
#include "winchisel/core/registry.hpp"
#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

namespace winchisel::platform {

// A missing key/value is represented by std::monostate, not an error.
winchisel::core::Result<winchisel::core::RegistryValue> read_registry_value(
    winchisel::core::RegistryTarget const& target);
winchisel::core::Result<void> write_registry_value(
    winchisel::core::RegistryTarget const& target,
    winchisel::core::RegistryValue const& value);
// Labeled writes record a revert-journal entry with the native before-state
// of every value that actually changed (no-op pairs are skipped, so
// re-applying an already-matching profile stays silent). An empty label
// disables recording. Journal failures never fail the write.
winchisel::core::Result<void> write_registry_values_atomic(
    std::vector<std::pair<winchisel::core::RegistryTarget, winchisel::core::RegistryValue>> const& changes,
    std::string_view page = {}, std::string_view label = {});
winchisel::core::Result<void> rollback_registry_values(
    std::vector<std::pair<winchisel::core::RegistryTarget, winchisel::core::RegistryValue>> const& previous);

// True when the stored native value already equals the desired value, so
// journal recording can skip no-op pairs (re-applying matching state stays
// silent without changing write behavior).
bool registry_native_matches(winchisel::core::RegistryNativeValue const& before,
    winchisel::core::RegistryTarget const& target, winchisel::core::RegistryValue const& desired);

// Pure-data struct lives in Core (reused by the revert journal); the alias
// keeps existing unqualified Platform code compiling unchanged.
using winchisel::core::RegistryNativeValue;
winchisel::core::Result<RegistryNativeValue> read_registry_native(winchisel::core::RegistryTarget const& target);
winchisel::core::Result<void> write_registry_native(
    winchisel::core::RegistryTarget const& target, RegistryNativeValue const& value);

}  // namespace winchisel::platform
