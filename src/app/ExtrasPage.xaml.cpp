#include "pch.h"
#include "ExtrasPage.xaml.h"
#include "winchisel/platform/system.hpp"

#if __has_include("ExtrasPage.g.cpp")
#include "ExtrasPage.g.cpp"
#endif

namespace winrt::Winchisel::implementation {
namespace muxc=Microsoft::UI::Xaml::Controls;

ExtrasPage::ExtrasPage(){InitializeComponent();load_states();load_command_states();}
bool ExtrasPage::read_dword(HKEY root,wchar_t const* path,wchar_t const* name,DWORD& value){DWORD size=sizeof(value);return RegGetValueW(root,path,name,RRF_RT_REG_DWORD,nullptr,&value,&size)==ERROR_SUCCESS;}
bool ExtrasPage::read_string(HKEY root,wchar_t const* path,wchar_t const* name,std::wstring& value){DWORD size{};if(RegGetValueW(root,path,name,RRF_RT_REG_SZ,nullptr,nullptr,&size)!=ERROR_SUCCESS)return false;value.resize(size/sizeof(wchar_t));if(RegGetValueW(root,path,name,RRF_RT_REG_SZ,nullptr,value.data(),&size)!=ERROR_SUCCESS)return false;if(!value.empty()&&!value.back())value.pop_back();return true;}
bool ExtrasPage::write_dword(HKEY root,wchar_t const* path,wchar_t const* name,std::optional<DWORD> value){HKEY key{};if(RegCreateKeyExW(root,path,0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr)!=ERROR_SUCCESS)return false;LSTATUS result=value?RegSetValueExW(key,name,0,REG_DWORD,reinterpret_cast<BYTE const*>(&*value),sizeof(DWORD)):RegDeleteValueW(key,name);RegCloseKey(key);return result==ERROR_SUCCESS||(!value&&result==ERROR_FILE_NOT_FOUND);}
bool ExtrasPage::write_string(HKEY root,wchar_t const* path,wchar_t const* name,std::wstring const& value){HKEY key{};if(RegCreateKeyExW(root,path,0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr)!=ERROR_SUCCESS)return false;auto result=RegSetValueExW(key,name,0,REG_SZ,reinterpret_cast<BYTE const*>(value.c_str()),static_cast<DWORD>((value.size()+1)*sizeof(wchar_t)));RegCloseKey(key);return result==ERROR_SUCCESS;}
void ExtrasPage::show_result(bool ok,std::wstring const& text){ResultBar().Severity(ok?muxc::InfoBarSeverity::Success:muxc::InfoBarSeverity::Error);ResultBar().Message(text);ResultBar().IsOpen(true);}

void ExtrasPage::load_states(){
 loading_=true;DWORD value{};std::wstring text;
 ModernStandby().IsOn(read_dword(HKEY_LOCAL_MACHINE,L"SYSTEM\\CurrentControlSet\\Control\\Power",L"PlatformAoAcOverride",value)&&value==0);
 SyncProvider().IsOn(read_dword(HKEY_CURRENT_USER,L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced",L"ShowSyncProviderNotifications",value)&&value==0);
 auto all=[&](wchar_t const* path,std::initializer_list<std::pair<wchar_t const*,DWORD>> rules){for(auto const& [name,expected]:rules)if(!read_dword(HKEY_LOCAL_MACHINE,path,name,value)||value!=expected)return false;return true;};
 Brave().IsOn(all(L"SOFTWARE\\Policies\\BraveSoftware\\Brave",{{L"BraveRewardsDisabled",1},{L"BraveWalletDisabled",1},{L"BraveVPNDisabled",1},{L"BraveAIChatEnabled",0},{L"BraveStatsPingEnabled",0},{L"BraveNewsDisabled",1},{L"BraveTalkDisabled",1},{L"TorDisabled",1},{L"BraveP3AEnabled",0},{L"UrlKeyedAnonymizedDataCollectionEnabled",0},{L"SafeBrowsingExtendedReportingEnabled",0},{L"MetricsReportingEnabled",0}}));
 const bool edge_base=all(L"SOFTWARE\\Policies\\Microsoft\\Edge",{{L"PersonalizationReportingEnabled",0},{L"ShowRecommendationsEnabled",0},{L"HideFirstRunExperience",1},{L"UserFeedbackAllowed",0},{L"ConfigureDoNotTrack",1},{L"AlternateErrorPagesEnabled",0},{L"EdgeCollectionsEnabled",0},{L"EdgeShoppingAssistantEnabled",0},{L"MicrosoftEdgeInsiderPromotionEnabled",0},{L"ShowMicrosoftRewards",0},{L"WebWidgetAllowed",0},{L"DiagnosticData",0},{L"EdgeAssetDeliveryServiceEnabled",0},{L"WalletDonationEnabled",0},{L"DefaultBrowserSettingsCampaignEnabled",0}});
 DWORD edge_shortcut{}; std::wstring edge_extension;
 Edge().IsOn(edge_base&&read_dword(HKEY_LOCAL_MACHINE,L"SOFTWARE\\Policies\\Microsoft\\EdgeUpdate",L"CreateDesktopShortcutDefault",edge_shortcut)&&edge_shortcut==0&&read_string(HKEY_LOCAL_MACHINE,L"SOFTWARE\\Policies\\Microsoft\\Edge\\ExtensionInstallBlocklist",L"1",edge_extension)&&edge_extension==L"ofefcgjbeghpigppfmkologfjadafddi");
 Ctfmon().IsOn(all(L"Software\\Microsoft\\Input",{{L"InputServiceEnabled",0},{L"InputServiceEnabledForCCI",0}}));
 CtfmonDll().IsOn(read_string(HKEY_LOCAL_MACHINE,L"SYSTEM\\CurrentControlSet\\Services\\TextInputManagementService\\Parameters",L"ServiceDll",text)&&_wcsicmp(text.c_str(),L"%SystemRoot%\\System32\\MSCTF.DLL")==0);
 TimerResolution().IsOn(read_dword(HKEY_LOCAL_MACHINE,L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\kernel",L"GlobalTimerResolutionRequests",value)&&value==1);
 Ipv6().IsOn(read_dword(HKEY_LOCAL_MACHINE,L"SYSTEM\\CurrentControlSet\\Services\\Tcpip6\\Parameters",L"DisabledComponents",value)&&(value&0x20));
 Teredo().IsOn(read_dword(HKEY_LOCAL_MACHINE,L"SYSTEM\\CurrentControlSet\\Services\\Tcpip6\\Parameters",L"DisabledComponents",value)&&(value&1));
 Ps7().IsOn(read_string(HKEY_LOCAL_MACHINE,L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment",L"POWERSHELL_TELEMETRY_OPTOUT",text)&&text==L"1");
 loading_=false;
}

void ExtrasPage::ToggleChanged(Windows::Foundation::IInspectable const& sender,Microsoft::UI::Xaml::RoutedEventArgs const&){if(loading_ || command_running_)return;auto toggle=sender.as<muxc::ToggleSwitch>();bool enabled=toggle.IsOn(),ok=true;
 if(toggle==ModernStandby())ok=write_dword(HKEY_LOCAL_MACHINE,L"SYSTEM\\CurrentControlSet\\Control\\Power",L"PlatformAoAcOverride",enabled?std::optional<DWORD>{0}:std::nullopt);
 else if(toggle==SyncProvider())ok=write_dword(HKEY_CURRENT_USER,L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced",L"ShowSyncProviderNotifications",enabled?0:1);
 else if(toggle==Ctfmon()){ok=write_dword(HKEY_LOCAL_MACHINE,L"Software\\Microsoft\\Input",L"InputServiceEnabled",enabled?0:1);ok=write_dword(HKEY_LOCAL_MACHINE,L"Software\\Microsoft\\Input",L"InputServiceEnabledForCCI",enabled?0:1)&&ok;}
 else if(toggle==CtfmonDll())ok=write_string(HKEY_LOCAL_MACHINE,L"SYSTEM\\CurrentControlSet\\Services\\TextInputManagementService\\Parameters",L"ServiceDll",enabled?L"%SystemRoot%\\System32\\MSCTF.DLL":L"%SystemRoot%\\System32\\TabSvc.dll");
 else if(toggle==TimerResolution())ok=write_dword(HKEY_LOCAL_MACHINE,L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\kernel",L"GlobalTimerResolutionRequests",enabled?std::optional<DWORD>{1}:std::nullopt);
 else if(toggle==Ipv6()){DWORD current{};read_dword(HKEY_LOCAL_MACHINE,L"SYSTEM\\CurrentControlSet\\Services\\Tcpip6\\Parameters",L"DisabledComponents",current);ok=write_dword(HKEY_LOCAL_MACHINE,L"SYSTEM\\CurrentControlSet\\Services\\Tcpip6\\Parameters",L"DisabledComponents",enabled?current|0x20:current&~0x20);}
 else if(toggle==Ps7()){if(enabled)ok=write_string(HKEY_LOCAL_MACHINE,L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment",L"POWERSHELL_TELEMETRY_OPTOUT",L"1");else{HKEY key{};if(RegOpenKeyExW(HKEY_LOCAL_MACHINE,L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment",0,KEY_SET_VALUE,&key)==ERROR_SUCCESS){auto result=RegDeleteValueW(key,L"POWERSHELL_TELEMETRY_OPTOUT");RegCloseKey(key);ok=result==ERROR_SUCCESS||result==ERROR_FILE_NOT_FOUND;}}}
 else if(toggle==Brave()){std::vector<std::pair<wchar_t const*,DWORD>> rules={{L"BraveRewardsDisabled",1},{L"BraveWalletDisabled",1},{L"BraveVPNDisabled",1},{L"BraveAIChatEnabled",0},{L"BraveStatsPingEnabled",0},{L"BraveNewsDisabled",1},{L"BraveTalkDisabled",1},{L"TorDisabled",1},{L"BraveP3AEnabled",0},{L"UrlKeyedAnonymizedDataCollectionEnabled",0},{L"SafeBrowsingExtendedReportingEnabled",0},{L"MetricsReportingEnabled",0}};for(auto const& [name,value]:rules)ok=write_dword(HKEY_LOCAL_MACHINE,L"SOFTWARE\\Policies\\BraveSoftware\\Brave",name,enabled?std::optional<DWORD>{value}:std::nullopt)&&ok;}
 else if(toggle==Edge()){std::vector<std::pair<wchar_t const*,DWORD>> rules={{L"PersonalizationReportingEnabled",0},{L"ShowRecommendationsEnabled",0},{L"HideFirstRunExperience",1},{L"UserFeedbackAllowed",0},{L"ConfigureDoNotTrack",1},{L"AlternateErrorPagesEnabled",0},{L"EdgeCollectionsEnabled",0},{L"EdgeShoppingAssistantEnabled",0},{L"MicrosoftEdgeInsiderPromotionEnabled",0},{L"ShowMicrosoftRewards",0},{L"WebWidgetAllowed",0},{L"DiagnosticData",0},{L"EdgeAssetDeliveryServiceEnabled",0},{L"WalletDonationEnabled",0},{L"DefaultBrowserSettingsCampaignEnabled",0}};for(auto const& [name,value]:rules)ok=write_dword(HKEY_LOCAL_MACHINE,L"SOFTWARE\\Policies\\Microsoft\\Edge",name,enabled?std::optional<DWORD>{value}:std::nullopt)&&ok;ok=write_dword(HKEY_LOCAL_MACHINE,L"SOFTWARE\\Policies\\Microsoft\\EdgeUpdate",L"CreateDesktopShortcutDefault",enabled?std::optional<DWORD>{0}:std::nullopt)&&ok;if(enabled)ok=write_string(HKEY_LOCAL_MACHINE,L"SOFTWARE\\Policies\\Microsoft\\Edge\\ExtensionInstallBlocklist",L"1",L"ofefcgjbeghpigppfmkologfjadafddi")&&ok;}
 else if(toggle==Widgets()){loading_=true;toggle.IsOn(!enabled);loading_=false;run_command(CommandAction::widgets,enabled);return;}
 else if(toggle==Teredo()){loading_=true;toggle.IsOn(!enabled);loading_=false;run_command(CommandAction::teredo,enabled);return;}
 else if(toggle==Hpet()){loading_=true;toggle.IsOn(!enabled);loading_=false;run_command(CommandAction::hpet,enabled);return;}
 else return;
 if(!ok){load_states();show_result(false,L"Could not apply the setting.");}else show_result(true,L"Setting applied.");
}
void ExtrasPage::set_command_busy(bool busy) {
 command_running_=busy; Loading().IsActive(busy); Items().IsHitTestVisible(!busy); PowerPlanButton().IsEnabled(!busy);
}

winrt::fire_and_forget ExtrasPage::load_command_states() {
 auto lifetime=get_strong(); if(command_running_) co_return; command_running_=true; Loading().IsActive(true); Items().IsHitTestVisible(false); auto queue=DispatcherQueue();
 co_await winrt::resume_background(); const auto state=winchisel::platform::read_extras_command_state();
 (void)queue.TryEnqueue([lifetime, state] {
  lifetime->command_running_=false; lifetime->Loading().IsActive(false); lifetime->Items().IsHitTestVisible(true);
  lifetime->loading_=true; lifetime->Widgets().IsOn(state.widgets_removed); lifetime->Hpet().IsOn(state.hpet_disabled);
  if(state.power_plan_active) lifetime->PowerPlanButton().Content(box_value(L"Active")); lifetime->loading_=false;
 });
}

winrt::fire_and_forget ExtrasPage::run_command(CommandAction action, bool enabled) {
 auto lifetime=get_strong(); if(command_running_) co_return; set_command_busy(true); ResultBar().IsOpen(false); auto queue=DispatcherQueue();
 co_await winrt::resume_background();
 winchisel::core::Result<void> result{};
 switch(action){case CommandAction::power_plan: result=winchisel::platform::apply_winchisel_power_plan(); break; case CommandAction::widgets: result=winchisel::platform::set_widgets_removed(enabled); break; case CommandAction::teredo: result=winchisel::platform::set_teredo_disabled(enabled); break; case CommandAction::hpet: result=winchisel::platform::set_hpet_disabled(enabled); break;}
 (void)queue.TryEnqueue([lifetime, action, enabled, result] {
  lifetime->set_command_busy(false);
  if(!result){lifetime->load_states();lifetime->show_result(false,L"Could not apply the setting.");return;}
  lifetime->loading_=true;
  if(action==CommandAction::widgets)lifetime->Widgets().IsOn(enabled); else if(action==CommandAction::teredo)lifetime->Teredo().IsOn(enabled); else if(action==CommandAction::hpet)lifetime->Hpet().IsOn(enabled); else lifetime->PowerPlanButton().Content(box_value(L"Active"));
  lifetime->loading_=false; lifetime->show_result(true,action==CommandAction::power_plan?L"Winchisel power plan applied successfully.":L"Setting applied.");
 });
}
void ExtrasPage::PowerPlanClick(Windows::Foundation::IInspectable const&,Microsoft::UI::Xaml::RoutedEventArgs const&){run_command(CommandAction::power_plan);}
}
