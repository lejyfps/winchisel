#pragma once

#include "winchisel/core/download.hpp"
#include "winchisel/core/error.hpp"

#include <span>
#include <string>
#include <vector>

namespace winchisel::platform {

struct DownloadInstallResult {
    std::size_t succeeded{};
    std::size_t failed{};
    std::vector<std::string> failure_details;
};

winchisel::core::Result<std::vector<bool>> scan_downloads_installed(
    std::span<winchisel::core::DownloadCatalogEntry const> catalog, bool force_refresh = false);
winchisel::core::Result<DownloadInstallResult> install_downloads(
    std::span<winchisel::core::DownloadCatalogEntry const* const> items);

}  // namespace winchisel::platform
