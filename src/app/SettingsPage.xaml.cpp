#include "pch.h"
#include "SettingsPage.xaml.h"

#if __has_include("SettingsPage.g.cpp")
#include "SettingsPage.g.cpp"
#endif

#include "winchisel/application/session.hpp"

using namespace winrt::Microsoft::UI::Xaml;

namespace winrt::Winchisel::implementation {

SettingsPage::SettingsPage() {
    InitializeComponent();
    const auto& settings = winchisel::application::Session::instance().settings();
    Language().SelectedIndex(settings.language == winchisel::core::Language::german ? 1 : 0);
    CheckUpdates().IsOn(settings.check_updates_on_startup);
    ShowConsole().IsOn(settings.show_console);
    Autostart().IsOn(settings.autostart_enabled);
    loading_ = false;

    save_timer_ = winrt::Microsoft::UI::Xaml::DispatcherTimer();
    save_timer_.Interval(std::chrono::milliseconds(600));
    save_timer_token_ = save_timer_.Tick([this](auto&&, auto&&) { save_settings(); });
}

SettingsPage::~SettingsPage() {
    if (save_timer_) {
        save_timer_.Tick(save_timer_token_);
        save_timer_.Stop();
    }
}

void SettingsPage::Language_SelectionChanged(
    winrt::Windows::Foundation::IInspectable const&,
    winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&) {
    queue_save();
}

void SettingsPage::Settings_Toggled(
    winrt::Windows::Foundation::IInspectable const&,
    winrt::Microsoft::UI::Xaml::RoutedEventArgs const&) {
    queue_save();
}

void SettingsPage::queue_save() {
    if (loading_) {
        return;
    }
    save_timer_.Stop();
    save_timer_.Start();
}

void SettingsPage::save_settings() {
    save_timer_.Stop();
    auto settings = winchisel::application::Session::instance().settings();
    settings.language = Language().SelectedIndex() == 1
        ? winchisel::core::Language::german
        : winchisel::core::Language::english;
    settings.check_updates_on_startup = CheckUpdates().IsOn();
    settings.show_console = ShowConsole().IsOn();
    settings.autostart_enabled = Autostart().IsOn();
    winchisel::application::Session::instance().set_settings(settings);
}

}  // namespace winrt::Winchisel::implementation
