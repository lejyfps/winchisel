#pragma once

#include "ScheduledTasksPage.g.h"
#include "ScheduledTasksPage.xaml.g.h"

#include "winchisel/core/startup.hpp"
#include "winchisel/core/error.hpp"

#include <deque>
#include <functional>
#include <string>
#include <vector>

namespace winrt::Winchisel::implementation {

struct ScheduledTasksPage : ScheduledTasksPageT<ScheduledTasksPage> {
    ScheduledTasksPage();
    void Refresh_Click(
        winrt::Windows::Foundation::IInspectable const&,
        winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
    void Search_TextChanged(
        winrt::Windows::Foundation::IInspectable const&,
        winrt::Microsoft::UI::Xaml::Controls::AutoSuggestBoxTextChangedEventArgs const&);
    void SortBox_SelectionChanged(
        winrt::Windows::Foundation::IInspectable const&,
        winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
    void HideMicrosoft_Changed(
        winrt::Windows::Foundation::IInspectable const&,
        winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);

private:
    struct EntryRow {
        winchisel::core::StartupEntry entry;
        winrt::Microsoft::UI::Xaml::Controls::ToggleSwitch control{nullptr};
        winrt::Microsoft::UI::Xaml::FrameworkElement card{nullptr};
    };
    void submit(std::function<winchisel::core::Result<void>()> change);
    winrt::fire_and_forget process_changes();
    void load_entries();
    void render_list(std::vector<winchisel::core::StartupEntry> const& view);
    std::vector<winchisel::core::StartupEntry> current_view() const;
    void save_entry(std::size_t index);
    void apply_filter();
    void show_write_error(std::string const& detail = {});
    void render_status();

    std::vector<winchisel::core::StartupEntry> entries_;
    std::vector<EntryRow> rows_;
    std::deque<std::function<winchisel::core::Result<void>()>> pending_changes_;
    bool work_running_{};
    bool loading_{true};
    int sort_mode_{};
    bool hide_microsoft_{};
};

}  // namespace winrt::Winchisel::implementation

namespace winrt::Winchisel::factory_implementation {

struct ScheduledTasksPage : ScheduledTasksPageT<ScheduledTasksPage, implementation::ScheduledTasksPage> {};

}  // namespace winrt::Winchisel::factory_implementation
