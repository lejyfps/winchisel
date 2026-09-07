#pragma once

#include "SettingsPage.g.h"
#include "SettingsPage.xaml.g.h"

namespace winrt::Winchisel::implementation {

struct SettingsPage : SettingsPageT<SettingsPage> {
    SettingsPage();
    ~SettingsPage();
    void Language_SelectionChanged(
        winrt::Windows::Foundation::IInspectable const&,
        winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
    void Settings_Toggled(
        winrt::Windows::Foundation::IInspectable const&,
        winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);

private:
    void queue_save();
    void save_settings();

    bool loading_{true};
    winrt::Microsoft::UI::Xaml::DispatcherTimer save_timer_{nullptr};
    winrt::event_token save_timer_token_{};
};

}  // namespace winrt::Winchisel::implementation

namespace winrt::Winchisel::factory_implementation {

struct SettingsPage : SettingsPageT<SettingsPage, implementation::SettingsPage> {};

}  // namespace winrt::Winchisel::factory_implementation
