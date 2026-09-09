#pragma once
#include "winchisel/core/error.hpp"
#include "winchisel/core/registry.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
namespace winchisel::platform {
winchisel::core::Result<int> read_dns_profile();
winchisel::core::Result<void> write_dns_profile(int index);
winchisel::core::Result<bool> read_scheduled_task(std::string_view id);
winchisel::core::Result<void> write_scheduled_task(std::string_view id, bool enabled);
// Vendor-specific GPU power tweaks, USB selective suspend, and hibernate state
// cannot be expressed as static catalog registry rules (dynamic subkeys,
// powercfg commands), so they are handled through these special toggles.
enum class GpuVendor : std::uint8_t { amd, nvidia, intel };
bool is_special_performance_toggle(std::string_view id);
winchisel::core::Result<bool> read_special_performance_toggle(std::string_view id);
winchisel::core::Result<void> write_special_performance_toggle(std::string_view id, bool enabled);
// Availability gate: GPU vendor tweaks report false when no matching adapter
// is present, so the UI disables them and they can never be applied to the
// wrong hardware. USB and hibernate tweaks are always available.
winchisel::core::Result<bool> is_special_available(std::string_view id);
winchisel::core::Result<void> apply_registry_and_tasks(
    std::vector<std::pair<winchisel::core::RegistryTarget, winchisel::core::RegistryValue>> const& registry,
    std::vector<std::pair<std::string, bool>> const& tasks,
    std::optional<int> dns_profile = std::nullopt);
}
