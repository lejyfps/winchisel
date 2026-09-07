#pragma once

#include "PrivacyPage.g.h"
#include "PrivacyPage.xaml.g.h"

namespace winrt::Winchisel::implementation {

struct PrivacyPage : PrivacyPageT<PrivacyPage> {
    PrivacyPage();
};

}  // namespace winrt::Winchisel::implementation

namespace winrt::Winchisel::factory_implementation {

struct PrivacyPage : PrivacyPageT<PrivacyPage, implementation::PrivacyPage> {};

}  // namespace winrt::Winchisel::factory_implementation
