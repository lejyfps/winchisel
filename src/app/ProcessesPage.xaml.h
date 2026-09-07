#pragma once

#include "ProcessesPage.g.h"
#include "ProcessesPage.xaml.g.h"

namespace winrt::Winchisel::implementation {

struct ProcessesPage : ProcessesPageT<ProcessesPage> {
    ProcessesPage();
    void RefreshClick(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
    void FilterChanged(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
private:
    struct ProcessRow { std::uint32_t pid{}; std::uint32_t parent{}; std::wstring name, priority, affinity, status; };
    std::vector<ProcessRow> rows_;
    bool initialized_{};
    void load_processes();
    void render_processes();
};

}  // namespace winrt::Winchisel::implementation

namespace winrt::Winchisel::factory_implementation {

struct ProcessesPage : ProcessesPageT<ProcessesPage, implementation::ProcessesPage> {};

}  // namespace winrt::Winchisel::factory_implementation
