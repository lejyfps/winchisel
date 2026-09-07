#include "pch.h"
#include "PerformancePage.xaml.h"

#if __has_include("PerformancePage.g.cpp")
#include "PerformancePage.g.cpp"
#endif

namespace winrt::Winchisel::implementation {

PerformancePage::PerformancePage() {
    InitializeComponent();
}

}  // namespace winrt::Winchisel::implementation
