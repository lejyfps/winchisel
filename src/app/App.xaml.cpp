#include "pch.h"
#include "App.xaml.h"
#include "MainWindow.xaml.h"
#include "winchisel/platform/system.hpp"

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
        e.Handled(true);
    });
    InitializeComponent();
}

void App::OnLaunched(LaunchActivatedEventArgs const&) {
    window_ = make<MainWindow>();
    window_.Activate();
}

}  // namespace winrt::Winchisel::implementation
