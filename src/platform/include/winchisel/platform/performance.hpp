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
// Windows Update policy selection (Normal / Security Updates Only /
// Paused Until 2051 / Disabled). -1 means a custom mix of policy keys.
winchisel::core::Result<int> read_update_policy();
winchisel::core::Result<void> write_update_policy(int index);
winchisel::core::Result<bool> read_scheduled_task(std::string_view id);
winchisel::core::Result<void> write_scheduled_task(std::string_view id, bool enabled);
// Vendor-specific GPU power tweaks, USB selective suspend, hibernate state,
// System Protection, the classic context menu, NIC power saving, PCIe link
// power, and NVIDIA Control Panel values cannot be expressed as static catalog
// registry rules (dynamic subkeys, power APIs, driver APIs, powercfg/WMI
// commands), so they are handled through these special toggles.
enum class GpuVendor : std::uint8_t { amd, nvidia, intel };
bool is_special_performance_toggle(std::string_view id);
winchisel::core::Result<bool> read_special_performance_toggle(std::string_view id);
winchisel::core::Result<void> write_special_performance_toggle(std::string_view id, bool enabled);
// Availability gate: GPU vendor tweaks report false when no matching adapter
// is present, so the UI disables them and they can never be applied to the
// wrong hardware. USB, hibernate, and System Protection tweaks are always
// available.
winchisel::core::Result<bool> is_special_available(std::string_view id);
// Restarts Explorer so applied taskbar/Start menu settings become visible.
// Best effort: Explorer relaunches on its own; open Explorer windows close.
void restart_shell();
winchisel::core::Result<void> apply_registry_and_tasks(
    std::vector<std::pair<winchisel::core::RegistryTarget, winchisel::core::RegistryValue>> const& registry,
    std::vector<std::pair<std::string, bool>> const& tasks,
    std::optional<int> dns_profile = std::nullopt,
    std::optional<int> update_policy = std::nullopt);
}
