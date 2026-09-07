#include "pch.h"
#include "PrivacyPage.xaml.h"

#if __has_include("PrivacyPage.g.cpp")
#include "PrivacyPage.g.cpp"
#endif

#include "winchisel/core/tweak.hpp"
#include "winchisel/platform/registry.hpp"

#include <algorithm>
#include <cctype>

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

std::string description_for(std::string_view id) {
    if (id == "security") return "Windows protection, Defender, SmartScreen, and account security.";
    if (id == "ads") return "Advertising identifiers, suggestions, and tailored experiences.";
    if (id == "lock_screen") return "Lock screen content, spotlight, and notification visibility.";
    if (id == "general") return "General Windows privacy and online experience settings.";
    if (id == "speech") return "Online speech recognition and voice activation.";
    if (id == "inking") return "Inking, typing personalization, and learning data.";
    if (id == "diagnostics") return "Diagnostic data, feedback, and telemetry collection.";
    if (id == "activity_history") return "Activity history and cross-device timeline data.";
    if (id == "search") return "Windows Search, cloud content, and search history.";
    if (id == "app_permissions") return "Application access to devices, data, and capabilities.";
    if (id == "windows_ai") return "Windows AI features and model access permissions.";
    if (id == "edge_ai") return "Microsoft Edge AI and related cloud features.";
    return "Microsoft Office AI and connected experience settings.";
}

std::string lower(std::string value) {
    std::ranges::transform(value, value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

Controls::Border category_card(hstring const& title, hstring const& description) {
    auto resources = Application::Current().Resources();
    auto card = Controls::Border();
    card.Background(resources.Lookup(box_value(L"CardBackgroundFillColorDefaultBrush")).try_as<Media::Brush>());
    card.BorderBrush(resources.Lookup(box_value(L"CardStrokeColorDefaultBrush")).try_as<Media::Brush>());
    card.BorderThickness({1, 1, 1, 1});
    card.Padding({16, 12, 16, 12});
    auto content = Controls::StackPanel();
    auto heading = Controls::TextBlock();
    heading.Text(title);
    heading.Style(resources.Lookup(box_value(L"BodyStrongTextBlockStyle")).try_as<Style>());
    content.Children().Append(heading);
    auto detail = Controls::TextBlock();
    detail.Text(description);
    detail.TextWrapping(TextWrapping::Wrap);
    detail.Style(resources.Lookup(box_value(L"CaptionTextBlockStyle")).try_as<Style>());
    content.Children().Append(detail);
    card.Child(content);
    return card;
}

Controls::Border setting_card(hstring const& title, hstring const& description, FrameworkElement const& control) {
    auto card = category_card(title, description);
    auto layout = Controls::Grid();
    layout.ColumnDefinitions().Append(Controls::ColumnDefinition());
    auto trailing = Controls::ColumnDefinition();
    trailing.Width(GridLength{0, GridUnitType::Auto});
    layout.ColumnDefinitions().Append(trailing);

    auto text = Controls::StackPanel();
    auto resources = Application::Current().Resources();
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

PrivacyPage::PrivacyPage() {
    InitializeComponent();
    security_toggles_ = {
        {L"Workplace join messages", L"Allow Windows to show prompts for a work or school account.", true, true,
         {target(Hive::local_machine, "SOFTWARE\\Policies\\Microsoft\\Windows\\WorkplaceJoin", "BlockAADWorkplaceJoin", Type::dword),
          target(Hive::current_user, "SOFTWARE\\Policies\\Microsoft\\Windows\\WorkplaceJoin", "BlockAADWorkplaceJoin", Type::dword)},
         {Value{std::monostate{}}, Value{std::monostate{}}}, {Value{std::uint32_t{1}}, Value{std::uint32_t{1}}}},
        {L"Automatic device encryption", L"Allow Windows to automatically enable device encryption when supported.", false, false,
         {target(Hive::local_machine, "SYSTEM\\CurrentControlSet\\Control\\BitLocker", "PreventDeviceEncryption", Type::dword)},
         {Value{std::uint32_t{0}}}, {Value{std::uint32_t{1}}}},
        {L"Wi-Fi Sense", L"Allow Wi-Fi hotspot reporting and automatic connections to suggested hotspots.", false, false,
         {target(Hive::local_machine, "SOFTWARE\\Microsoft\\PolicyManager\\default\\WiFi\\AllowWiFiHotSpotReporting", "value", Type::dword),
          target(Hive::local_machine, "SOFTWARE\\Microsoft\\PolicyManager\\default\\WiFi\\AllowAutoConnectToWiFiSenseHotspots", "value", Type::dword)},
         {Value{std::uint32_t{1}}, Value{std::uint32_t{1}}}, {Value{std::uint32_t{0}}, Value{std::uint32_t{0}}}},
        {L"Automatic maintenance", L"Allow Windows to run scheduled maintenance when the PC is idle.", false, false,
         {target(Hive::local_machine, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Schedule\\Maintenance", "MaintenanceDisabled", Type::dword)},
         {Value{std::uint32_t{0}}}, {Value{std::uint32_t{1}}}},
        {L"Windows Error Reporting", L"Allow Windows to send problem reports that can help diagnose crashes.", false, true,
         {target(Hive::local_machine, "SOFTWARE\\Policies\\Microsoft\\Windows\\Windows Error Reporting", "Disabled", Type::dword),
          target(Hive::current_user, "SOFTWARE\\Policies\\Microsoft\\Windows\\Windows Error Reporting", "Disabled", Type::dword)},
         {Value{std::uint32_t{0}}, Value{std::uint32_t{0}}}, {Value{std::uint32_t{1}}, Value{std::uint32_t{1}}}},
        {L"Remote Assistance", L"Allow trusted people to connect remotely when you request help.", false, false,
         {target(Hive::local_machine, "SYSTEM\\CurrentControlSet\\Control\\Remote Assistance", "fAllowToGetHelp", Type::dword)},
         {Value{std::uint32_t{1}}}, {Value{std::uint32_t{0}}}},
    };
    render_groups();
}

void PrivacyPage::Search_TextChanged(Windows::Foundation::IInspectable const&, Controls::AutoSuggestBoxTextChangedEventArgs const&) {
    render_groups();
}

void PrivacyPage::render_groups() {
    Groups().Children().Clear();
    const auto query = lower(to_string(Search().Text()));
    std::size_t matches{};
    for (const auto& group : winchisel::core::k_privacy_security_groups) {
        const auto description = description_for(group.id);
        const auto searchable = lower(std::string(group.title) + " " + description);
        if (!query.empty() && searchable.find(query) == std::string::npos) {
            continue;
        }
        ++matches;
        auto expander = Controls::Expander();
        expander.Header(box_value(to_hstring(group.title)));
        expander.HorizontalAlignment(HorizontalAlignment::Stretch);
        expander.HorizontalContentAlignment(HorizontalAlignment::Stretch);
        expander.IsExpanded(group.id == "security");
        if (group.id == "security") {
            expander.Content(security_content());
        } else {
            expander.Content(category_card(to_hstring(group.title), to_hstring(description)));
        }
        Groups().Children().Append(expander);
    }
    if (matches == 0) {
        auto empty = Controls::InfoBar();
        empty.Title(L"No matching categories");
        empty.Message(L"Try a different search term.");
        empty.Severity(Controls::InfoBarSeverity::Informational);
        empty.IsOpen(true);
        Groups().Children().Append(empty);
    }
}

Controls::StackPanel PrivacyPage::security_content() {
    auto content = Controls::StackPanel();
    content.Spacing(8);
    content.HorizontalAlignment(HorizontalAlignment::Stretch);
    content.Children().Append(category_card(L"Security controls", L"These settings use Windows policy and system security values. Changes apply immediately and are reloaded if Windows rejects a write."));

    uac_level_ = Controls::ComboBox();
    for (auto const& option : {L"Always notify", L"Notify for app changes", L"Default: notify without dimming", L"Notify without secure desktop", L"Never notify"}) {
        auto item = Controls::ComboBoxItem();
        item.Content(box_value(option));
        uac_level_.Items().Append(item);
    }
    uac_level_.SelectionChanged([this](auto&&, auto&&) { save_uac_level(); });
    content.Children().Append(setting_card(L"User Account Control", L"Choose how Windows asks before apps make system-wide changes.", uac_level_));

    for (std::size_t index = 0; index < security_toggles_.size(); ++index) {
        auto& tweak = security_toggles_[index];
        tweak.control = Controls::ToggleSwitch();
        tweak.control.Toggled([this, index](auto&&, auto&&) { save_security_toggle(index); });
        content.Children().Append(setting_card(tweak.title, tweak.description, tweak.control));
    }
    load_security();
    load_uac_level();
    return content;
}

void PrivacyPage::load_security() {
    loading_security_ = true;
    for (auto& tweak : security_toggles_) {
        bool any_match = false;
        bool all_match = true;
        for (std::size_t index = 0; index < tweak.targets.size(); ++index) {
            const auto actual = winchisel::platform::read_registry_value(tweak.targets[index]);
            const bool missing = actual && std::holds_alternative<std::monostate>(*actual);
            const bool matches = actual && (winchisel::core::registry_value_matches(*actual, tweak.enabled_values[index]) ||
                (tweak.enabled_when_missing && missing));
            any_match = any_match || matches;
            all_match = all_match && matches;
        }
        tweak.control.IsOn(tweak.match_any_target ? any_match : all_match);
    }
    loading_security_ = false;
}

void PrivacyPage::save_security_toggle(std::size_t index) {
    if (loading_security_ || index >= security_toggles_.size()) return;
    const auto& tweak = security_toggles_[index];
    const auto& values = tweak.control.IsOn() ? tweak.enabled_values : tweak.disabled_values;
    for (std::size_t target_index = 0; target_index < tweak.targets.size(); ++target_index) {
        if (!winchisel::platform::write_registry_value(tweak.targets[target_index], values[target_index])) {
            load_security();
            return;
        }
    }
}

void PrivacyPage::load_uac_level() {
    loading_uac_ = true;
    const auto prompt = winchisel::platform::read_registry_value(target(Hive::local_machine, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\System", "ConsentPromptBehaviorAdmin", Type::dword));
    const auto secure = winchisel::platform::read_registry_value(target(Hive::local_machine, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\System", "PromptOnSecureDesktop", Type::dword));
    const auto* prompt_value = prompt ? std::get_if<std::uint32_t>(&*prompt) : nullptr;
    const auto* secure_value = secure ? std::get_if<std::uint32_t>(&*secure) : nullptr;
    std::int32_t selection = 2;
    if (prompt_value && secure_value) {
        if (*prompt_value == 1 && *secure_value == 1) selection = 0;
        else if (*prompt_value == 2 && *secure_value == 1) selection = 1;
        else if (*prompt_value == 5 && *secure_value == 0) selection = 3;
        else if (*prompt_value == 0 && *secure_value == 0) selection = 4;
    }
    uac_level_.SelectedIndex(selection);
    loading_uac_ = false;
}

void PrivacyPage::save_uac_level() {
    if (loading_uac_) return;
    constexpr std::array<std::pair<std::uint32_t, std::uint32_t>, 5> values{{{1, 1}, {2, 1}, {5, 1}, {5, 0}, {0, 0}}};
    const auto selected = uac_level_.SelectedIndex();
    if (selected < 0 || selected >= static_cast<std::int32_t>(values.size())) return;
    const auto key = "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\System";
    const auto prompt = target(Hive::local_machine, key, "ConsentPromptBehaviorAdmin", Type::dword);
    const auto secure = target(Hive::local_machine, key, "PromptOnSecureDesktop", Type::dword);
    if (!winchisel::platform::write_registry_value(prompt, values[selected].first) ||
        !winchisel::platform::write_registry_value(secure, values[selected].second)) {
        load_uac_level();
    }
}

}  // namespace winrt::Winchisel::implementation
