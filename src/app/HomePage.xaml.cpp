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
    auto weak = get_weak();
    timer_token_ = timer_.Tick([weak](auto&&, auto&&) { if (auto self = weak.get()) self->Refresh(); });
    timer_.Start();
}

HomePage::~HomePage() {
    if (timer_) {
        timer_.Tick(timer_token_);
        timer_.Stop();
    }
}

winrt::fire_and_forget HomePage::Refresh() {
    if (refresh_running_) co_return;
    refresh_running_ = true;
    const auto weak = get_weak();
    const auto queue = DispatcherQueue();
    co_await winrt::resume_background();
    auto info = winchisel::platform::query_home_info();
    (void)queue.TryEnqueue([weak, info = std::move(info)] {
        auto self = weak.get();
        if (!self) return;
        self->refresh_running_ = false;
        self->CpuBrand().Text(winrt::to_hstring(info.cpu_brand));
        self->CpuSub().Text(winrt::to_hstring(info.cpu_cores + (info.cpu_speed.empty() ? "" : " · " + info.cpu_speed)));
        self->CpuUsage().Text(winrt::to_hstring(info.cpu_usage));
        self->CpuBar().Value(info.cpu_usage_percent);

        self->GpuName().Text(winrt::to_hstring(info.gpu_name));
        self->GpuVram().Text(winrt::to_hstring(info.gpu_vram));

        self->MemValue().Text(winrt::to_hstring(info.memory_total + " · " + info.memory_used));
        self->MemDetails().Text(winrt::to_hstring(info.ram_details));
        self->MemBar().Value(info.memory_fraction * 100.0);

        self->StorTotal().Text(winrt::to_hstring(info.storage_total));
        self->StorUsed().Text(winrt::to_hstring(info.storage_used));
        self->StorBar().Value(info.storage_fraction * 100.0);

        self->OsVersion().Text(winrt::to_hstring(info.os_version));
        self->OsBuild().Text(winrt::to_hstring(info.windows_build + " — " + info.computer_name));
        self->Board().Text(winrt::to_hstring(info.motherboard));
        self->Bios().Text(winrt::to_hstring(info.bios));
        self->Display().Text(winrt::to_hstring(info.display));
        self->Uptime().Text(winrt::to_hstring(info.uptime));
    });
}

}  // namespace winrt::Winchisel::implementation
