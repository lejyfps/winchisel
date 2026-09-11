#include "pch.h"
#include "AsyncSupport.hpp"
#include "Localization.hpp"
#include "CleanupPage.xaml.h"
#include "winchisel/platform/system.hpp"
#include "winchisel/application/session.hpp"

#if __has_include("CleanupPage.g.cpp")
#include "CleanupPage.g.cpp"
#endif

#include "winchisel/core/i18n.hpp"
#include "winchisel/platform/cleanup.hpp"

#include <cwchar>
#include <thread>

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace winrt::Winchisel::implementation {
void CleanupPage::show_write_error(std::string const& detail){auto message=detail.empty()?"A Windows setting could not be changed. See %APPDATA%\\Winchisel\\logs\\winchisel.log for details.":detail;ResultBar().Title(hstring{winchisel::core::loc(L"Cleanup failed.")});ResultBar().Message(to_hstring(message));ResultBar().Severity(Controls::InfoBarSeverity::Error);ResultBar().IsOpen(true);winchisel::platform::boot_log(("cleanup UI: "+message).c_str());}
namespace {

hstring format_bytes(std::uint64_t bytes) {
    static constexpr wchar_t const* units[] = {L"B", L"KB", L"MB", L"GB", L"TB"};
    double value = static_cast<double>(bytes);
    int unit{};
    while (value >= 1024.0 && unit < 4) {
        value /= 1024.0;
        ++unit;
    }
    wchar_t buffer[64]{};
    if (unit == 0) {
        swprintf_s(buffer, L"%llu %s", static_cast<unsigned long long>(bytes), units[unit]);
    } else {
        swprintf_s(buffer, L"%.1f %s", value, units[unit]);
    }
    return hstring(buffer);
}

hstring category_title(winchisel::core::CleanupCategory category) {
    using winchisel::core::CleanupCategory;
    switch (category) {
        case CleanupCategory::user_temp: return winchisel::ui::tr(L"Temporary files");
        case CleanupCategory::windows_temp: return winchisel::ui::tr(L"Windows temporary files");
        case CleanupCategory::recycle_bin: return winchisel::ui::tr(L"Recycle Bin");
        case CleanupCategory::thumbnails: return winchisel::ui::tr(L"Thumbnails");
        case CleanupCategory::delivery_optimization: return winchisel::ui::tr(L"Delivery Optimization");
        case CleanupCategory::shader_cache: return winchisel::ui::tr(L"Shader caches");
        case CleanupCategory::update_cleanup: return winchisel::ui::tr(L"Windows Update cleanup");
        case CleanupCategory::previous_installations: return winchisel::ui::tr(L"Previous installations");
        case CleanupCategory::prefetch: return winchisel::ui::tr(L"Prefetch");
    }
    return winchisel::ui::tr(L"Temporary files");
}

hstring category_description(winchisel::core::CleanupCategory category) {
    using winchisel::core::CleanupCategory;
    switch (category) {
        case CleanupCategory::user_temp:
            return winchisel::ui::tr(L"Your personal temp folder. Safe to empty; apps recreate what they need.");
        case CleanupCategory::windows_temp:
            return winchisel::ui::tr(L"System-wide temp folder. Files in use are skipped.");
        case CleanupCategory::recycle_bin: return winchisel::ui::tr(L"Empties the recycle bin on all drives.");
        case CleanupCategory::thumbnails:
            return winchisel::ui::tr(
                L"Explorer thumbnail and icon caches. Rebuilt automatically; entries may be locked while Explorer runs.");
        case CleanupCategory::delivery_optimization:
            return winchisel::ui::tr(L"Update payloads shared with other PCs. Locked files are skipped.");
        case CleanupCategory::shader_cache:
            return winchisel::ui::tr(L"GPU shader caches. Rebuilt on demand; one short stutter may follow.");
        case CleanupCategory::update_cleanup:
            return winchisel::ui::tr(
                L"Runs DISM component cleanup. Takes a while; nothing is uninstalled. Needs administrator rights.");
        case CleanupCategory::previous_installations:
            return winchisel::ui::tr(
                L"Removes Windows.old and setup leftovers. You lose the rollback option. Needs administrator rights.");
        case CleanupCategory::prefetch:
            return winchisel::ui::tr(
                L"Removes prefetch traces (the folder itself stays). Frees little; Windows rebuilds it. Needs administrator rights.");
    }
    return hstring{};
}

Controls::Border setting_card(FrameworkElement const& control, hstring const& description) {
    auto resources = Application::Current().Resources();
    auto card = Controls::Border();
    card.Background(resources.Lookup(box_value(L"CardBackgroundFillColorDefaultBrush")).try_as<Media::Brush>());
    card.BorderBrush(resources.Lookup(box_value(L"CardStrokeColorDefaultBrush")).try_as<Media::Brush>());
    card.BorderThickness({1, 1, 1, 1});
    card.Padding({16, 12, 16, 12});
    card.HorizontalAlignment(HorizontalAlignment::Stretch);
    auto layout = Controls::Grid();
    layout.ColumnDefinitions().Append(Controls::ColumnDefinition());
    auto trailing = Controls::ColumnDefinition();
    trailing.Width(GridLength{0, GridUnitType::Auto});
    layout.ColumnDefinitions().Append(trailing);
    layout.ColumnSpacing(12);
    auto text = Controls::StackPanel();
    text.Children().Append(control);
    auto detail = Controls::TextBlock();
    detail.Text(description);
    detail.TextWrapping(TextWrapping::Wrap);
    detail.Style(resources.Lookup(box_value(L"CaptionTextBlockStyle")).try_as<Style>());
    text.Children().Append(detail);
    layout.Children().Append(text);
    card.Child(layout);
    return card;
}

}  // namespace

CleanupPage::CleanupPage() {
    InitializeComponent();
    phase_b_visible_ = winchisel::platform::cleanup_phase_b_visible();
    elevated_ = winchisel::platform::cleanup_process_elevated();
    RefreshButton().Content(box_value(winchisel::ui::tr(L"Refresh")));
    CleanButton().Content(box_value(winchisel::ui::tr(L"Clean selected")));
    CancelButton().Content(box_value(winchisel::ui::tr(L"Cancel")));
    ElevateButton().Content(box_value(winchisel::ui::tr(L"Restart as administrator")));
    AdminNote().Text(winchisel::ui::tr(L"Restart Winchisel as administrator to enable the system categories."));
    poll_timer_ = DispatcherQueue().CreateTimer();
    poll_timer_.Interval(std::chrono::milliseconds(200));
    poll_timer_.IsRepeating(true);
    auto weak = get_weak();
    poll_timer_.Tick([weak](auto&&, auto&&) {
        if (auto self = weak.get()) self->poll();
    });
    start_scan();
}

CleanupPage::~CleanupPage() {
    if (poll_timer_) poll_timer_.Stop();
    // Never join: the worker only touches its Operation and platform APIs,
    // so it ends on its own. Cancelling here stops further deletions.
    if (op_ && !op_->done.load()) op_->cancel.store(true);
}

void CleanupPage::Refresh_Click(Windows::Foundation::IInspectable const&, RoutedEventArgs const&) {
    if (busy_) return;
    start_scan();
}

void CleanupPage::Cancel_Click(Windows::Foundation::IInspectable const&, RoutedEventArgs const&) {
    if (!busy_ || !op_) return;
    op_->cancel.store(true);
    Status().Text(winchisel::ui::tr(L"Cancelling..."));
    CancelButton().IsEnabled(false);
}

void CleanupPage::Elevate_Click(Windows::Foundation::IInspectable const&, RoutedEventArgs const&) {
    if (winchisel::platform::restart_elevated()) return;
    winchisel::ui::show_toast(Controls::InfoBarSeverity::Error, std::wstring(L"Cleanup"),
        std::wstring(winchisel::ui::tr(L"Restart Winchisel as administrator to enable the system categories").c_str()));
}

void CleanupPage::Clean_Click(Windows::Foundation::IInspectable const&, RoutedEventArgs const&) {
    confirm_and_clean();
}

void CleanupPage::start_scan() {
    if (busy_) return;
    busy_ = true;
    ResultBar().IsOpen(false);
    auto op = std::make_shared<Operation>();
    op->job = Job::scanning;
    op_ = op;
    Status().Text(winchisel::ui::tr(L"Scanning cleanup categories..."));
    update_chrome();
    std::thread([op] {
        try {
            op->scan_result = winchisel::platform::scan_cleanup();
        } catch (...) {
            op->scan_result = std::unexpected(winchisel::core::Error{.detail = "Cleanup scan failed unexpectedly."});
        }
        op->done.store(true);
    }).detach();
    poll_timer_.Start();
}

void CleanupPage::poll() {
    auto op = op_;
    if (!op || !op->done.load()) {
        // Throttled progress readout: the worker only stores the latest file
        // under a mutex, the 200 ms timer renders it. No queue flooding.
        if (op && op->job == Job::cleaning && !op->done.load()) {
            std::string current;
            {
                std::lock_guard lock(op->progress_mutex);
                current = op->progress_file;
            }
            if (!current.empty()) {
                Status().Text(hstring{
                    std::wstring(winchisel::ui::tr(L"Cleaning...").c_str()) + L" " + to_hstring(current)});
            }
        }
        return;
    }
    poll_timer_.Stop();
    op_ = nullptr;
    busy_ = false;
    if (op->job == Job::scanning) {
        if (!op->scan_result) {
            show_write_error(op->scan_result.error().detail);
            Status().Text(winchisel::ui::tr(L"Scan failed"));
        } else {
            scan_ = std::move(*op->scan_result);
            render();
            std::uint64_t total{};
            for (auto const& entry : scan_) total += entry.bytes;
            Summary().Text(
                hstring{std::wstring(format_bytes(total).c_str()) + L" " +
                        std::wstring(winchisel::ui::tr(L"Reclaimable:").c_str())});
            Status().Text(winchisel::ui::tr(L"Ready"));
        }
    } else {
        finish_clean(op->clean_result);
        // Sizes changed: rescan so cards show the honest post-clean state.
        start_scan();
        return;
    }
    update_chrome();
}

void CleanupPage::finish_clean(winchisel::core::Result<winchisel::platform::CleanupSummary> const& result) {
    if (!result) {
        const bool cancelled = result.error().detail.find("cancel") != std::string::npos;
        ResultBar().Title(hstring{winchisel::core::loc(
            cancelled ? L"Cleanup was cancelled." : L"Cleanup failed.")});
        ResultBar().Message(to_hstring(result.error().detail));
        ResultBar().Severity(
            cancelled ? Controls::InfoBarSeverity::Informational : Controls::InfoBarSeverity::Error);
        ResultBar().IsOpen(true);
        return;
    }
    auto const& summary = *result;
    std::wstring message = std::wstring(format_bytes(summary.bytes_freed).c_str());
    message += L" ";
    message += winchisel::core::loc(L"freed");
    message += L", ";
    message += std::to_wstring(summary.files_removed);
    message += L" ";
    message += winchisel::core::loc(L"files removed");
    message += L" (";
    message += std::to_wstring(summary.skipped);
    message += L" ";
    message += winchisel::core::loc(L"skipped");
    message += L", ";
    message += std::to_wstring(summary.errors);
    message += L" ";
    message += winchisel::core::loc(L"errors");
    message += L")";
    if (!summary.first_error.empty()) {
        message += L". ";
        message += winchisel::core::loc(L"First error");
        message += L": " + to_hstring(summary.first_error);
    }
    ResultBar().Title(hstring{winchisel::core::loc(L"Cleanup finished.")});
    ResultBar().Message(message);
    ResultBar().Severity(summary.errors == 0 ? Controls::InfoBarSeverity::Success
                                            : Controls::InfoBarSeverity::Warning);
    ResultBar().IsOpen(true);
    winchisel::ui::show_toast(summary.errors == 0 ? Controls::InfoBarSeverity::Success
                                                 : Controls::InfoBarSeverity::Warning,
        std::wstring(winchisel::ui::tr(L"Cleanup").c_str()), message);
}

winrt::fire_and_forget CleanupPage::confirm_and_clean() {
    auto error_weak = get_weak();
    winrt::Microsoft::UI::Dispatching::DispatcherQueue error_queue{nullptr};
    try {
        error_queue = DispatcherQueue();
    } catch (...) {
    }
    try {
        auto error_lifetime = get_strong();
        if (busy_) co_return;
        std::vector<Row*> selected;
        for (auto& row : rows_) {
            if (row.box && row.box.IsEnabled() && winrt::unbox_value_or<bool>(row.box.IsChecked(), false)) {
                selected.push_back(&row);
            }
        }
        if (selected.empty()) {
            winchisel::ui::show_toast(Controls::InfoBarSeverity::Informational,
                std::wstring(winchisel::ui::tr(L"Cleanup").c_str()),
                std::wstring(winchisel::ui::tr(L"Nothing selected.").c_str()));
            co_return;
        }
        std::uint64_t total{};
        bool has_previous{};
        bool has_update{};
        for (auto const* row : selected) {
            for (auto const& entry : scan_) {
                if (entry.category == row->category) {
                    total += entry.bytes;
                    break;
                }
            }
            has_previous = has_previous || row->category == winchisel::core::CleanupCategory::previous_installations;
            has_update = has_update || row->category == winchisel::core::CleanupCategory::update_cleanup;
        }
        winchisel::core::DialogSlot dialog_slot;
        if (!winchisel::ui::dialog_available(dialog_slot)) co_return;
        Controls::ContentDialog dialog;
        dialog.XamlRoot(XamlRoot());
        dialog.Title(box_value(winchisel::ui::tr(L"Clean selected categories?")));
        auto content = Controls::StackPanel();
        content.Spacing(8);
        auto headline = Controls::TextBlock();
        headline.Text(hstring{std::wstring(format_bytes(total).c_str()) + L" " +
                                std::wstring(winchisel::ui::tr(L"Reclaimable:").c_str())});
        headline.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
        content.Children().Append(headline);
        auto list = Controls::StackPanel();
        list.Spacing(2);
        for (auto const* row : selected) {
            auto line = Controls::TextBlock();
            line.Text(hstring{L"\u2022 " + std::wstring(category_title(row->category).c_str())});
            line.TextWrapping(TextWrapping::Wrap);
            list.Children().Append(line);
        }
        auto scroll = Controls::ScrollViewer();
        scroll.MaxHeight(240);
        scroll.Content(list);
        content.Children().Append(scroll);
        if (has_previous || has_update) {
            auto warning = Controls::TextBlock();
            warning.Text(winchisel::ui::tr(
                L"This permanently deletes the selected files. Removing previous installations also removes the Windows rollback option."));
            warning.TextWrapping(TextWrapping::Wrap);
            warning.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
            content.Children().Append(warning);
        }
        dialog.Content(content);
        dialog.PrimaryButtonText(winchisel::ui::tr(L"Clean selected"));
        dialog.CloseButtonText(winchisel::ui::tr(L"Cancel"));
        dialog.DefaultButton(Controls::ContentDialogButton::Close);
        if (co_await dialog.ShowAsync() != Controls::ContentDialogResult::Primary) co_return;
        if (busy_) co_return;
        busy_ = true;
        ResultBar().IsOpen(false);
        auto op = std::make_shared<Operation>();
        op->job = Job::cleaning;
        for (auto const* row : selected) op->selection.push_back(row->category);
        op_ = op;
        Status().Text(winchisel::ui::tr(L"Cleaning..."));
        update_chrome();
        std::thread([op] {
            try {
                op->clean_result = winchisel::platform::clean_cleanup(
                    op->selection,
                    [op](winchisel::core::CleanupCategory, std::string_view file) {
                        std::lock_guard lock(op->progress_mutex);
                        op->progress_file = std::string(file);
                    },
                    op->cancel);
            } catch (...) {
                op->clean_result =
                    std::unexpected(winchisel::core::Error{.detail = "Cleanup failed unexpectedly."});
            }
            op->done.store(true);
        }).detach();
        poll_timer_.Start();
    } catch (...) {
        winchisel::ui::report_async_error(error_queue, [error_weak](winrt::hstring const& text) {
            if (auto self = error_weak.get()) {
                self->busy_ = false;
                self->update_chrome();
                self->show_write_error(to_string(text));
            }
        });
    }
}

void CleanupPage::render() {
    // Keep the user's checkboxes across rescans (post-clean refresh).
    std::vector<std::pair<winchisel::core::CleanupCategory, bool>> previous;
    for (auto const& row : rows_) {
        if (row.box) previous.emplace_back(row.category, winrt::unbox_value_or<bool>(row.box.IsChecked(), false));
    }
    rows_.clear();
    Items().Children().Clear();
    auto checked_for = [&](winchisel::core::CleanupCategory category, bool fallback) {
        for (auto const& [id, checked] : previous) {
            if (id == category) return checked;
        }
        return fallback;
    };
    for (auto const& entry : scan_) {
        if (winchisel::core::cleanup_is_phase_b(entry.category) && !phase_b_visible_) continue;
        auto box = Controls::CheckBox();
        box.Content(box_value(category_title(entry.category)));
        const bool admin_locked =
            winchisel::core::cleanup_is_phase_b(entry.category) && !elevated_;
        box.IsEnabled(!admin_locked);
        const bool fallback = !winchisel::core::cleanup_is_phase_b(entry.category) && entry.bytes > 0;
        box.IsChecked(admin_locked ? false : checked_for(entry.category, fallback));
        if (admin_locked) {
            Controls::ToolTipService::SetToolTip(box, box_value(winchisel::ui::tr(L"Requires administrator")));
        }
        auto size = Controls::TextBlock();
        size.VerticalAlignment(VerticalAlignment::Center);
        size.Style(Application::Current()
                       .Resources()
                       .Lookup(box_value(L"BodyStrongTextBlockStyle"))
                       .try_as<winrt::Microsoft::UI::Xaml::Style>());
        size.Text(entry.size_unknown ? winchisel::ui::tr(L"Runs DISM; size is determined during cleanup.")
                                     : format_bytes(entry.bytes));
        auto header = Controls::Grid();
        header.ColumnDefinitions().Append(Controls::ColumnDefinition());
        auto size_column = Controls::ColumnDefinition();
        size_column.Width(GridLength{0, GridUnitType::Auto});
        header.ColumnDefinitions().Append(size_column);
        header.ColumnSpacing(12);
        header.Children().Append(box);
        Controls::Grid::SetColumn(size, 1);
        header.Children().Append(size);
        auto card = setting_card(header, category_description(entry.category));
        card.Tag(box_value(
            std::wstring(category_title(entry.category).c_str()) + L" " +
            std::wstring(category_description(entry.category).c_str())));
        Items().Children().Append(card);
        rows_.push_back(Row{entry.category, box, size, card});
    }
}

void CleanupPage::update_chrome() {
    const bool has_scan = !scan_.empty();
    RefreshButton().IsEnabled(!busy_);
    CleanButton().IsEnabled(!busy_ && has_scan);
    CancelButton().IsEnabled(busy_);
    AdminBox().Visibility(
        phase_b_visible_ && !elevated_ ? Visibility::Visible : Visibility::Collapsed);
}

}  // namespace winrt::Winchisel::implementation
