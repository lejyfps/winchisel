#include "pch.h"
#include "HomePage.xaml.h"

#if __has_include("HomePage.g.cpp")
#include "HomePage.g.cpp"
#endif

#include "winchisel/platform/home.hpp"

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace winrt::Winchisel::implementation {

HomePage::HomePage() {
    InitializeComponent();
    Refresh();
    timer_ = DispatcherTimer();
    timer_.Interval(std::chrono::seconds(5));
    timer_token_ = timer_.Tick([this](auto&&, auto&&) { Refresh(); });
    timer_.Start();
}

HomePage::~HomePage() {
    if (timer_) {
        timer_.Tick(timer_token_);
        timer_.Stop();
    }
}

void HomePage::Refresh() {
    const auto info = winchisel::platform::query_home_info();
    CpuBrand().Text(winrt::to_hstring(info.cpu_brand));
    CpuSub().Text(winrt::to_hstring(info.cpu_cores + (info.cpu_speed.empty() ? "" : " · " + info.cpu_speed)));
    CpuUsage().Text(winrt::to_hstring(info.cpu_usage));
    CpuBar().Value(info.cpu_usage_percent);

    GpuName().Text(winrt::to_hstring(info.gpu_name));
    GpuVram().Text(winrt::to_hstring(info.gpu_vram));

    MemValue().Text(winrt::to_hstring(info.memory_total + " · " + info.memory_used));
    MemDetails().Text(winrt::to_hstring(info.ram_details));
    MemBar().Value(info.memory_fraction * 100.0);

    StorTotal().Text(winrt::to_hstring(info.storage_total));
    StorUsed().Text(winrt::to_hstring(info.storage_used));
    StorBar().Value(info.storage_fraction * 100.0);

    OsVersion().Text(winrt::to_hstring(info.os_version));
    OsBuild().Text(winrt::to_hstring(info.windows_build + " — " + info.computer_name));
    Board().Text(winrt::to_hstring(info.motherboard));
    Bios().Text(winrt::to_hstring(info.bios));
    Display().Text(winrt::to_hstring(info.display));
    Uptime().Text(winrt::to_hstring(info.uptime));
}

}  // namespace winrt::Winchisel::implementation
