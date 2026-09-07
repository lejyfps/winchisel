#include "pch.h"
#include "App.xaml.h"
#include "MainWindow.xaml.h"



using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace winrt::Winchisel::implementation {

App::App() {
    InitializeComponent();
}

void App::OnLaunched(LaunchActivatedEventArgs const&) {
    window_ = make<Winchisel::implementation::MainWindow>();
    window_.Activate();
}

}  // namespace winrt::Winchisel::implementation
