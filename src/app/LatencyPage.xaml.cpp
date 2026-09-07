#include "pch.h"
#include "LatencyPage.xaml.h"
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
    timer_.Tick([this](auto&&, auto&&) { poll_analysis(); });
}

LatencyPage::~LatencyPage() {
    timer_.Stop();
}

void LatencyPage::Analyze_Click(Windows::Foundation::IInspectable const&, RoutedEventArgs const&) {
    if (analysis_.valid()) return;
    progress_ = 4;
    Progress().Value(progress_);
    Status().Text(L"Starting USB topology analysis...");
    render_message(L"Scanning input devices, USB controllers, and hubs. This does not change any system setting.");
    AnalyzeButton().IsEnabled(false);
    AnalyzeButton().Content(box_value(L"Analyzing..."));
    auto weak = get_weak();
    auto queue = DispatcherQueue();
    analysis_ = std::async(std::launch::async, [weak, queue] {
        return winchisel::platform::analyze_usb_topology([weak, queue](int value, std::string_view status) {
            const auto status_text = to_hstring(status);
            queue.TryEnqueue([weak, value, status_text] {
                if (auto page = weak.get()) {
                    page->progress_ = value;
                    page->Progress().Value(value);
                    page->Status().Text(status_text);
                }
            });
        });
    });
    timer_.Start();
}

void LatencyPage::poll_analysis() {
    if (!analysis_.valid()) { timer_.Stop(); return; }
    if (analysis_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        return;
    }
    const auto result = analysis_.get();
    timer_.Stop();
    Progress().Value(100);
    AnalyzeButton().IsEnabled(true);
    AnalyzeButton().Content(box_value(L"Analyze again"));
    if (result) {
        Status().Text(L"Analysis complete");
        render_report(*result);
    } else {
        Status().Text(L"Analysis failed");
        render_message(to_hstring(result.error().detail), true);
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
