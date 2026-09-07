#pragma once

#include "DebloaterPage.g.h"
#include "DebloaterPage.xaml.g.h"
#include "winchisel/core/debloater.hpp"
#include "winchisel/platform/debloater.hpp"

#include <future>
#include <vector>

namespace winrt::Winchisel::implementation {

struct DebloaterPage : DebloaterPageT<DebloaterPage> {
    DebloaterPage();
    ~DebloaterPage();
    void Tabs_SelectionChanged(winrt::Windows::Foundation::IInspectable const&,
                               winrt::Microsoft::UI::Xaml::Controls::SelectorBarSelectionChangedEventArgs const&);
    void Refresh_Click(winrt::Windows::Foundation::IInspectable const&,
                       winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
    void Install_Click(winrt::Windows::Foundation::IInspectable const&,
                       winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
    void Remove_Click(winrt::Windows::Foundation::IInspectable const&,
                      winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
    void Search_TextChanged(winrt::Windows::Foundation::IInspectable const&,
                            winrt::Microsoft::UI::Xaml::Controls::AutoSuggestBoxTextChangedEventArgs const&);
    void Filter_SelectionChanged(winrt::Windows::Foundation::IInspectable const&,
                                 winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
    void Items_SelectionChanged(winrt::Windows::Foundation::IInspectable const&,
                                winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);

private:
    enum class Operation { none, scan, install, remove };
    void start_scan(bool clear_notice = true);
    void render_items();
    void poll_worker();
    void update_actions();
    winrt::fire_and_forget confirm_action(bool install);
    void start_action(bool install);

    std::span<winchisel::core::DebloatCatalogEntry const> catalog_;
    std::vector<bool> installed_;
    std::vector<std::size_t> visible_indices_;
    std::future<winchisel::core::Result<std::vector<bool>>> scan_worker_;
    std::future<winchisel::core::Result<winchisel::platform::DebloatActionResult>> action_worker_;
    winrt::Microsoft::UI::Xaml::DispatcherTimer timer_{nullptr};
    winrt::event_token timer_token_{};
    Operation operation_{Operation::none};
    bool ui_ready_{};
};

}  // namespace winrt::Winchisel::implementation

namespace winrt::Winchisel::factory_implementation {

struct DebloaterPage : DebloaterPageT<DebloaterPage, implementation::DebloaterPage> {};

}  // namespace winrt::Winchisel::factory_implementation
