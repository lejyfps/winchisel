#pragma once

#include "MainWindow.g.h"
#include "MainWindow.xaml.g.h"
#include "winchisel/core/revert.hpp"
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
    // Manual: full feedback (toasts + confirm dialog). Automatic (startup):
    // toasts but same dialog. Silent (poller): title bar only, downloads
    // automatically, never a dialog.
    enum class UpdateCheckMode { Manual, Automatic, Silent };
    void Update_Click(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
    void UpdateRestart_Click(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
    void UpdateDismiss_Click(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
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
    void apply_backdrop();
    void apply_titlebar_theme();
    void update_titlebar_inset();
    winrt::fire_and_forget check_for_updates(UpdateCheckMode mode);
    winrt::fire_and_forget install_pending_update();
    winrt::fire_and_forget show_history();
    // Change-history dialog: rows newest-first with per-row Undo plus an
    // Undo-all action. Any successful undo rebuilds the current page so the
    // UI keeps showing the real system state.
    void refresh_history_list(winrt::Microsoft::UI::Xaml::Controls::StackPanel const& list,
        std::vector<winchisel::core::RevertEntry> entries);
    // History footer in either state: action buttons ([Clear history]
    // [Undo all]) or the inline delete confirm. Inline (not a nested dialog)
    // because the app-wide DialogSlot allows only one ContentDialog at a time.
    void refresh_history_footer(winrt::Microsoft::UI::Xaml::Controls::StackPanel const& footer,
        winrt::Microsoft::UI::Xaml::Controls::StackPanel const& content,
        winrt::Microsoft::UI::Xaml::Controls::StackPanel const& list,
        winrt::Microsoft::UI::Xaml::Controls::ScrollViewer const& scroll, bool confirm);
    winrt::fire_and_forget clear_history_entries(
        winrt::Microsoft::UI::Xaml::Controls::StackPanel const& list,
        winrt::Microsoft::UI::Xaml::Controls::ScrollViewer const& scroll,
        winrt::Microsoft::UI::Xaml::Controls::StackPanel const& footer,
        winrt::Microsoft::UI::Xaml::Controls::StackPanel const& content);

    // Last real page tag (the sidebar "Change history" entry below opens a
    // dialog instead of navigating, so the selection is put back here).
    winrt::hstring current_nav_tag_{L"home"};
    // Reselects the nav item with the given tag (menu + footer). Used to undo
    // the selection of the action-only history entry.
    void select_nav_item(winrt::hstring const& tag);
    winrt::fire_and_forget undo_history_entry(std::string key,
        winrt::Microsoft::UI::Xaml::Controls::StackPanel const& list);
    winrt::fire_and_forget undo_all_history(winrt::Microsoft::UI::Xaml::Controls::StackPanel const& list);
    void notify_if_updated();
    void poll_for_updates();
    // De-zentes Page-Einblenden (150 ms Fade), nur wenn Smooth Scrolling an.
    void fade_page_in();
    // Title-bar update states (Zed-style): the check button, a labeled
    // progress button (disabled while busy, like the final restart button)
    // and the restart button swap dynamically so no separate status card
    // is needed.
    void show_update_idle();
    void show_update_busy(winrt::hstring const& label);
    void show_update_progress(unsigned percent);
    void show_update_ready(winrt::hstring const& version);
    bool update_check_running_{};
    // A staged update waiting for the user's restart (Zed-style: download in
    // the background, install on explicit restart instead of auto-closing).
    bool update_ready_{};
    std::filesystem::path pending_staged_;
    winchisel::platform::ReleaseArtifact pending_artifact_;
    std::string pending_version_;
    winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer toast_timer_{nullptr};
    winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer update_poll_timer_{nullptr};
    winrt::Microsoft::UI::Xaml::FrameworkElement make_page(winrt::hstring const& tag);
};

}  // namespace winrt::Winchisel::implementation

namespace winrt::Winchisel::factory_implementation {

struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow> {};

}  // namespace winrt::Winchisel::factory_implementation
