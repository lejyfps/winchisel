#pragma once

#include "LatencyPage.g.h"
#include "LatencyPage.xaml.g.h"

namespace winrt::Winchisel::implementation {

struct LatencyPage : LatencyPageT<LatencyPage> {
    LatencyPage();
};

}  // namespace winrt::Winchisel::implementation

namespace winrt::Winchisel::factory_implementation {

struct LatencyPage : LatencyPageT<LatencyPage, implementation::LatencyPage> {};

}  // namespace winrt::Winchisel::factory_implementation
