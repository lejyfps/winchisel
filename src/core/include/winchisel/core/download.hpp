#pragma once

#include <span>
#include <string_view>

namespace winchisel::core {

enum class DownloadCategory {
    Browsers, DocumentViewers, MessagingEmailCalendar, OnlineStorageBackup,
    Multimedia, Imaging, CustomizationUtilities, Gaming, Compression,
    FileDiskManagement, RemoteAccess, OpticalDiscTools, OtherUtilities,
    PrivacySecurity, DevelopmentApps, RuntimesDependencies
};

struct DownloadCatalogEntry {
    std::string_view name;
    DownloadCategory category;
    std::string_view winget_ids; // Pipe-separated aliases; the first is used to install.
    std::string_view website_url;
};

std::span<DownloadCatalogEntry const> get_download_catalog() noexcept;
std::string_view download_category_name(DownloadCategory category) noexcept;

}  // namespace winchisel::core
