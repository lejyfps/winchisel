#include "pch.h"
#include "ProcessesPage.xaml.h"
#include <TlHelp32.h>
#include <processthreadsapi.h>

#if __has_include("ProcessesPage.g.cpp")
#include "ProcessesPage.g.cpp"
#endif

namespace winrt::Winchisel::implementation {

ProcessesPage::ProcessesPage() {
    InitializeComponent();
    initialized_ = true;
    load_processes();
}

void ProcessesPage::RefreshClick(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&) { load_processes(); }
void ProcessesPage::FilterChanged(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&) { if(initialized_) render_processes(); }

void ProcessesPage::load_processes() {
    rows_.clear();
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    if(snapshot==INVALID_HANDLE_VALUE){StatusText().Text(L"Process scan failed.");return;}
    PROCESSENTRY32W entry{};entry.dwSize=sizeof(entry);
    if(Process32FirstW(snapshot,&entry)) do {
        ProcessRow row{entry.th32ProcessID,entry.th32ParentProcessID,entry.szExeFile,L"Unavailable",L"Unavailable",L"Running"};
        HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,false,row.pid);
        if(process){
            switch(GetPriorityClass(process)){
            case IDLE_PRIORITY_CLASS:row.priority=L"Idle";break;case BELOW_NORMAL_PRIORITY_CLASS:row.priority=L"Below normal";break;
            case NORMAL_PRIORITY_CLASS:row.priority=L"Normal";break;case ABOVE_NORMAL_PRIORITY_CLASS:row.priority=L"Above normal";break;
            case HIGH_PRIORITY_CLASS:row.priority=L"High";break;case REALTIME_PRIORITY_CLASS:row.priority=L"Realtime";break;default:break;}
            DWORD_PTR process_mask{},system_mask{};if(GetProcessAffinityMask(process,&process_mask,&system_mask)){wchar_t value[32]{};swprintf_s(value,L"0x%llX",static_cast<unsigned long long>(process_mask));row.affinity=value;}
            CloseHandle(process);
        }
        rows_.push_back(std::move(row));
    } while(Process32NextW(snapshot,&entry));
    CloseHandle(snapshot);
    std::sort(rows_.begin(),rows_.end(),[](auto const& a,auto const& b){return _wcsicmp(a.name.c_str(),b.name.c_str())<0;});
    render_processes();
}

void ProcessesPage::render_processes() {
    ProcessList().Items().Clear();
    auto mode=FilterBox().SelectedIndex();DWORD own_session{};ProcessIdToSessionId(GetCurrentProcessId(),&own_session);std::size_t visible{};
    for(auto const& row:rows_){
        if(mode==1&&row.pid==0)continue;
        if(mode==2){DWORD session{};if(!ProcessIdToSessionId(row.pid,&session)||session!=own_session)continue;}
        Microsoft::UI::Xaml::Controls::Grid grid;grid.Padding({12,8,12,8});
        for(auto width:{80.0,0.0,80.0,120.0,120.0,100.0}){Microsoft::UI::Xaml::Controls::ColumnDefinition column;if(width==0)column.Width({1,Microsoft::UI::Xaml::GridUnitType::Star});else column.Width({width,Microsoft::UI::Xaml::GridUnitType::Pixel});grid.ColumnDefinitions().Append(column);}
        auto add=[&](std::wstring const& text,int column){Microsoft::UI::Xaml::Controls::TextBlock block;block.Text(text);block.VerticalAlignment(Microsoft::UI::Xaml::VerticalAlignment::Center);Microsoft::UI::Xaml::Controls::Grid::SetColumn(block,column);grid.Children().Append(block);};
        add(std::to_wstring(row.pid),0);add(row.name,1);add(L"—",2);add(row.priority,3);add(row.affinity,4);add(row.status,5);
        ProcessList().Items().Append(grid);++visible;
    }
    StatusText().Text(L"Visible: "+std::to_wstring(visible)+L"   Total: "+std::to_wstring(rows_.size()));
}

}  // namespace winrt::Winchisel::implementation
