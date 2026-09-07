#include "pch.h"
#include "PrivacyPage.xaml.h"

#if __has_include("PrivacyPage.g.cpp")
#include "PrivacyPage.g.cpp"
#endif

#include "winchisel/core/tweak.hpp"

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace winrt::Winchisel::implementation {

PrivacyPage::PrivacyPage() {
    InitializeComponent();
    for (const auto& group : winchisel::core::k_privacy_security_groups) {
        Controls::Expander expander;
        expander.Header(box_value(to_hstring(group.title)));
        expander.IsExpanded(group.id == "security");
        Groups().Children().Append(expander);
    }
}

}  // namespace winrt::Winchisel::implementation
