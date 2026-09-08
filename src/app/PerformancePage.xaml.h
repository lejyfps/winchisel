#pragma once

#include "PerformancePage.g.h"
#include "PerformancePage.xaml.g.h"

#include "winchisel/core/registry.hpp"

#include <vector>

namespace winrt::Winchisel::implementation {

struct PerformancePage : PerformancePageT<PerformancePage> {
    PerformancePage();
    void Recommended_Click(
        winrt::Windows::Foundation::IInspectable const&,
        winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
    void Defaults_Click(
        winrt::Windows::Foundation::IInspectable const&,
        winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
    void Search_TextChanged(
        winrt::Windows::Foundation::IInspectable const&,
        winrt::Microsoft::UI::Xaml::Controls::AutoSuggestBoxTextChangedEventArgs const&);

private:
    struct GamingToggle {
        winrt::hstring title;
        winrt::hstring description;
        bool recommended{};
        bool windows_default{};
        bool match_any_target{};
        bool missing_counts_as_enabled{};
        std::vector<winchisel::core::RegistryTarget> targets;
        std::vector<winchisel::core::RegistryValue> enabled_values;
        std::vector<winchisel::core::RegistryValue> disabled_values;
        winrt::Microsoft::UI::Xaml::Controls::ToggleSwitch control{nullptr};
    };
    struct CatalogToggle {
        std::string id;
        winrt::Microsoft::UI::Xaml::Controls::ToggleSwitch control{nullptr};
    };
    struct CatalogSelection {
        std::string id;
        std::vector<std::string> options;
        winrt::Microsoft::UI::Xaml::Controls::ComboBox control{nullptr};
    };

    void load_gaming_toggles();
    void save_gaming_toggle(std::size_t index);
    void apply_gaming_profile(bool recommended);
    void apply_catalog_profile(bool recommended);
    void load_gaming_selections();
    void save_mouse_hover_time();
    void save_background_apps();
    void load_catalog_toggles();
    void save_catalog_toggle(std::size_t index);
    void load_catalog_selections();
    void save_catalog_selection(std::size_t index);
    void show_write_error(std::string const& detail = {});

    bool loading_gaming_toggles_{true};
    bool loading_gaming_selections_{true};
    std::vector<GamingToggle> gaming_toggles_;
    std::vector<CatalogToggle> catalog_toggles_;
    std::vector<CatalogSelection> catalog_selections_;
    winrt::Microsoft::UI::Xaml::Controls::ComboBox mouse_hover_time_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::ComboBox background_apps_{nullptr};
};

}  // namespace winrt::Winchisel::implementation

namespace winrt::Winchisel::factory_implementation {

struct PerformancePage : PerformancePageT<PerformancePage, implementation::PerformancePage> {};

}  // namespace winrt::Winchisel::factory_implementation
