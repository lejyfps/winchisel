#include "pch.h"
#include "AsyncSupport.hpp"
#include "LatencyPage.xaml.h"
#include "AsyncLifetime.hpp"
#include "winchisel/application/session.hpp"
#include "winchisel/platform/cleanup.hpp"
#include "winchisel/platform/latency.hpp"
#include "TeachingTips.hpp"
#include "winchisel/platform/usb_cadence_capture.hpp"
#include "winchisel/platform/usb_identity.hpp"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <shlobj.h>
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
        if (auto page = weak.get()) {
            page->poll_analysis();
            page->poll_cadence();
        }
    });
    Loaded([weak](auto&&, auto&&) { if (auto page = weak.get()) { if (page->analysis_.valid() || page->cadence_.valid()) page->timer_.Start(); } });
    Unloaded([weak](auto&&, auto&&) { if (auto page = weak.get()) page->timer_.Stop(); });
    render_message(L"Click 'Analyze USB Latency' to begin analysis.");
}

LatencyPage::~LatencyPage() {
    if (timer_) {
        timer_.Stop();
        timer_.Tick(timer_token_);
    }
    winchisel::ui::finish_in_background(analysis_);
    winchisel::ui::finish_in_background(cadence_);
}

void LatencyPage::Analyze_Click(Windows::Foundation::IInspectable const&, RoutedEventArgs const&) {
    auto error_weak=get_weak();
    winrt::Microsoft::UI::Dispatching::DispatcherQueue error_queue{nullptr};
    try { error_queue=DispatcherQueue(); } catch (...) {}
    try {
        auto error_lifetime=get_strong();

    if (analysis_.valid()) return;
    progress_ = 0;
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
                    // Real worker values only: show them as-is, ignore stale ones.
                    const int shown = std::clamp(value, 0, 100);
                    if (shown < page->progress_) return;
                    page->progress_ = shown;
                    page->Progress().Value(shown);
                    page->ProgressPercent().Text(to_hstring(std::to_string(shown) + "%"));
                    page->Status().Text(status_text);
                }
            });
        });
    });
    timer_.Start();

    } catch (...) {
        winchisel::ui::report_async_error(error_queue, [error_weak](winrt::hstring const& text) {
            if (auto self=error_weak.get()) { self->timer_.Stop(); self->AnalyzeButton().IsEnabled(true); self->AnalyzeButton().Content(box_value(L"Analyze again")); self->render_message(text,true); }
        });
    }
}

void LatencyPage::poll_analysis() {
    auto error_weak=get_weak();
    winrt::Microsoft::UI::Dispatching::DispatcherQueue error_queue{nullptr};
    try { error_queue=DispatcherQueue(); } catch (...) {}
    try {
        auto error_lifetime=get_strong();

    if (!analysis_.valid()) return;
    if (analysis_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;

    auto result = analysis_.get();
    timer_.Stop();
    AnalyzeButton().IsEnabled(true);
    AnalyzeButton().Content(box_value(L"Analyze again"));
    progress_ = 100;
    Progress().Value(progress_);
    ProgressPercent().Text(L"100%");
    if (result) {
        Status().Text(L"Analysis complete");
        render_report(*result);
    } else {
        Status().Text(L"Analysis failed");
        render_message(to_hstring(result.error().detail), true);
    }

    } catch (...) {
        winchisel::ui::report_async_error(error_queue, [error_weak](winrt::hstring const& text) {
            if (auto self=error_weak.get()) { self->timer_.Stop(); self->AnalyzeButton().IsEnabled(true); self->AnalyzeButton().Content(box_value(L"Analyze again")); self->render_message(text,true); }
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

    auto brush_for = [&](winchisel::platform::LatencyColor color) {
        switch (color) {
        case winchisel::platform::LatencyColor::success: return success;
        case winchisel::platform::LatencyColor::warning: return caution;
        case winchisel::platform::LatencyColor::critical: return critical;
        case winchisel::platform::LatencyColor::accent: return accent;
        default: return muted;
        }
    };
    auto card_background = resources.Lookup(box_value(L"CardBackgroundFillColorDefaultBrush")).try_as<Media::Brush>();
    auto card_stroke = resources.Lookup(box_value(L"CardStrokeColorDefaultBrush")).try_as<Media::Brush>();
    OptimizationsList().Children().Clear();
    if (report.optimizations.empty()) {
        OptimizationsCard().Visibility(Visibility::Collapsed);
    } else {
        for (auto const& opt : report.optimizations) {
            auto card = Controls::Border();
            card.Padding({16, 12, 16, 12});
            card.Background(card_background);
            card.BorderBrush(card_stroke);
            card.BorderThickness({1, 1, 1, 1});
            card.CornerRadius({4, 4, 4, 4});
            auto text = Controls::TextBlock();
            text.Text(to_hstring(opt.text));
            text.TextWrapping(TextWrapping::Wrap);
            text.Foreground(brush_for(opt.color));
            card.Child(text);
            OptimizationsList().Children().Append(card);
        }
        const auto count = report.optimizations.size();
        OptimizationsCard().Header(box_value(L"Optimizations available (" + std::to_wstring(count) + L")"));
        OptimizationsCard().Visibility(Visibility::Visible);
        OptimizationsCard().IsExpanded(false);
    }
}

void LatencyPage::render_message(hstring const& message, bool is_error) {
    OptimizationsList().Children().Clear();
    OptimizationsCard().Visibility(Visibility::Collapsed);
    winchisel::platform::LatencyAnalysis report;
    report.lines.push_back({to_string(message), is_error ? winchisel::platform::LatencyColor::critical : winchisel::platform::LatencyColor::muted, false});
    render_report(report);
}

namespace {

using winchisel::platform::LatencyAnalysis;
using winchisel::platform::LatencyColor;
namespace cadence = winchisel::platform::cadence;

void push_line(LatencyAnalysis& report, std::string text, LatencyColor color = LatencyColor::normal, bool bold = false) {
    report.lines.push_back({std::move(text), color, bold});
}

std::string speed_label(cadence::UsbSpeed speed, unsigned long raw) {
    switch (speed) {
    case cadence::UsbSpeed::low: return "Low-Speed";
    case cadence::UsbSpeed::full: return "Full-Speed";
    case cadence::UsbSpeed::high: return "High-Speed";
    case cadence::UsbSpeed::super: return "SuperSpeed";
    case cadence::UsbSpeed::unknown: break;
    }
    char buffer[48]{};
    std::snprintf(buffer, sizeof(buffer), "Unknown (ETW raw %lu)", raw);
    return buffer;
}

std::string format_hz(double hz) {
    char buffer[32]{};
    std::snprintf(buffer, sizeof(buffer), "%.0f Hz", hz);
    return buffer;
}

// Baut Report + Export-JSON/CSV aus einem Capture-Ergebnis. Die Headline-Rate ist
// ausdrücklich die beobachtete Completion-Kadenz, keine Input-Latenz.
struct CadenceExport {
    LatencyAnalysis report;
    std::string json;
    std::string csv;
};

CadenceExport build_cadence_report(CadenceSession const& session, int selected,
                                   std::vector<CadenceHistoryEntry> const& history) {
    auto const& outcome = session.capture;
    LatencyAnalysis report;
    std::ostringstream json;
    json << std::fixed << std::setprecision(2);
    std::ostringstream csv;
    csv << "vid,pid,pipe,container,endpoint,speed_raw,b_interval,completions,rate_hz,median_us,p95_us,p99_us,p999_us,steady,stalls,stall_ms,status\n";
    push_line(report, "");
    push_line(report, "  OBSERVED USB TRANSFER/COMPLETION CADENCE", LatencyColor::accent, true);
    push_line(report, "  =====================================================================", LatencyColor::separator);
    push_line(report, "");
    push_line(report, "  This is the observed completion cadence - not input latency and not", LatencyColor::muted);
    push_line(report, "  a guaranteed device poll rate. Driver queueing, DPC batching, HID", LatencyColor::muted);
    push_line(report, "  idle and ETW event losses can skew the reading.", LatencyColor::muted);
    push_line(report, "");

    json << "{\"tool\":\"winchisel-cadence\",\"version\":1,";
    json << "\"events_lost\":" << outcome.events_lost
         << ",\"realtime_buffers_lost\":" << outcome.realtime_buffers_lost
         << ",\"buffers_written\":" << outcome.buffers_written
         << ",\"aborted\":" << (outcome.aborted ? "true" : "false");

    // Grobe CPU-Zeitreihe: Mittel/Peak + ausdrücklicher Nicht-Kausalitätsvorbehalt.
    if (!outcome.cpu.empty()) {
        double sum = 0.0, peak = 0.0;
        for (auto const& sample : outcome.cpu) {
            sum += sample.busy_pct;
            if (sample.busy_pct > peak) peak = sample.busy_pct;
        }
        const double avg = sum / static_cast<double>(outcome.cpu.size());
        char buffer[192]{};
        std::snprintf(buffer, sizeof(buffer),
                      "  System load during capture: avg %.0f%%, peak %.0f%% (%zu samples, ~500 ms QPC raster).",
                      avg, peak, outcome.cpu.size());
        push_line(report, buffer, LatencyColor::muted);
        push_line(report, "  Rough time correlation only - never proof that load caused a stall.",
                  LatencyColor::muted);
        push_line(report, "");
        json << ",\"cpu_avg\":" << avg << ",\"cpu_peak\":" << peak << ",\"cpu\":[";
        bool cpu_first = true;
        for (auto const& sample : outcome.cpu) {
            if (!cpu_first) json << ",";
            cpu_first = false;
            json << "{\"t_us\":" << sample.t_us << ",\"pct\":" << sample.busy_pct << "}";
        }
        json << "]";
    }
    json << ",\"devices\":[";

    // Container-ID-Korrelation je Instanz (kein VID:PID-Dedup): trennt zwei
    // baugleiche Geräte. Fail-open: ohne Treffer wird das ausgewiesen.
    const auto instances = cadence::enumerate_usb_instances();

    const bool invalid = outcome.events_lost > 0 || outcome.realtime_buffers_lost > 0;
    if (invalid) {
        char buffer[160]{};
        std::snprintf(buffer, sizeof(buffer),
                      "  MEASUREMENT INVALID: ETW lost %lu events (%lu realtime buffers).",
                      outcome.events_lost, outcome.realtime_buffers_lost);
        push_line(report, buffer, LatencyColor::critical, true);
        push_line(report, "  Discard the Hz reading below; it is shown for orientation only.", LatencyColor::critical);
        push_line(report, "");
    }
    if (outcome.aborted) push_line(report, "  Capture aborted by user.", LatencyColor::warning);
    if (outcome.devices.empty()) {
        push_line(report, "  No USB interrupt traffic captured. Move the device during capture", LatencyColor::muted);
        push_line(report, "  (wiggle mouse, press gamepad buttons) and measure again.", LatencyColor::muted);
        json << "]}";
        return {report, json.str()};
    }

    cadence::CadenceConfig config;
    struct DeviceResult {
        cadence::CadenceResult analysis;
        std::string container_id;
    };
    std::vector<DeviceResult> results;
    results.reserve(outcome.devices.size());
    bool first = true;
    for (auto const& device : outcome.devices) {
        DeviceResult entry;
        entry.analysis = cadence::analyze_capture(device.intervals_us, config, invalid);
        auto const& id = device.identity;
        if (!id.interface_path.empty())
            entry.container_id = cadence::resolve_container_id(id.interface_path, instances).value_or("");
        auto const& result = entry.analysis;
        if (!first) {
            json << ",";
            csv << "\n";
        }
        first = false;
        json << "{\"vid\":\"" << id.vid << "\",\"pid\":\"" << id.pid << "\",\"pipe\":" << id.pipe
             << ",\"container\":\"" << entry.container_id << "\""
             << ",\"endpoint\":" << id.endpoint_address
             << ",\"interface\":" << (id.has_interface ? static_cast<int>(id.interface_number) : -1)
             << ",\"alt_setting\":" << (id.has_interface ? static_cast<int>(id.alt_setting) : -1)
             << ",\"speed_raw\":" << id.speed_raw << ",\"b_interval\":" << id.b_interval
             << ",\"has_companion\":" << (id.has_companion ? "true" : "false")
             << ",\"completions\":" << device.completions << ",\"rate_hz\":" << result.rate_hz
             << ",\"median_us\":" << result.median_us << ",\"p95_us\":" << result.p95_us
             << ",\"p99_us\":" << result.p99_us << ",\"p999_us\":" << result.p999_us << ",\"threshold_us\":" << result.threshold_us
             << ",\"steady\":" << result.steady_samples << ",\"stalls\":" << result.stalls.size()
             << ",\"stall_ms\":" << result.stall_ms
             << ",\"truncated\":" << (device.truncated ? "true" : "false")
             << ",\"status\":" << static_cast<int>(result.status) << "}";
        csv << id.vid << "," << id.pid << "," << id.pipe << "," << entry.container_id << ","
            << id.endpoint_address << "," << id.speed_raw << "," << id.b_interval << ","
            << device.completions << "," << result.rate_hz << "," << result.median_us << ","
            << result.p95_us << "," << result.p99_us << "," << result.p999_us << ","
            << result.steady_samples << "," << result.stalls.size() << "," << result.stall_ms << ","
            << static_cast<int>(result.status);
        results.push_back(std::move(entry));
    }
    json << "]}";

    // Gerätewähler: ausgewähltes Gerät voll, Rest als Einzeiler.
    const std::size_t head = (std::min<std::size_t>)(outcome.devices.size(), 5);
    int current = selected < 0 ? 0 : selected >= static_cast<int>(head) ? 0 : selected;
    for (std::size_t i = 0; i < head; ++i) {
        auto const& device = outcome.devices[i];
        auto const& id = device.identity;
        auto const& result = results[i].analysis;
        std::string tag = "  [" + std::to_string(i) + "] ";
        tag += !id.vid.empty() && !id.pid.empty() ? "[" + id.vid + ":" + id.pid + "]" : "[unknown VID:PID]";
        if (i != static_cast<std::size_t>(current)) {
            tag += " ~" + format_hz(result.rate_hz) + " (" + std::to_string(device.completions) + " completions)";
            push_line(report, tag, LatencyColor::muted);
            continue;
        }
        push_line(report, tag + " (selected)", LatencyColor::normal, true);
        std::string ctx = "      pipe 0x";
        {
            char buffer[32]{};
            std::snprintf(buffer, sizeof(buffer), "%llX", id.pipe);
            ctx += buffer;
        }
        ctx += " | " + speed_label(id.speed, id.speed_raw);
        if (!id.port_path.empty()) ctx += " | ports " + id.port_path;
        ctx += " | completions " + std::to_string(device.completions);
        push_line(report, ctx, LatencyColor::muted);
        if (id.endpoint_address != 0) {
            char buffer[160]{};
            const bool interrupt = (id.endpoint_attributes & 0x03) == 0x03;
            std::snprintf(buffer, sizeof(buffer), "      Endpoint 0x%02lX, %s, max packet %lu",
                          id.endpoint_address, interrupt ? "interrupt" : "non-interrupt",
                          id.max_packet);
            push_line(report, buffer, LatencyColor::muted);
            if (id.has_interface) {
                char iface[96]{};
                std::snprintf(iface, sizeof(iface), "      Interface %lu, Alt-Setting %lu",
                              id.interface_number, id.alt_setting);
                push_line(report, iface, LatencyColor::muted);
            }
            if (id.has_companion) {
                char companion[128]{};
                std::snprintf(companion, sizeof(companion),
                              "      SuperSpeed companion: burst %lu, %lu bytes/interval",
                              id.max_burst + 1, id.bytes_per_interval);
                push_line(report, companion, LatencyColor::muted);
            }
        }
        if (device.truncated)
            push_line(report, "      Samples capped at 200000 - tail cut, head rate still valid.",
                      LatencyColor::warning);
        if (!id.interface_path.empty()) {
            push_line(report, "      " + id.interface_path, LatencyColor::muted);
            if (!results[i].container_id.empty()) {
                push_line(report, "      container " + results[i].container_id, LatencyColor::muted);
            } else {
                push_line(report, "      container unresolved - two identical devices may mix here",
                          LatencyColor::warning);
            }
        }

        switch (result.status) {
        case cadence::CadenceStatus::ok: {
            push_line(report, "      Observed cadence: " + format_hz(result.rate_hz) + "  (" +
                                      std::to_string(result.steady_samples) + " steady samples)",
                      LatencyColor::success, true);
            break;
        }
        case cadence::CadenceStatus::insufficient_data:
            push_line(report, "      Too few steady samples (" + std::to_string(result.steady_samples) +
                                      ") - no reliable rate. Repeat while using the device.",
                      LatencyColor::warning, true);
            break;
        case cadence::CadenceStatus::events_lost:
            push_line(report, "      Invalid (ETW losses) - rate for orientation: " + format_hz(result.rate_hz),
                      LatencyColor::critical, true);
            break;
        default:
            push_line(report, "      No rate available.", LatencyColor::warning, true);
            break;
        }

        if (id.speed != cadence::UsbSpeed::unknown && id.b_interval > 0) {
            if (auto expected = cadence::expected_hz(id.speed, id.b_interval)) {
                char buffer[128]{};
                std::snprintf(buffer, sizeof(buffer), "      Configured (bInterval=%d): %s",
                              id.b_interval, format_hz(*expected).c_str());
                push_line(report, buffer, LatencyColor::muted);
                if (result.status == cadence::CadenceStatus::ok && result.rate_hz < *expected * 0.98)
                    push_line(report, "      Below configured rate - try another port/hub, check power saving.",
                              LatencyColor::warning);
            }
        } else {
            push_line(report, "      No configured rate (speed or bInterval unknown) - bInterval alone is not a time.",
                      LatencyColor::muted);
            push_line(report, "      If this is a setup/config interface (bulk-only), no poll rate applies -",
                      LatencyColor::muted);
            push_line(report, "      switch the device to gaming/operating mode and measure again.",
                      LatencyColor::muted);
        }

        {
            char buffer[192]{};
            std::snprintf(buffer, sizeof(buffer),
                          "      Median %.1f us - P95 %.1f us - P99 %.1f us - P99.9 %.1f us - stall threshold %.1f us",
                          result.median_us, result.p95_us, result.p99_us, result.p999_us,
                          result.threshold_us);
            push_line(report, buffer, LatencyColor::muted);
        }
        if (!result.histogram.empty() && result.kept_samples > 0) {
            std::size_t peak = 0;
            for (auto const& bin : result.histogram) peak = (std::max)(peak, bin.count);
            for (auto const& bin : result.histogram) {
                char label[64]{};
                if (bin.overflow) std::snprintf(label, sizeof(label), ">%.0f us", bin.lo_us);
                else std::snprintf(label, sizeof(label), "%.0f-%.0f us", bin.lo_us, bin.hi_us);
                const double pct = 100.0 * static_cast<double>(bin.count) /
                                   static_cast<double>(result.kept_samples);
                const int bars = peak > 0 ? static_cast<int>(24.0 * static_cast<double>(bin.count) /
                                                             static_cast<double>(peak))
                                          : 0;
                std::string line = "      ";
                line += label;
                line += " ";
                line += std::string(static_cast<std::size_t>(bars), '#');
                char tail[32]{};
                std::snprintf(tail, sizeof(tail), " %.1f%%", pct);
                line += tail;
                push_line(report, line, bin.overflow ? LatencyColor::warning : LatencyColor::muted);
            }
        }
        if (!result.stalls.empty() && result.status != cadence::CadenceStatus::events_lost) {
            char buffer[160]{};
            std::snprintf(buffer, sizeof(buffer), "      Stalls: %zu gap(s) excluded (%.0f ms total) - not device verdict.",
                          result.stalls.size(), result.stall_ms);
            push_line(report, buffer, LatencyColor::muted);
        }
        push_line(report, "");

        // Topologie-Kontext zum gewählten Gerät: Diagnosehilfe, keine Ursache.
        bool context_shown = false;
        for (auto const& entry : session.topologies) {
            auto lower_eq = [](std::string a, std::string b) {
                std::ranges::transform(a, a.begin(), [](unsigned char c) {
                    return static_cast<char>(std::tolower(c));
                });
                std::ranges::transform(b, b.begin(), [](unsigned char c) {
                    return static_cast<char>(std::tolower(c));
                });
                return a == b;
            };
            if (!lower_eq(entry.vid, id.vid) || !lower_eq(entry.pid, id.pid)) continue;
            if (!entry.context.found) break;
            auto const& topo = entry.context;
            context_shown = true;
            push_line(report, "      Hardware context (topology - context, not cause):", LatencyColor::normal, true);
            push_line(report, "      via " + topo.controller_name + " (" + topo.platform + ")",
                      LatencyColor::muted);
            push_line(report, "      " + std::to_string(topo.chip_count) + " chip(s), " +
                                      std::to_string(topo.hub_count) + " hub(s)" +
                                      (topo.hub_names.empty() ? "" : " - " + topo.hub_names.front()),
                      topo.chip_count == 0 ? LatencyColor::success
                                           : topo.chip_count == 1 ? LatencyColor::warning
                                                                  : LatencyColor::critical);
            if (topo.msi_status == "Line-Based")
                push_line(report, "      IRQ: Line-Based (higher latency) - MSI preferred", LatencyColor::warning);
            else if (topo.msi_status == "MSI")
                push_line(report, "      IRQ: MSI (low latency interrupts)", LatencyColor::success);
            if (topo.selective_suspend)
                push_line(report, "      Selective Suspend ENABLED (causes latency spikes)", LatencyColor::warning);
            break;
        }
        if (!context_shown)
            push_line(report, "      No topology match for this VID:PID.", LatencyColor::muted);
        push_line(report, "");
    }
    if (!history.empty()) {
        push_line(report, "  PREVIOUS RUNS (this session)", LatencyColor::normal, true);
        for (auto const& entry : history)
            push_line(report, "    " + entry.time + "  " + entry.label + "  " + entry.status,
                      LatencyColor::muted);
        push_line(report, "");
    }
    push_line(report, "  =====================================================================", LatencyColor::separator);
    return {report, json.str(), csv.str()};
}

} // namespace

void LatencyPage::Cadence_Click(Windows::Foundation::IInspectable const&, RoutedEventArgs const&) {
    auto error_weak = get_weak();
    winrt::Microsoft::UI::Dispatching::DispatcherQueue error_queue{nullptr};
    try { error_queue = DispatcherQueue(); } catch (...) {}
    try {
        auto error_lifetime = get_strong();

        if (cadence_.valid() || analysis_.valid()) return;
        std::string detail;
        if (!cadence::etw_providers_available(&detail)) {
            render_message(to_hstring("USB ETW providers unavailable (" + detail + "). No measurement possible."), true);
            return;
        }
        // #3 TeachingTip (einmalig, nur ohne Elevation): Live-Traffic braucht
        // Admin — sonst ggf. unvollständig/ungültig.
        if (!winchisel::platform::cleanup_process_elevated() && !winchisel::ui::teaching_tip_seen("cadence_admin")) {
            winchisel::ui::dismiss_teaching_tip("cadence_admin");
            Controls::TeachingTip tip;
            tip.Title(L"Administrator rights recommended");
            tip.Subtitle(L"Live USB traffic needs elevation — without it the measurement may be incomplete or invalid. Capturing changes nothing on your system.");
            tip.Target(CadenceButton());
            tip.PreferredPlacement(Controls::TeachingTipPlacementMode::Bottom);
            tip.IsLightDismissEnabled(true);
            tip.XamlRoot(XamlRoot());
            tip.IsOpen(true);
        }
        progress_ = 0;
        Progress().Value(progress_);
        ProgressPercent().Text(L"0%");
        Status().Text(winchisel::ui::tr(L"Measuring USB transfer cadence..."));
        render_message(winchisel::ui::tr(L"Capturing USB transfer cadence for 7 seconds. Use your devices meanwhile. This does not change any system setting."));
        AnalyzeButton().IsEnabled(false);
        CadenceButton().IsEnabled(false);
        CadenceButton().Content(box_value(winchisel::ui::tr(L"Measuring...")));
        CadenceCancelButton().Visibility(Visibility::Visible);
        ExportMenu().Visibility(Visibility::Collapsed);
        CadenceDevices().Visibility(Visibility::Collapsed);
        export_ready_ = false;
        cancel_flag_.store(false);
        auto weak = get_weak();
        auto queue = DispatcherQueue();
        auto* cancel = &cancel_flag_;
        cadence_ = std::async(std::launch::async, [weak, queue, cancel] {
            cadence::CaptureConfig config;
            config.seconds = 7;
            config.cancel = cancel;
            config.progress = [weak, queue](int value, std::string const& status) {
                const auto text = to_hstring(status);
                winchisel::ui::enqueue_safe(queue, [weak, value, text] {
                    if (auto page = weak.get()) {
                        // Capture nutzt 0..85 %, Topologie danach 85..100 %.
                        const int shown = std::clamp(value * 85 / 100, 0, 100);
                        if (shown < page->progress_) return;
                        page->progress_ = shown;
                        page->Progress().Value(shown);
                        page->ProgressPercent().Text(to_hstring(std::to_string(shown) + "%"));
                        page->Status().Text(text);
                    }
                });
            };
            auto captured = cadence::capture_usb_cadence(config);
            if (!captured) return winchisel::core::Result<CadenceSession>(std::unexpected(captured.error()));
            CadenceSession session;
            session.capture = std::move(*captured);
            // Topologie-Kontext zu den Top-Geräten (Diagnosehilfe, keine Ursache).
            std::vector<std::pair<std::string, std::string>> seen;
            for (auto const& device : session.capture.devices) {
                if (session.topologies.size() >= 5) break;
                if (device.identity.vid.empty() || device.identity.pid.empty()) continue;
                if (std::ranges::any_of(seen, [&](auto const& pair) {
                        return pair.first == device.identity.vid && pair.second == device.identity.pid;
                    }))
                    continue;
                seen.emplace_back(device.identity.vid, device.identity.pid);
                winchisel::ui::enqueue_safe(queue, [weak] {
                    if (auto page = weak.get()) {
                        page->progress_ = 90;
                        page->Progress().Value(90);
                        page->ProgressPercent().Text(L"90%");
                        page->Status().Text(winchisel::ui::tr(L"Resolving hardware context..."));
                    }
                });
                CadenceTopologyEntry entry;
                entry.vid = device.identity.vid;
                entry.pid = device.identity.pid;
                entry.context =
                    winchisel::platform::topology_context_for(device.identity.vid, device.identity.pid);
                session.topologies.push_back(std::move(entry));
            }
            return winchisel::core::Result<CadenceSession>(std::move(session));
        });
        timer_.Start();

    } catch (...) {
        winchisel::ui::report_async_error(error_queue, [error_weak](winrt::hstring const& text) {
            if (auto self = error_weak.get()) { self->timer_.Stop(); self->AnalyzeButton().IsEnabled(true); self->CadenceButton().IsEnabled(true); self->CadenceCancelButton().Visibility(Visibility::Collapsed); self->render_message(text, true); }
        });
    }
}

void LatencyPage::CadenceCancel_Click(Windows::Foundation::IInspectable const&, RoutedEventArgs const&) {
    cancel_flag_.store(true);
    Status().Text(winchisel::ui::tr(L"Cancelling..."));
}

void LatencyPage::CadenceDevices_SelectionChanged(Windows::Foundation::IInspectable const&,
                                                  Controls::SelectionChangedEventArgs const&) {
    const auto index = CadenceDevices().SelectedIndex();
    if (index < 0 || !last_session_) return;
    selected_device_ = index;
    render_selected();
}

void LatencyPage::poll_cadence() {
    auto error_weak = get_weak();
    winrt::Microsoft::UI::Dispatching::DispatcherQueue error_queue{nullptr};
    try { error_queue = DispatcherQueue(); } catch (...) {}
    try {
        auto error_lifetime = get_strong();

        if (!cadence_.valid()) return;
        if (cadence_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;

        auto result = cadence_.get();
        timer_.Stop();
        AnalyzeButton().IsEnabled(true);
        CadenceButton().IsEnabled(true);
        CadenceButton().Content(box_value(winchisel::ui::tr(L"Measure transfer cadence")));
        CadenceCancelButton().Visibility(Visibility::Collapsed);
        progress_ = 100;
        Progress().Value(progress_);
        ProgressPercent().Text(L"100%");
        if (result) {
            Status().Text(winchisel::ui::tr(L"Cadence measurement complete"));
            last_session_ = std::move(*result);
            selected_device_ = 0;
            // Verlauf (max. 5): Top-Gerät je Lauf.
            if (!last_session_->capture.devices.empty()) {
                auto const& top = last_session_->capture.devices.front();
                cadence::CadenceConfig config;
                auto analysis = cadence::analyze_capture(top.intervals_us, config, false);
                SYSTEMTIME time{};
                GetLocalTime(&time);
                char stamp[16]{};
                std::snprintf(stamp, sizeof(stamp), "%02u:%02u:%02u", time.wHour, time.wMinute,
                              time.wSecond);
                char label[48]{};
                std::snprintf(label, sizeof(label), "[%s:%s] %s", top.identity.vid.c_str(),
                              top.identity.pid.c_str(), format_hz(analysis.rate_hz).c_str());
                history_.push_back({stamp, label, analysis.rate_hz,
                                    analysis.status == cadence::CadenceStatus::ok ? "ok" : "limited"});
                while (history_.size() > 5) history_.erase(history_.begin());
            }
            // Gerätewähler füllen.
            CadenceDevices().Items().Clear();
            const std::size_t head = (std::min<std::size_t>)(last_session_->capture.devices.size(), 5);
            for (std::size_t i = 0; i < head; ++i) {
                auto const& id = last_session_->capture.devices[i].identity;
                std::string label = "[" + std::to_string(i) + "] ";
                label += !id.vid.empty() ? id.vid + ":" + id.pid : "unknown";
                CadenceDevices().Items().Append(box_value(to_hstring(label)));
            }
            CadenceDevices().SelectedIndex(0);
            CadenceDevices().Visibility(head > 1 ? Visibility::Visible : Visibility::Collapsed);
            render_selected();
        } else {
            Status().Text(winchisel::ui::tr(L"Cadence measurement failed"));
            render_message(to_hstring(result.error().detail), true);
        }

    } catch (...) {
        winchisel::ui::report_async_error(error_queue, [error_weak](winrt::hstring const& text) {
            if (auto self = error_weak.get()) { self->timer_.Stop(); self->AnalyzeButton().IsEnabled(true); self->CadenceButton().IsEnabled(true); self->CadenceCancelButton().Visibility(Visibility::Collapsed); self->render_message(text, true); }
        });
    }
}

void LatencyPage::render_selected() {
    if (!last_session_) return;
    auto exported = build_cadence_report(*last_session_, selected_device_, history_);
    last_export_json_ = std::move(exported.json);
    last_export_csv_ = std::move(exported.csv);
    export_ready_ = true;
    ExportMenu().Visibility(Visibility::Visible);
    render_report(exported.report);
}

bool write_text_file(std::wstring const& path, std::string const& content, std::string& error) {
    HANDLE handle = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        error = "Export failed (Win32 " + std::to_string(GetLastError()) + ").";
        return false;
    }
    DWORD written = 0;
    const BOOL ok = WriteFile(handle, content.data(), static_cast<DWORD>(content.size()), &written, nullptr);
    CloseHandle(handle);
    if (ok == FALSE || written != content.size()) {
        error = "Export incomplete.";
        return false;
    }
    return true;
}

std::wstring export_path(wchar_t const* extension) {
    wchar_t* path = nullptr;
    if (SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &path) != S_OK || path == nullptr)
        return {};
    std::wstring folder(path);
    CoTaskMemFree(path);
    SYSTEMTIME time{};
    GetLocalTime(&time);
    wchar_t file[64]{};
    swprintf_s(file, L"Winchisel-cadence-%04u%02u%02u-%02u%02u%02u%ls", time.wYear, time.wMonth,
               time.wDay, time.wHour, time.wMinute, time.wSecond, extension);
    return folder + L"\\" + file;
}

void LatencyPage::Export_Click(Windows::Foundation::IInspectable const&, RoutedEventArgs const&) {
    if (!export_ready_ || last_export_json_.empty()) return;
    std::string error;
    const auto full = export_path(L".json");
    if (full.empty() || !write_text_file(full, last_export_json_, error)) {
        render_message(to_hstring(error.empty() ? "Export failed." : error), true);
        return;
    }
    Status().Text(winchisel::ui::tr(L"Exported: ") + full);
}

void LatencyPage::ExportCsv_Click(Windows::Foundation::IInspectable const&, RoutedEventArgs const&) {
    if (!export_ready_ || last_export_csv_.empty()) return;
    std::string error;
    const auto full = export_path(L".csv");
    if (full.empty() || !write_text_file(full, last_export_csv_, error)) {
        render_message(to_hstring(error.empty() ? "Export failed." : error), true);
        return;
    }
    Status().Text(winchisel::ui::tr(L"Exported: ") + full);
}

}  // namespace winrt::Winchisel::implementation
