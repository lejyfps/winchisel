#pragma once

#include "PrivacyPage.g.h"
#include "PrivacyPage.xaml.g.h"

#include "winchisel/core/registry.hpp"

#include <vector>

namespace winrt::Winchisel::implementation {

struct PrivacyPage : PrivacyPageT<PrivacyPage> {
    PrivacyPage();
    ~PrivacyPage();
    void Search_TextChanged(
        winrt::Windows::Foundation::IInspectable const&,
        winrt::Microsoft::UI::Xaml::Controls::AutoSuggestBoxTextChangedEventArgs const&);
    void Recommended_Click(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
    void Defaults_Click(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);

private:
    struct SecurityToggle {
        winrt::hstring title;
        winrt::hstring description;
        bool enabled_when_missing{};
        bool match_any_target{};
        std::vector<winchisel::core::RegistryTarget> targets;
        std::vector<winchisel::core::RegistryValue> enabled_values;
        std::vector<winchisel::core::RegistryValue> disabled_values;
        winrt::Microsoft::UI::Xaml::Controls::ToggleSwitch control{nullptr};
    };
    struct PrivacyToggle { std::string id; winrt::Microsoft::UI::Xaml::Controls::ToggleSwitch control{nullptr}; };

    void render_groups();
    winrt::Microsoft::UI::Xaml::Controls::StackPanel security_content();
    void load_security();
    void save_security_toggle(std::size_t index);
    void load_uac_level();
    void save_uac_level();
    winrt::Microsoft::UI::Xaml::Controls::StackPanel privacy_content(std::int32_t group, std::string const& query);
    void load_privacy_toggles();
    void save_privacy_toggle(std::size_t index);
    void load_privacy_selections();
    void save_privacy_selections();
    void apply_profile(bool recommended);
    void show_write_error(std::string const& detail = {});

    std::vector<SecurityToggle> security_toggles_;
    std::vector<PrivacyToggle> privacy_toggles_;
    winrt::Microsoft::UI::Xaml::Controls::ComboBox uac_level_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::ComboBox smart_app_control_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::ComboBox powershell_policy_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::ComboBox ads_mode_{nullptr};
    bool loading_security_{};
    bool loading_uac_{};
    winrt::Microsoft::UI::Xaml::DispatcherTimer search_timer_{nullptr};
    winrt::event_token search_timer_token_{};
};

}  // namespace winrt::Winchisel::implementation

namespace winrt::Winchisel::factory_implementation {

struct PrivacyPage : PrivacyPageT<PrivacyPage, implementation::PrivacyPage> {};

}  // namespace winrt::Winchisel::factory_implementation
