#include "pch.h"
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
#include "ExtrasPage.xaml.h"
#include "SettingsPage.xaml.h"
#include "Localization.hpp"
#include "winchisel/application/session.hpp"
#include "winchisel/core/i18n.hpp"
#include "winchisel/core/navigation.hpp"
#include "winchisel/platform/shell.hpp"
#include "winchisel/platform/system.hpp"
#include "winchisel/platform/update.hpp"

#include <microsoft.ui.xaml.window.h>
#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Microsoft.UI.Interop.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
#include <algorithm>
#include <filesystem>


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
    std::wstring executable(32768, L'\0');
    executable.resize(GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size())));
    return std::filesystem::path(executable).parent_path() / L"assets" / name;
}

winchisel::core::Screen screen_from_tag(winrt::hstring const& tag) {
    if (tag == L"debloater") return winchisel::core::Screen::debloater;
    if (tag == L"performance") return winchisel::core::Screen::performance;
    if (tag == L"privacy_security") return winchisel::core::Screen::privacy_security;
    if (tag == L"downloads") return winchisel::core::Screen::downloads;
    if (tag == L"processes") return winchisel::core::Screen::processes;
    if (tag == L"latency") return winchisel::core::Screen::latency;
    if (tag == L"extras") return winchisel::core::Screen::extras;
    if (tag == L"settings") return winchisel::core::Screen::settings;
    return winchisel::core::Screen::home;
}

}  // namespace

MainWindow::MainWindow() {
    InitializeComponent();
    SystemBackdrop(Media::MicaBackdrop());
    ExtendsContentIntoTitleBar(true);
    SetTitleBar(AppTitleBar());
    apply_theme();
    auto app_window = app_window_from(*this);
    const auto icon = asset_path(L"icon.ico");
    app_window.SetIcon(icon.c_str());
    app_window.SetTaskbarIcon(icon.c_str());
    auto logo = asset_path(L"icon.png").generic_wstring();
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
    Closed([](auto&&, auto&&) {
        winchisel::ui::language_reload() = {};
        winchisel::ui::theme_reload() = {};
        winchisel::ui::toast_handler() = {};
    });
    winchisel::ui::language_reload() = [this] { reload_language(); };
    winchisel::ui::theme_reload() = [this] { apply_theme(); };
    winchisel::ui::toast_handler() = [this](auto severity, auto title, auto message) {
        ToastBar().Severity(severity);
        ToastBar().Title(hstring{title});
        ToastBar().Message(hstring{message});
        ToastBar().IsOpen(true);
    };
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
        auto home = make<HomePage>(); pages_.emplace(L"home", home); ContentFrame().Content(home);
    }
}

void MainWindow::update_titlebar_inset() {
    const auto scale = AppTitleBar().XamlRoot() ? AppTitleBar().XamlRoot().RasterizationScale() : 1.0;
    TitleBarActions().Margin({0, 0, app_window_from(*this).TitleBar().RightInset() / scale + 8, 0});
}

void MainWindow::Update_Click(IInspectable const&, RoutedEventArgs const&) { CheckForUpdates(true); }

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
    check_for_updates(manual);
}

winrt::fire_and_forget MainWindow::check_for_updates(bool manual) {
    auto lifetime = get_strong();
    update_check_running_ = true;
    UpdateButton().IsEnabled(false);
    winrt::apartment_context ui_thread;
    co_await winrt::resume_background();
    auto manifest = winchisel::platform::check_github_latest_release();
    co_await ui_thread;
    if (!manifest) {
        update_check_running_ = false; UpdateButton().IsEnabled(true);
        if (manual) winchisel::ui::show_toast(Controls::InfoBarSeverity::Error, L"Update check failed", L"The latest release could not be checked.");
        co_return;
    }
    if (!winchisel::platform::is_newer_version(manifest->version, winchisel::platform::current_app_version())) {
        update_check_running_ = false; UpdateButton().IsEnabled(true);
        if (manual) winchisel::ui::show_toast(Controls::InfoBarSeverity::Success, L"Winchisel is up to date", L"You already have the latest version.");
        co_return;
    }
    const auto artifact_id = winchisel::platform::update_artifact_id();
    const auto artifact = std::ranges::find_if(manifest->artifacts, [artifact_id](auto const& item) { return item.id == artifact_id; });
    if (artifact == manifest->artifacts.end()) {
        update_check_running_ = false; UpdateButton().IsEnabled(true);
        if (manual) winchisel::ui::show_toast(Controls::InfoBarSeverity::Error, L"Update unavailable", L"No compatible update package was found.");
        co_return;
    }
    Controls::ContentDialog dialog;
    dialog.XamlRoot(Content().XamlRoot());
    dialog.Title(box_value(hstring{winchisel::core::loc(L"Winchisel update available")}));
    dialog.Content(box_value(L"Version " + to_hstring(manifest->version) + L" is available. Download and install it now?"));
    dialog.PrimaryButtonText(hstring{winchisel::core::loc(L"Update")});
    dialog.CloseButtonText(hstring{winchisel::core::loc(L"Later")});
    if (co_await dialog.ShowAsync() != Controls::ContentDialogResult::Primary) {
        update_check_running_ = false; UpdateButton().IsEnabled(true); co_return;
    }
    co_await winrt::resume_background();
    auto staged = winchisel::platform::stage_release_artifact(*manifest, artifact_id);
    if (!staged) {
        co_await ui_thread;
        update_check_running_ = false; UpdateButton().IsEnabled(true);
        winchisel::platform::show_error_message(L"The update could not be downloaded or verified."); co_return;
    }
    auto launched = winchisel::platform::launch_staged_update(*staged, *artifact);
    co_await ui_thread;
    if (!launched) {
        update_check_running_ = false; UpdateButton().IsEnabled(true);
        winchisel::platform::show_error_message(L"The update installer could not be started."); co_return;
    }
    Close();
}

void MainWindow::apply_theme() {
    const auto theme = winchisel::application::Session::instance().settings().theme;
    Nav().RequestedTheme(theme == winchisel::core::Theme::light ? ElementTheme::Light
        : theme == winchisel::core::Theme::dark ? ElementTheme::Dark : ElementTheme::Default);
    apply_titlebar_theme();
}

void MainWindow::apply_titlebar_theme() {
    const bool dark = Nav().ActualTheme() == ElementTheme::Dark;
    auto titlebar = app_window_from(*this).TitleBar();
    titlebar.BackgroundColor(Windows::UI::Colors::Transparent());
    titlebar.ButtonBackgroundColor(Windows::UI::Colors::Transparent());
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
    if (const auto existing = pages_.find(key); existing != pages_.end()) { ContentFrame().Content(existing->second); return; }
    if (auto page = make_page(tag)) { pages_.emplace(key, page); ContentFrame().Content(page); }
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
    else if (tag == L"extras") page = make<ExtrasPage>();
    else if (tag == L"settings") page = make<SettingsPage>();
    if (page) {
        page.Loaded([page](auto&&, auto&&) { winchisel::ui::localize_tree(page); });
    }
    return page;
}

void MainWindow::localize_nav() {
    auto label = [](winrt::hstring const& tag) -> std::wstring {
        if (tag == L"debloater") return winchisel::core::loc(L"Debloater");
        if (tag == L"performance") return winchisel::core::loc(L"Performance");
        if (tag == L"privacy_security") return winchisel::core::loc(L"Privacy & Security");
        if (tag == L"downloads") return winchisel::core::loc(L"Downloads");
        if (tag == L"processes") return winchisel::core::loc(L"Processes");
        if (tag == L"latency") return winchisel::core::loc(L"Latency");
        if (tag == L"extras") return winchisel::core::loc(L"Extras");
        if (tag == L"settings") return winchisel::core::loc(L"Settings");
        return winchisel::core::loc(L"Home");
    };
    if (auto items = Nav().MenuItems()) {
        for (std::uint32_t index = 0; index < items.Size(); ++index) {
            if (auto item = items.GetAt(index).try_as<Controls::NavigationViewItem>()) {
                item.Content(box_value(hstring{label(winrt::unbox_value_or<winrt::hstring>(item.Tag(), L"home"))}));
            }
        }
    }
}

void MainWindow::reload_language() {
    localize_nav();
    pages_.clear();
    auto item = Nav().SelectedItem().try_as<Controls::NavigationViewItem>();
    const auto tag = item ? winrt::unbox_value_or<winrt::hstring>(item.Tag(), L"home") : L"home";
    if (auto page = make_page(tag)) {
        pages_.emplace(std::wstring(tag.c_str()), page);
        ContentFrame().Content(page);
    }
}

}  // namespace winrt::Winchisel::implementation
