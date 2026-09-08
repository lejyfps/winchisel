#include "pch.h"
#include "App.xaml.h"
#include "MainWindow.xaml.h"
#include "winchisel/platform/system.hpp"
#include "winchisel/platform/update.hpp"
#include "winchisel/application/session.hpp"

#include <algorithm>

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
    window_ = make<MainWindow>();
    window_.Activate();
    if (winchisel::application::Session::instance().settings().check_updates_on_startup) check_for_updates();
}

winrt::fire_and_forget App::check_for_updates() {
    auto lifetime = get_strong();
    winrt::apartment_context ui_thread;
    co_await winrt::resume_background();
    auto manifest = winchisel::platform::check_github_latest_release();
    if (!manifest || !winchisel::platform::is_newer_version(manifest->version, winchisel::platform::current_app_version())) co_return;
    const auto artifact_id = winchisel::platform::update_artifact_id();
    const auto artifact = std::ranges::find_if(manifest->artifacts, [artifact_id](auto const& item) { return item.id == artifact_id; });
    if (artifact == manifest->artifacts.end()) co_return;
    co_await ui_thread;
    Controls::ContentDialog dialog;
    dialog.XamlRoot(lifetime->window_.Content().XamlRoot());
    dialog.Title(box_value(L"Winchisel update available"));
    dialog.Content(box_value(L"Version " + to_hstring(manifest->version) + L" is available. Download and install it now?"));
    dialog.PrimaryButtonText(L"Update"); dialog.CloseButtonText(L"Later");
    if (co_await dialog.ShowAsync() != Controls::ContentDialogResult::Primary) co_return;
    co_await winrt::resume_background();
    auto staged = winchisel::platform::stage_release_artifact(*manifest, artifact_id);
    if (!staged) {
        co_await ui_thread;
        winchisel::platform::show_error_message(L"The update could not be downloaded or verified.");
        co_return;
    }
    auto launched = winchisel::platform::launch_staged_update(*staged, *artifact);
    co_await ui_thread;
    if (!launched) { winchisel::platform::show_error_message(L"The update installer could not be started."); co_return; }
    lifetime->window_.Close();
}

}  // namespace winrt::Winchisel::implementation
