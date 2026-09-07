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

    void load_gaming_toggles();
    void save_gaming_toggle(std::size_t index);
    void apply_gaming_profile(bool recommended);
    void load_gaming_selections();
    void save_mouse_hover_time();
    void save_background_apps();

    bool loading_gaming_toggles_{true};
    bool loading_gaming_selections_{true};
    std::vector<GamingToggle> gaming_toggles_;
    winrt::Microsoft::UI::Xaml::Controls::ComboBox mouse_hover_time_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::ComboBox background_apps_{nullptr};
};

}  // namespace winrt::Winchisel::implementation

namespace winrt::Winchisel::factory_implementation {

struct PerformancePage : PerformancePageT<PerformancePage, implementation::PerformancePage> {};

}  // namespace winrt::Winchisel::factory_implementation
