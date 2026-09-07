#include "pch.h"
#include "App.xaml.h"

#include "winchisel/application/session.hpp"

int WINAPI wWinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPWSTR, _In_ int) {
    if (!winchisel::application::Session::instance().bootstrap()) {
        return 0;
    }

    winrt::init_apartment(winrt::apartment_type::single_threaded);
    winrt::Microsoft::UI::Xaml::Application::Start([](auto&&) {
        winrt::make<winrt::Winchisel::implementation::App>();
    });
    return 0;
}
