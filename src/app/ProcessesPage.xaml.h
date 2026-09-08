#pragma once

#include "ProcessesPage.g.h"
#include "ProcessesPage.xaml.g.h"
#include <unordered_map>
#include <unordered_set>
#include <optional>

namespace winrt::Winchisel::implementation {

struct ProcessesPage : ProcessesPageT<ProcessesPage> {
    ProcessesPage();
    void RefreshClick(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
    void FilterChanged(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
    void ProcessSelectionChanged(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
    void SortPid(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
    void SortName(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
    void SortCpu(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
    void SortPriority(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
    void SortAffinity(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
    void SortStatus(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
private:
    enum class SortColumn { pid,name,cpu,priority,affinity,status };
    struct ProcessRow { std::uint32_t pid{}; std::uint32_t parent{}; std::size_t depth{}; bool has_children{}; double cpu{}; std::wstring name,path,priority,affinity,status; };
    std::vector<ProcessRow> rows_;
    std::unordered_map<std::uint32_t, std::uint64_t> previous_process_times_;
    std::unordered_map<std::uint32_t, std::uint64_t> previous_process_created_;
    std::uint64_t previous_system_time_{};
    std::unordered_set<std::uint32_t> expanded_;
    std::vector<std::uint32_t> rendered_pids_;
    SortColumn sort_column_{SortColumn::name};
    bool ascending_{true};
    winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer refresh_timer_{nullptr};
    std::uint32_t open_overlays_{};
    bool refresh_pending_{};
    std::uint32_t selected_pid_{};
    bool restoring_selection_{};
    bool initialized_{};
    bool scan_running_{};
    winrt::fire_and_forget load_processes();
    void render_processes();
    void set_sort(SortColumn);
    void add_process_menu(winrt::Microsoft::UI::Xaml::FrameworkElement const&, ProcessRow const&);
    bool set_priority(std::uint32_t,DWORD);
    bool set_affinity(std::uint32_t,DWORD_PTR);
    bool set_io_priority(std::uint32_t,std::uint32_t);
    bool set_always(std::wstring const&,wchar_t const*,DWORD);
    bool remove_always(std::wstring const&,wchar_t const*);
    std::optional<DWORD> read_always(std::wstring const&,wchar_t const*);
    std::optional<std::uint32_t> read_io_priority(std::uint32_t);
    winrt::fire_and_forget confirm_realtime(std::uint32_t);
    winrt::fire_and_forget edit_affinity(std::uint32_t,std::wstring);
};

}  // namespace winrt::Winchisel::implementation

namespace winrt::Winchisel::factory_implementation {

struct ProcessesPage : ProcessesPageT<ProcessesPage, implementation::ProcessesPage> {};

}  // namespace winrt::Winchisel::factory_implementation
