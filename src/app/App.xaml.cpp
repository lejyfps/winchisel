#include "pch.h"
#include "App.xaml.h"
#include "MainWindow.xaml.h"
#include "winchisel/platform/system.hpp"
#include "winchisel/application/session.hpp"

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace winrt::Winchisel::implementation {

App::App() {
    UnhandledException([](IInspectable const&, UnhandledExceptionEventArgs const& e) {
        try {
            winchisel::platform::boot_log(winrt::to_string(e.Message()).c_str());
            winchisel::platform::show_error_message(e.Message().c_str());
        } catch (...) {
        }
        // Unknown UI exceptions may leave application state inconsistent. Log
        // and report them, but let WinUI terminate instead of continuing in a
        // potentially corrupted state.
        e.Handled(false);
    });
    InitializeComponent();
}

void App::OnLaunched(LaunchActivatedEventArgs const&) {
    auto main = make<MainWindow>();
    window_ = main;
    window_.Activate();
    if (winchisel::application::Session::instance().settings().check_updates_on_startup)
        get_self<MainWindow>(main)->CheckForUpdates(false);
}

}  // namespace winrt::Winchisel::implementation
