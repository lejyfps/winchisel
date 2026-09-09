#include "pch.h"
#include "AsyncSupport.hpp"
#include "ExtrasPage.xaml.h"
#include "winchisel/platform/system.hpp"

#if __has_include("ExtrasPage.g.cpp")
#include "ExtrasPage.g.cpp"
#endif

namespace winrt::Winchisel::implementation {
namespace muxc=Microsoft::UI::Xaml::Controls;
namespace {
constexpr wchar_t policy_backup_path[]=L"SOFTWARE\\Winchisel\\PolicyBackup";
std::wstring backup_name(std::wstring_view group,std::wstring_view name,std::wstring_view suffix){return std::wstring(group)+L"."+std::wstring(name)+L"."+std::wstring(suffix);}
bool backup_complete(HKEY backup,std::wstring const& marker,std::wstring const& saved,bool need_value){DWORD existing{},size=sizeof(existing);if(RegQueryValueExW(backup,marker.c_str(),nullptr,nullptr,reinterpret_cast<BYTE*>(&existing),&size)!=ERROR_SUCCESS)return false;if(!need_value||!existing)return true;size=0;return RegQueryValueExW(backup,saved.c_str(),nullptr,nullptr,nullptr,&size)==ERROR_SUCCESS;}
bool backup_dword(std::wstring_view group,wchar_t const* path,wchar_t const* name){HKEY backup{};if(RegCreateKeyExW(HKEY_LOCAL_MACHINE,policy_backup_path,0,nullptr,0,KEY_QUERY_VALUE|KEY_SET_VALUE,nullptr,&backup,nullptr)!=ERROR_SUCCESS)return false;auto marker=backup_name(group,name,L"present"),saved=backup_name(group,name,L"value");DWORD value{},size=sizeof(value);if(backup_complete(backup,marker,saved,true)){RegCloseKey(backup);return true;}const auto status=RegGetValueW(HKEY_LOCAL_MACHINE,path,name,RRF_RT_REG_DWORD,nullptr,&value,&size);if(status!=ERROR_SUCCESS&&status!=ERROR_FILE_NOT_FOUND){RegCloseKey(backup);return false;}const bool present=status==ERROR_SUCCESS;DWORD flag=present?1:0;bool ok=true;if(present)ok=RegSetValueExW(backup,saved.c_str(),0,REG_DWORD,reinterpret_cast<BYTE*>(&value),sizeof(value))==ERROR_SUCCESS;if(ok)ok=RegSetValueExW(backup,marker.c_str(),0,REG_DWORD,reinterpret_cast<BYTE*>(&flag),sizeof(flag))==ERROR_SUCCESS;RegCloseKey(backup);return ok;}
bool restore_dword(std::wstring_view group,wchar_t const* path,wchar_t const* name){HKEY backup{};if(RegOpenKeyExW(HKEY_LOCAL_MACHINE,policy_backup_path,0,KEY_QUERY_VALUE|KEY_SET_VALUE,&backup)!=ERROR_SUCCESS)return false;auto marker=backup_name(group,name,L"present"),saved=backup_name(group,name,L"value");DWORD present{},value{},size=sizeof(DWORD);if(RegQueryValueExW(backup,marker.c_str(),nullptr,nullptr,reinterpret_cast<BYTE*>(&present),&size)!=ERROR_SUCCESS){RegCloseKey(backup);return false;}if(present){size=sizeof(value);if(RegQueryValueExW(backup,saved.c_str(),nullptr,nullptr,reinterpret_cast<BYTE*>(&value),&size)!=ERROR_SUCCESS){RegCloseKey(backup);return false;}}HKEY target{};bool ok=RegCreateKeyExW(HKEY_LOCAL_MACHINE,path,0,nullptr,0,KEY_SET_VALUE,nullptr,&target,nullptr)==ERROR_SUCCESS;if(ok){const auto result=present?RegSetValueExW(target,name,0,REG_DWORD,reinterpret_cast<BYTE*>(&value),sizeof(value)):RegDeleteValueW(target,name);ok=result==ERROR_SUCCESS||(!present&&result==ERROR_FILE_NOT_FOUND);RegCloseKey(target);}if(ok){RegDeleteValueW(backup,marker.c_str());RegDeleteValueW(backup,saved.c_str());}RegCloseKey(backup);return ok;}
bool backup_string(std::wstring_view group,wchar_t const* path,wchar_t const* name){HKEY backup{};if(RegCreateKeyExW(HKEY_LOCAL_MACHINE,policy_backup_path,0,nullptr,0,KEY_QUERY_VALUE|KEY_SET_VALUE,nullptr,&backup,nullptr)!=ERROR_SUCCESS)return false;auto marker=backup_name(group,name,L"present"),saved=backup_name(group,name,L"value");if(backup_complete(backup,marker,saved,true)){RegCloseKey(backup);return true;}DWORD size=0;const auto status=RegGetValueW(HKEY_LOCAL_MACHINE,path,name,RRF_RT_REG_SZ,nullptr,nullptr,&size);if(status!=ERROR_SUCCESS&&status!=ERROR_FILE_NOT_FOUND){RegCloseKey(backup);return false;}const bool present=status==ERROR_SUCCESS;std::vector<BYTE> value(size);if(present&&RegGetValueW(HKEY_LOCAL_MACHINE,path,name,RRF_RT_REG_SZ,nullptr,value.data(),&size)!=ERROR_SUCCESS){RegCloseKey(backup);return false;}DWORD flag=present?1:0;bool ok=true;if(present)ok=RegSetValueExW(backup,saved.c_str(),0,REG_SZ,value.data(),size)==ERROR_SUCCESS;if(ok)ok=RegSetValueExW(backup,marker.c_str(),0,REG_DWORD,reinterpret_cast<BYTE*>(&flag),sizeof(flag))==ERROR_SUCCESS;RegCloseKey(backup);return ok;}
bool restore_string(std::wstring_view group,wchar_t const* path,wchar_t const* name){HKEY backup{};if(RegOpenKeyExW(HKEY_LOCAL_MACHINE,policy_backup_path,0,KEY_QUERY_VALUE|KEY_SET_VALUE,&backup)!=ERROR_SUCCESS)return false;auto marker=backup_name(group,name,L"present"),saved=backup_name(group,name,L"value");DWORD present{},size=sizeof(present);if(RegQueryValueExW(backup,marker.c_str(),nullptr,nullptr,reinterpret_cast<BYTE*>(&present),&size)!=ERROR_SUCCESS){RegCloseKey(backup);return false;}std::vector<BYTE> value;if(present){size=0;if(RegQueryValueExW(backup,saved.c_str(),nullptr,nullptr,nullptr,&size)!=ERROR_SUCCESS){RegCloseKey(backup);return false;}value.resize(size);if(RegQueryValueExW(backup,saved.c_str(),nullptr,nullptr,value.data(),&size)!=ERROR_SUCCESS){RegCloseKey(backup);return false;}}HKEY target{};bool ok=RegCreateKeyExW(HKEY_LOCAL_MACHINE,path,0,nullptr,0,KEY_SET_VALUE,nullptr,&target,nullptr)==ERROR_SUCCESS;if(ok){const auto result=present?RegSetValueExW(target,name,0,REG_SZ,value.data(),size):RegDeleteValueW(target,name);ok=result==ERROR_SUCCESS||(!present&&result==ERROR_FILE_NOT_FOUND);RegCloseKey(target);}if(ok){RegDeleteValueW(backup,marker.c_str());RegDeleteValueW(backup,saved.c_str());}RegCloseKey(backup);return ok;}
}

ExtrasPage::ExtrasPage(){InitializeComponent();load_states();load_command_states();}
bool ExtrasPage::read_dword(HKEY root,wchar_t const* path,wchar_t const* name,DWORD& value){DWORD size=sizeof(value);return RegGetValueW(root,path,name,RRF_RT_REG_DWORD,nullptr,&value,&size)==ERROR_SUCCESS;}
bool ExtrasPage::read_string(HKEY root,wchar_t const* path,wchar_t const* name,std::wstring& value){DWORD size{};if(RegGetValueW(root,path,name,RRF_RT_REG_SZ,nullptr,nullptr,&size)!=ERROR_SUCCESS)return false;value.resize(size/sizeof(wchar_t));if(RegGetValueW(root,path,name,RRF_RT_REG_SZ,nullptr,value.data(),&size)!=ERROR_SUCCESS)return false;if(!value.empty()&&!value.back())value.pop_back();return true;}
bool ExtrasPage::write_dword(HKEY root,wchar_t const* path,wchar_t const* name,std::optional<DWORD> value){HKEY key{};if(RegCreateKeyExW(root,path,0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr)!=ERROR_SUCCESS)return false;LSTATUS result=value?RegSetValueExW(key,name,0,REG_DWORD,reinterpret_cast<BYTE const*>(&*value),sizeof(DWORD)):RegDeleteValueW(key,name);RegCloseKey(key);return result==ERROR_SUCCESS||(!value&&result==ERROR_FILE_NOT_FOUND);}
bool ExtrasPage::write_string(HKEY root,wchar_t const* path,wchar_t const* name,std::wstring const& value){HKEY key{};if(RegCreateKeyExW(root,path,0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr)!=ERROR_SUCCESS)return false;auto result=RegSetValueExW(key,name,0,REG_SZ,reinterpret_cast<BYTE const*>(value.c_str()),static_cast<DWORD>((value.size()+1)*sizeof(wchar_t)));RegCloseKey(key);return result==ERROR_SUCCESS;}
void ExtrasPage::show_result(bool ok,std::wstring const& text){ResultBar().Title(ok?L"Applied":L"Could not apply setting");ResultBar().Severity(ok?muxc::InfoBarSeverity::Success:muxc::InfoBarSeverity::Error);ResultBar().Message(text);ResultBar().IsOpen(true);winchisel::platform::boot_log(winrt::to_string(text).c_str());}

void ExtrasPage::load_states(){
    reload_registry_states();
}

winrt::fire_and_forget ExtrasPage::reload_registry_states() {
    auto weak = get_weak();
    winrt::Microsoft::UI::Dispatching::DispatcherQueue queue{nullptr};
    try { queue = DispatcherQueue(); } catch (...) {}
    try {
        auto lifetime = get_strong();
        winrt::apartment_context ui;
        co_await winrt::resume_background();
        auto snapshot = read_registry_snapshot();
        co_await ui;
        if (auto self = weak.get()) self->apply_registry_snapshot(snapshot);
    } catch (...) {
        winchisel::ui::report_async_error(queue, [weak](winrt::hstring const& text) {
            if (auto self = weak.get()) self->show_result(false, std::wstring(text));
        });
    }
}

ExtrasPage::RegistrySnapshot ExtrasPage::read_registry_snapshot(){
    RegistrySnapshot snapshot{};
    DWORD value{};
    snapshot.modern_standby = read_dword(HKEY_LOCAL_MACHINE,L"SYSTEM\\CurrentControlSet\\Control\\Power",L"PlatformAoAcOverride",value)&&value==0;
    snapshot.sync_provider = read_dword(HKEY_CURRENT_USER,L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced",L"ShowSyncProviderNotifications",value)&&value==0;
    auto all=[&](wchar_t const* path,std::initializer_list<std::pair<wchar_t const*,DWORD>> rules){for(auto const& [name,expected]:rules)if(!read_dword(HKEY_LOCAL_MACHINE,path,name,value)||value!=expected)return false;return true;};
    snapshot.brave = all(L"SOFTWARE\\Policies\\BraveSoftware\\Brave",{{L"BraveRewardsDisabled",1},{L"BraveWalletDisabled",1},{L"BraveVPNDisabled",1},{L"BraveAIChatEnabled",0},{L"BraveStatsPingEnabled",0},{L"BraveNewsDisabled",1},{L"BraveTalkDisabled",1},{L"TorDisabled",1},{L"BraveP3AEnabled",0},{L"UrlKeyedAnonymizedDataCollectionEnabled",0},{L"SafeBrowsingExtendedReportingEnabled",0},{L"MetricsReportingEnabled",0}});
    const bool edge_base=all(L"SOFTWARE\\Policies\\Microsoft\\Edge",{{L"PersonalizationReportingEnabled",0},{L"ShowRecommendationsEnabled",0},{L"HideFirstRunExperience",1},{L"UserFeedbackAllowed",0},{L"ConfigureDoNotTrack",1},{L"AlternateErrorPagesEnabled",0},{L"EdgeCollectionsEnabled",0},{L"EdgeShoppingAssistantEnabled",0},{L"MicrosoftEdgeInsiderPromotionEnabled",0},{L"ShowMicrosoftRewards",0},{L"WebWidgetAllowed",0},{L"DiagnosticData",0},{L"EdgeAssetDeliveryServiceEnabled",0},{L"WalletDonationEnabled",0},{L"DefaultBrowserSettingsCampaignEnabled",0}});
    DWORD edge_shortcut{}; std::wstring edge_extension;
    snapshot.edge = edge_base&&read_dword(HKEY_LOCAL_MACHINE,L"SOFTWARE\\Policies\\Microsoft\\EdgeUpdate",L"CreateDesktopShortcutDefault",edge_shortcut)&&edge_shortcut==0&&read_string(HKEY_LOCAL_MACHINE,L"SOFTWARE\\Policies\\Microsoft\\Edge\\ExtensionInstallBlocklist",L"1",edge_extension)&&edge_extension==L"ofefcgjbeghpigppfmkologfjadafddi";
    snapshot.ctfmon = all(L"Software\\Microsoft\\Input",{{L"InputServiceEnabled",0},{L"InputServiceEnabledForCCI",0}});
    std::wstring text;
    snapshot.ctfmon_dll = read_string(HKEY_LOCAL_MACHINE,L"SYSTEM\\CurrentControlSet\\Services\\TextInputManagementService\\Parameters",L"ServiceDll",text)&&_wcsicmp(text.c_str(),L"%SystemRoot%\\System32\\MSCTF.DLL")==0;
    snapshot.timer_resolution = read_dword(HKEY_LOCAL_MACHINE,L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\kernel",L"GlobalTimerResolutionRequests",value)&&value==1;
    DWORD current{};
    if (read_dword(HKEY_LOCAL_MACHINE,L"SYSTEM\\CurrentControlSet\\Services\\Tcpip6\\Parameters",L"DisabledComponents",current)) { snapshot.ipv6_value = current; }
    snapshot.ipv6 = (snapshot.ipv6_value&0x20)!=0;
    snapshot.teredo = (snapshot.ipv6_value&1)!=0;
    snapshot.ps7 = read_string(HKEY_LOCAL_MACHINE,L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment",L"POWERSHELL_TELEMETRY_OPTOUT",text)&&text==L"1";
    return snapshot;
}

void ExtrasPage::apply_registry_snapshot(RegistrySnapshot const& snapshot){
    loading_ = true;
    ModernStandby().IsOn(snapshot.modern_standby);
    SyncProvider().IsOn(snapshot.sync_provider);
    Brave().IsOn(snapshot.brave);
    Edge().IsOn(snapshot.edge);
    Ctfmon().IsOn(snapshot.ctfmon);
    CtfmonDll().IsOn(snapshot.ctfmon_dll);
    TimerResolution().IsOn(snapshot.timer_resolution);
    Ipv6().IsOn(snapshot.ipv6);
    Teredo().IsOn(snapshot.teredo);
    Ps7().IsOn(snapshot.ps7);
    loading_ = false;
}

ExtrasPage::WorkResult ExtrasPage::do_registry_work(RegistryToggle which, bool enabled){
    bool ok = false;
    std::string note;
    // Snapshot helpers for group transactions: capture the current values
    // before writing so a partial failure can be rolled back.
    auto snap_dwords = [&](wchar_t const* path, auto const& rules) {
        std::vector<std::optional<DWORD>> snap;
        for (auto const& [name, _] : rules) {
            DWORD value{}, size = sizeof(value);
            snap.push_back(RegGetValueW(HKEY_LOCAL_MACHINE, path, name, RRF_RT_REG_DWORD, nullptr, &value, &size) == ERROR_SUCCESS
                          ? std::optional<DWORD>{value} : std::nullopt);
        }
        return snap;
    };
    auto restore_dwords = [&](wchar_t const* path, auto const& rules, std::vector<std::optional<DWORD>> const& snap) {
        bool restored = true;
        for (std::size_t i{}; i < rules.size(); ++i) {
            restored = write_dword(HKEY_LOCAL_MACHINE, path, rules[i].first, snap[i]) && restored;
        }
        return restored;
    };
    switch (which) {
    case RegistryToggle::modern_standby:
        ok = write_dword(HKEY_LOCAL_MACHINE,L"SYSTEM\\CurrentControlSet\\Control\\Power",L"PlatformAoAcOverride",enabled?std::optional<DWORD>{0}:std::nullopt);
        break;
    case RegistryToggle::sync_provider:
        ok = write_dword(HKEY_CURRENT_USER,L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced",L"ShowSyncProviderNotifications",enabled?0:1);
        break;
    case RegistryToggle::ctfmon:
        ok = write_dword(HKEY_LOCAL_MACHINE,L"Software\\Microsoft\\Input",L"InputServiceEnabled",enabled?0:1);
        ok = write_dword(HKEY_LOCAL_MACHINE,L"Software\\Microsoft\\Input",L"InputServiceEnabledForCCI",enabled?0:1)&&ok;
        break;
    case RegistryToggle::ctfmon_dll:
        ok = write_string(HKEY_LOCAL_MACHINE,L"SYSTEM\\CurrentControlSet\\Services\\TextInputManagementService\\Parameters",L"ServiceDll",enabled?L"%SystemRoot%\\System32\\MSCTF.DLL":L"%SystemRoot%\\System32\\TabSvc.dll");
        break;
    case RegistryToggle::timer_resolution:
        ok = write_dword(HKEY_LOCAL_MACHINE,L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\kernel",L"GlobalTimerResolutionRequests",enabled?std::optional<DWORD>{1}:std::nullopt);
        break;
    case RegistryToggle::ipv6: {
        DWORD current{};
        read_dword(HKEY_LOCAL_MACHINE,L"SYSTEM\\CurrentControlSet\\Services\\Tcpip6\\Parameters",L"DisabledComponents",current);
        ok = write_dword(HKEY_LOCAL_MACHINE,L"SYSTEM\\CurrentControlSet\\Services\\Tcpip6\\Parameters",L"DisabledComponents",enabled?current|0x20:current&~0x20);
        break;
    }
    case RegistryToggle::ps7:
        if (enabled) {
            ok = write_string(HKEY_LOCAL_MACHINE,L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment",L"POWERSHELL_TELEMETRY_OPTOUT",L"1");
        } else {
            HKEY key{};
            if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment",0,KEY_SET_VALUE,&key)==ERROR_SUCCESS){
                auto result=RegDeleteValueW(key,L"POWERSHELL_TELEMETRY_OPTOUT");RegCloseKey(key);ok=result==ERROR_SUCCESS||result==ERROR_FILE_NOT_FOUND;
            } else ok = false;
        }
        break;
    case RegistryToggle::brave: {
        constexpr wchar_t path[]=L"SOFTWARE\\Policies\\BraveSoftware\\Brave";
        std::vector<std::pair<wchar_t const*,DWORD>> rules={{L"BraveRewardsDisabled",1},{L"BraveWalletDisabled",1},{L"BraveVPNDisabled",1},{L"BraveAIChatEnabled",0},{L"BraveStatsPingEnabled",0},{L"BraveNewsDisabled",1},{L"BraveTalkDisabled",1},{L"TorDisabled",1},{L"BraveP3AEnabled",0},{L"UrlKeyedAnonymizedDataCollectionEnabled",0},{L"SafeBrowsingExtendedReportingEnabled",0},{L"MetricsReportingEnabled",0}};
        if(enabled){
            ok=true;
            const auto snapshot = snap_dwords(path, rules);
            for(auto const& [name,_]:rules)ok=backup_dword(L"Brave",path,name)&&ok;
            if(ok)for(auto const& [name,value]:rules)ok=write_dword(HKEY_LOCAL_MACHINE,path,name,value)&&ok;
            if(!ok)note = restore_dwords(path, rules, snapshot) ? " Previous values were restored." : " Previous values could not be fully restored.";
        }
        else{
            ok=true;
            for(std::size_t i{};i<rules.size();++i){
                if(restore_dword(L"Brave",path,rules[i].first))continue;
                ok=false;
                for(std::size_t j{};j<i;++j)write_dword(HKEY_LOCAL_MACHINE,path,rules[j].first,rules[j].second);
                note = " The policy state may be mixed; retry to complete the restore.";
                break;
            }
        }
        break;
    }
    case RegistryToggle::edge: {
        constexpr wchar_t path[]=L"SOFTWARE\\Policies\\Microsoft\\Edge";constexpr wchar_t update_path[]=L"SOFTWARE\\Policies\\Microsoft\\EdgeUpdate";constexpr wchar_t extension_path[]=L"SOFTWARE\\Policies\\Microsoft\\Edge\\ExtensionInstallBlocklist";
        constexpr wchar_t extension_name[]=L"1";constexpr wchar_t extension_value[]=L"ofefcgjbeghpigppfmkologfjadafddi";
        std::vector<std::pair<wchar_t const*,DWORD>> rules={{L"PersonalizationReportingEnabled",0},{L"ShowRecommendationsEnabled",0},{L"HideFirstRunExperience",1},{L"UserFeedbackAllowed",0},{L"ConfigureDoNotTrack",1},{L"AlternateErrorPagesEnabled",0},{L"EdgeCollectionsEnabled",0},{L"EdgeShoppingAssistantEnabled",0},{L"MicrosoftEdgeInsiderPromotionEnabled",0},{L"ShowMicrosoftRewards",0},{L"WebWidgetAllowed",0},{L"DiagnosticData",0},{L"EdgeAssetDeliveryServiceEnabled",0},{L"WalletDonationEnabled",0},{L"DefaultBrowserSettingsCampaignEnabled",0}};
        if(enabled){
            ok=true;
            const auto snapshot = snap_dwords(path, rules);
            const auto snapshot_update = snap_dwords(update_path, std::vector<std::pair<wchar_t const*,DWORD>>{{L"CreateDesktopShortcutDefault",0}});
            DWORD ext_size{};
            const bool ext_had_value = RegGetValueW(HKEY_LOCAL_MACHINE,extension_path,extension_name,RRF_RT_REG_SZ,nullptr,nullptr,&ext_size)==ERROR_SUCCESS;
            std::vector<BYTE> ext_data(ext_size);
            if(ext_had_value&&RegGetValueW(HKEY_LOCAL_MACHINE,extension_path,extension_name,RRF_RT_REG_SZ,nullptr,ext_data.data(),&ext_size)!=ERROR_SUCCESS){ok=false;}
            for(auto const& [name,_]:rules)ok=backup_dword(L"Edge",path,name)&&ok;
            ok=backup_dword(L"EdgeUpdate",update_path,L"CreateDesktopShortcutDefault")&&ok;
            ok=backup_string(L"EdgeExtension",extension_path,extension_name)&&ok;
            if(ok){for(auto const& [name,value]:rules)ok=write_dword(HKEY_LOCAL_MACHINE,path,name,value)&&ok;ok=write_dword(HKEY_LOCAL_MACHINE,update_path,L"CreateDesktopShortcutDefault",0)&&ok;ok=write_string(HKEY_LOCAL_MACHINE,extension_path,extension_name,extension_value)&&ok;}
            if(!ok){
                bool restored = restore_dwords(path, rules, snapshot);
                restored = restore_dwords(update_path, std::vector<std::pair<wchar_t const*,DWORD>>{{L"CreateDesktopShortcutDefault",0}}, snapshot_update) && restored;
                if(ext_had_value){
                    HKEY k{};
                    if(RegCreateKeyExW(HKEY_LOCAL_MACHINE,extension_path,0,nullptr,0,KEY_SET_VALUE,nullptr,&k,nullptr)==ERROR_SUCCESS){
                        restored = (RegSetValueExW(k,extension_name,0,REG_SZ,ext_data.data(),ext_size)==ERROR_SUCCESS) && restored;
                        RegCloseKey(k);
                    } else restored = false;
                } else {
                    HKEY k{};
                    if(RegOpenKeyExW(HKEY_LOCAL_MACHINE,extension_path,0,KEY_SET_VALUE,&k)==ERROR_SUCCESS){
                        const auto r=RegDeleteValueW(k,extension_name);RegCloseKey(k);
                        restored = (r==ERROR_SUCCESS||r==ERROR_FILE_NOT_FOUND) && restored;
                    } else restored = false;
                }
                note = restored ? " Previous values were restored." : " Previous values could not be fully restored.";
            }
        }
        else{
            ok=true;
            for(std::size_t i{};i<rules.size();++i){
                if(restore_dword(L"Edge",path,rules[i].first))continue;
                ok=false;
                for(std::size_t j{};j<i;++j)write_dword(HKEY_LOCAL_MACHINE,path,rules[j].first,rules[j].second);
                note = " The policy state may be mixed; retry to complete the restore.";
                break;
            }
            if(ok&&!restore_dword(L"EdgeUpdate",update_path,L"CreateDesktopShortcutDefault")){ok=false;write_dword(HKEY_LOCAL_MACHINE,update_path,L"CreateDesktopShortcutDefault",0);note=" The policy state may be mixed; retry to complete the restore.";}
            if(ok&&!restore_string(L"EdgeExtension",extension_path,extension_name)){ok=false;write_string(HKEY_LOCAL_MACHINE,extension_path,extension_name,extension_value);note=" The policy state may be mixed; retry to complete the restore.";}
        }
        break;
    }
    }
    return {ok, ok ? ERROR_SUCCESS : GetLastError(), std::move(note)};
}

winrt::fire_and_forget ExtrasPage::apply_registry_toggle(RegistryToggle which, muxc::ToggleSwitch toggle, bool enabled) {
    auto weak = get_weak();
    winrt::Microsoft::UI::Dispatching::DispatcherQueue queue{nullptr};
    try { queue = DispatcherQueue(); } catch (...) {}
    try {
        auto lifetime = get_strong();
        toggle.IsEnabled(false);
        loading_ = true;
        winrt::apartment_context ui;
        co_await winrt::resume_background();
        const auto work = do_registry_work(which, enabled);
        const auto snapshot = read_registry_snapshot();
        co_await ui;
        if (auto self = weak.get()) {
            self->apply_registry_snapshot(snapshot);
            toggle.IsEnabled(true);
            if (!work.ok) self->show_result(false, L"Could not apply the setting. Windows error " + std::to_wstring(work.error) + L"." + std::wstring(work.note.begin(), work.note.end()));
            else if (!work.note.empty()) self->show_result(true, L"Setting applied." + std::wstring(work.note.begin(), work.note.end()));
            else self->show_result(true, L"Setting applied.");
        }
    } catch (...) {
        winchisel::ui::report_async_error(queue, [weak](winrt::hstring const& text) {
            if (auto self = weak.get()) { self->loading_ = false; self->show_result(false, std::wstring(text)); }
        });
    }
}

void ExtrasPage::ToggleChanged(Windows::Foundation::IInspectable const& sender,Microsoft::UI::Xaml::RoutedEventArgs const&){
    if(loading_ || command_running_)return;
    auto toggle=sender.as<muxc::ToggleSwitch>();bool enabled=toggle.IsOn();
    if(toggle==Widgets()){loading_=true;toggle.IsOn(!enabled);loading_=false;run_command(CommandAction::widgets,enabled);return;}
    else if(toggle==Teredo()){loading_=true;toggle.IsOn(!enabled);loading_=false;run_command(CommandAction::teredo,enabled);return;}
    else if(toggle==Hpet()){loading_=true;toggle.IsOn(!enabled);loading_=false;run_command(CommandAction::hpet,enabled);return;}
    RegistryToggle which{};
    if(toggle==ModernStandby())which=RegistryToggle::modern_standby;
    else if(toggle==SyncProvider())which=RegistryToggle::sync_provider;
    else if(toggle==Ctfmon())which=RegistryToggle::ctfmon;
    else if(toggle==CtfmonDll())which=RegistryToggle::ctfmon_dll;
    else if(toggle==TimerResolution())which=RegistryToggle::timer_resolution;
    else if(toggle==Ipv6())which=RegistryToggle::ipv6;
    else if(toggle==Ps7())which=RegistryToggle::ps7;
    else if(toggle==Brave())which=RegistryToggle::brave;
    else if(toggle==Edge())which=RegistryToggle::edge;
    else return;
    apply_registry_toggle(which, toggle, enabled);
}
void ExtrasPage::set_command_busy(bool busy) {
 command_running_=busy; Loading().Visibility(busy?Microsoft::UI::Xaml::Visibility::Visible:Microsoft::UI::Xaml::Visibility::Collapsed); Items().IsHitTestVisible(!busy); PowerPlanButton().IsEnabled(!busy);
}

winrt::fire_and_forget ExtrasPage::load_command_states() {
    auto error_weak=get_weak();
    winrt::Microsoft::UI::Dispatching::DispatcherQueue error_queue{nullptr};
    try { error_queue=DispatcherQueue(); } catch (...) {}
    try {
        auto error_lifetime=get_strong();

 auto lifetime=get_strong(); if(command_running_) co_return; command_running_=true; Loading().Visibility(Microsoft::UI::Xaml::Visibility::Visible); Items().IsHitTestVisible(false); auto queue=DispatcherQueue();
 co_await winrt::resume_background(); const auto state=winchisel::platform::read_extras_command_state();
 (void)winchisel::ui::enqueue_safe(queue, [lifetime, state] {
  lifetime->command_running_=false; lifetime->Loading().Visibility(Microsoft::UI::Xaml::Visibility::Collapsed); lifetime->Items().IsHitTestVisible(true);
  lifetime->loading_=true; lifetime->Widgets().IsOn(state.widgets_removed); if(state.hpet_disabled) lifetime->Hpet().IsOn(*state.hpet_disabled);
  if(state.power_plan_active) lifetime->PowerPlanButton().Content(box_value(L"Active")); lifetime->loading_=false;
 });

    } catch (...) {
        winchisel::ui::report_async_error(error_queue, [error_weak](winrt::hstring const& text) {
            if (auto self=error_weak.get()) { self->set_command_busy(false); self->show_result(false,std::wstring(text)); }
        });
    }
}

winrt::fire_and_forget ExtrasPage::run_command(CommandAction action, bool enabled) {
    auto error_weak=get_weak();
    winrt::Microsoft::UI::Dispatching::DispatcherQueue error_queue{nullptr};
    try { error_queue=DispatcherQueue(); } catch (...) {}
    try {
        auto error_lifetime=get_strong();

 auto lifetime=get_strong(); if(command_running_) co_return; set_command_busy(true); ResultBar().IsOpen(false); auto queue=DispatcherQueue();
 co_await winrt::resume_background();
 winchisel::core::Result<void> result{};
 switch(action){case CommandAction::power_plan: result=winchisel::platform::apply_winchisel_power_plan(); break; case CommandAction::widgets: result=winchisel::platform::set_widgets_removed(enabled); break; case CommandAction::teredo: result=winchisel::platform::set_teredo_disabled(enabled); break; case CommandAction::hpet: result=winchisel::platform::set_hpet_disabled(enabled); break;}
 (void)winchisel::ui::enqueue_safe(queue, [lifetime, action, enabled, result] {
  lifetime->set_command_busy(false);
  if(!result){lifetime->load_states();lifetime->show_result(false,std::wstring(L"Could not apply the setting: ")+std::wstring(winrt::to_hstring(result.error().detail)));return;}
  lifetime->loading_=true;
  if(action==CommandAction::widgets)lifetime->Widgets().IsOn(enabled); else if(action==CommandAction::teredo)lifetime->Teredo().IsOn(enabled); else if(action==CommandAction::hpet)lifetime->Hpet().IsOn(enabled); else lifetime->PowerPlanButton().Content(box_value(L"Active"));
  lifetime->loading_=false; lifetime->show_result(true,action==CommandAction::power_plan?L"Winchisel power plan applied successfully.":L"Setting applied.");
 });

    } catch (...) {
        winchisel::ui::report_async_error(error_queue, [error_weak](winrt::hstring const& text) {
            if (auto self=error_weak.get()) { self->set_command_busy(false); self->show_result(false,std::wstring(text)); }
        });
    }
}
void ExtrasPage::PowerPlanClick(Windows::Foundation::IInspectable const&,Microsoft::UI::Xaml::RoutedEventArgs const&){run_command(CommandAction::power_plan);}
}
