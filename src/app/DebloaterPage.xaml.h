#pragma once

#include "DebloaterPage.g.h"
#include "DebloaterPage.xaml.g.h"

namespace winrt::Winchisel::implementation {

struct DebloaterPage : DebloaterPageT<DebloaterPage> {
    DebloaterPage();
    void Tabs_SelectionChanged(winrt::Windows::Foundation::IInspectable const&,
                               winrt::Microsoft::UI::Xaml::Controls::SelectorBarSelectionChangedEventArgs const&);
    void Refresh_Click(winrt::Windows::Foundation::IInspectable const&,
                       winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
    void Install_Click(winrt::Windows::Foundation::IInspectable const&,
                       winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
    void Remove_Click(winrt::Windows::Foundation::IInspectable const&,
                      winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
};

}  // namespace winrt::Winchisel::implementation

namespace winrt::Winchisel::factory_implementation {

struct DebloaterPage : DebloaterPageT<DebloaterPage, implementation::DebloaterPage> {};

}  // namespace winrt::Winchisel::factory_implementation
