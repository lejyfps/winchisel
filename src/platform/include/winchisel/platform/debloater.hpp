#pragma once

#include "winchisel/core/debloater.hpp"
#include "winchisel/core/error.hpp"

#include <span>
#include <string>
#include <vector>

namespace winchisel::platform {

winchisel::core::Result<std::vector<bool>> scan_debloater_installed(
    std::span<winchisel::core::DebloatCatalogEntry const> catalog, bool force_refresh = false);

struct DebloatActionResult {
    std::size_t succeeded{};
    std::size_t failed{};
    bool reboot_required{};
    std::vector<std::string> failure_details;
};
winchisel::core::Result<DebloatActionResult> apply_debloater_action(
    std::span<winchisel::core::DebloatCatalogEntry const* const> items, bool install);
// Refreshes already-installed items: DISM capabilities/features are removed
// and re-added so the latest payload is applied; Windows apps open their
// Store listing (or are re-registered when no Store ID exists).
winchisel::core::Result<DebloatActionResult> apply_debloater_update(
    std::span<winchisel::core::DebloatCatalogEntry const* const> items);

}  // namespace winchisel::platform
