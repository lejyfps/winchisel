#pragma once

#include "App.g.h"

namespace winrt::Winchisel::implementation {

struct App : AppT<App> {
    App();
    void OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const&);

private:
    winrt::Microsoft::UI::Xaml::Window window_{nullptr};
};

}  // namespace winrt::Winchisel::implementation

namespace winrt::Winchisel::factory_implementation {

struct App : AppT<App, implementation::App> {};

}  // namespace winrt::Winchisel::factory_implementation
