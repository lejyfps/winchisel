#pragma once

#include "winchisel/core/error.hpp"
#include "winchisel/core/registry.hpp"
#include <utility>
#include <vector>

namespace winchisel::platform {

// A missing key/value is represented by std::monostate, not an error.
winchisel::core::Result<winchisel::core::RegistryValue> read_registry_value(
    winchisel::core::RegistryTarget const& target);
winchisel::core::Result<void> write_registry_value(
    winchisel::core::RegistryTarget const& target,
    winchisel::core::RegistryValue const& value);
winchisel::core::Result<void> write_registry_values_atomic(
    std::vector<std::pair<winchisel::core::RegistryTarget, winchisel::core::RegistryValue>> const& changes);
winchisel::core::Result<void> rollback_registry_values(
    std::vector<std::pair<winchisel::core::RegistryTarget, winchisel::core::RegistryValue>> const& previous);

}  // namespace winchisel::platform
