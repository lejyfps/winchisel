#pragma once

#include "MainWindow.g.h"
#include "MainWindow.xaml.g.h"
#include "winchisel/platform/update.hpp"
#include <cstdint>
#include <filesystem>
#include <list>
#include <optional>
#include <string>
#include <unordered_map>

namespace winrt::Winchisel::implementation {

struct MainWindow : MainWindowT<MainWindow> {
    MainWindow();
    void Nav_SelectionChanged(
        winrt::Microsoft::UI::Xaml::Controls::NavigationView const&,
        winrt::Microsoft::UI::Xaml::Controls::NavigationViewSelectionChangedEventArgs const&);
    void reload_language();
    void CheckForUpdates(bool manual);
    void Update_Click(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
    void UpdateLater_Click(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
    void UpdateRestart_Click(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
    void BugReport_Click(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
    void Donate_Click(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
private:
    std::unordered_map<std::wstring, winrt::Microsoft::UI::Xaml::FrameworkElement> pages_;
    // LRU order of cached pages (most recent at the back). Bounds the number
    // of fully materialized page trees kept alive while navigating.
    std::list<std::wstring> page_lru_;
    static constexpr std::size_t k_max_cached_pages = 4;
    void touch_page(std::wstring const& key);
    void localize_nav();
    void update_title(std::optional<std::string> available = std::nullopt);
    void apply_theme();
    void apply_titlebar_theme();
    void update_titlebar_inset();
    winrt::fire_and_forget check_for_updates(bool manual);
    winrt::fire_and_forget install_pending_update();
    void notify_if_updated();
    bool update_check_running_{};
    // A staged update waiting for the user's restart (Zed-style: download in
    // the background, install on explicit restart instead of auto-closing).
    bool update_ready_{};
    std::filesystem::path pending_staged_;
    winchisel::platform::ReleaseArtifact pending_artifact_;
    std::string pending_version_;
    winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer toast_timer_{nullptr};
    winrt::Microsoft::UI::Xaml::FrameworkElement make_page(winrt::hstring const& tag);
};

}  // namespace winrt::Winchisel::implementation

namespace winrt::Winchisel::factory_implementation {

struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow> {};

}  // namespace winrt::Winchisel::factory_implementation
