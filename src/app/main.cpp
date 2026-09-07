#include "pch.h"
#include "App.xaml.h"

#include "winchisel/application/session.hpp"
#include "winchisel/platform/system.hpp"

int WINAPI wWinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPWSTR, _In_ int) {
    winchisel::platform::boot_log("wWinMain");
    winchisel::platform::set_current_directory_to_exe();

    if (!winchisel::application::Session::instance().bootstrap()) {
        winchisel::platform::boot_log("bootstrap returned false");
        return 0;
    }

    try {
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        winchisel::platform::boot_log("Application::Start");
        winrt::Microsoft::UI::Xaml::Application::Start([](auto&&) {
            winchisel::platform::boot_log("Start callback");
            try {
                winrt::make<winrt::Winchisel::implementation::App>();
                winchisel::platform::boot_log("App constructed");
            } catch (winrt::hresult_error const& e) {
                winchisel::platform::boot_log("App ctor hresult");
                winchisel::platform::show_error_message(e.message().c_str());
            }
        });
        winchisel::platform::boot_log("Application::Start returned");
    } catch (winrt::hresult_error const& e) {
        winchisel::platform::boot_log("hresult_error");
        const auto msg = e.message();
        winchisel::platform::show_error_message(msg.c_str());
        return 1;
    } catch (std::exception const& e) {
        winchisel::platform::boot_log(e.what());
        winchisel::platform::show_error_message(L"Winchisel failed to start.");
        return 1;
    } catch (...) {
        winchisel::platform::boot_log("unknown exception");
        winchisel::platform::show_error_message(L"Winchisel failed to start.");
        return 1;
    }
    return 0;
}
