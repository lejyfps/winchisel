#pragma once

#include "HomePage.g.h"
#include "HomePage.xaml.g.h"

namespace winrt::Winchisel::implementation {

struct HomePage : HomePageT<HomePage> {
    HomePage();
    ~HomePage();
    winrt::fire_and_forget Refresh();

private:
    winrt::Microsoft::UI::Xaml::DispatcherTimer timer_{nullptr};
    winrt::event_token timer_token_{};
    bool refresh_running_{};
};

}  // namespace winrt::Winchisel::implementation

namespace winrt::Winchisel::factory_implementation {

struct HomePage : HomePageT<HomePage, implementation::HomePage> {};

}  // namespace winrt::Winchisel::factory_implementation
