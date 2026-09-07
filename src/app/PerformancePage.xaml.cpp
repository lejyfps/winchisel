#include "pch.h"
#include "PerformancePage.xaml.h"

#if __has_include("PerformancePage.g.cpp")
#include "PerformancePage.g.cpp"
#endif

#include "winchisel/core/tweak.hpp"
#include "winchisel/platform/registry.hpp"

#include <array>

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace winrt::Winchisel::implementation {
namespace {

using Hive = winchisel::core::RegistryHive;
using Type = winchisel::core::RegistryValueType;
using Target = winchisel::core::RegistryTarget;
using Value = winchisel::core::RegistryValue;

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
                tweak.control.Toggled([this, index](auto&&, auto&&) { save_gaming_toggle(index); });
                content.Children().Append(setting_card(tweak.title, tweak.description, tweak.control));
            }
            mouse_hover_time_ = Controls::ComboBox();
            for (auto const& option : {L"1ms (Instant)", L"10ms (Very Fast)", L"50ms (Fast)", L"100ms (Moderate)", L"200ms", L"400ms (Default)"}) {
                auto item = Controls::ComboBoxItem();
                item.Content(box_value(option));
                mouse_hover_time_.Items().Append(item);
            }
            mouse_hover_time_.SelectionChanged([this](auto&&, auto&&) { save_mouse_hover_time(); });
            content.Children().Append(setting_card(L"Mouse Hover Time", L"Sets how long the pointer must hover before Windows responds.", mouse_hover_time_));
            background_apps_ = Controls::ComboBox();
            for (auto const& option : {L"User in Control (Default)", L"Force Allow", L"Force Deny"}) {
                auto item = Controls::ComboBoxItem();
                item.Content(box_value(option));
                background_apps_.Items().Append(item);
            }
            background_apps_.SelectionChanged([this](auto&&, auto&&) { save_background_apps(); });
            content.Children().Append(setting_card(L"Let Apps Run in Background", L"Controls whether apps may continue running in the background.", background_apps_));
            expander.Content(content);
        }
        Groups().Children().Append(expander);
    }
    load_gaming_toggles();
    load_gaming_selections();
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
        load_gaming_selections();
    }
}

void PerformancePage::Recommended_Click(Windows::Foundation::IInspectable const&, RoutedEventArgs const&) {
    apply_gaming_profile(true);
}

void PerformancePage::Defaults_Click(Windows::Foundation::IInspectable const&, RoutedEventArgs const&) {
    apply_gaming_profile(false);
}

}  // namespace winrt::Winchisel::implementation
