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
    void CheckForUpdates(bool manual);
    void Update_Click(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
    void BugReport_Click(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
    void Donate_Click(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
private:
    std::unordered_map<std::wstring, winrt::Microsoft::UI::Xaml::FrameworkElement> pages_;
    void localize_nav();
    void apply_theme();
    void apply_titlebar_theme();
    void update_titlebar_inset();
    winrt::fire_and_forget check_for_updates(bool manual);
    bool update_check_running_{};
    winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer toast_timer_{nullptr};
    winrt::Microsoft::UI::Xaml::FrameworkElement make_page(winrt::hstring const& tag);
};

}  // namespace winrt::Winchisel::implementation

namespace winrt::Winchisel::factory_implementation {

struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow> {};

}  // namespace winrt::Winchisel::factory_implementation
