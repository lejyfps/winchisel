#include "pch.h"
#include "AsyncSupport.hpp"
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
    timer_ = DispatcherTimer();
    timer_.Interval(std::chrono::seconds(5));
    auto weak = get_weak();
    timer_token_ = timer_.Tick([weak](auto&&, auto&&) { if (auto self = weak.get()) self->Refresh(); });
    Loaded([weak](auto&&, auto&&) { if (auto self = weak.get()) { self->timer_.Start(); if (!self->refresh_running_) self->Refresh(); } });
    Unloaded([weak](auto&&, auto&&) { if (auto self = weak.get()) self->timer_.Stop(); });
}

HomePage::~HomePage() {
    if (timer_) {
        timer_.Tick(timer_token_);
        timer_.Stop();
    }
}

winrt::fire_and_forget HomePage::Refresh() {
    auto error_weak=get_weak();
    winrt::Microsoft::UI::Dispatching::DispatcherQueue error_queue{nullptr};
    try { error_queue=DispatcherQueue(); } catch (...) {}
    try {
        auto error_lifetime=get_strong();

    if (refresh_running_) co_return;
    refresh_running_ = true;
    const auto weak = get_weak();
    const auto queue = DispatcherQueue();
    co_await winrt::resume_background();
    auto info = winchisel::platform::query_home_info();
    (void)winchisel::ui::enqueue_safe(queue, [weak, info = std::move(info)] {
        auto self = weak.get();
        if (!self) return;
        self->refresh_running_ = false;
        self->CpuBrand().Text(winrt::to_hstring(info.cpu_brand));
        self->CpuSub().Text(winrt::to_hstring(info.cpu_speed));
        self->CpuUsage().Text(winrt::to_hstring(info.cpu_usage));
        self->CpuBar().Value(info.cpu_usage_percent);

        self->GpuName().Text(winrt::to_hstring(info.gpu_name));
        self->GpuVram().Text(winrt::to_hstring(info.gpu_vram));

        self->MemValue().Text(winrt::to_hstring(info.memory_total + " · " + info.memory_used));
        self->MemDetails().Text(winrt::to_hstring(info.ram_details));
        self->MemBar().Value(info.memory_fraction * 100.0);
        self->MemUsage().Text(to_hstring(static_cast<int>(info.memory_fraction * 100.0f)) + L"%");

        self->StorTotal().Text(winrt::to_hstring(info.storage_total));
        self->StorUsed().Text(winrt::to_hstring(info.storage_used));
        self->StorBar().Value(info.storage_fraction * 100.0);
        self->StorUsage().Text(to_hstring(static_cast<int>(info.storage_fraction * 100.0f)) + L"%");

        self->OsVersion().Text(winrt::to_hstring(info.os_version));
        self->OsBuild().Text(winrt::to_hstring(info.windows_build + " — " + info.computer_name));
        self->Board().Text(winrt::to_hstring(info.motherboard));
        self->Bios().Text(winrt::to_hstring(info.bios));
        self->Display().Text(winrt::to_hstring(info.display));
        self->Uptime().Text(winrt::to_hstring(info.uptime));
        self->ProcessCount().Text(winrt::to_hstring(info.process_count));
        self->CpuThreads().Text(winrt::to_hstring(info.cpu_cores));
        self->ComputerName().Text(winrt::to_hstring(info.computer_name));
    });

    } catch (...) {
        winchisel::ui::report_async_error(error_queue, [error_weak](winrt::hstring const& text) {
            if (auto self=error_weak.get()) { self->refresh_running_=false; (void)text; }
        });
    }
}

}  // namespace winrt::Winchisel::implementation
