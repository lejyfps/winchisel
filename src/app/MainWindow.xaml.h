#pragma once

#include "MainWindow.g.h"
#include "MainWindow.xaml.g.h"

namespace winrt::Winchisel::implementation {

struct MainWindow : MainWindowT<MainWindow> {
    MainWindow();
    void Nav_SelectionChanged(
        winrt::Microsoft::UI::Xaml::Controls::NavigationView const&,
        winrt::Microsoft::UI::Xaml::Controls::NavigationViewSelectionChangedEventArgs const&);
};

}  // namespace winrt::Winchisel::implementation

namespace winrt::Winchisel::factory_implementation {

struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow> {};

}  // namespace winrt::Winchisel::factory_implementation
