#pragma once

#include "winchisel/core/debloater.hpp"
#include "winchisel/core/error.hpp"

#include <span>
#include <vector>

namespace winchisel::platform {

winchisel::core::Result<std::vector<bool>> scan_debloater_installed(
    std::span<winchisel::core::DebloatCatalogEntry const> catalog);

struct DebloatActionResult { std::size_t succeeded{}; std::size_t failed{}; };
winchisel::core::Result<DebloatActionResult> apply_debloater_action(
    std::span<winchisel::core::DebloatCatalogEntry const* const> items, bool install);

}  // namespace winchisel::platform
