#include "pch.h"
#include "PerformancePage.xaml.h"
#include "winchisel/platform/system.hpp"

#if __has_include("PerformancePage.g.cpp")
#include "PerformancePage.g.cpp"
#endif

#include "winchisel/core/tweak.hpp"
#include "winchisel/platform/registry.hpp"
#include "winchisel/platform/performance.hpp"

#include <array>
#include <algorithm>
#include <cctype>

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace winrt::Winchisel::implementation {
void PerformancePage::show_write_error(std::string const& detail){auto message=detail.empty()?"A Windows setting could not be changed. See %APPDATA%\\Winchisel\\logs\\winchisel.log for details.":detail;ResultBar().Title(L"Could not apply setting");ResultBar().Message(to_hstring(message));ResultBar().Severity(Controls::InfoBarSeverity::Error);ResultBar().IsOpen(true);winchisel::platform::boot_log(("performance UI: "+message).c_str());}
namespace {

using Hive = winchisel::core::RegistryHive;
using Type = winchisel::core::RegistryValueType;
using Target = winchisel::core::RegistryTarget;
using Value = winchisel::core::RegistryValue;

std::string lower(std::string value) { std::ranges::transform(value,value.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});return value; }

std::string_view service_name(std::string_view id) {
    static constexpr std::pair<std::string_view,std::string_view> values[]{
        {"gaming-sysmain-service","SysMain"},{"gaming-windows-search-service","WSearch"},{"gaming-print-spooler-service","Spooler"},{"gaming-telemetry-service","DiagTrack"},{"gaming-connected-devices-platform-service","CDPSvc"},{"gaming-compatibility-assistant-service","PcaSvc"},{"gaming-error-reporting-service","WerSvc"},{"gaming-geolocation-service","lfsvc"},{"gaming-retail-demo-service","RetailDemo"},{"gaming-insider-service","wisvc"},{"gaming-phone-service","PhoneSvc"},{"gaming-wallet-service","WalletService"},{"gaming-smart-card-services","SCardSvr"},{"gaming-maps-broker-service","MapsBroker"},{"gaming-fax-service","Fax"},{"gaming-wmp-network-service","WMPNetworkSvc"},{"gaming-mixed-reality-service","MixedRealityOpenXRSvc"},{"gaming-mobile-hotspot-service","icssvc"},{"gaming-sms-router-service","SmsRouter"},{"gaming-parental-controls-service","WpcMonSvc"},{"gaming-payments-nfc-service","SEMgrSvc"},{"gaming-spot-verifier-service","svsvc"},{"gaming-remote-access-manager","RasMan"},{"gaming-remote-access-auto","RasAuto"},{"gaming-remote-desktop-services","TermService"},{"gaming-remote-desktop-configuration","SessionEnv"},{"gaming-remote-desktop-port-redirector","UmRdpService"},{"gaming-xbox-auth-manager","XblAuthManager"},{"gaming-xbox-game-save","XblGameSave"},{"gaming-xbox-networking","XboxNetApiSvc"},{"gaming-biometric-service","WbioSrvc"},{"gaming-touch-keyboard-service","TabletInputService"},{"gaming-sensor-monitoring-service","SensrSvc"},{"gaming-sensor-data-service","SensorDataService"},{"gaming-ai-fabric-service","AIFabricSvc"}};
    for(auto const& [key,name]:values)if(key==id)return name;return {};
}

Target target(Hive hive, char const* key, char const* name, Type type) {
    return {.hive = hive, .key_path = key, .value_name = name, .type = type};
}

Controls::Border setting_card(hstring const& title, hstring const& description, FrameworkElement const& control) {
    auto card = Controls::Border();
    auto resources = Application::Current().Resources();
    card.Background(resources.Lookup(box_value(L"CardBackgroundFillColorDefaultBrush")).try_as<Media::Brush>());
    card.BorderBrush(resources.Lookup(box_value(L"CardStrokeColorDefaultBrush")).try_as<Media::Brush>());
    card.BorderThickness({1, 1, 1, 1});
    card.Padding({16, 12, 16, 12});
    card.HorizontalAlignment(HorizontalAlignment::Stretch);
    card.Tag(box_value(title + L" " + description));

    auto layout = Controls::Grid();
    layout.ColumnDefinitions().Append(Controls::ColumnDefinition());
    auto trailing_column = Controls::ColumnDefinition();
    trailing_column.Width(GridLength{0, GridUnitType::Auto});
    layout.ColumnDefinitions().Append(trailing_column);
    auto text = Controls::StackPanel();
    auto heading = Controls::TextBlock();
    heading.Text(title);
    heading.Style(resources.Lookup(box_value(L"BodyStrongTextBlockStyle")).try_as<Style>());
    text.Children().Append(heading);
    auto detail = Controls::TextBlock();
    detail.Text(description);
    detail.TextWrapping(TextWrapping::Wrap);
    detail.Style(resources.Lookup(box_value(L"CaptionTextBlockStyle")).try_as<Style>());
    text.Children().Append(detail);
    layout.Children().Append(text);
    Controls::Grid::SetColumn(control, 1);
    control.VerticalAlignment(VerticalAlignment::Center);
    control.HorizontalAlignment(HorizontalAlignment::Right);
    layout.Children().Append(control);
    card.Child(layout);
    return card;
}

}  // namespace

PerformancePage::PerformancePage() {
    InitializeComponent();
    gaming_toggles_ = {
        {L"Game Mode", L"Optimizes Windows scheduling for games.", true, true, false, true,
         {target(Hive::current_user, "Software\\Microsoft\\GameBar", "AutoGameModeEnabled", Type::dword)},
         {Value{std::uint32_t{1}}}, {Value{std::uint32_t{0}}}},
        {L"Enhance Pointer Precision", L"Uses Windows mouse acceleration.", false, true, false, false,
         {target(Hive::current_user, "Control Panel\\Mouse", "MouseSpeed", Type::string)},
         {Value{std::string{"1"}}}, {Value{std::string{"0"}}}},
        {L"Startup Delay for Apps", L"Delays startup applications after sign-in.", false, false, false, false,
         {target(Hive::current_user, "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Serialize", "StartupDelayInMSec", Type::dword)},
         {Value{std::uint32_t{10000}}}, {Value{std::uint32_t{0}}}},
        {L"Storage Sense", L"Lets Windows automatically free storage space.", false, true, true, true,
         {target(Hive::current_user, "SOFTWARE\\Policies\\Microsoft\\Windows\\StorageSense", "AllowStorageSenseGlobal", Type::dword),
          target(Hive::local_machine, "SOFTWARE\\Policies\\Microsoft\\Windows\\StorageSense", "AllowStorageSenseGlobal", Type::dword)},
         {Value{std::uint32_t{1}}, Value{std::uint32_t{1}}}, {Value{std::uint32_t{0}}, Value{std::uint32_t{0}}}},
        {L"Search Entire File System", L"Includes all files in Explorer search.", false, false, false, false,
         {target(Hive::current_user, "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Search\\Preferences", "WholeFileSystem", Type::dword)},
         {Value{std::uint32_t{1}}}, {Value{std::uint32_t{0}}}},
        {L"WebView2 in Windows Search", L"Controls web content in the Windows Search experience.", false, true, false, false,
         {target(Hive::local_machine, "SYSTEM\\CurrentControlSet\\Control\\FeatureManagement\\Overrides\\8\\1694661260", "EnabledState", Type::dword),
          target(Hive::local_machine, "SYSTEM\\CurrentControlSet\\Control\\FeatureManagement\\Overrides\\8\\1694661260", "EnabledStateOptions", Type::dword),
          target(Hive::local_machine, "SYSTEM\\CurrentControlSet\\Control\\FeatureManagement\\Overrides\\8\\1694661260", "Variant", Type::dword),
          target(Hive::local_machine, "SYSTEM\\CurrentControlSet\\Control\\FeatureManagement\\Overrides\\8\\1694661260", "VariantPayload", Type::dword),
          target(Hive::local_machine, "SYSTEM\\CurrentControlSet\\Control\\FeatureManagement\\Overrides\\8\\1694661260", "VariantPayloadKind", Type::dword)},
         {Value{std::uint32_t{2}}, Value{std::monostate{}}, Value{std::monostate{}}, Value{std::monostate{}}, Value{std::monostate{}}},
         {Value{std::uint32_t{1}}, Value{std::uint32_t{0}}, Value{std::uint32_t{0}}, Value{std::uint32_t{0}}, Value{std::uint32_t{0}}}},
        {L"Allow Desktop Wallpaper Compression", L"Controls JPEG compression for desktop wallpapers.", false, true, false, true,
         {target(Hive::current_user, "Control Panel\\Desktop", "JPEGImportQuality", Type::dword)},
         {Value{std::uint32_t{0}}}, {Value{std::uint32_t{100}}}},
        {L"Enable Menu Show Delay", L"Adds a delay before Windows menus open.", false, true, false, false,
         {target(Hive::current_user, "Control Panel\\Desktop", "MenuShowDelay", Type::string)},
         {Value{std::string{"400"}}}, {Value{std::string{"0"}}}},
        {L"Alt+Tab Filter", L"Controls which window types appear in Alt+Tab.", true, true, false, false,
         {target(Hive::current_user, "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced", "MultiTaskingAltTabFilter", Type::dword)},
         {Value{std::uint32_t{3}}}, {Value{std::uint32_t{0}}}},
    };

    std::int32_t group_index = 0;
    for (const auto& group : winchisel::core::k_performance_groups) {
        Controls::Expander expander;
        expander.Header(box_value(to_hstring(group.title)));
        expander.HorizontalAlignment(HorizontalAlignment::Stretch);
        expander.HorizontalContentAlignment(HorizontalAlignment::Stretch);
        expander.IsExpanded(group.id == "gaming");
        if (group.id == "gaming") {
            auto content = Controls::StackPanel();
            content.Spacing(8);
            content.HorizontalAlignment(HorizontalAlignment::Stretch);
            for (std::size_t index = 0; index < gaming_toggles_.size(); ++index) {
                auto& tweak = gaming_toggles_[index];
                tweak.control = Controls::ToggleSwitch();
                Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(tweak.control,tweak.title);
                tweak.control.OnContent(box_value(L""));
                tweak.control.OffContent(box_value(L""));
                tweak.control.MinWidth(0);
                tweak.control.Width(40);
                tweak.control.Toggled([this, index](auto&&, auto&&) { save_gaming_toggle(index); });
                content.Children().Append(setting_card(tweak.title, tweak.description, tweak.control));
            }
            mouse_hover_time_ = Controls::ComboBox();
            Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(mouse_hover_time_,L"Mouse Hover Time");
            for (auto const& option : {L"1ms (Instant)", L"10ms (Very Fast)", L"50ms (Fast)", L"100ms (Moderate)", L"200ms", L"400ms (Default)"}) {
                auto item = Controls::ComboBoxItem();
                item.Content(box_value(option));
                mouse_hover_time_.Items().Append(item);
            }
            mouse_hover_time_.SelectionChanged([this](auto&&, auto&&) { save_mouse_hover_time(); });
            content.Children().Append(setting_card(L"Mouse Hover Time", L"Sets how long the pointer must hover before Windows responds.", mouse_hover_time_));
            background_apps_ = Controls::ComboBox();
            Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(background_apps_,L"Let Apps Run in Background");
            for (auto const& option : {L"User in Control (Default)", L"Force Allow", L"Force Deny"}) {
                auto item = Controls::ComboBoxItem();
                item.Content(box_value(option));
                background_apps_.Items().Append(item);
            }
            background_apps_.SelectionChanged([this](auto&&, auto&&) { save_background_apps(); });
            content.Children().Append(setting_card(L"Let Apps Run in Background", L"Controls whether apps may continue running in the background.", background_apps_));
            expander.Content(content);
        } else {
            auto content = Controls::StackPanel();
            content.Spacing(8);
            content.HorizontalAlignment(HorizontalAlignment::Stretch);
            for (auto const& item : winchisel::core::get_performance_catalog()) {
                if (item.group != group_index) continue;
                if (item.id=="gaming-memory-integrity"||item.id=="gaming-performance-prefetch"||item.id=="gaming-disable-mpo-min-fps") continue;
                FrameworkElement control{nullptr};
                if (item.input == 0) {
                    auto toggle = Controls::ToggleSwitch();
                    Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(toggle,to_hstring(item.name));
                    toggle.OnContent(box_value(L""));
                    toggle.OffContent(box_value(L""));
                    toggle.MinWidth(0);
                    toggle.Width(40);
                    const auto toggle_index = catalog_toggles_.size();
                    const bool supported = item.group==7 || std::ranges::any_of(winchisel::core::get_performance_registry_rules(), [&](auto const& rule) { return rule.id == item.id; });
                    toggle.IsEnabled(supported);
                    catalog_toggles_.push_back({std::string(item.id), toggle});
                    toggle.Toggled([this, toggle_index](auto&&, auto&&) { save_catalog_toggle(toggle_index); });
                    control = toggle;
                } else {
                    auto combo = Controls::ComboBox();
                    Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(combo,to_hstring(item.name));
                    std::vector<std::string> options;
                    std::size_t start{};
                    while (start <= item.options.size()) {
                        auto end = item.options.find('|', start);
                        if (end == std::string_view::npos) end = item.options.size();
                        auto option = item.options.substr(start, end - start);
                        if (!option.empty()) { options.emplace_back(option); auto choice = Controls::ComboBoxItem(); choice.Content(box_value(to_hstring(option))); combo.Items().Append(choice); }
                        if (end == item.options.size()) break;
                        start = end + 1;
                    }
                    combo.SelectedIndex(combo.Items().Size() ? 0 : -1);
                    const auto selection_index=catalog_selections_.size();
                    combo.IsEnabled(true);
                    catalog_selections_.push_back({std::string(item.id),std::move(options),combo});
                    combo.SelectionChanged([this,selection_index](auto&&,auto&&){save_catalog_selection(selection_index);});
                    control = combo;
                }
                auto card=setting_card(to_hstring(item.name),to_hstring(item.description),control);
                std::string_view child_id;
                if(item.id=="gaming-virtualization-based-security")child_id="gaming-memory-integrity";
                else if(item.id=="gaming-sysmain-service")child_id="gaming-performance-prefetch";
                else if(item.id=="gaming-disable-mpo")child_id="gaming-disable-mpo-min-fps";
                if(child_id.empty())content.Children().Append(card);
                else {
                    auto nested=Controls::Expander();nested.Header(card);nested.HorizontalAlignment(HorizontalAlignment::Stretch);nested.HorizontalContentAlignment(HorizontalAlignment::Stretch);
                    auto child=std::ranges::find_if(winchisel::core::get_performance_catalog(),[&](auto const& candidate){return candidate.id==child_id;});
                    if(child!=winchisel::core::get_performance_catalog().end()){
                        auto toggle=Controls::ToggleSwitch();Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(toggle,to_hstring(child->name));toggle.OnContent(box_value(L""));toggle.OffContent(box_value(L""));toggle.MinWidth(0);toggle.Width(40);auto child_index=catalog_toggles_.size();toggle.IsEnabled(true);catalog_toggles_.push_back({std::string(child->id),toggle});toggle.Toggled([this,child_index](auto&&,auto&&){save_catalog_toggle(child_index);});auto child_card=setting_card(to_hstring(child->name),to_hstring(child->description),toggle);child_card.Margin({24,8,0,0});nested.Content(child_card);
                    }
                    content.Children().Append(nested);
                }
            }
            expander.Content(content);
        }
        Groups().Children().Append(expander);
        ++group_index;
    }
    load_gaming_toggles();
    load_gaming_selections();
    load_catalog_toggles();
    load_catalog_selections();
}

void PerformancePage::load_catalog_toggles() {
    loading_gaming_toggles_ = true;
    for (auto& item : catalog_toggles_) {
        bool found{}, enabled = true;
        for (auto const& rule : winchisel::core::get_performance_registry_rules()) {
            if (rule.id != item.id) continue;
            found = true;
            auto type = rule.kind == 0 ? Type::dword : rule.kind == 1 ? Type::string : Type::binary;
            auto actual = winchisel::platform::read_registry_value(target(rule.root == 0 ? Hive::current_user : Hive::local_machine, rule.path.data(), rule.name.data(), type));
            bool matches{};
            if (actual) {
                std::size_t start{};
                while (start <= rule.enabled_values.size()) {
                    auto end = rule.enabled_values.find('|', start); if (end == std::string_view::npos) end = rule.enabled_values.size(); auto expected = rule.enabled_values.substr(start, end-start);
                    if (expected == "__MISSING__" && std::holds_alternative<std::monostate>(*actual)) matches = true;
                    else if (auto dword=std::get_if<std::uint32_t>(&*actual); dword && expected==std::to_string(*dword)) matches=true;
                    else if (auto text=std::get_if<std::string>(&*actual); text && expected==*text) matches=true;
                    else if (auto bytes=std::get_if<std::vector<std::uint8_t>>(&*actual); bytes && rule.byte_index>=0 && static_cast<std::size_t>(rule.byte_index)<bytes->size()) matches=(((*bytes)[rule.byte_index]&rule.bit_mask)!=0);
                    if(matches||end==rule.enabled_values.size())break;start=end+1;
                }
            }
            enabled = enabled && matches;
        }
        if (found) item.control.IsOn(enabled);
        else if(auto state=winchisel::platform::read_scheduled_task(item.id);state)item.control.IsOn(*state);
    }
    loading_gaming_toggles_ = false;
}

void PerformancePage::save_catalog_toggle(std::size_t index) {
    if (loading_gaming_toggles_ || index >= catalog_toggles_.size()) return;
    auto const& item = catalog_toggles_[index]; const bool enabled = item.control.IsOn(); bool ok = true;
    for (auto const& rule : winchisel::core::get_performance_registry_rules()) {
        if (rule.id != item.id) continue;
        auto type = rule.kind == 0 ? Type::dword : rule.kind == 1 ? Type::string : Type::binary;
        auto destination = target(rule.root == 0 ? Hive::current_user : Hive::local_machine, rule.path.data(), rule.name.data(), type);
        auto values = enabled ? rule.enabled_values : rule.disabled_values; auto separator = values.find('|'); auto value = values.substr(0, separator);
        Value desired;
        if (rule.kind == 0) desired = value == "__MISSING__" ? Value{std::monostate{}} : Value{static_cast<std::uint32_t>(std::stoul(std::string(value)))};
        else if (rule.kind == 1) desired = value == "__MISSING__" ? Value{std::monostate{}} : Value{std::string(value)};
        else { auto current=winchisel::platform::read_registry_value(destination); auto bytes=current?std::get_if<std::vector<std::uint8_t>>(&*current):nullptr; std::vector<std::uint8_t> data=bytes?*bytes:std::vector<std::uint8_t>{}; if(rule.byte_index<0)continue;if(data.size()<=static_cast<std::size_t>(rule.byte_index))data.resize(rule.byte_index+1);if(enabled)data[rule.byte_index]|=rule.bit_mask;else data[rule.byte_index]&=static_cast<std::uint8_t>(~rule.bit_mask);desired=std::move(data); }
        if (!winchisel::platform::write_registry_value(destination, desired)) ok = false;
    }
    const bool has_registry=std::ranges::any_of(winchisel::core::get_performance_registry_rules(),[&](auto const& rule){return rule.id==item.id;});
    if(!has_registry&&!winchisel::platform::write_scheduled_task(item.id,enabled))ok=false;
    if (!ok) {show_write_error();load_catalog_toggles();}
}

void PerformancePage::load_catalog_selections() {
    loading_gaming_selections_ = true;
    for (auto& item : catalog_selections_) {
        if(item.id=="gaming-dns-server"){auto profile=winchisel::platform::read_dns_profile();if(profile)item.control.SelectedIndex(*profile);continue;}
        std::uint32_t value{}; bool found{};
        Target destination;
        if (item.id=="gaming-win32-priority") destination=target(Hive::local_machine,"SYSTEM\\CurrentControlSet\\Control\\PriorityControl","Win32PrioritySeparation",Type::dword);
        else if(item.id=="gaming-performance-svchost-split-threshold") destination=target(Hive::local_machine,"SYSTEM\\CurrentControlSet\\Control","SvcHostSplitThresholdInKB",Type::dword);
        else if(item.id=="visual-effects-mode") destination=target(Hive::current_user,"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\VisualEffects","VisualFXSetting",Type::dword);
        else if(auto service=service_name(item.id);!service.empty()) destination=target(Hive::local_machine,("SYSTEM\\CurrentControlSet\\Services\\"+std::string(service)).c_str(),"Start",Type::dword);
        else continue;
        auto current=winchisel::platform::read_registry_value(destination);if(current)if(auto dword=std::get_if<std::uint32_t>(&*current)){value=*dword;found=true;}if(!found)continue;
        int selected=0;
        if(item.id=="gaming-win32-priority")selected=value==24?1:0;
        else if(item.id=="gaming-performance-svchost-split-threshold"){constexpr std::array<std::uint32_t,10> values{380000,327680,491520,655360,983040,1310720,1966080,2621440,5242880,10485760};for(std::size_t i{};i<values.size();++i)if(values[i]==value)selected=static_cast<int>(i);}
        else if(item.id=="visual-effects-mode")selected=static_cast<int>(std::min(value,3u));
        else for(std::size_t i{};i<item.options.size();++i){auto option=lower(item.options[i]);if((value==4&&option.find("disabled")!=std::string::npos)||(value==3&&option.find("manual")!=std::string::npos)||(value==2&&option.find("automatic")!=std::string::npos)){selected=static_cast<int>(i);break;}}
        item.control.SelectedIndex(selected);
    }
    loading_gaming_selections_ = false;
}

void PerformancePage::save_catalog_selection(std::size_t index) {
    if(loading_gaming_selections_||index>=catalog_selections_.size())return;auto const& item=catalog_selections_[index];auto selected=item.control.SelectedIndex();if(selected<0)return;Target destination;std::uint32_t value{};
    if(item.id=="gaming-dns-server"){auto result=winchisel::platform::write_dns_profile(selected);if(!result){show_write_error(result.error().detail);load_catalog_selections();}return;}
    if(item.id=="gaming-win32-priority"){destination=target(Hive::local_machine,"SYSTEM\\CurrentControlSet\\Control\\PriorityControl","Win32PrioritySeparation",Type::dword);value=selected==0?38:24;}
    else if(item.id=="gaming-performance-svchost-split-threshold"){constexpr std::array<std::uint32_t,10> values{380000,327680,491520,655360,983040,1310720,1966080,2621440,5242880,10485760};if(selected>=static_cast<int>(values.size()))return;destination=target(Hive::local_machine,"SYSTEM\\CurrentControlSet\\Control","SvcHostSplitThresholdInKB",Type::dword);value=values[selected];}
    else if(item.id=="visual-effects-mode"){destination=target(Hive::current_user,"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\VisualEffects","VisualFXSetting",Type::dword);value=static_cast<std::uint32_t>(selected);}
    else if(auto service=service_name(item.id);!service.empty()){destination=target(Hive::local_machine,("SYSTEM\\CurrentControlSet\\Services\\"+std::string(service)).c_str(),"Start",Type::dword);auto option=lower(item.options[static_cast<std::size_t>(selected)]);value=option.find("disabled")!=std::string::npos?4:option.find("manual")!=std::string::npos?3:2;}
    else return;
    if(auto result=winchisel::platform::write_registry_value(destination,Value{value});!result){show_write_error(result.error().detail);load_catalog_selections();}
}

void PerformancePage::load_gaming_toggles() {
    loading_gaming_toggles_ = true;
    for (auto& tweak : gaming_toggles_) {
        bool has_match = false;
        bool all_match = true;
        for (std::size_t index = 0; index < tweak.targets.size(); ++index) {
            const auto actual = winchisel::platform::read_registry_value(tweak.targets[index]);
            const bool missing = actual && std::holds_alternative<std::monostate>(*actual);
            const bool matches = actual && (winchisel::core::registry_value_matches(*actual, tweak.enabled_values[index]) ||
                (tweak.missing_counts_as_enabled && missing));
            has_match = has_match || matches;
            all_match = all_match && matches;
        }
        tweak.control.IsOn(tweak.match_any_target ? has_match : all_match);
    }
    loading_gaming_toggles_ = false;
}

void PerformancePage::save_gaming_toggle(std::size_t index) {
    if (loading_gaming_toggles_ || index >= gaming_toggles_.size()) {
        return;
    }
    const auto& tweak = gaming_toggles_[index];
    const auto& values = tweak.control.IsOn() ? tweak.enabled_values : tweak.disabled_values;
    for (std::size_t target_index = 0; target_index < tweak.targets.size(); ++target_index) {
        if (!winchisel::platform::write_registry_value(tweak.targets[target_index], values[target_index])) {
            show_write_error();
            load_gaming_toggles();
            return;
        }
    }
}

void PerformancePage::apply_gaming_profile(bool recommended) {
    loading_gaming_toggles_ = true;
    for (auto& tweak : gaming_toggles_) {
        tweak.control.IsOn(recommended ? tweak.recommended : tweak.windows_default);
    }
    loading_gaming_toggles_ = false;
    for (std::size_t index = 0; index < gaming_toggles_.size(); ++index) {
        save_gaming_toggle(index);
    }
    loading_gaming_selections_ = true;
    mouse_hover_time_.SelectedIndex(recommended ? 0 : 5);
    background_apps_.SelectedIndex(recommended ? 2 : 0);
    loading_gaming_selections_ = false;
    save_mouse_hover_time();
    save_background_apps();
    apply_catalog_profile(recommended);
}

void PerformancePage::apply_catalog_profile(bool recommended) {
    const auto profile_for = [recommended](std::string_view id, bool selection) -> std::optional<std::int32_t> {
        const auto rule = std::ranges::find_if(winchisel::core::get_performance_profile_rules(), [id](auto const& value) { return value.id == id; });
        if (rule == winchisel::core::get_performance_profile_rules().end()) return std::nullopt;
        const auto value = selection ? (recommended ? rule->recommended_selection : rule->default_selection)
                                     : (recommended ? rule->recommended_toggle : rule->default_toggle);
        return value < 0 ? std::nullopt : std::optional<std::int32_t>{value};
    };
    loading_gaming_toggles_ = true;
    for (auto& item : catalog_toggles_) if (auto value = profile_for(item.id, false)) item.control.IsOn(*value != 0);
    loading_gaming_toggles_ = false;
    for (std::size_t index{}; index < catalog_toggles_.size(); ++index) {
        if (profile_for(catalog_toggles_[index].id, false)) save_catalog_toggle(index);
    }
    loading_gaming_selections_ = true;
    for (auto& item : catalog_selections_) {
        if (auto value = profile_for(item.id, true); value && *value >= 0 && *value < static_cast<std::int32_t>(item.options.size())) item.control.SelectedIndex(*value);
    }
    loading_gaming_selections_ = false;
    for (std::size_t index{}; index < catalog_selections_.size(); ++index) {
        if (profile_for(catalog_selections_[index].id, true)) save_catalog_selection(index);
    }
}

void PerformancePage::load_gaming_selections() {
    loading_gaming_selections_ = true;
    const auto hover = winchisel::platform::read_registry_value(
        target(Hive::current_user, "Control Panel\\Mouse", "MouseHoverTime", Type::string));
    std::int32_t hover_index = 5;
    if (hover) {
        if (const auto* value = std::get_if<std::string>(&*hover)) {
            if (*value == "1") hover_index = 0;
            else if (*value == "10") hover_index = 1;
            else if (*value == "50") hover_index = 2;
            else if (*value == "100") hover_index = 3;
            else if (*value == "200") hover_index = 4;
        }
    }
    mouse_hover_time_.SelectedIndex(hover_index);

    const auto user_value = winchisel::platform::read_registry_value(
        target(Hive::current_user, "SOFTWARE\\Policies\\Microsoft\\Windows\\AppPrivacy", "LetAppsRunInBackground", Type::dword));
    const auto machine_value = winchisel::platform::read_registry_value(
        target(Hive::local_machine, "SOFTWARE\\Policies\\Microsoft\\Windows\\AppPrivacy", "LetAppsRunInBackground", Type::dword));
    const auto* value = user_value ? std::get_if<std::uint32_t>(&*user_value) : nullptr;
    if (!value && machine_value) value = std::get_if<std::uint32_t>(&*machine_value);
    background_apps_.SelectedIndex(value && *value == 1 ? 1 : value && *value == 2 ? 2 : 0);
    loading_gaming_selections_ = false;
}

void PerformancePage::save_mouse_hover_time() {
    if (loading_gaming_selections_) return;
    constexpr std::array values{"1", "10", "50", "100", "200", "400"};
    const auto index = mouse_hover_time_.SelectedIndex();
    if (index < 0 || index >= static_cast<std::int32_t>(values.size()) ||
        !winchisel::platform::write_registry_value(
            target(Hive::current_user, "Control Panel\\Mouse", "MouseHoverTime", Type::string), std::string(values[index]))) {
        show_write_error();
        load_gaming_selections();
    }
}

void PerformancePage::save_background_apps() {
    if (loading_gaming_selections_) return;
    const auto index = background_apps_.SelectedIndex();
    const Value value = index == 1 ? Value{std::uint32_t{1}} : index == 2 ? Value{std::uint32_t{2}} : Value{std::monostate{}};
    const auto user = target(Hive::current_user, "SOFTWARE\\Policies\\Microsoft\\Windows\\AppPrivacy", "LetAppsRunInBackground", Type::dword);
    const auto machine = target(Hive::local_machine, "SOFTWARE\\Policies\\Microsoft\\Windows\\AppPrivacy", "LetAppsRunInBackground", Type::dword);
    if (!winchisel::platform::write_registry_value(user, value) || !winchisel::platform::write_registry_value(machine, value)) {
        show_write_error();
        load_gaming_selections();
    }
}

void PerformancePage::Recommended_Click(Windows::Foundation::IInspectable const&, RoutedEventArgs const&) {
    apply_gaming_profile(true);
}

void PerformancePage::Defaults_Click(Windows::Foundation::IInspectable const&, RoutedEventArgs const&) {
    apply_gaming_profile(false);
}

void PerformancePage::Search_TextChanged(Windows::Foundation::IInspectable const&,
    Controls::AutoSuggestBoxTextChangedEventArgs const&) {
    auto query = to_string(Search().Text());
    std::ranges::transform(query, query.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    for (auto const& child : Groups().Children()) {
        auto expander = child.try_as<Controls::Expander>();
        if (!expander) continue;
        auto header = to_string(unbox_value<hstring>(expander.Header()));
        std::ranges::transform(header, header.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        const bool category_match = !query.empty() && header.find(query) != std::string::npos;
        bool any = query.empty() || category_match;
        if (auto content = expander.Content().try_as<Controls::StackPanel>()) {
            for (auto const& row : content.Children()) {
                auto element = row.try_as<FrameworkElement>();
                auto text = element && element.Tag() ? to_string(unbox_value<hstring>(element.Tag())) : std::string{};
                std::ranges::transform(text, text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                const bool match = query.empty() || category_match || text.find(query) != std::string::npos;
                if (element) element.Visibility(match ? Visibility::Visible : Visibility::Collapsed);
                any = any || match;
            }
        }
        expander.Visibility(any ? Visibility::Visible : Visibility::Collapsed);
        if (!query.empty() && any) expander.IsExpanded(true);
    }
}

}  // namespace winrt::Winchisel::implementation
