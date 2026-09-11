#pragma once

#include "ExtrasPage.g.h"
#include "ExtrasPage.xaml.g.h"
#include <optional>
#include <string>
#include <vector>

namespace winrt::Winchisel::implementation {

struct ExtrasPage : ExtrasPageT<ExtrasPage> {
    ExtrasPage();
    void ToggleChanged(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
    void PowerPlanClick(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
    void UltimatePlanClick(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
private:
    enum class CommandAction { power_plan, ultimate_plan, widgets, teredo, hpet, dynamic_tick };
    enum class RegistryToggle { modern_standby, sync_provider, ctfmon, ctfmon_dll, timer_resolution, ipv6, ps7, brave, edge, long_paths, developer_mode, verbose_boot };
    struct RegistrySnapshot {
        bool modern_standby{}, sync_provider{}, brave{}, edge{}, ctfmon{}, ctfmon_dll{}, timer_resolution{}, ipv6{}, teredo{}, ps7{}, long_paths{}, developer_mode{}, verbose_boot{};
        DWORD ipv6_value{};
    };
    struct WorkResult { bool ok{}; DWORD error{ERROR_SUCCESS}; std::string note; };
    bool loading_{};
    bool command_running_{};
    void load_states();
    winrt::fire_and_forget reload_registry_states();
    RegistrySnapshot read_registry_snapshot();
    void apply_registry_snapshot(RegistrySnapshot const&);
    WorkResult do_registry_work(RegistryToggle, bool enabled);
    winrt::fire_and_forget apply_registry_toggle(RegistryToggle, winrt::Microsoft::UI::Xaml::Controls::ToggleSwitch, bool enabled);
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
