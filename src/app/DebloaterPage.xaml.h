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
    enum class Operation { none, scan, install, update, remove };
    struct ApplySelection {
        std::vector<winchisel::core::DebloatCatalogEntry const*> install;
        std::vector<winchisel::core::DebloatCatalogEntry const*> update;
        std::vector<winchisel::core::DebloatCatalogEntry const*> remove;
    };
    ApplySelection selected_split();
    void start_scan(bool clear_notice = true);
    void render_items();
    void apply_filter();
    bool matches_filter(std::size_t index, std::string const& query, std::uint32_t tab, int filter) const;
    void poll_worker();
    void update_actions();
    winrt::fire_and_forget confirm_action(bool install);
    void start_action(bool install);

    std::span<winchisel::core::DebloatCatalogEntry const> catalog_;
    std::vector<bool> installed_;
    std::vector<std::size_t> visible_indices_;
    std::vector<std::string> search_index_;
    std::future<winchisel::core::Result<std::vector<bool>>> scan_worker_;
    std::future<winchisel::core::Result<winchisel::platform::DebloatActionResult>> action_worker_;
    winrt::Microsoft::UI::Xaml::DispatcherTimer timer_{nullptr};
    winrt::Microsoft::UI::Xaml::DispatcherTimer search_timer_{nullptr};
    winrt::event_token timer_token_{}, search_timer_token_{};
    Operation operation_{Operation::none};
    bool ui_ready_{};
};

}  // namespace winrt::Winchisel::implementation

namespace winrt::Winchisel::factory_implementation {

struct DebloaterPage : DebloaterPageT<DebloaterPage, implementation::DebloaterPage> {};

}  // namespace winrt::Winchisel::factory_implementation
