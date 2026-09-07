#pragma once

#include "PerformancePage.g.h"
#include "PerformancePage.xaml.g.h"

namespace winrt::Winchisel::implementation {

struct PerformancePage : PerformancePageT<PerformancePage> {
    PerformancePage();
};

}  // namespace winrt::Winchisel::implementation

namespace winrt::Winchisel::factory_implementation {

struct PerformancePage : PerformancePageT<PerformancePage, implementation::PerformancePage> {};

}  // namespace winrt::Winchisel::factory_implementation
