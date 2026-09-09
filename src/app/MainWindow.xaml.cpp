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
        winchisel::ui::toast_handler() = {};
    });
    winchisel::ui::language_reload() = [this] { reload_language(); };
    winchisel::ui::theme_reload() = [this] { apply_theme(); };
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
    auto error_weak=get_weak();
    winrt::Microsoft::UI::Dispatching::DispatcherQueue error_queue{nullptr};
    try { error_queue=DispatcherQueue(); } catch (...) {}
    try {
        auto error_lifetime=get_strong();

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
        winchisel::ui::show_toast(Controls::InfoBarSeverity::Success, L"Winchisel is up to date", L"The latest version is already running.");
        co_return;
    }
    update_title(manifest->version);
    if (winchisel::platform::is_packaged_install()) {
        // Store builds must update through the Store: downloading and running
        // the GitHub setup here would violate Store policy and install a
        // second copy next to the Store package. Notify and deep-link instead.
        update_check_running_ = false; UpdateButton().IsEnabled(true);
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
        update_check_running_ = false; UpdateButton().IsEnabled(true);
        if (manual) winchisel::ui::show_toast(Controls::InfoBarSeverity::Error, L"Update unavailable", L"No compatible update package was found.");
        co_return;
    }
    // The dialog slot is scoped to the prompt only so other dialogs stay
    // available during the potentially long download and install.
    {
        winchisel::core::DialogSlot dialog_slot; if(!winchisel::ui::dialog_available(dialog_slot)){update_check_running_=false; UpdateButton().IsEnabled(true); co_return;}
        Controls::ContentDialog dialog;
        dialog.XamlRoot(Content().XamlRoot());
        dialog.Title(box_value(hstring{winchisel::core::loc(L"Winchisel update available")}));
        dialog.Content(box_value(L"Version " + to_hstring(manifest->version) + L" is available. Download and install it now?"));
        dialog.PrimaryButtonText(hstring{winchisel::core::loc(L"Update")});
        dialog.CloseButtonText(hstring{winchisel::core::loc(L"Later")});
        if (co_await dialog.ShowAsync() != Controls::ContentDialogResult::Primary) {
            update_check_running_ = false; UpdateButton().IsEnabled(true); co_return;
        }
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

    } catch (...) {
        winchisel::ui::report_async_error(error_queue, [error_weak](winrt::hstring const& text) {
            if (auto self=error_weak.get()) { self->update_check_running_=false; self->UpdateButton().IsEnabled(true); (void)text; }
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
}

void MainWindow::reload_language() {
    localize_nav();
    pages_.clear();
    page_lru_.clear();
    auto item = Nav().SelectedItem().try_as<Controls::NavigationViewItem>();
    const auto tag = item ? winrt::unbox_value_or<winrt::hstring>(item.Tag(), L"home") : L"home";
    if (auto page = make_page(tag)) {
        pages_.emplace(std::wstring(tag.c_str()), page);
        ContentFrame().Content(page);
    }
}

}  // namespace winrt::Winchisel::implementation
