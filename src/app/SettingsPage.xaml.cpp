#include "pch.h"
#include "SettingsPage.xaml.h"
#include "AsyncLifetime.hpp"

#if __has_include("SettingsPage.g.cpp")
#include "SettingsPage.g.cpp"
#endif

#include "Localization.hpp"
#include "winchisel/application/session.hpp"
#include "winchisel/core/i18n.hpp"
#include "winchisel/platform/system.hpp"

#include <chrono>

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace winrt::Winchisel::implementation {

SettingsPage::SettingsPage() {
    InitializeComponent();
    const auto& settings = winchisel::application::Session::instance().settings();
    Language().SelectedIndex(static_cast<int>(settings.language));
    Theme().SelectedIndex(static_cast<int>(settings.theme));
    CheckUpdates().IsOn(settings.check_updates_on_startup);
    ShowConsole().IsOn(settings.show_console);
    Autostart().IsOn(settings.autostart_enabled);
    loading_ = false;

    save_timer_ = DispatcherTimer();
    save_timer_.Interval(std::chrono::milliseconds(600));
    save_timer_token_ = save_timer_.Tick([this](auto&&, auto&&) { save_settings(); });
    poll_timer_ = DispatcherTimer();
    poll_timer_.Interval(std::chrono::milliseconds(200));
    poll_timer_token_ = poll_timer_.Tick([this](auto&&, auto&&) { poll_worker(); });
}

SettingsPage::~SettingsPage() {
    if (save_timer_) {
        save_timer_.Tick(save_timer_token_);
        save_timer_.Stop();
    }
    if (poll_timer_) {
        poll_timer_.Tick(poll_timer_token_);
        poll_timer_.Stop();
    }
    if (action_dialog_) {
        action_dialog_.Hide();
    }
    winchisel::ui::finish_in_background(worker_);
}

void SettingsPage::Language_SelectionChanged(IInspectable const&, Controls::SelectionChangedEventArgs const&) {
    queue_save();
}

void SettingsPage::Theme_SelectionChanged(IInspectable const&, Controls::SelectionChangedEventArgs const&) {
    queue_save();
}

void SettingsPage::Settings_Toggled(IInspectable const&, RoutedEventArgs const&) {
    queue_save();
}

void SettingsPage::queue_save() {
    if (loading_) {
        return;
    }
    save_timer_.Stop();
    save_timer_.Start();
}

void SettingsPage::save_settings() {
    save_timer_.Stop();
    auto settings = winchisel::application::Session::instance().settings();
    settings.language = static_cast<winchisel::core::Language>(std::max(Language().SelectedIndex(), 0));
    settings.theme = static_cast<winchisel::core::Theme>(std::max(Theme().SelectedIndex(), 0));
    settings.check_updates_on_startup = CheckUpdates().IsOn();
    settings.show_console = ShowConsole().IsOn();
    settings.autostart_enabled = Autostart().IsOn();
    const auto previous_language = winchisel::core::ui_language();
    const auto previous_theme = winchisel::application::Session::instance().settings().theme;
    if (auto result = winchisel::application::Session::instance().set_settings(settings); !result) {
        loading_ = true;
        const auto& current = winchisel::application::Session::instance().settings();
        Language().SelectedIndex(static_cast<int>(current.language));
        Theme().SelectedIndex(static_cast<int>(current.theme));
        CheckUpdates().IsOn(current.check_updates_on_startup);
        ShowConsole().IsOn(current.show_console);
        Autostart().IsOn(current.autostart_enabled);
        loading_ = false;
        show_result(false, hstring{winchisel::core::loc(L"Settings could not be saved")} + L": " + to_hstring(result.error().detail));
        return;
    }
    if (previous_language != settings.language && winchisel::ui::language_reload()) {
        winchisel::ui::language_reload()();
    }
    if (previous_theme != settings.theme && winchisel::ui::theme_reload()) {
        winchisel::ui::theme_reload()();
    }
}

void SettingsPage::Restore_Click(IInspectable const&, RoutedEventArgs const&) { run_dialog(Action::restore); }
void SettingsPage::Repair_Click(IInspectable const&, RoutedEventArgs const&) { run_dialog(Action::repair); }
void SettingsPage::Cleanup_Click(IInspectable const&, RoutedEventArgs const&) { start_cleanup(); }
void SettingsPage::Temp_Click(IInspectable const&, RoutedEventArgs const&) { run_dialog(Action::temp); }

void SettingsPage::start_cleanup() {
    if (action_ != Action::none) {
        return;
    }
    action_ = Action::cleanup;
    ResultBar().IsOpen(false);
    set_busy(true);
    worker_ = std::async(std::launch::async, [] { return winchisel::platform::run_disk_cleanup(); });
    poll_timer_.Start();
}

void SettingsPage::poll_worker() {
    if (!worker_.valid() || worker_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        return;
    }
    auto result = worker_.get();
    action_ = Action::none;
    poll_timer_.Stop();
    set_busy(false);
    show_result(static_cast<bool>(result), result ? L"Disk Cleanup finished successfully." : L"Disk Cleanup failed.");
}

void SettingsPage::set_busy(bool busy) {
    Loading().Visibility(busy && action_ == Action::cleanup ? Visibility::Visible : Visibility::Collapsed);
    RestoreButton().IsEnabled(!busy);
    RepairButton().IsEnabled(!busy);
    CleanupButton().IsEnabled(!busy);
    TempButton().IsEnabled(!busy);
}

void SettingsPage::show_result(bool ok, hstring const& text) {
    ResultBar().Severity(ok ? Controls::InfoBarSeverity::Success : Controls::InfoBarSeverity::Error);
    ResultBar().Message(text);
    ResultBar().IsOpen(true);
    winchisel::ui::show_toast(ok ? Controls::InfoBarSeverity::Success : Controls::InfoBarSeverity::Error,
        ok ? winchisel::core::loc(L"Settings") : winchisel::core::loc(L"Could not apply setting"),
        std::wstring(text));
}

void SettingsPage::set_stage(hstring const& text) {
    if (dialog_stage_) {
        dialog_stage_.Text(text);
    }
}

void SettingsPage::append_log(std::string_view text) {
    if (text.empty() || !dialog_log_) {
        return;
    }
    if (log_lines_.empty() && dialog_log_.Text() == L"Waiting for output...") {
        dialog_log_.Text(L"");
    }
    log_lines_.emplace_back(text);
    if (log_lines_.size() > 12) {
        log_lines_.erase(log_lines_.begin(), log_lines_.begin() + static_cast<std::ptrdiff_t>(log_lines_.size() - 12));
    }
    std::string joined;
    for (std::size_t index{}; index < log_lines_.size(); ++index) {
        if (index) {
            joined += "\n";
        }
        joined += log_lines_[index];
    }
    dialog_log_.Text(to_hstring(joined));
    if (dialog_scroll_) {
        dialog_scroll_.UpdateLayout();
        dialog_scroll_.ChangeView(nullptr, dialog_scroll_.ScrollableHeight(), nullptr);
    }
}

void SettingsPage::finish_dialog(winchisel::core::Result<void> const& result) {
    const bool ok = static_cast<bool>(result);
    hstring message = action_ == Action::restore
        ? (ok ? L"Restore point created successfully." : L"Restore point failed.")
        : action_ == Action::repair
        ? (ok ? L"System repair finished successfully." : L"System repair failed.")
        : (ok ? L"Temporary files were removed successfully." : L"Temporary file cleanup failed.");
    hstring fail_title = action_ == Action::restore ? L"Restore point failed"
        : action_ == Action::repair ? L"System repair failed"
        : L"Temporary Files - Remove";
    if (!ok && !result.error().detail.empty()) {
        append_log(result.error().detail);
    }
    if (dialog_ring_) {
        dialog_ring_.IsActive(false);
        dialog_ring_.Visibility(Visibility::Collapsed);
    }
    set_stage(message);
    if (action_dialog_) {
        if (!ok) {
            action_dialog_.Title(box_value(fail_title));
        }
        action_dialog_.CloseButtonText(L"Close");
    }
    show_result(ok, message);
    action_ = Action::none;
    set_busy(false);
}

fire_and_forget SettingsPage::run_dialog(Action action) {
    auto lifetime = get_strong();
    if (action_ != Action::none) {
        co_return;
    }
    action_ = action;
    ResultBar().IsOpen(false);
    set_busy(true);
    log_lines_.clear();

    const bool with_log = action == Action::repair || action == Action::temp;
    hstring title = action == Action::restore ? L"Create Restore Point"
        : action == Action::repair ? L"System Repair"
        : L"Temporary Files - Remove";
    hstring initial = action == Action::restore ? L"Creating restore point. This can take a moment..."
        : action == Action::repair ? L"Repair running..."
        : L"Removing temporary files...";

    auto content = Controls::StackPanel();
    content.Spacing(12);
    content.MinWidth(480);
    dialog_ring_ = Controls::ProgressRing();
    dialog_ring_.IsActive(true);
    dialog_ring_.Width(32);
    dialog_ring_.Height(32);
    dialog_ring_.HorizontalAlignment(HorizontalAlignment::Left);
    auto header = Controls::StackPanel();
    header.Orientation(Controls::Orientation::Horizontal);
    header.Spacing(12);
    header.Children().Append(dialog_ring_);
    auto header_text = Controls::StackPanel();
    dialog_stage_ = Controls::TextBlock();
    dialog_stage_.Text(initial);
    dialog_stage_.TextWrapping(TextWrapping::Wrap);
    header_text.Children().Append(dialog_stage_);
    auto hint = Controls::TextBlock();
    hint.Text(L"This can take a while. Keep the window open until it finishes.");
    hint.TextWrapping(TextWrapping::Wrap);
    hint.Style(Application::Current().Resources().Lookup(box_value(L"CaptionTextBlockStyle")).try_as<Microsoft::UI::Xaml::Style>());
    header_text.Children().Append(hint);
    header.Children().Append(header_text);
    content.Children().Append(header);

    if (with_log) {
        auto log_title = Controls::TextBlock();
        log_title.Text(L"Live log");
        log_title.Style(Application::Current().Resources().Lookup(box_value(L"BodyStrongTextBlockStyle")).try_as<Microsoft::UI::Xaml::Style>());
        content.Children().Append(log_title);
        dialog_scroll_ = Controls::ScrollViewer();
        dialog_scroll_.Height(180);
        dialog_log_ = Controls::TextBlock();
        dialog_log_.Text(L"Waiting for output...");
        dialog_log_.TextWrapping(TextWrapping::Wrap);
        dialog_log_.FontFamily(Media::FontFamily(L"Consolas"));
        dialog_scroll_.Content(dialog_log_);
        content.Children().Append(dialog_scroll_);
    } else {
        dialog_log_ = nullptr;
        dialog_scroll_ = nullptr;
    }

    action_dialog_ = Controls::ContentDialog();
    action_dialog_.XamlRoot(XamlRoot());
    action_dialog_.Title(box_value(title));
    action_dialog_.Content(content);

    auto queue = DispatcherQueue();
    auto weak = get_weak();
    worker_ = std::async(std::launch::async, [action, queue, weak] {
        auto progress = [queue, weak](bool is_stage, std::string_view text) {
            std::string copy{text};
            (void)queue.TryEnqueue([weak, is_stage, copy = std::move(copy)] {
                if (auto page = weak.get()) {
                    if (is_stage) {
                        page->set_stage(to_hstring(copy));
                    } else {
                        page->append_log(copy);
                    }
                }
            });
        };
        winchisel::core::Result<void> result{};
        switch (action) {
        case Action::restore: result = winchisel::platform::create_restore_point(); break;
        case Action::repair: result = winchisel::platform::run_system_repair(progress); break;
        case Action::temp: result = winchisel::platform::remove_temp_files(progress); break;
        default: break;
        }
        (void)queue.TryEnqueue([weak, result] {
            if (auto page = weak.get()) {
                page->finish_dialog(result);
            }
        });
        return result;
    });

    co_await action_dialog_.ShowAsync();
    action_dialog_ = nullptr;
    dialog_ring_ = nullptr;
    dialog_stage_ = nullptr;
    dialog_log_ = nullptr;
    dialog_scroll_ = nullptr;
}

}  // namespace winrt::Winchisel::implementation
