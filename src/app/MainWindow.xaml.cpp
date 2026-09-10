#include "pch.h"
#include "AsyncSupport.hpp"
#include "MainWindow.xaml.h"

#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

#include "HomePage.xaml.h"
#include "DebloaterPage.xaml.h"
#include "PerformancePage.xaml.h"
#include "PrivacyPage.xaml.h"
#include "DownloadsPage.xaml.h"
#include "ProcessesPage.xaml.h"
#include "LatencyPage.xaml.h"
#include "StartupPage.xaml.h"
#include "ScheduledTasksPage.xaml.h"
#include "ExtrasPage.xaml.h"
#include "SettingsPage.xaml.h"
#include "Localization.hpp"
#include "winchisel/application/session.hpp"
#include "winchisel/core/i18n.hpp"
#include "winchisel/core/navigation.hpp"
#include "winchisel/core/tweak.hpp"
#include "winchisel/platform/shell.hpp"
#include "winchisel/platform/system.hpp"
#include "winchisel/platform/update.hpp"

#include <microsoft.ui.xaml.window.h>
#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Microsoft.UI.Interop.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
#include <algorithm>
#include <filesystem>
#include <memory>


using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace winrt::Winchisel::implementation {
namespace {

Microsoft::UI::Windowing::AppWindow app_window_from(Window const& window) {
    HWND hwnd{};
    window.as<IWindowNative>()->get_WindowHandle(&hwnd);
    return Microsoft::UI::Windowing::AppWindow::GetFromWindowId(Microsoft::UI::GetWindowIdFromWindow(hwnd));
}

std::filesystem::path asset_path(std::wstring_view name) {
    std::wstring executable(MAX_PATH, L'\0');
    for (;;) {
        const auto length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
        if (!length) return {};
        if (length < executable.size()) {
            executable.resize(length);
            break;
        }
        if (executable.size() >= 32768) return {};
        executable.resize(executable.size() * 2, L'\0');
    }
    return std::filesystem::path(executable).parent_path() / L"assets" / name;
}

// "NEW" badges share the tweak gate: visible until a version newer than the
// introducing one runs (see PerformancePage/PrivacyPage show_new_badges).
bool show_new_badges() {
    const auto current = winchisel::platform::current_app_version();
    return !winchisel::platform::is_newer_version(current, winchisel::core::k_new_tweaks_version);
}

// Pill badge in the tweak "NEW" style (accent tint + glyph + label), sized
// compact so label + badge fit the nav pane (see OpenPaneLength).
Controls::Border new_badge() {
    auto resources = Application::Current().Resources();
    auto accent = resources.Lookup(box_value(L"AccentTextFillColorPrimaryBrush")).try_as<Media::Brush>();
    Media::Brush tint{nullptr};
    if (auto solid = accent.try_as<Media::SolidColorBrush>()) {
        auto color = solid.Color();
        color.A = 0x2E;
        tint = Media::SolidColorBrush(color);
    }
    auto badge = Controls::Border();
    badge.CornerRadius({10, 10, 10, 10});
    badge.Padding({7, 1, 7, 1});
    badge.Margin({6, 0, 0, 0});
    badge.VerticalAlignment(VerticalAlignment::Center);
    badge.Background(tint);
    auto row = Controls::StackPanel();
    row.Orientation(Controls::Orientation::Horizontal);
    row.Spacing(4);
    row.VerticalAlignment(VerticalAlignment::Center);
    auto icon = Controls::FontIcon();
    icon.Glyph(hstring{L"\uE735"});
    icon.FontSize(10);
    icon.Foreground(accent);
    icon.VerticalAlignment(VerticalAlignment::Center);
    row.Children().Append(icon);
    auto caption = Controls::TextBlock();
    caption.Text(hstring{winchisel::core::loc(L"New")});
    caption.Foreground(accent);
    caption.FontSize(10);
    caption.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
    caption.VerticalAlignment(VerticalAlignment::Center);
    row.Children().Append(caption);
    badge.Child(row);
    return badge;
}

winchisel::core::Screen screen_from_tag(winrt::hstring const& tag) {
    if (tag == L"debloater") return winchisel::core::Screen::debloater;
    if (tag == L"performance") return winchisel::core::Screen::performance;
    if (tag == L"privacy_security") return winchisel::core::Screen::privacy_security;
    if (tag == L"downloads") return winchisel::core::Screen::downloads;
    if (tag == L"processes") return winchisel::core::Screen::processes;
    if (tag == L"latency") return winchisel::core::Screen::latency;
    if (tag == L"startup") return winchisel::core::Screen::startup;
    if (tag == L"scheduled_tasks") return winchisel::core::Screen::scheduled_tasks;
    if (tag == L"extras") return winchisel::core::Screen::extras;
    if (tag == L"settings") return winchisel::core::Screen::settings;
    return winchisel::core::Screen::home;
}

// First ScrollViewer in the visual tree: the main content scroller of a page.
// Used to keep the scroll position when a page is rebuilt (language switch or
// Settings badge toggles) instead of jumping back to the top.
Controls::ScrollViewer find_first_scroll_viewer(DependencyObject const& root) {
    if (!root) return nullptr;
    if (auto viewer = root.try_as<Controls::ScrollViewer>()) return viewer;
    const auto count = Media::VisualTreeHelper::GetChildrenCount(root);
    for (int index = 0; index < count; ++index) {
        if (auto found = find_first_scroll_viewer(Media::VisualTreeHelper::GetChild(root, index))) return found;
    }
    return nullptr;
}

}  // namespace

MainWindow::MainWindow() {
    InitializeComponent();
    SystemBackdrop(Media::MicaBackdrop());
    ExtendsContentIntoTitleBar(true);
    SetTitleBar(AppTitleBar());
    apply_theme();
    auto app_window = app_window_from(*this);
    update_title();
    const auto icon = asset_path(L"logo.ico");
    app_window.SetIcon(icon.c_str());
    app_window.SetTaskbarIcon(icon.c_str());
    auto logo = asset_path(L"logo.png").generic_wstring();
    Media::Imaging::BitmapImage logo_image;
    logo_image.UriSource(Windows::Foundation::Uri(L"file:///" + logo));
    AppTitleBarIcon().Source(logo_image);
    update_titlebar_inset();
    SizeChanged([weak = get_weak()](auto&&, auto&&) { if (auto self = weak.get()) self->update_titlebar_inset(); });
    app_window.Resize({1280, 720});
    if (auto area = Microsoft::UI::Windowing::DisplayArea::GetFromWindowId(
            app_window.Id(), Microsoft::UI::Windowing::DisplayAreaFallback::Primary)) {
        const auto work = area.WorkArea();
        app_window.Move({work.X + (work.Width - 1280) / 2, work.Y + (work.Height - 720) / 2});
    }
    if (auto presenter = app_window.Presenter().try_as<Microsoft::UI::Windowing::OverlappedPresenter>()) {
        presenter.PreferredMinimumWidth(1100);
        presenter.PreferredMinimumHeight(650);
    }
    localize_nav();
    Closed([this](auto&&, auto&&) {
        if (auto settings = pages_.find(L"settings"); settings != pages_.end()) {
            if (auto page = settings->second.try_as<implementation::SettingsPage>()) page->flush_pending_save();
        }
        winchisel::ui::language_reload() = {};
        winchisel::ui::theme_reload() = {};
        winchisel::ui::update_check() = {};
        winchisel::ui::toast_handler() = {};
    });
    winchisel::ui::language_reload() = [this] { reload_language(); };
    winchisel::ui::theme_reload() = [this] { apply_theme(); };
    winchisel::ui::update_check() = [this] { CheckForUpdates(true); };
    winchisel::ui::toast_handler() = [this](auto severity, auto title, auto message) {
        ToastBar().Severity(severity);
        ToastBar().Title(hstring{title});
        ToastBar().Message(hstring{message});
        ToastBar().IsOpen(true);
        toast_timer_.Stop();
        toast_timer_.Start();
    };
    toast_timer_ = DispatcherQueue().CreateTimer();
    toast_timer_.Interval(std::chrono::seconds(3));
    toast_timer_.IsRepeating(false);
    toast_timer_.Tick([weak = get_weak()](auto&&, auto&&) { if (auto self = weak.get()) self->ToastBar().IsOpen(false); });
    // Background update poller (Zed-style): silent check every 5 minutes,
    // title bar only, never a dialog. Toggleable in Settings.
    update_poll_timer_ = DispatcherQueue().CreateTimer();
    update_poll_timer_.Interval(std::chrono::minutes(5));
    update_poll_timer_.IsRepeating(true);
    update_poll_timer_.Tick([poll_weak = get_weak()](auto&&, auto&&) { if (auto self = poll_weak.get()) self->poll_for_updates(); });
    update_poll_timer_.Start();
    if (auto items = Nav().MenuItems(); items.Size() > 0) {
        Nav().SelectedItem(items.GetAt(0));
        const auto weak = get_weak();
        for (std::uint32_t index = 0; index < items.Size() && index < 9; ++index) {
            Input::KeyboardAccelerator accelerator;
            accelerator.Modifiers(Windows::System::VirtualKeyModifiers::Control);
            accelerator.Key(static_cast<Windows::System::VirtualKey>(static_cast<int>(Windows::System::VirtualKey::Number1) + index));
            accelerator.Invoked([weak, index](auto const&, Input::KeyboardAcceleratorInvokedEventArgs const& args) {
                if (auto self = weak.get(); self && index < self->Nav().MenuItems().Size()) {
                    self->Nav().SelectedItem(self->Nav().MenuItems().GetAt(index));
                    args.Handled(true);
                }
            });
            Nav().KeyboardAccelerators().Append(accelerator);
        }
    }
    if (ContentFrame().Content() == nullptr) {
        auto home = make_page(L"home"); pages_.emplace(L"home", home); touch_page(L"home"); ContentFrame().Content(home);
    }
    notify_if_updated();
}

void MainWindow::update_titlebar_inset() {
    const auto scale = AppTitleBar().XamlRoot() ? AppTitleBar().XamlRoot().RasterizationScale() : 1.0;
    TitleBarActions().Margin({0, 0, app_window_from(*this).TitleBar().RightInset() / scale + 8, 0});
}

void MainWindow::Update_Click(IInspectable const&, RoutedEventArgs const&) {
    // Reachable only when idle: every other state hides the check button.
    CheckForUpdates(true);
}

void MainWindow::show_update_idle() {
    update_check_running_ = false;
    UpdateButton().Visibility(Visibility::Visible);
    UpdateButton().IsEnabled(true);
    UpdateProgressButton().Visibility(Visibility::Collapsed);
    UpdateRestartButton().Visibility(Visibility::Collapsed);
    UpdateDismissButton().Visibility(Visibility::Collapsed);
}

void MainWindow::show_update_busy(hstring const& label) {
    UpdateButton().Visibility(Visibility::Collapsed);
    UpdateRestartButton().Visibility(Visibility::Collapsed);
    UpdateDismissButton().Visibility(Visibility::Collapsed);
    UpdateStatusLabel().Text(label);
    UpdateDownloadRing().IsIndeterminate(true);
    UpdateProgressButton().Visibility(Visibility::Visible);
}

void MainWindow::show_update_progress(unsigned percent) {
    UpdateButton().Visibility(Visibility::Collapsed);
    UpdateRestartButton().Visibility(Visibility::Collapsed);
    UpdateDismissButton().Visibility(Visibility::Collapsed);
    UpdateStatusLabel().Text(L"Downloading update … " + to_hstring(percent) + L"%");
    UpdateDownloadRing().IsIndeterminate(false);
    UpdateDownloadRing().Value(static_cast<double>(percent));
    UpdateProgressButton().Visibility(Visibility::Visible);
}

void MainWindow::show_update_ready(hstring const& version) {
    UpdateButton().Visibility(Visibility::Collapsed);
    UpdateProgressButton().Visibility(Visibility::Collapsed);
    Controls::ToolTipService::SetToolTip(UpdateRestartButton(), box_value(L"Update to Version: " + version));
    UpdateRestartButton().Visibility(Visibility::Visible);
    UpdateDismissButton().Visibility(Visibility::Visible);
}

void MainWindow::UpdateRestart_Click(IInspectable const&, RoutedEventArgs const&) {
    if (!update_ready_ || update_check_running_ || pending_staged_.empty()) return;
    update_ready_ = false;
    show_update_busy(L"Installing update …");
    install_pending_update();
}

void MainWindow::UpdateDismiss_Click(IInspectable const&, RoutedEventArgs const&) {
    // Zed's dismiss (×) on the ready pill: stand down and snooze this
    // version for automatic checks, like declining the download dialog.
    // A manual check still offers it again; staged files stay for reuse.
    if (!update_ready_ || update_check_running_ || pending_staged_.empty()) return;
    winchisel::platform::boot_log(("update dismissed v" + pending_version_).c_str());
    update_ready_ = false;
    auto dismissed = winchisel::application::Session::instance().settings();
    dismissed.dismissed_update_version = pending_version_;
    (void)winchisel::application::Session::instance().set_settings(dismissed);
    pending_staged_.clear();
    pending_artifact_ = winchisel::platform::ReleaseArtifact{};
    pending_version_.clear();
    show_update_idle();
}

winrt::fire_and_forget MainWindow::install_pending_update() {
    auto error_weak = get_weak();
    winrt::Microsoft::UI::Dispatching::DispatcherQueue error_queue{nullptr};
    try { error_queue = DispatcherQueue(); } catch (...) {}
    try {
        auto error_lifetime = get_strong();

        auto lifetime = get_strong();
        update_check_running_ = true;
        winrt::apartment_context ui_thread;
        const auto staged = pending_staged_;
        const auto artifact = pending_artifact_;
        const auto version = pending_version_;
        const bool portable = winchisel::platform::is_portable_install();
        winchisel::platform::boot_log("update launch begin");
        co_await winrt::resume_background();
        auto launched = winchisel::platform::launch_staged_update(staged, artifact);
        co_await ui_thread;
        pending_staged_.clear();
        pending_version_.clear();
        if (!launched) {
            winchisel::platform::boot_log(("update launch failed: " + launched.error().detail).c_str());
            show_update_idle();
            winchisel::platform::show_error_message(portable
                ? L"The updater could not be started. Details: %LocalAppData%\\Winchisel\\logs\\updater.log"
                : L"The update installer could not be started."); co_return;
        }
        // Portable handoff is verified end to end (replace + relaunch), so the
        // next start can confirm it. The setup installer reports back nothing
        // (its own finish page covers that case), so no note is written there.
        if (portable) winchisel::platform::note_pending_update(version);
        winchisel::platform::boot_log("update launch ok, closing app");
        Close();

    } catch (...) {
        winchisel::ui::report_async_error(error_queue, [error_weak](winrt::hstring const& text) {
            if (auto self = error_weak.get()) {
                self->show_update_idle(); (void)text;
            }
        });
    }
}

void MainWindow::notify_if_updated() {
    const auto noted = winchisel::platform::take_pending_update_note();
    if (!noted || noted->empty()) return;
    if (*noted == winchisel::platform::current_app_version()) {
        const hstring title = L"Updated to Winchisel v" + to_hstring(*noted);
        winchisel::ui::show_toast(Controls::InfoBarSeverity::Success, std::wstring(title.c_str()),
            L"Release notes: github.com/lejyfps/winchisel/releases");
        // The new version is confirmed running: drop the updater's backups
        // so only the new executable remains.
        winchisel::platform::cleanup_update_backups();
    }
}

void MainWindow::BugReport_Click(IInspectable const&, RoutedEventArgs const&) {
    if (!winchisel::platform::open_https_url(L"https://github.com/lyrx2k/winchisel/issues"))
        winchisel::ui::show_toast(Controls::InfoBarSeverity::Error, L"Winchisel", L"Could not open the bug report page.");
}

void MainWindow::Donate_Click(IInspectable const&, RoutedEventArgs const&) {
    if (!winchisel::platform::open_https_url(L"https://pally.gg/p/lejy"))
        winchisel::ui::show_toast(Controls::InfoBarSeverity::Error, L"Winchisel", L"Could not open the donation page.");
}

void MainWindow::CheckForUpdates(bool manual) {
    if (update_check_running_) {
        if (manual) winchisel::ui::show_toast(Controls::InfoBarSeverity::Informational, L"Winchisel", L"An update check is already running.");
        return;
    }
    check_for_updates(manual ? UpdateCheckMode::Manual : UpdateCheckMode::Automatic);
}

void MainWindow::poll_for_updates() {
    if (update_check_running_ || update_ready_) return;
    if (!winchisel::application::Session::instance().settings().poll_for_updates) return;
    check_for_updates(UpdateCheckMode::Silent);
}

winrt::fire_and_forget MainWindow::check_for_updates(UpdateCheckMode mode) {
    auto error_weak=get_weak();
    winrt::Microsoft::UI::Dispatching::DispatcherQueue error_queue{nullptr};
    try { error_queue=DispatcherQueue(); } catch (...) {}
    try {
        auto error_lifetime=get_strong();

    auto lifetime = get_strong();
    update_check_running_ = true;
    show_update_busy(L"Checking for updates …");
    const bool nightly_channel = winchisel::application::Session::instance().settings().nightly_updates;
    winrt::apartment_context ui_thread;
    co_await winrt::resume_background();
    auto manifest = nightly_channel ? winchisel::platform::check_github_nightly_release()
                                    : winchisel::platform::check_github_latest_release();
    co_await ui_thread;
    if (!manifest) {
        show_update_idle();
        if (mode == UpdateCheckMode::Manual) winchisel::ui::show_toast(Controls::InfoBarSeverity::Error, L"Update check failed", L"The latest release could not be checked.");
        co_return;
    }
    // Defense in depth: the stable channel (/releases/latest) excludes
    // pre-releases server-side, but never offer a nightly to a user who did
    // not opt in, even if one is returned.
    if (!nightly_channel && winchisel::platform::is_prerelease_version(manifest->version)) {
        show_update_idle();
        if (mode != UpdateCheckMode::Silent) winchisel::ui::show_toast(Controls::InfoBarSeverity::Success, L"Winchisel is up to date", L"The latest version is already running.");
        co_return;
    }
    const std::string current_version = winchisel::platform::current_app_version();
    const bool newer = winchisel::platform::is_newer_version(manifest->version, current_version);
    // Opt-out path: offer the latest stable release even when it is older
    // than a running nightly (downgrade back to stable). manifest->version
    // is regex-validated, but comparing against 0.0.0 additionally proves it
    // parses (stoul overflow) and is not the dev placeholder.
    const bool downgrade_to_stable = !nightly_channel && !newer
        && winchisel::platform::is_prerelease_version(current_version)
        && !winchisel::platform::is_prerelease_version(manifest->version)
        && winchisel::platform::is_newer_version(manifest->version, "0.0.0");
    if (!newer && !downgrade_to_stable) {
        show_update_idle();
        if (mode != UpdateCheckMode::Silent) winchisel::ui::show_toast(Controls::InfoBarSeverity::Success, L"Winchisel is up to date", L"The latest version is already running.");
        co_return;
    }
    // A dismissed version stays dismissed for automatic checks: no nagging
    // on every restart. Manual checks bypass this and offer it again.
    if (mode != UpdateCheckMode::Manual && winchisel::application::Session::instance().settings().dismissed_update_version == manifest->version) {
        winchisel::platform::boot_log(("update prompt dismissed, skipping v" + manifest->version).c_str());
        show_update_idle();
        co_return;
    }
    if (downgrade_to_stable) winchisel::platform::boot_log(("update downgrade to stable v" + manifest->version).c_str());
    update_title(manifest->version);
    if (winchisel::platform::is_packaged_install()) {
        // Store builds must update through the Store: downloading and running
        // the GitHub setup here would violate Store policy and install a
        // second copy next to the Store package. The silent poller never
        // prompts; it just logs and stands down (the Store updates itself).
        show_update_idle();
        if (mode == UpdateCheckMode::Silent) {
            winchisel::platform::boot_log("update skipped for packaged install (silent poll)");
            co_return;
        }
        winchisel::core::DialogSlot store_slot; if(!winchisel::ui::dialog_available(store_slot)){co_return;}
        Controls::ContentDialog store_dialog;
        store_dialog.XamlRoot(Content().XamlRoot());
        store_dialog.Title(box_value(hstring{winchisel::core::loc(L"Winchisel update available")}));
        store_dialog.Content(box_value(L"Version " + to_hstring(manifest->version) + hstring(L" is available. ") + hstring{winchisel::core::loc(L"This Store version updates through the Microsoft Store. Open it now to install the update?")}));
        store_dialog.PrimaryButtonText(hstring{winchisel::core::loc(L"Open Store")});
        store_dialog.CloseButtonText(hstring{winchisel::core::loc(L"Later")});
        if (co_await store_dialog.ShowAsync() == Controls::ContentDialogResult::Primary) {
            if (!winchisel::platform::open_store_updates_page())
                winchisel::ui::show_toast(Controls::InfoBarSeverity::Error, L"Winchisel", L"Could not open the Microsoft Store.");
        }
        co_return;
    }
    const auto artifact_id = winchisel::platform::update_artifact_id();
    const auto artifact = std::ranges::find_if(manifest->artifacts, [artifact_id](auto const& item) { return item.id == artifact_id; });
    if (artifact == manifest->artifacts.end()) {
        show_update_idle();
        if (mode == UpdateCheckMode::Manual) winchisel::ui::show_toast(Controls::InfoBarSeverity::Error, L"Update unavailable", L"No compatible update package was found.");
        co_return;
    }
    // The slot covers the confirm prompt only. The download itself is
    // non-modal (Zed-style): the title-bar ring tracks progress and the
    // update installs on explicit restart instead of auto-closing the app.
    // The silent poller skips the prompt and downloads straight away.
    const hstring version_text = to_hstring(manifest->version);
    if (mode != UpdateCheckMode::Silent) {
        winchisel::core::DialogSlot dialog_slot; if(!winchisel::ui::dialog_available(dialog_slot)){show_update_idle(); co_return;}
    {
        Controls::ContentDialog dialog;
        dialog.XamlRoot(Content().XamlRoot());
        dialog.Title(box_value(hstring{winchisel::core::loc(L"Winchisel update available")}));
        if (downgrade_to_stable) {
            dialog.Content(box_value(L"Version " + version_text + L" (stable) is available. You are running " + to_hstring(current_version) + L". Switch back to the stable release? It will be downloaded in the background."));
            dialog.PrimaryButtonText(hstring{winchisel::core::loc(L"Switch to stable")});
        } else {
            dialog.Content(box_value(L"Version " + version_text + L" is available. Download it in the background?"));
            dialog.PrimaryButtonText(hstring{winchisel::core::loc(L"Download")});
        }
        dialog.CloseButtonText(hstring{winchisel::core::loc(L"Later")});
        if (co_await dialog.ShowAsync() != Controls::ContentDialogResult::Primary) {
            // "Later" snoozes this version for automatic checks (no nagging
            // every restart); a manual check still offers it again.
            auto dismissed = winchisel::application::Session::instance().settings();
            dismissed.dismissed_update_version = manifest->version;
            (void)winchisel::application::Session::instance().set_settings(dismissed);
            show_update_idle(); co_return;
        }
    }
    }
    show_update_progress(0);
    auto ui_queue = DispatcherQueue();
    auto progress_weak = get_weak();
    winchisel::platform::boot_log(("update stage begin v" + manifest->version).c_str());
    co_await winrt::resume_background();
    auto staged = winchisel::platform::stage_release_artifact(*manifest, artifact_id,
        [ui_queue, progress_weak](std::uint64_t done, std::uint64_t total) {
            (void)winchisel::ui::enqueue_safe(ui_queue, [progress_weak, done, total] {
                if (auto self = progress_weak.get()) {
                    if (!total) return;
                    const auto percent = static_cast<unsigned>(done * 100 / total);
                    self->show_update_progress(percent);
                }
            });
        });
    co_await ui_thread;
    if (!staged) {
        winchisel::platform::boot_log(("update stage failed: " + staged.error().detail).c_str());
        show_update_idle();
        if (mode != UpdateCheckMode::Silent) winchisel::platform::show_error_message(L"The update could not be downloaded or verified.");
        co_return;
    }
    pending_staged_ = *staged;
    pending_artifact_ = *artifact;
    pending_version_ = manifest->version;
    update_ready_ = true;
    update_check_running_ = false;
    show_update_ready(version_text);
    // The poller found and staged this on its own: one toast so the ready
    // button in the title bar is not a silent surprise. Later polls skip
    // while update_ready_ holds, so this fires once per version.
    if (mode == UpdateCheckMode::Silent) winchisel::ui::show_toast(Controls::InfoBarSeverity::Success, L"Update ready", L"Version " + version_text + L" is ready. Restart Winchisel to install it.");
    winchisel::platform::boot_log("update staged, waiting for restart");

    } catch (...) {
        winchisel::ui::report_async_error(error_queue, [error_weak](winrt::hstring const& text) {
            if (auto self = error_weak.get()) {
                self->show_update_idle(); (void)text;
            }
        });
    }
}

void MainWindow::apply_theme() {
    const auto theme = winchisel::application::Session::instance().settings().theme;
    RootLayout().RequestedTheme(theme == winchisel::core::Theme::light ? ElementTheme::Light
        : theme == winchisel::core::Theme::dark ? ElementTheme::Dark : ElementTheme::Default);
    apply_titlebar_theme();
}

void MainWindow::apply_titlebar_theme() {
    const bool dark = RootLayout().ActualTheme() == ElementTheme::Dark;
    auto titlebar = app_window_from(*this).TitleBar();
    const auto background = Windows::UI::ColorHelper::FromArgb(255, dark ? 32 : 243, dark ? 32 : 243, dark ? 32 : 243);
    titlebar.BackgroundColor(background);
    titlebar.ButtonBackgroundColor(background);
    titlebar.ButtonHoverBackgroundColor(Windows::UI::ColorHelper::FromArgb(255, dark ? 50 : 229, dark ? 50 : 229, dark ? 50 : 229));
    titlebar.ButtonPressedBackgroundColor(Windows::UI::ColorHelper::FromArgb(255, dark ? 60 : 218, dark ? 60 : 218, dark ? 60 : 218));
    titlebar.ForegroundColor(dark ? Windows::UI::Colors::White() : Windows::UI::Colors::Black());
    titlebar.ButtonForegroundColor(dark ? Windows::UI::Colors::White() : Windows::UI::Colors::Black());
    titlebar.InactiveForegroundColor(Windows::UI::Colors::Gray());
    titlebar.ButtonInactiveForegroundColor(Windows::UI::Colors::Gray());
}

void MainWindow::Nav_SelectionChanged(
    Controls::NavigationView const&,
    Controls::NavigationViewSelectionChangedEventArgs const& args) {
    auto item = args.SelectedItem().try_as<Controls::NavigationViewItem>();
    if (!item) {
        return;
    }
    const auto tag = winrt::unbox_value_or<winrt::hstring>(item.Tag(), L"home");
    winchisel::application::Session::instance().set_screen(screen_from_tag(tag));
    const std::wstring key(tag.c_str());
    if (const auto existing = pages_.find(key); existing != pages_.end()) { touch_page(key); ContentFrame().Content(existing->second); return; }
    if (auto page = make_page(tag)) { pages_.emplace(key, page); touch_page(key); ContentFrame().Content(page); }
}

void MainWindow::touch_page(std::wstring const& key) {
    page_lru_.remove(key);
    page_lru_.push_back(key);
    while (page_lru_.size() > k_max_cached_pages) {
        auto const& oldest = page_lru_.front();
        if (oldest == key) break;
        pages_.erase(oldest);
        page_lru_.pop_front();
    }
}

FrameworkElement MainWindow::make_page(winrt::hstring const& tag) {
    FrameworkElement page{nullptr};
    if (tag == L"home") page = make<HomePage>();
    else if (tag == L"debloater") page = make<DebloaterPage>();
    else if (tag == L"performance") page = make<PerformancePage>();
    else if (tag == L"privacy_security") page = make<PrivacyPage>();
    else if (tag == L"downloads") page = make<DownloadsPage>();
    else if (tag == L"processes") page = make<ProcessesPage>();
    else if (tag == L"latency") page = make<LatencyPage>();
    else if (tag == L"startup") page = make<StartupPage>();
    else if (tag == L"scheduled_tasks") page = make<ScheduledTasksPage>();
    else if (tag == L"extras") page = make<ExtrasPage>();
    else if (tag == L"settings") page = make<SettingsPage>();
    if (page) {
        auto first_load = std::make_shared<bool>(true);
        page.Loaded([first_load](auto const& sender, auto&&) {
            if (std::exchange(*first_load, false)) winchisel::ui::localize_tree(sender);
        });
    }
    return page;
}

void MainWindow::update_title(std::optional<std::string> available) {
    auto resources = Application::Current().Resources();
    AppTitleText().Inlines().Clear();
    Documents::Run app;
    app.Text(L"Winchisel");
    AppTitleText().Inlines().Append(app);
    Documents::Run version;
    version.Text(L" [v" + to_hstring(winchisel::platform::current_app_version()) + L"]");
    version.Foreground(resources.Lookup(box_value(L"TextFillColorSecondaryBrush")).try_as<Media::Brush>());
    AppTitleText().Inlines().Append(version);
    if (available) {
        Documents::Run update;
        update.Text(L" (Update available: v" + to_hstring(*available) + L")");
        update.FontWeight(Windows::UI::Text::FontWeights::Bold());
        update.Foreground(resources.Lookup(box_value(L"SystemFillColorSuccessBrush")).try_as<Media::Brush>());
        AppTitleText().Inlines().Append(update);
    }
}

void MainWindow::localize_nav() {
    const auto direction = winchisel::core::ui_language() == winchisel::core::Language::arabic
        ? FlowDirection::RightToLeft : FlowDirection::LeftToRight;
    Nav().FlowDirection(direction);
    ContentFrame().FlowDirection(direction);
    auto label = [](winrt::hstring const& tag) -> std::wstring {
        if (tag == L"debloater") return winchisel::core::loc(L"Debloater");
        if (tag == L"performance") return winchisel::core::loc(L"Performance");
        if (tag == L"privacy_security") return winchisel::core::loc(L"Privacy & Security");
        if (tag == L"downloads") return winchisel::core::loc(L"Downloads");
        if (tag == L"processes") return winchisel::core::loc(L"Processes");
        if (tag == L"latency") return winchisel::core::loc(L"Latency");
        if (tag == L"startup") return winchisel::core::loc(L"Startup Manager");
        if (tag == L"scheduled_tasks") return winchisel::core::loc(L"Scheduled Tasks");
        if (tag == L"extras") return winchisel::core::loc(L"Extras");
        if (tag == L"settings") return winchisel::core::loc(L"Settings");
        return winchisel::core::loc(L"Home");
    };
    auto localize_items = [&](auto const& items) {
        for (std::uint32_t index = 0; index < items.Size(); ++index) {
            if (auto item = items.GetAt(index).try_as<Controls::NavigationViewItem>()) {
                item.Content(box_value(hstring{label(winrt::unbox_value_or<winrt::hstring>(item.Tag(), L"home"))}));
            }
        }
    };
    localize_items(Nav().MenuItems());
    localize_items(Nav().FooterMenuItems());
    if (show_new_badges()) {
        auto badge_items = [&](auto const& items) {
            for (std::uint32_t index = 0; index < items.Size(); ++index) {
                if (auto item = items.GetAt(index).try_as<Controls::NavigationViewItem>()) {
                    const auto tag = winrt::unbox_value_or<winrt::hstring>(item.Tag(), L"home");
                    if (tag == L"startup" || tag == L"scheduled_tasks") {
                        const auto text = hstring{label(tag)};
                        auto panel = Controls::StackPanel();
                        panel.Orientation(Controls::Orientation::Horizontal);
                        panel.VerticalAlignment(VerticalAlignment::Center);
                        auto caption = Controls::TextBlock();
                        caption.Text(text);
                        caption.VerticalAlignment(VerticalAlignment::Center);
                        panel.Children().Append(caption);
                        panel.Children().Append(new_badge());
                        item.Content(panel);
                        Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(item, text + hstring{L", new"});
                    }
                }
            }
        };
        badge_items(Nav().MenuItems());
        badge_items(Nav().FooterMenuItems());
    }
}

void MainWindow::reload_language() {
    localize_nav();
    // Rebuilding the current page (language switch, Settings badge toggles)
    // resets its ScrollViewer to the top. Carry the offset over so a toggle
    // deep down the page doesn't yank the user back up.
    double saved_offset{};
    if (auto content = ContentFrame().Content()) {
        if (auto viewer = find_first_scroll_viewer(content.try_as<DependencyObject>())) {
            saved_offset = viewer.VerticalOffset();
        }
    }
    pages_.clear();
    page_lru_.clear();
    auto item = Nav().SelectedItem().try_as<Controls::NavigationViewItem>();
    const auto tag = item ? winrt::unbox_value_or<winrt::hstring>(item.Tag(), L"home") : L"home";
    if (auto page = make_page(tag)) {
        pages_.emplace(std::wstring(tag.c_str()), page);
        if (saved_offset > 0.5) {
            page.Loaded([offset = saved_offset](auto const& sender, auto&&) {
                if (auto viewer = find_first_scroll_viewer(sender.try_as<DependencyObject>())) {
                    viewer.ChangeView(nullptr, offset, nullptr);
                }
            });
        }
        ContentFrame().Content(page);
    }
}

}  // namespace winrt::Winchisel::implementation
