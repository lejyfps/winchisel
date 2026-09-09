#pragma once

#include "StartupPage.g.h"
#include "StartupPage.xaml.g.h"

#include "winchisel/core/startup.hpp"
#include "winchisel/core/error.hpp"

#include <deque>
#include <functional>
#include <string>
#include <vector>

namespace winrt::Winchisel::implementation {

struct StartupPage : StartupPageT<StartupPage> {
    StartupPage();
    void Refresh_Click(
        winrt::Windows::Foundation::IInspectable const&,
        winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
    void Search_TextChanged(
        winrt::Windows::Foundation::IInspectable const&,
        winrt::Microsoft::UI::Xaml::Controls::AutoSuggestBoxTextChangedEventArgs const&);

private:
    struct EntryRow {
        winchisel::core::StartupEntry entry;
        winrt::Microsoft::UI::Xaml::Controls::ToggleSwitch control{nullptr};
        winrt::Microsoft::UI::Xaml::FrameworkElement card{nullptr};
    };
    void submit(std::function<winchisel::core::Result<void>()> change);
    winrt::fire_and_forget process_changes();
    void load_entries();
    void save_entry(std::size_t index);
    void apply_filter();
    void show_write_error(std::string const& detail = {});
    void render_status();

    std::vector<winchisel::core::StartupEntry> entries_;
    std::vector<EntryRow> rows_;
    std::deque<std::function<winchisel::core::Result<void>()>> pending_changes_;
    bool work_running_{};
    bool loading_{true};
};

}  // namespace winrt::Winchisel::implementation

namespace winrt::Winchisel::factory_implementation {

struct StartupPage : StartupPageT<StartupPage, implementation::StartupPage> {};

}  // namespace winrt::Winchisel::factory_implementation
