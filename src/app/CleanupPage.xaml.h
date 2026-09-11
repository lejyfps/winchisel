#pragma once

#include "CleanupPage.g.h"
#include "CleanupPage.xaml.g.h"

#include "winchisel/core/cleanup.hpp"
#include "winchisel/core/error.hpp"
#include "winchisel/platform/cleanup.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace winrt::Winchisel::implementation {

struct CleanupPage : CleanupPageT<CleanupPage> {
    CleanupPage();
    ~CleanupPage();
    void Refresh_Click(
        winrt::Windows::Foundation::IInspectable const&,
        winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
    void Clean_Click(
        winrt::Windows::Foundation::IInspectable const&,
        winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
    void Cancel_Click(
        winrt::Windows::Foundation::IInspectable const&,
        winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
    void Elevate_Click(
        winrt::Windows::Foundation::IInspectable const&,
        winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);

private:
    enum class Job { none, scanning, cleaning };
    struct Row {
        winchisel::core::CleanupCategory category{};
        winrt::Microsoft::UI::Xaml::Controls::CheckBox box{nullptr};
        winrt::Microsoft::UI::Xaml::Controls::TextBlock size{nullptr};
        winrt::Microsoft::UI::Xaml::FrameworkElement card{nullptr};
    };
    // Shared with the detached worker thread: the thread only touches this
    // (plus platform APIs), so page destruction never blocks on a join. The
    // timer polls `done`; all cross-thread reads happen after it flips.
    struct Operation {
        std::atomic<bool> cancel{false};
        std::atomic<bool> done{false};
        Job job{Job::none};
        winchisel::core::Result<winchisel::platform::CleanupScan> scan_result{};
        winchisel::core::Result<winchisel::platform::CleanupSummary> clean_result{};
        std::vector<winchisel::core::CleanupCategory> selection{};
        std::mutex progress_mutex{};
        std::string progress_file{};
    };

    void start_scan();
    void poll();
    void finish_clean(winchisel::core::Result<winchisel::platform::CleanupSummary> const& result);
    winrt::fire_and_forget confirm_and_clean();
    void render();
    void update_chrome();
    void show_write_error(std::string const& detail = {});

    std::vector<winchisel::platform::CleanupScanEntry> scan_{};
    std::vector<Row> rows_{};
    std::shared_ptr<Operation> op_{};
    bool busy_{};
    bool phase_b_visible_{};
    bool elevated_{};
    winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer poll_timer_{nullptr};
};

}  // namespace winrt::Winchisel::implementation

namespace winrt::Winchisel::factory_implementation {

struct CleanupPage : CleanupPageT<CleanupPage, implementation::CleanupPage> {};

}  // namespace winrt::Winchisel::factory_implementation
