#pragma once

#include "ProcessesPage.g.h"
#include "ProcessesPage.xaml.g.h"

namespace winrt::Winchisel::implementation {

struct ProcessesPage : ProcessesPageT<ProcessesPage> {
    ProcessesPage();
};

}  // namespace winrt::Winchisel::implementation

namespace winrt::Winchisel::factory_implementation {

struct ProcessesPage : ProcessesPageT<ProcessesPage, implementation::ProcessesPage> {};

}  // namespace winrt::Winchisel::factory_implementation
