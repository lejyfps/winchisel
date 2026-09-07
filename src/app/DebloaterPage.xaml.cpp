#include "pch.h"
#include "DebloaterPage.xaml.h"

#if __has_include("DebloaterPage.g.cpp")
#include "DebloaterPage.g.cpp"
#endif

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace winrt::Winchisel::implementation {

DebloaterPage::DebloaterPage() {
    InitializeComponent();
}

void DebloaterPage::Tabs_SelectionChanged(
    winrt::Windows::Foundation::IInspectable const&,
    winrt::Microsoft::UI::Xaml::Controls::SelectorBarSelectionChangedEventArgs const&) {}

void DebloaterPage::Refresh_Click(
    winrt::Windows::Foundation::IInspectable const&,
    winrt::Microsoft::UI::Xaml::RoutedEventArgs const&) {}

void DebloaterPage::Install_Click(
    winrt::Windows::Foundation::IInspectable const&,
    winrt::Microsoft::UI::Xaml::RoutedEventArgs const&) {}

void DebloaterPage::Remove_Click(
    winrt::Windows::Foundation::IInspectable const&,
    winrt::Microsoft::UI::Xaml::RoutedEventArgs const&) {}

}  // namespace winrt::Winchisel::implementation
