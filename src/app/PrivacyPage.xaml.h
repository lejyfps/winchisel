#pragma once

#include "PrivacyPage.g.h"
#include "PrivacyPage.xaml.g.h"

#include "winchisel/core/registry.hpp"

#include <vector>

namespace winrt::Winchisel::implementation {

struct PrivacyPage : PrivacyPageT<PrivacyPage> {
    PrivacyPage();
    void Search_TextChanged(
        winrt::Windows::Foundation::IInspectable const&,
        winrt::Microsoft::UI::Xaml::Controls::AutoSuggestBoxTextChangedEventArgs const&);

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

    void render_groups();
    winrt::Microsoft::UI::Xaml::Controls::StackPanel security_content();
    void load_security();
    void save_security_toggle(std::size_t index);
    void load_uac_level();
    void save_uac_level();

    std::vector<SecurityToggle> security_toggles_;
    winrt::Microsoft::UI::Xaml::Controls::ComboBox uac_level_{nullptr};
    bool loading_security_{};
    bool loading_uac_{};
};

}  // namespace winrt::Winchisel::implementation

namespace winrt::Winchisel::factory_implementation {

struct PrivacyPage : PrivacyPageT<PrivacyPage, implementation::PrivacyPage> {};

}  // namespace winrt::Winchisel::factory_implementation
