#pragma once

#include "LatencyPage.g.h"
#include "LatencyPage.xaml.g.h"
#include "winchisel/platform/latency.hpp"

#include <future>
#include <optional>

namespace winrt::Winchisel::implementation {

struct LatencyPage : LatencyPageT<LatencyPage> {
    LatencyPage();
    ~LatencyPage();
    void Analyze_Click(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);

private:
    void poll_analysis();
    void render_report(winchisel::platform::LatencyAnalysis const& report);
    void render_message(winrt::hstring const& message, bool is_error = false);
    std::future<winchisel::core::Result<winchisel::platform::LatencyAnalysis>> analysis_;
    std::optional<winchisel::core::Result<winchisel::platform::LatencyAnalysis>> pending_result_;
    winrt::Microsoft::UI::Xaml::DispatcherTimer timer_{nullptr};
    winrt::event_token timer_token_{};
    int progress_{};
    int target_progress_{};
    int progress_tick_{};
};

}  // namespace winrt::Winchisel::implementation

namespace winrt::Winchisel::factory_implementation {

struct LatencyPage : LatencyPageT<LatencyPage, implementation::LatencyPage> {};

}  // namespace winrt::Winchisel::factory_implementation
