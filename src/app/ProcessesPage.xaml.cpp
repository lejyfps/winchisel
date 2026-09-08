#include "pch.h"
#include "AsyncSupport.hpp"
#include "ProcessesPage.xaml.h"
#include "winchisel/platform/process.hpp"
#include "winchisel/core/affinity.hpp"
#include <TlHelp32.h>
#include <processthreadsapi.h>
#include <algorithm>
#include <functional>
#include <memory>

#if __has_include("ProcessesPage.g.cpp")
#include "ProcessesPage.g.cpp"
#endif

namespace winrt::Winchisel::implementation {
namespace muxc=Microsoft::UI::Xaml::Controls;
namespace mux=Microsoft::UI::Xaml;

namespace {
std::uint64_t file_time(FILETIME const& value){return(static_cast<std::uint64_t>(value.dwHighDateTime)<<32)|value.dwLowDateTime;}
std::uint64_t system_cpu_time(){FILETIME idle{},kernel{},user{};return GetSystemTimes(&idle,&kernel,&user)?file_time(kernel)+file_time(user):0;}
std::wstring affinity_label(DWORD_PTR process_mask,DWORD_PTR system_mask){
    if(GetActiveProcessorGroupCount()>1)return L"Unavailable (>64 CPUs)";
    if(!process_mask||!system_mask)return L"Unavailable";if(process_mask==system_mask)return L"All cores";
    std::wstring out=L"CPU ";bool first=true;for(unsigned i=0;i<sizeof(DWORD_PTR)*8;++i)if(process_mask&(DWORD_PTR{1}<<i)){if(!first)out+=L",";out+=std::to_wstring(i);first=false;}return out;
}
muxc::ScrollViewer find_scroll_viewer(mux::DependencyObject const& root){if(!root)return nullptr;if(auto viewer=root.try_as<muxc::ScrollViewer>())return viewer;auto count=Microsoft::UI::Xaml::Media::VisualTreeHelper::GetChildrenCount(root);for(int i=0;i<count;++i)if(auto found=find_scroll_viewer(Microsoft::UI::Xaml::Media::VisualTreeHelper::GetChild(root,i)))return found;return nullptr;}
}

ProcessesPage::ProcessesPage(){
    InitializeComponent();initialized_=true;load_processes();
    refresh_timer_=DispatcherQueue().CreateTimer();refresh_timer_.Interval(std::chrono::seconds(2));refresh_timer_.IsRepeating(true);
    auto weak=get_weak();refresh_timer_.Tick([weak](auto&&,auto&&){if(auto self=weak.get()){if(self->open_overlays_)self->refresh_pending_=true;else self->load_processes();}});
    Loaded([weak](auto&&,auto&&){if(auto self=weak.get())self->refresh_timer_.Start();});Unloaded([weak](auto&&,auto&&){if(auto self=weak.get())self->refresh_timer_.Stop();});
}

void ProcessesPage::RefreshClick(Windows::Foundation::IInspectable const&,mux::RoutedEventArgs const&){load_processes();}
void ProcessesPage::FilterChanged(Windows::Foundation::IInspectable const&,muxc::SelectionChangedEventArgs const&){if(initialized_)render_processes();}
void ProcessesPage::ProcessSelectionChanged(Windows::Foundation::IInspectable const&,muxc::SelectionChangedEventArgs const&){if(restoring_selection_)return;if(auto grid=ProcessList().SelectedItem().try_as<muxc::Grid>()){auto tag=grid.Tag();if(tag)selected_pid_=winrt::unbox_value<std::uint32_t>(tag);}else selected_pid_=0;}
void ProcessesPage::SortPid(Windows::Foundation::IInspectable const&,mux::RoutedEventArgs const&){set_sort(SortColumn::pid);}
void ProcessesPage::SortName(Windows::Foundation::IInspectable const&,mux::RoutedEventArgs const&){set_sort(SortColumn::name);}
void ProcessesPage::SortCpu(Windows::Foundation::IInspectable const&,mux::RoutedEventArgs const&){set_sort(SortColumn::cpu);}
void ProcessesPage::SortPriority(Windows::Foundation::IInspectable const&,mux::RoutedEventArgs const&){set_sort(SortColumn::priority);}
void ProcessesPage::SortAffinity(Windows::Foundation::IInspectable const&,mux::RoutedEventArgs const&){set_sort(SortColumn::affinity);}
void ProcessesPage::SortStatus(Windows::Foundation::IInspectable const&,mux::RoutedEventArgs const&){set_sort(SortColumn::status);}
void ProcessesPage::set_sort(SortColumn column){if(sort_column_==column)ascending_=!ascending_;else{sort_column_=column;ascending_=true;}render_processes();}

winrt::fire_and_forget ProcessesPage::load_processes(){
    auto error_lifetime=get_strong();
    auto error_queue=DispatcherQueue();
    auto error_weak=get_weak();
    try {

    if(scan_running_)co_return;scan_running_=true;auto weak=get_weak();auto queue=DispatcherQueue();auto previous_times=previous_process_times_;auto previous_created=previous_process_created_;auto previous_system=previous_system_time_;std::unordered_map<std::uint32_t,std::wstring> previous_paths;for(auto const& row:rows_)previous_paths.emplace(row.pid,row.path);
    co_await winrt::resume_background();
    std::vector<ProcessRow> rows;auto current_system=system_cpu_time();auto system_delta=current_system>previous_system?current_system-previous_system:0;
    std::unordered_map<std::uint32_t,std::uint64_t> current_times;
    std::unordered_map<std::uint32_t,std::uint64_t> current_created;
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);if(snapshot==INVALID_HANDLE_VALUE){(void)winchisel::ui::enqueue_safe(queue, [weak]{if(auto self=weak.get()){self->scan_running_=false;self->StatusText().Text(L"Process scan failed.");}});co_return;}
    PROCESSENTRY32W entry{};entry.dwSize=sizeof(entry);
    std::wstring path_query(32768, L'\0');
    if(Process32FirstW(snapshot,&entry))do{
        ProcessRow row{};row.pid=entry.th32ProcessID;row.parent=entry.th32ParentProcessID;row.name=entry.szExeFile;row.priority=L"Unavailable";row.affinity=L"Unavailable";row.status=L"Running";
        HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,false,row.pid);
        if(process){
            FILETIME created{},exited{},kernel{},user{};if(GetProcessTimes(process,&created,&exited,&kernel,&user)){auto value=file_time(kernel)+file_time(user),created_value=file_time(created);current_times[row.pid]=value;current_created[row.pid]=created_value;if(auto old=previous_times.find(row.pid);old!=previous_times.end()&&previous_created[row.pid]==created_value&&system_delta&&value>=old->second){auto ncpus=GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);if(!ncpus)ncpus=1;row.cpu=100.0*static_cast<double>(value-old->second)/static_cast<double>(system_delta)*static_cast<double>(ncpus);} }
            switch(GetPriorityClass(process)){case IDLE_PRIORITY_CLASS:row.priority=L"Idle";break;case BELOW_NORMAL_PRIORITY_CLASS:row.priority=L"Below normal";break;case NORMAL_PRIORITY_CLASS:row.priority=L"Normal";break;case ABOVE_NORMAL_PRIORITY_CLASS:row.priority=L"Above normal";break;case HIGH_PRIORITY_CLASS:row.priority=L"High";break;case REALTIME_PRIORITY_CLASS:row.priority=L"Realtime";break;default:break;}
            DWORD_PTR process_mask{},system_mask{};if(GetProcessAffinityMask(process,&process_mask,&system_mask))row.affinity=affinity_label(process_mask,system_mask);
            if(auto old=previous_created.find(row.pid);old!=previous_created.end()&&current_created[row.pid]==old->second){if(auto path=previous_paths.find(row.pid);path!=previous_paths.end())row.path=path->second;}if(row.path.empty()){DWORD size=static_cast<DWORD>(path_query.size());if(QueryFullProcessImageNameW(process,0,path_query.data(),&size))row.path.assign(path_query.data(),size);}CloseHandle(process);
        }
        rows.push_back(std::move(row));
    }while(Process32NextW(snapshot,&entry));CloseHandle(snapshot);
    (void)winchisel::ui::enqueue_safe(queue, [weak,rows=std::move(rows),current_times=std::move(current_times),current_created=std::move(current_created),current_system]()mutable{if(auto self=weak.get()){self->scan_running_=false;self->rows_=std::move(rows);self->previous_process_times_=std::move(current_times);self->previous_process_created_=std::move(current_created);self->previous_system_time_=current_system;std::unordered_set<std::uint32_t> live;for(auto const& row:self->rows_)live.insert(row.pid);for(auto it=self->expanded_.begin();it!=self->expanded_.end();)if(!live.contains(*it))it=self->expanded_.erase(it);else++it;self->render_processes();}});

    } catch (...) {
        winchisel::ui::report_async_error(error_queue, [error_weak](winrt::hstring const& text) {
            if (auto self=error_weak.get()) { self->scan_running_=false; self->StatusText().Text(text); }
        });
    }
}

void ProcessesPage::render_processes(){
    auto scroll=find_scroll_viewer(ProcessList());auto vertical_offset=scroll?scroll.VerticalOffset():0.0;restoring_selection_=true;auto mode=FilterBox().SelectedIndex();DWORD own_session{};ProcessIdToSessionId(GetCurrentProcessId(),&own_session);
    std::unordered_map<std::uint32_t,ProcessRow const*> by_pid;std::unordered_map<std::uint32_t,std::vector<ProcessRow const*>> children;
    for(auto& row:rows_){by_pid[row.pid]=&row;children[row.parent].push_back(&row);row.has_children=false;row.depth=0;}for(auto& row:rows_)row.has_children=children.contains(row.pid)&&!children[row.pid].empty();
    auto compare=[&](ProcessRow const* a,ProcessRow const* b){int result=0;switch(sort_column_){case SortColumn::pid:result=a->pid<b->pid?-1:a->pid>b->pid?1:0;break;case SortColumn::name:result=_wcsicmp(a->name.c_str(),b->name.c_str());break;case SortColumn::cpu:result=a->cpu<b->cpu?-1:a->cpu>b->cpu?1:0;break;case SortColumn::priority:result=_wcsicmp(a->priority.c_str(),b->priority.c_str());break;case SortColumn::affinity:result=_wcsicmp(a->affinity.c_str(),b->affinity.c_str());break;case SortColumn::status:result=_wcsicmp(a->status.c_str(),b->status.c_str());break;}return ascending_?result<0:result>0;};
    for(auto& [_,bucket]:children)std::sort(bucket.begin(),bucket.end(),compare);std::unordered_set<std::uint32_t> allowed;
    if(mode==0)for(auto const& row:rows_)allowed.insert(row.pid);
    else if(mode==2)for(auto const& row:rows_){DWORD session{};if(ProcessIdToSessionId(row.pid,&session)&&session==own_session)allowed.insert(row.pid);}
    else{
        for(auto const& row:rows_)if(row.cpu>0.0){auto current=row.pid;while(by_pid.contains(current)&&allowed.insert(current).second){auto parent=by_pid[current]->parent;if(parent==current)break;current=parent;}}
        std::vector<std::uint32_t> pending(allowed.begin(),allowed.end());while(!pending.empty()){auto current=pending.back();pending.pop_back();for(auto child:children[current])if(allowed.insert(child->pid).second)pending.push_back(child->pid);}
    }
    std::vector<ProcessRow const*> visible;std::unordered_set<std::uint32_t> traversal;
    std::function<void(ProcessRow const*,std::size_t)> append=[&](ProcessRow const* row,std::size_t depth){
        if(!row||depth>rows_.size()||!traversal.insert(row->pid).second)return;
        const_cast<ProcessRow*>(row)->depth=depth;visible.push_back(row);if(row->has_children&&expanded_.contains(row->pid))for(auto child:children[row->pid])append(child,depth+1);traversal.erase(row->pid);
    };
    std::vector<ProcessRow const*> roots;for(auto const& row:rows_)if(allowed.contains(row.pid)&&(!allowed.contains(row.parent)||row.parent==row.pid))roots.push_back(&row);for(auto& [_,bucket]:children)std::erase_if(bucket,[&](auto row){return !allowed.contains(row->pid);});for(auto& row:rows_)row.has_children=allowed.contains(row.pid)&&children.contains(row.pid)&&!children[row.pid].empty();std::sort(roots.begin(),roots.end(),compare);for(auto root:roots)append(root,0);
    double total_cpu{};for(auto const& row:rows_)total_cpu+=row.cpu;
    std::vector<std::uint32_t> current_pids;current_pids.reserve(visible.size());std::vector<std::uint8_t> current_children;current_children.reserve(visible.size());std::vector<std::wstring> current_names;current_names.reserve(visible.size());
    for(auto row:visible){current_pids.push_back(row->pid);current_children.push_back(row->has_children?1:0);current_names.push_back(row->name);}
    if(current_pids==rendered_pids_&&current_children==rendered_children_&&current_names==rendered_names_&&ProcessList().Items().Size()==visible.size()){
        for(std::uint32_t index{};index<ProcessList().Items().Size();++index){auto grid=ProcessList().Items().GetAt(index).try_as<muxc::Grid>();if(!grid)continue;auto row=visible[index];wchar_t cpu[24]{};swprintf_s(cpu,L"%.1f%%",row->cpu);for(auto const& child:grid.Children())if(auto text=child.try_as<muxc::TextBlock>()){switch(muxc::Grid::GetColumn(text)){case 2:text.Text(cpu);break;case 3:text.Text(row->priority);break;case 4:text.Text(row->affinity);break;case 5:text.Text(row->status);break;default:break;}}}
        restoring_selection_=false;wchar_t total[32]{};swprintf_s(total,L"%.1f%%",total_cpu);StatusText().Text(L"Visible: "+std::to_wstring(visible.size())+L"   Processes: "+std::to_wstring(rows_.size())+L"   Total CPU: "+total+L"   •   Right-click a process for actions");return;
    }
    rendered_pids_=std::move(current_pids);rendered_children_=std::move(current_children);rendered_names_=std::move(current_names);ProcessList().Items().Clear();
    muxc::Grid selected_grid{nullptr};for(auto row:visible){
        muxc::Grid grid;grid.Tag(winrt::box_value(row->pid));grid.Padding({12,6,12,6});for(auto width:{80.0,0.0,80.0,120.0,120.0,100.0}){muxc::ColumnDefinition col;col.Width(width==0?mux::GridLength{1,mux::GridUnitType::Star}:mux::GridLength{width,mux::GridUnitType::Pixel});grid.ColumnDefinitions().Append(col);}
        Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(grid,L"Process "+row->name+L", PID "+std::to_wstring(row->pid));muxc::StackPanel pid_panel;pid_panel.Orientation(muxc::Orientation::Horizontal);pid_panel.Spacing(4);auto pid=row->pid;if(row->has_children){muxc::Button expand;expand.Padding({0,0,0,0});expand.Width(20);expand.Height(20);expand.MinWidth(0);expand.MinHeight(0);expand.CornerRadius({2});muxc::FontIcon chevron;chevron.Glyph(expanded_.contains(row->pid)?L"\uE70D":L"\uE76C");chevron.FontSize(10);expand.Content(chevron);Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(expand,(expanded_.contains(row->pid)?L"Collapse ":L"Expand ")+row->name);auto weak=get_weak();expand.Click([weak,pid](auto&&,auto&&){if(auto self=weak.get()){if(!self->expanded_.insert(pid).second)self->expanded_.erase(pid);self->DispatcherQueue().TryEnqueue([weak]{if(auto page=weak.get())page->render_processes();});}});pid_panel.Children().Append(expand);}else{muxc::Border spacer;spacer.Width(20);pid_panel.Children().Append(spacer);}muxc::TextBlock pid_text;pid_text.Text(std::to_wstring(row->pid));pid_text.VerticalAlignment(mux::VerticalAlignment::Center);pid_panel.Children().Append(pid_text);grid.Children().Append(pid_panel);
        auto add=[&](std::wstring const& text,int column,double left=0){muxc::TextBlock block;block.Text(text);block.Margin({left,0,0,0});block.TextTrimming(mux::TextTrimming::CharacterEllipsis);block.VerticalAlignment(mux::VerticalAlignment::Center);if(column==1&&!row->path.empty())muxc::ToolTipService::SetToolTip(block,winrt::box_value(row->path));muxc::Grid::SetColumn(block,column);grid.Children().Append(block);};
        wchar_t cpu[24]{};swprintf_s(cpu,L"%.1f%%",row->cpu);add(row->name,1,row->depth*16.0);add(cpu,2);add(row->priority,3);add(row->affinity,4);add(row->status,5);auto weak=get_weak();grid.RightTapped([weak,pid=row->pid](auto const& sender,auto const& args){if(auto self=weak.get()){auto item=std::ranges::find(self->rows_,pid,&ProcessRow::pid);if(item!=self->rows_.end()){auto element=sender.template as<mux::FrameworkElement>();self->add_process_menu(element,*item);if(auto flyout=element.ContextFlyout())flyout.ShowAt(element);args.Handled(true);}}});ProcessList().Items().Append(grid);if(row->pid==selected_pid_)selected_grid=grid;
    }
    if(selected_grid)ProcessList().SelectedItem(selected_grid);else selected_pid_=0;restoring_selection_=false;
    wchar_t total[32]{};swprintf_s(total,L"%.1f%%",total_cpu);StatusText().Text(L"Visible: "+std::to_wstring(visible.size())+L"   Processes: "+std::to_wstring(rows_.size())+L"   Total CPU: "+total+L"   •   Right-click a process for actions");
    if(scroll&&vertical_offset>0){DispatcherQueue().TryEnqueue([scroll,vertical_offset]{scroll.ChangeView(nullptr,vertical_offset,nullptr,true);});}
}

bool ProcessesPage::set_priority(std::uint32_t pid,DWORD value){HANDLE process=OpenProcess(PROCESS_SET_INFORMATION|PROCESS_QUERY_LIMITED_INFORMATION,false,pid);if(!process)return false;bool ok=SetPriorityClass(process,value)!=FALSE;CloseHandle(process);if(ok){if(open_overlays_)refresh_pending_=true;else load_processes();}else StatusText().Text(L"Could not change process priority.");return ok;}
bool ProcessesPage::set_affinity(std::uint32_t pid,DWORD_PTR mask){if(GetActiveProcessorGroupCount()>1){StatusText().Text(L"Affinity editing is unavailable on systems with multiple processor groups (>64 logical CPUs).");return false;}HANDLE process=OpenProcess(PROCESS_SET_INFORMATION|PROCESS_QUERY_LIMITED_INFORMATION,false,pid);if(!process){StatusText().Text(L"Could not open process for affinity change. Windows error "+std::to_wstring(GetLastError())+L".");return false;}DWORD_PTR current{},system{};bool ok=GetProcessAffinityMask(process,&current,&system)&&mask&&(mask&system)&&SetProcessAffinityMask(process,mask&system);const auto error=ok?ERROR_SUCCESS:GetLastError();CloseHandle(process);if(ok){if(open_overlays_)refresh_pending_=true;else load_processes();}else StatusText().Text(L"Could not change process affinity. Windows error "+std::to_wstring(error)+L".");return ok;}
bool ProcessesPage::set_io_priority(std::uint32_t pid,std::uint32_t value){auto result=winchisel::platform::set_process_io_priority(pid,value);if(result){if(open_overlays_)refresh_pending_=true;else load_processes();return true;}StatusText().Text(L"Could not change I/O priority.");return false;}
bool ProcessesPage::set_always(std::wstring const& name,wchar_t const* value_name,DWORD value){return static_cast<bool>(winchisel::platform::set_ifeo_dword(name,value_name,value));}
bool ProcessesPage::remove_always(std::wstring const& name,wchar_t const* value_name){return static_cast<bool>(winchisel::platform::remove_ifeo_dword(name,value_name));}
std::optional<DWORD> ProcessesPage::read_always(std::wstring const& name,wchar_t const* value_name){if(auto value=winchisel::platform::read_ifeo_dword(name,value_name))return static_cast<DWORD>(*value);return std::nullopt;}
std::optional<std::uint32_t> ProcessesPage::read_io_priority(std::uint32_t pid){return winchisel::platform::read_process_io_priority(pid);}

void ProcessesPage::add_process_menu(mux::FrameworkElement const& element,ProcessRow const& row){
    muxc::MenuFlyout menu;auto submenu=[&](wchar_t const* text){muxc::MenuFlyoutSubItem item;item.Text(text);menu.Items().Append(item);return item;};
    auto weak=get_weak();menu.Opened([weak](auto&&,auto&&){if(auto self=weak.get())++self->open_overlays_;});menu.Closed([weak](auto&&,auto&&){if(auto self=weak.get()){if(self->open_overlays_)--self->open_overlays_;if(!self->open_overlays_&&self->refresh_pending_){self->refresh_pending_=false;self->load_processes();}}});
    auto add=[&](muxc::MenuFlyoutSubItem const& parent,wchar_t const* text,bool active,auto action){muxc::ToggleMenuFlyoutItem item;item.Text(text);item.IsChecked(active);item.Click(action);parent.Items().Append(item);};auto pid=row.pid;auto name=row.name;
    auto current=submenu(L"CPU priority · Current");for(auto const& [text,value]:std::vector<std::pair<wchar_t const*,DWORD>>{{L"Idle",IDLE_PRIORITY_CLASS},{L"Below normal",BELOW_NORMAL_PRIORITY_CLASS},{L"Normal",NORMAL_PRIORITY_CLASS},{L"Above normal",ABOVE_NORMAL_PRIORITY_CLASS},{L"High",HIGH_PRIORITY_CLASS}})add(current,text,row.priority==text,[this,pid,value](auto&&,auto&&){set_priority(pid,value);});add(current,L"Realtime…",row.priority==L"Realtime",[this,pid](auto&&,auto&&){confirm_realtime(pid);});
    auto cpu_saved=read_always(name,L"CpuPriorityClass");auto always=submenu(L"CPU priority · Always");add(always,L"Default (remove saved rule)",!cpu_saved,[this,name](auto&&,auto&&){if(remove_always(name,L"CpuPriorityClass"))StatusText().Text(L"Permanent CPU priority rule removed.");else StatusText().Text(L"Could not remove permanent CPU priority rule.");});for(auto const& [text,value]:std::vector<std::pair<wchar_t const*,DWORD>>{{L"Idle",1},{L"Below normal",5},{L"Normal",2},{L"Above normal",6},{L"High",3},{L"Realtime",4}})add(always,text,cpu_saved&&*cpu_saved==value,[this,pid,name,value](auto&&,auto&&){if(set_always(name,L"CpuPriorityClass",value)){DWORD cls=value==1?IDLE_PRIORITY_CLASS:value==5?BELOW_NORMAL_PRIORITY_CLASS:value==2?NORMAL_PRIORITY_CLASS:value==6?ABOVE_NORMAL_PRIORITY_CLASS:value==3?HIGH_PRIORITY_CLASS:REALTIME_PRIORITY_CLASS;set_priority(pid,cls);}else StatusText().Text(L"Could not save permanent CPU priority.");});
    auto io_value=read_io_priority(pid);auto io=submenu(L"I/O priority · Current");add(io,L"Low",io_value&&*io_value==1,[this,pid](auto&&,auto&&){set_io_priority(pid,1);});add(io,L"Normal",io_value&&*io_value==2,[this,pid](auto&&,auto&&){set_io_priority(pid,2);});
    auto io_saved=read_always(name,L"IoPriority");auto io_always=submenu(L"I/O priority · Always");add(io_always,L"Default (remove saved rule)",!io_saved,[this,name](auto&&,auto&&){if(remove_always(name,L"IoPriority"))StatusText().Text(L"Permanent I/O priority rule removed.");else StatusText().Text(L"Could not remove permanent I/O priority rule.");});add(io_always,L"Low",io_saved&&*io_saved==1,[this,pid,name](auto&&,auto&&){if(set_always(name,L"IoPriority",1))set_io_priority(pid,1);else StatusText().Text(L"Could not save permanent I/O priority.");});add(io_always,L"Normal",io_saved&&*io_saved==2,[this,pid,name](auto&&,auto&&){if(set_always(name,L"IoPriority",2))set_io_priority(pid,2);else StatusText().Text(L"Could not save permanent I/O priority.");});
    auto affinity=submenu(L"Affinity");if(GetActiveProcessorGroupCount()>1){muxc::MenuFlyoutItem unavailable;unavailable.Text(L"Unavailable on systems with >64 CPUs");unavailable.IsEnabled(false);affinity.Items().Append(unavailable);element.ContextFlyout(menu);return;}add(affinity,L"Edit…",false,[this,pid,name](auto&&,auto&&){edit_affinity(pid,name);});
    auto mode=[this,pid](int selected){HANDLE p=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,false,pid);DWORD_PTR current{},system{};if(!p||!GetProcessAffinityMask(p,&current,&system)){if(p)CloseHandle(p);return;}CloseHandle(p);set_affinity(pid,static_cast<DWORD_PTR>(winchisel::core::affinity_mask(system,selected)));};
    add(affinity,L"All cores",row.affinity==L"All cores",[mode](auto&&,auto&&){mode(0);});add(affinity,L"Even logical CPUs",false,[mode](auto&&,auto&&){mode(1);});add(affinity,L"Odd logical CPUs",false,[mode](auto&&,auto&&){mode(2);});add(affinity,L"First half",false,[mode](auto&&,auto&&){mode(3);});add(affinity,L"Second half",false,[mode](auto&&,auto&&){mode(4);});element.ContextFlyout(menu);
}

winrt::fire_and_forget ProcessesPage::confirm_realtime(std::uint32_t pid){
    auto error_lifetime=get_strong();
    auto error_queue=DispatcherQueue();
    auto error_weak=get_weak();
    try {
    winchisel::core::DialogSlot dialog_slot; if(!winchisel::ui::dialog_available(dialog_slot))co_return;

auto lifetime=get_strong();++open_overlays_;muxc::ContentDialog dialog;dialog.XamlRoot(XamlRoot());dialog.Title(winrt::box_value(L"Realtime priority"));dialog.Content(winrt::box_value(L"Realtime priority can make Windows unresponsive. Apply it only when you understand the risk."));dialog.PrimaryButtonText(L"Apply");dialog.CloseButtonText(L"Cancel");auto result=co_await dialog.ShowAsync();if(open_overlays_)--open_overlays_;refresh_pending_=false;if(result==muxc::ContentDialogResult::Primary)set_priority(pid,REALTIME_PRIORITY_CLASS);else load_processes();
    } catch (...) {
        winchisel::ui::report_async_error(error_queue, [error_weak](winrt::hstring const& text) {
            if (auto self=error_weak.get()) { self->open_overlays_=0; self->refresh_pending_=false; self->StatusText().Text(text); }
        });
    }
}

winrt::fire_and_forget ProcessesPage::edit_affinity(std::uint32_t pid,std::wstring name){
    auto error_lifetime=get_strong();
    auto error_queue=DispatcherQueue();
    auto error_weak=get_weak();
    try {
    winchisel::core::DialogSlot dialog_slot; if(!winchisel::ui::dialog_available(dialog_slot))co_return;


    auto lifetime=get_strong();++open_overlays_;HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,false,pid);DWORD_PTR current{},system{};if(!process||!GetProcessAffinityMask(process,&current,&system)){if(process)CloseHandle(process);if(open_overlays_)--open_overlays_;StatusText().Text(L"Could not read process affinity.");co_return;}CloseHandle(process);
    auto selected=std::make_shared<DWORD_PTR>(current);auto boxes=std::make_shared<std::vector<std::pair<muxc::CheckBox,DWORD_PTR>>>();muxc::StackPanel content;content.Spacing(8);muxc::TextBlock hint;hint.Text(L"Choose at least one logical processor:");content.Children().Append(hint);muxc::Grid grid;for(int i=0;i<4;++i){muxc::ColumnDefinition column;column.Width({1,mux::GridUnitType::Star});grid.ColumnDefinitions().Append(column);}int n{};
    for(unsigned i=0;i<sizeof(DWORD_PTR)*8;++i)if(system&(DWORD_PTR{1}<<i)){muxc::CheckBox box;box.Content(winrt::box_value(L"CPU "+std::to_wstring(i)));box.IsChecked((current&(DWORD_PTR{1}<<i))!=0);auto bit=DWORD_PTR{1}<<i;box.Checked([selected,bit](auto&&,auto&&){*selected|=bit;});box.Unchecked([selected,bit](auto&&,auto&&){*selected&=~bit;});muxc::Grid::SetColumn(box,n%4);muxc::Grid::SetRow(box,n/4);if(n%4==0){muxc::RowDefinition row;row.Height(mux::GridLengthHelper::Auto());grid.RowDefinitions().Append(row);}grid.Children().Append(box);boxes->emplace_back(box,bit);++n;}content.Children().Append(grid);
    muxc::StackPanel tools;tools.Orientation(muxc::Orientation::Horizontal);tools.Spacing(8);muxc::Button invert;invert.Content(winrt::box_value(L"Invert"));invert.Click([selected,boxes,system](auto&&,auto&&){*selected=(~*selected)&system;if(!*selected)*selected=system;for(auto const& [box,bit]:*boxes)box.IsChecked((*selected&bit)!=0);});tools.Children().Append(invert);muxc::Button all;all.Content(winrt::box_value(L"All cores"));all.Click([selected,boxes,system](auto&&,auto&&){*selected=system;for(auto const& [box,bit]:*boxes)box.IsChecked(true);});tools.Children().Append(all);content.Children().Append(tools);
    muxc::ContentDialog dialog;dialog.XamlRoot(XamlRoot());dialog.Title(winrt::box_value(L"Affinity — "+name+L" (PID "+std::to_wstring(pid)+L")"));dialog.Content(content);dialog.PrimaryButtonText(L"Apply");dialog.CloseButtonText(L"Cancel");auto result=co_await dialog.ShowAsync();if(open_overlays_)--open_overlays_;refresh_pending_=false;if(result==muxc::ContentDialogResult::Primary){if(!*selected)StatusText().Text(L"Select at least one CPU.");else set_affinity(pid,*selected);}else load_processes();

    } catch (...) {
        winchisel::ui::report_async_error(error_queue, [error_weak](winrt::hstring const& text) {
            if (auto self=error_weak.get()) { self->open_overlays_=0; self->refresh_pending_=false; self->StatusText().Text(text); }
        });
    }
}

} // namespace winrt::Winchisel::implementation
