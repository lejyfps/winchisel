#pragma once

#include "SettingsPage.g.h"
#include "SettingsPage.xaml.g.h"

#include "winchisel/core/error.hpp"

#include <deque>
#include <future>
#include <mutex>
#include <string>
#include <vector>

namespace winrt::Winchisel::implementation {

struct SettingsPage : SettingsPageT<SettingsPage> {
    SettingsPage();
    ~SettingsPage();
    void Language_SelectionChanged(
        winrt::Windows::Foundation::IInspectable const&,
        winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
    void Theme_SelectionChanged(
        winrt::Windows::Foundation::IInspectable const&,
        winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
    void Backdrop_SelectionChanged(
        winrt::Windows::Foundation::IInspectable const&,
        winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
    void Settings_Toggled(
        winrt::Windows::Foundation::IInspectable const&,
        winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
    void Restore_Click(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
    void History_Click(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
    void ClearRevert_Click(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
    void Repair_Click(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
    void Link_Click(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
    void flush_pending_save();

private:
    enum class Action { none, restore, repair };
    void queue_save();
    void save_settings();
    void set_busy(bool busy);
    void show_result(bool ok, winrt::hstring const& text);
    void set_stage(winrt::hstring const& text);
    void append_log(std::string_view text);
    void flush_log();
    void finish_dialog(winchisel::core::Result<void> const& result);
    winrt::fire_and_forget run_dialog(Action action);
    winrt::fire_and_forget clear_revert_data();

    bool loading_{true};
    Action action_{Action::none};
    std::future<winchisel::core::Result<void>> dialog_worker_;
    std::vector<std::string> log_lines_;
    std::mutex log_mutex_;
    std::deque<std::string> pending_log_;
    bool log_flush_queued_{};
    winrt::Microsoft::UI::Xaml::Controls::ContentDialog action_dialog_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::ProgressRing dialog_ring_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::TextBlock dialog_stage_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::TextBlock dialog_log_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::ScrollViewer dialog_scroll_{nullptr};
    winrt::Microsoft::UI::Xaml::DispatcherTimer save_timer_{nullptr};
    winrt::event_token save_timer_token_{};
};

}  // namespace winrt::Winchisel::implementation

namespace winrt::Winchisel::factory_implementation {

struct SettingsPage : SettingsPageT<SettingsPage, implementation::SettingsPage> {};

}  // namespace winrt::Winchisel::factory_implementation
