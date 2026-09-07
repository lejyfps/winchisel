#pragma once

#include "ExtrasPage.g.h"
#include "ExtrasPage.xaml.g.h"

namespace winrt::Winchisel::implementation {

struct ExtrasPage : ExtrasPageT<ExtrasPage> {
    ExtrasPage();
};

}  // namespace winrt::Winchisel::implementation

namespace winrt::Winchisel::factory_implementation {

struct ExtrasPage : ExtrasPageT<ExtrasPage, implementation::ExtrasPage> {};

}  // namespace winrt::Winchisel::factory_implementation
