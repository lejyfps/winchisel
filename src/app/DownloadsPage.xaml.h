#pragma once
#include "DownloadsPage.g.h"
#include "DownloadsPage.xaml.g.h"
#include "winchisel/core/download.hpp"
#include "winchisel/platform/download.hpp"
#include <future>
#include <map>
#include <string>
#include <vector>
namespace winrt::Winchisel::implementation {
struct DownloadsPage : DownloadsPageT<DownloadsPage> {
 DownloadsPage(); ~DownloadsPage();
  void Refresh_Click(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
  void Install_Click(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
  void Uninstall_Click(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
 void Search_TextChanged(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Controls::AutoSuggestBoxTextChangedEventArgs const&);
 void Filter_SelectionChanged(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
 void Items_SelectionChanged(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
 void Website_Click(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
private:
  enum class Operation { none, scan, install, uninstall };
   void start_scan(bool force=false, bool clear_notice=true); void render_items(); void apply_filter(bool scroll_top=false); void poll_worker(); void update_actions(); void complete_pull_refresh();
   winrt::fire_and_forget confirm_install(); winrt::fire_and_forget confirm_uninstall(); void start_install(); void start_uninstall();
   std::span<winchisel::core::DownloadCatalogEntry const> catalog_; std::vector<bool> installed_; std::vector<std::string> search_index_;
  std::vector<Microsoft::UI::Xaml::Controls::ListView> category_lists_;
  // #1 Virtualization-light: ListViews collapsed Kategorien leben hier weiter
  // (visual tree zahlt nur expandierte). Filter zählen auch detached.
  std::map<int, Windows::Foundation::IInspectable> detached_content_;
  std::vector<winrt::hstring> category_base_;
  void fill_category(Microsoft::UI::Xaml::Controls::Expander const& expander);
  void attach_group(Microsoft::UI::Xaml::Controls::Expander const& expander);
  void detach_group(Microsoft::UI::Xaml::Controls::Expander const& expander);
  Microsoft::UI::Xaml::Controls::ListView list_for(Microsoft::UI::Xaml::Controls::Expander const& expander);
  std::size_t count_category(int category_index, std::string const& query, int filter) const;
  std::future<winchisel::core::Result<std::vector<bool>>> scan_worker_;
  std::future<winchisel::core::Result<winchisel::platform::DownloadInstallResult>> install_worker_;
  std::future<winchisel::core::Result<winchisel::platform::DownloadInstallResult>> uninstall_worker_;
  Microsoft::UI::Xaml::DispatcherTimer timer_{nullptr}, search_timer_{nullptr}; winrt::event_token timer_token_{}, search_timer_token_{}, refresh_token_{}; Windows::Foundation::Deferral refresh_deferral_{nullptr}; Operation operation_{Operation::none}; bool ui_ready_{};
}; }
namespace winrt::Winchisel::factory_implementation { struct DownloadsPage : DownloadsPageT<DownloadsPage, implementation::DownloadsPage> {}; }
