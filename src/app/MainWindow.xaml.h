#pragma once

#include "MainWindow.g.h"
#include "MainWindow.xaml.g.h"
#include <string>
#include <unordered_map>

namespace winrt::Winchisel::implementation {

struct MainWindow : MainWindowT<MainWindow> {
    MainWindow();
    void Nav_SelectionChanged(
        winrt::Microsoft::UI::Xaml::Controls::NavigationView const&,
        winrt::Microsoft::UI::Xaml::Controls::NavigationViewSelectionChangedEventArgs const&);
    void reload_language();
private:
    std::unordered_map<std::wstring, winrt::Microsoft::UI::Xaml::FrameworkElement> pages_;
    void localize_nav();
    winrt::Microsoft::UI::Xaml::FrameworkElement make_page(winrt::hstring const& tag);
};

}  // namespace winrt::Winchisel::implementation

namespace winrt::Winchisel::factory_implementation {

struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow> {};

}  // namespace winrt::Winchisel::factory_implementation
