#pragma once

#include "PrivacyPage.g.h"
#include "PrivacyPage.xaml.g.h"

#include "winchisel/core/registry.hpp"

#include <deque>
#include <functional>
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
    void apply_filter();
    winrt::Microsoft::UI::Xaml::Controls::StackPanel security_content();
    void save_security_toggle(std::size_t index);
    void save_uac_level();
    winrt::Microsoft::UI::Xaml::Controls::StackPanel privacy_content(std::int32_t group);
    void save_privacy_toggle(std::size_t index);
    void save_smart_app_control();
    void save_powershell_policy();
    void save_ads_mode();
    void apply_profile(bool recommended);
    bool append_privacy_rules(std::vector<std::pair<winchisel::core::RegistryTarget, winchisel::core::RegistryValue>>& changes, std::string_view id, bool enabled) const;
    bool append_ads_rules(std::vector<std::pair<winchisel::core::RegistryTarget, winchisel::core::RegistryValue>>& changes, int mode) const;
    int detect_ads_mode() const;
    void show_write_error(std::string const& detail = {});
    void submit(std::function<winchisel::core::Result<void>()> change);
    winrt::fire_and_forget process_changes();

    std::vector<SecurityToggle> security_toggles_;
    std::vector<PrivacyToggle> privacy_toggles_;
    struct PrivacySnapshot {
        std::vector<bool> privacy;
        std::vector<bool> security;
        int uac{2};
        int smart_app_control{-1};
        int powershell{-1};
        int ads{2};
    };
    PrivacySnapshot read_snapshot() const;
    void apply_snapshot(PrivacySnapshot const&);
    std::deque<std::function<winchisel::core::Result<void>()>> pending_;
    bool work_running_{};
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
