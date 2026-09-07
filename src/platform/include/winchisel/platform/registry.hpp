#pragma once

#include "winchisel/core/error.hpp"
#include "winchisel/core/registry.hpp"

namespace winchisel::platform {

// A missing key/value is represented by std::monostate, not an error.
winchisel::core::Result<winchisel::core::RegistryValue> read_registry_value(
    winchisel::core::RegistryTarget const& target);
winchisel::core::Result<void> write_registry_value(
    winchisel::core::RegistryTarget const& target,
    winchisel::core::RegistryValue const& value);

}  // namespace winchisel::platform
