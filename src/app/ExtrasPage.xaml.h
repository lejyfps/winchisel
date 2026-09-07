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
    bool loading_{};
    void load_states();
    bool read_dword(HKEY,wchar_t const*,wchar_t const*,DWORD&);
    bool write_dword(HKEY,wchar_t const*,wchar_t const*,std::optional<DWORD>);
    bool read_string(HKEY,wchar_t const*,wchar_t const*,std::wstring&);
    bool write_string(HKEY,wchar_t const*,wchar_t const*,std::wstring const&);
    void show_result(bool,std::wstring const&);
};

}  // namespace winrt::Winchisel::implementation

namespace winrt::Winchisel::factory_implementation {

struct ExtrasPage : ExtrasPageT<ExtrasPage, implementation::ExtrasPage> {};

}  // namespace winrt::Winchisel::factory_implementation
