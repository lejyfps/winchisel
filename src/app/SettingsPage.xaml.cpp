#include "pch.h"
#include "SettingsPage.xaml.h"

#if __has_include("SettingsPage.g.cpp")
#include "SettingsPage.g.cpp"
#endif

namespace winrt::Winchisel::implementation {

SettingsPage::SettingsPage() {
    InitializeComponent();
}

}  // namespace winrt::Winchisel::implementation
