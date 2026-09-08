#pragma once
#include "winchisel/core/error.hpp"
#include "winchisel/core/registry.hpp"
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
winchisel::core::Result<void> apply_registry_and_tasks(
    std::vector<std::pair<winchisel::core::RegistryTarget, winchisel::core::RegistryValue>> const& registry,
    std::vector<std::pair<std::string, bool>> const& tasks,
    std::optional<int> dns_profile = std::nullopt);
}
