#include "pch.h"
#include "AsyncSupport.hpp"
#include "LatencyPage.xaml.h"
#include "AsyncLifetime.hpp"
#include "winchisel/platform/latency.hpp"

#include <chrono>
#include <sstream>

#if __has_include("LatencyPage.g.cpp")
#include "LatencyPage.g.cpp"
#endif

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace winrt::Winchisel::implementation {

LatencyPage::LatencyPage() {
    InitializeComponent();
    timer_ = Microsoft::UI::Xaml::DispatcherTimer();
    timer_.Interval(std::chrono::milliseconds(120));
    auto weak = get_weak();
    timer_token_ = timer_.Tick([weak](auto&&, auto&&) {
        if (auto page = weak.get()) page->poll_analysis();
    });
    Loaded([weak](auto&&, auto&&) { if (auto page = weak.get()) { if (page->analysis_.valid()) page->timer_.Start(); } });
    Unloaded([weak](auto&&, auto&&) { if (auto page = weak.get()) page->timer_.Stop(); });
    render_message(L"Click 'Analyze USB Latency' to begin analysis.");
}

LatencyPage::~LatencyPage() {
    if (timer_) {
        timer_.Stop();
        timer_.Tick(timer_token_);
    }
    winchisel::ui::finish_in_background(analysis_);
}

void LatencyPage::Analyze_Click(Windows::Foundation::IInspectable const&, RoutedEventArgs const&) {
    auto error_weak=get_weak();
    winrt::Microsoft::UI::Dispatching::DispatcherQueue error_queue{nullptr};
    try { error_queue=DispatcherQueue(); } catch (...) {}
    try {
        auto error_lifetime=get_strong();

    if (analysis_.valid()) return;
    progress_ = 0;
    target_progress_ = 4;
    progress_tick_ = 0;
    pending_result_.reset();
    Progress().Value(progress_);
    ProgressPercent().Text(L"0%");
    Status().Text(L"Starting USB topology analysis...");
    render_message(L"Scanning input devices, USB controllers, and hubs. This does not change any system setting.");
    AnalyzeButton().IsEnabled(false);
    AnalyzeButton().Content(box_value(L"Analyzing..."));
    auto weak = get_weak();
    auto queue = DispatcherQueue();
    analysis_ = std::async(std::launch::async, [weak, queue] {
        return winchisel::platform::analyze_usb_topology([weak, queue](int value, std::string_view status) {
            const auto status_text = to_hstring(status);
            winchisel::ui::enqueue_safe(queue, [weak, value, status_text] {
                if (auto page = weak.get()) {
                    page->target_progress_ = std::max(page->target_progress_, value);
                    page->Status().Text(status_text);
                }
            });
        });
    });
    timer_.Start();

    } catch (...) {
        winchisel::ui::report_async_error(error_queue, [error_weak](winrt::hstring const& text) {
            if (auto self=error_weak.get()) { self->timer_.Stop(); self->pending_result_.reset(); self->AnalyzeButton().IsEnabled(true); self->AnalyzeButton().Content(box_value(L"Analyze again")); self->render_message(text,true); }
        });
    }
}

void LatencyPage::poll_analysis() {
    auto error_weak=get_weak();
    winrt::Microsoft::UI::Dispatching::DispatcherQueue error_queue{nullptr};
    try { error_queue=DispatcherQueue(); } catch (...) {}
    try {
        auto error_lifetime=get_strong();

    if (analysis_.valid() && analysis_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        pending_result_.emplace(analysis_.get());
        target_progress_ = 100;
        Status().Text(L"Building final report...");
    } else if (analysis_.valid() && ++progress_tick_ % 2 == 0 && target_progress_ < 98) {
        const int step = target_progress_ < 35 ? 3 : target_progress_ < 75 ? 2 : 1;
        target_progress_ = std::min(target_progress_ + step, 98);
    }

    if (progress_ < target_progress_) {
        progress_ = std::min(progress_ + 2, target_progress_);
        Progress().Value(progress_);
        ProgressPercent().Text(to_hstring(std::to_string(progress_) + "%"));
    }
    if (!pending_result_ || progress_ < 100) return;

    auto result = std::move(*pending_result_);
    pending_result_.reset();
    timer_.Stop();
    AnalyzeButton().IsEnabled(true);
    AnalyzeButton().Content(box_value(L"Analyze again"));
    if (result) {
        Status().Text(L"Analysis complete");
        render_report(*result);
    } else {
        Status().Text(L"Analysis failed");
        render_message(to_hstring(result.error().detail), true);
    }

    } catch (...) {
        winchisel::ui::report_async_error(error_queue, [error_weak](winrt::hstring const& text) {
            if (auto self=error_weak.get()) { self->timer_.Stop(); self->pending_result_.reset(); self->AnalyzeButton().IsEnabled(true); self->AnalyzeButton().Content(box_value(L"Analyze again")); self->render_message(text,true); }
        });
    }
}

void LatencyPage::render_report(winchisel::platform::LatencyAnalysis const& report) {
    auto resources = Application::Current().Resources();
    auto normal = resources.Lookup(box_value(L"TextFillColorPrimaryBrush")).try_as<Media::Brush>();
    auto muted = resources.Lookup(box_value(L"TextFillColorSecondaryBrush")).try_as<Media::Brush>();
    auto accent = resources.Lookup(box_value(L"AccentTextFillColorPrimaryBrush")).try_as<Media::Brush>();
    auto success = resources.Lookup(box_value(L"SystemFillColorSuccessBrush")).try_as<Media::Brush>();
    auto caution = resources.Lookup(box_value(L"SystemFillColorCautionBrush")).try_as<Media::Brush>();
    auto critical = resources.Lookup(box_value(L"SystemFillColorCriticalBrush")).try_as<Media::Brush>();
    Report().Blocks().Clear();

    for (auto const& line : report.lines) {
        auto paragraph = Documents::Paragraph();
        auto run = Documents::Run();
        run.Text(to_hstring(line.text));
        Media::Brush brush = normal;
        switch (line.color) {
        case winchisel::platform::LatencyColor::success: brush = success; break;
        case winchisel::platform::LatencyColor::warning: brush = caution; break;
        case winchisel::platform::LatencyColor::critical: brush = critical; break;
        case winchisel::platform::LatencyColor::accent: brush = accent; break;
        case winchisel::platform::LatencyColor::muted:
        case winchisel::platform::LatencyColor::separator: brush = muted; break;
        default: break;
        }
        run.Foreground(brush);
        run.FontWeight(line.bold ? Windows::UI::Text::FontWeights::SemiBold() : Windows::UI::Text::FontWeights::Normal());
        paragraph.Inlines().Append(run);
        Report().Blocks().Append(paragraph);
    }
}

void LatencyPage::render_message(hstring const& message, bool is_error) {
    winchisel::platform::LatencyAnalysis report;
    report.lines.push_back({to_string(message), is_error ? winchisel::platform::LatencyColor::critical : winchisel::platform::LatencyColor::muted, false});
    render_report(report);
}

}  // namespace winrt::Winchisel::implementation
