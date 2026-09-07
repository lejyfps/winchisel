#include "winchisel/core/download.hpp"
#include "download_catalog.generated.hpp"

namespace winchisel::core {

std::span<DownloadCatalogEntry const> get_download_catalog() noexcept { return generated_download_catalog; }

std::string_view download_category_name(DownloadCategory category) noexcept {
    switch (category) {
    case DownloadCategory::Browsers: return "Browsers";
    case DownloadCategory::DocumentViewers: return "Document viewers";
    case DownloadCategory::MessagingEmailCalendar: return "Messaging, email & calendar";
    case DownloadCategory::OnlineStorageBackup: return "Online storage & backup";
    case DownloadCategory::Multimedia: return "Multimedia";
    case DownloadCategory::Imaging: return "Imaging";
    case DownloadCategory::CustomizationUtilities: return "Customization & utilities";
    case DownloadCategory::Gaming: return "Gaming";
    case DownloadCategory::Compression: return "Compression";
    case DownloadCategory::FileDiskManagement: return "File & disk management";
    case DownloadCategory::RemoteAccess: return "Remote access";
    case DownloadCategory::OpticalDiscTools: return "Optical disc tools";
    case DownloadCategory::OtherUtilities: return "Other utilities";
    case DownloadCategory::PrivacySecurity: return "Privacy & security";
    case DownloadCategory::DevelopmentApps: return "Development apps";
    default: return "Runtimes & dependencies";
    }
}

}  // namespace winchisel::core
