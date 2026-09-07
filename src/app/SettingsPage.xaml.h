#pragma once

#include "SettingsPage.g.h"
#include "SettingsPage.xaml.g.h"

namespace winrt::Winchisel::implementation {

struct SettingsPage : SettingsPageT<SettingsPage> {
    SettingsPage();
};

}  // namespace winrt::Winchisel::implementation

namespace winrt::Winchisel::factory_implementation {

struct SettingsPage : SettingsPageT<SettingsPage, implementation::SettingsPage> {};

}  // namespace winrt::Winchisel::factory_implementation
