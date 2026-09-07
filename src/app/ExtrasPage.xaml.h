#pragma once

#include "ExtrasPage.g.h"
#include "ExtrasPage.xaml.g.h"
#include <optional>

namespace winrt::Winchisel::implementation {

struct ExtrasPage : ExtrasPageT<ExtrasPage> {
    ExtrasPage();
    void ToggleChanged(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
    void PowerPlanClick(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
private:
    enum class CommandAction { power_plan, widgets, teredo, hpet };
    bool loading_{};
    bool command_running_{};
    void load_states();
    bool read_dword(HKEY,wchar_t const*,wchar_t const*,DWORD&);
    bool write_dword(HKEY,wchar_t const*,wchar_t const*,std::optional<DWORD>);
    bool read_string(HKEY,wchar_t const*,wchar_t const*,std::wstring&);
    bool write_string(HKEY,wchar_t const*,wchar_t const*,std::wstring const&);
    void show_result(bool,std::wstring const&);
    winrt::fire_and_forget run_command(CommandAction, bool enabled = false);
    winrt::fire_and_forget load_command_states();
    void set_command_busy(bool);
};

}  // namespace winrt::Winchisel::implementation

namespace winrt::Winchisel::factory_implementation {

struct ExtrasPage : ExtrasPageT<ExtrasPage, implementation::ExtrasPage> {};

}  // namespace winrt::Winchisel::factory_implementation
