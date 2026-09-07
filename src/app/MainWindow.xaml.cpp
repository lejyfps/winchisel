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
#include "winchisel/application/session.hpp"
#include "winchisel/core/navigation.hpp"

#include <microsoft.ui.xaml.window.h>
#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Microsoft.UI.Interop.h>


using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace winrt::Winchisel::implementation {
namespace {

Microsoft::UI::Windowing::AppWindow app_window_from(Window const& window) {
    HWND hwnd{};
    window.as<IWindowNative>()->get_WindowHandle(&hwnd);
    return Microsoft::UI::Windowing::AppWindow::GetFromWindowId(Microsoft::UI::GetWindowIdFromWindow(hwnd));
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
    auto app_window = app_window_from(*this);
    app_window.Resize({1280, 720});
    if (auto presenter = app_window.Presenter().try_as<Microsoft::UI::Windowing::OverlappedPresenter>()) {
        presenter.PreferredMinimumWidth(1100);
        presenter.PreferredMinimumHeight(650);
    }
    if (auto items = Nav().MenuItems(); items.Size() > 0) {
        Nav().SelectedItem(items.GetAt(0));
    }
    if (ContentFrame().Content() == nullptr) {
        ContentFrame().Content(make<HomePage>());
    }
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
    if (tag == L"home") {
        ContentFrame().Content(make<HomePage>());
    } else if (tag == L"debloater") {
        ContentFrame().Content(make<DebloaterPage>());
    } else if (tag == L"performance") {
        ContentFrame().Content(make<PerformancePage>());
    } else if (tag == L"privacy_security") {
        ContentFrame().Content(make<PrivacyPage>());
    } else if (tag == L"downloads") {
        ContentFrame().Content(make<DownloadsPage>());
    } else if (tag == L"processes") {
        ContentFrame().Content(make<ProcessesPage>());
    } else if (tag == L"latency") {
        ContentFrame().Content(make<LatencyPage>());
    } else if (tag == L"extras") {
        ContentFrame().Content(make<ExtrasPage>());
    } else if (tag == L"settings") {
        ContentFrame().Content(make<SettingsPage>());
    }
}

}  // namespace winrt::Winchisel::implementation
