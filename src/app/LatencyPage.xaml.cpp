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
    Status().Text(L"Starting Windows USB inventory…");
    render_report(L"Scanning input devices, USB controllers, and hubs. This does not change any system setting.");
    AnalyzeButton().IsEnabled(false);
    AnalyzeButton().Content(box_value(L"Analyzing…"));
    analysis_ = std::async(std::launch::async, [] { return winchisel::platform::analyze_usb_topology(); });
    timer_.Start();
}

void LatencyPage::poll_analysis() {
    if (!analysis_.valid()) { timer_.Stop(); return; }
    if (analysis_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        progress_ = std::min(progress_ + 4, 92);
        Progress().Value(progress_);
        Status().Text(L"Reading USB topology from Windows…");
        return;
    }
    const auto result = analysis_.get();
    timer_.Stop();
    Progress().Value(100);
    AnalyzeButton().IsEnabled(true);
    AnalyzeButton().Content(box_value(L"Analyze again"));
    if (result) {
        Status().Text(L"Analysis complete");
        render_report(to_hstring(*result));
    } else {
        Status().Text(L"Analysis failed");
        render_report(to_hstring(result.error().detail), true);
    }
}

void LatencyPage::render_report(hstring const& report, bool is_error) {
    auto resources = Application::Current().Resources();
    auto normal = resources.Lookup(box_value(L"TextFillColorPrimaryBrush")).try_as<Media::Brush>();
    auto muted = resources.Lookup(box_value(L"TextFillColorSecondaryBrush")).try_as<Media::Brush>();
    auto accent = resources.Lookup(box_value(L"AccentTextFillColorPrimaryBrush")).try_as<Media::Brush>();
    auto success = resources.Lookup(box_value(L"SystemFillColorSuccessBrush")).try_as<Media::Brush>();
    auto caution = resources.Lookup(box_value(L"SystemFillColorCautionBrush")).try_as<Media::Brush>();
    auto critical = resources.Lookup(box_value(L"SystemFillColorCriticalBrush")).try_as<Media::Brush>();
    Report().Blocks().Clear();

    std::wistringstream lines{std::wstring(report)};
    std::wstring line;
    while (std::getline(lines, line)) {
        auto paragraph = Documents::Paragraph();
        Media::Brush brush = is_error ? critical : normal;
        bool bold = false;
        std::size_t position{};
        while (position < line.size()) {
            const auto escape = line.find(L'\x1b', position);
            if (escape != position) {
                const auto text_end = escape == std::wstring::npos ? line.size() : escape;
                if (text_end > position) {
                    auto run = Documents::Run();
                    run.Text(line.substr(position, text_end - position));
                    run.Foreground(brush);
                    run.FontWeight(bold ? Windows::UI::Text::FontWeights::SemiBold() : Windows::UI::Text::FontWeights::Normal());
                    paragraph.Inlines().Append(run);
                }
                position = text_end;
                continue;
            }
            const auto end = line.find(L'm', position);
            if (end == std::wstring::npos) break;
            const auto code = line.substr(position + 2, end - position - 2);
            if (code == L"0") { brush = is_error ? critical : normal; bold = false; }
            else if (code == L"1") bold = true;
            else if (code == L"38;2;0;255;135") brush = success;
            else if (code == L"38;2;255;179;71") brush = caution;
            else if (code == L"38;2;255;107;107") brush = critical;
            else if (code == L"38;2;135;206;235") brush = accent;
            else if (code == L"38;2;108;108;108" || code == L"38;2;74;74;74" || code == L"90") brush = muted;
            else if (code == L"97") brush = normal;
            position = end + 1;
        }
        Report().Blocks().Append(paragraph);
    }
}

}  // namespace winrt::Winchisel::implementation
