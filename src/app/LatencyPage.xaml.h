#pragma once

#include "LatencyPage.g.h"
#include "LatencyPage.xaml.g.h"
#include "winchisel/platform/latency.hpp"
#include "winchisel/platform/usb_cadence_capture.hpp"

#include <atomic>
#include <future>
#include <optional>
#include <string>
#include <vector>

namespace winrt::Winchisel::implementation {

struct CadenceTopologyEntry {
    std::string vid;
    std::string pid;
    winchisel::platform::TopologyContext context;
};

struct CadenceSession {
    winchisel::platform::cadence::CaptureOutcome capture;
    std::vector<CadenceTopologyEntry> topologies;
};

struct CadenceHistoryEntry {
    std::string time;
    std::string label;
    double rate_hz = 0.0;
    std::string status;
};

struct LatencyPage : LatencyPageT<LatencyPage> {
    LatencyPage();
    ~LatencyPage();
    void Analyze_Click(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
    void Cadence_Click(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
    void CadenceCancel_Click(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
    void CadenceDevices_SelectionChanged(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
    void Export_Click(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
    void ExportCsv_Click(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);

private:
    void poll_analysis();
    void poll_cadence();
    void render_selected();
    void render_report(winchisel::platform::LatencyAnalysis const& report);
    void render_message(winrt::hstring const& message, bool is_error = false);
    std::future<winchisel::core::Result<winchisel::platform::LatencyAnalysis>> analysis_;
    std::future<winchisel::core::Result<CadenceSession>> cadence_;
    std::optional<CadenceSession> last_session_;
    int selected_device_ = 0;
    std::string last_export_json_;
    std::string last_export_csv_;
    bool export_ready_{false};
    std::atomic<bool> cancel_flag_{false};
    std::vector<CadenceHistoryEntry> history_;
    winrt::Microsoft::UI::Xaml::DispatcherTimer timer_{nullptr};
    winrt::event_token timer_token_{};
    int progress_{};
};

}  // namespace winrt::Winchisel::implementation

namespace winrt::Winchisel::factory_implementation {

struct LatencyPage : LatencyPageT<LatencyPage, implementation::LatencyPage> {};

}  // namespace winrt::Winchisel::factory_implementation
