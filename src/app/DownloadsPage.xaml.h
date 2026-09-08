#pragma once
#include "DownloadsPage.g.h"
#include "DownloadsPage.xaml.g.h"
#include "winchisel/core/download.hpp"
#include "winchisel/platform/download.hpp"
#include <future>
#include <vector>
namespace winrt::Winchisel::implementation {
struct DownloadsPage : DownloadsPageT<DownloadsPage> {
 DownloadsPage(); ~DownloadsPage();
 void Refresh_Click(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
 void Install_Click(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
 void Search_TextChanged(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Controls::AutoSuggestBoxTextChangedEventArgs const&);
 void Filter_SelectionChanged(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
 void Items_SelectionChanged(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
 void Website_Click(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
private:
 enum class Operation { none, scan, install };
 void start_scan(bool force=false, bool clear_notice=true); void render_items(); void poll_worker(); void update_actions();
 winrt::fire_and_forget confirm_install(); void start_install();
 std::span<winchisel::core::DownloadCatalogEntry const> catalog_; std::vector<bool> installed_;
 std::vector<Microsoft::UI::Xaml::Controls::ListView> category_lists_;
 std::future<winchisel::core::Result<std::vector<bool>>> scan_worker_;
 std::future<winchisel::core::Result<winchisel::platform::DownloadInstallResult>> install_worker_;
 Microsoft::UI::Xaml::DispatcherTimer timer_{nullptr}, search_timer_{nullptr}; winrt::event_token timer_token_{}, search_timer_token_{}; Operation operation_{Operation::none}; bool ui_ready_{};
}; }
namespace winrt::Winchisel::factory_implementation { struct DownloadsPage : DownloadsPageT<DownloadsPage, implementation::DownloadsPage> {}; }
