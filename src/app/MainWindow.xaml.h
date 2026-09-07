#pragma once

#include "MainWindow.g.h"
#include "MainWindow.xaml.g.h"

namespace winrt::Winchisel::implementation {

struct MainWindow : MainWindowT<MainWindow> {
    MainWindow();
    void Nav_SelectionChanged(
        winrt::Microsoft::UI::Xaml::Controls::NavigationView const& sender,
        winrt::Microsoft::UI::Xaml::Controls::NavigationViewSelectionChangedEventArgs const& args);
};

}  // namespace winrt::Winchisel::implementation

namespace winrt::Winchisel::factory_implementation {

struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow> {};

}  // namespace winrt::Winchisel::factory_implementation
