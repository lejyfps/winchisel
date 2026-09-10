#include "pch.h"
#include "AsyncSupport.hpp"
#include "PrivacyPage.xaml.h"
#include "Localization.hpp"
#include "winchisel/platform/system.hpp"
#include "winchisel/platform/update.hpp"
#include "winchisel/application/session.hpp"

#if __has_include("PrivacyPage.g.cpp")
#include "PrivacyPage.g.cpp"
#endif

#include "winchisel/core/tweak.hpp"
#include "winchisel/core/risk.hpp"
#include "winchisel/platform/registry.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <chrono>
#include <functional>
#include <optional>

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace winrt::Winchisel::implementation {
void PrivacyPage::show_write_error(std::string const& detail){auto message=detail.empty()?"A Windows setting could not be changed. See %APPDATA%\\Winchisel\\logs\\winchisel.log for details.":detail;ResultBar().Title(L"Could not apply setting");ResultBar().Message(to_hstring(message));ResultBar().Severity(Controls::InfoBarSeverity::Error);ResultBar().IsOpen(true);winchisel::platform::boot_log(("privacy UI: "+message).c_str());}
namespace {

using Hive = winchisel::core::RegistryHive;
using Type = winchisel::core::RegistryValueType;
using Target = winchisel::core::RegistryTarget;
using Value = winchisel::core::RegistryValue;

Target target(Hive hive, char const* key, char const* name, Type type) {
    return {.hive = hive, .key_path = key, .value_name = name, .type = type};
}

// Exception-free DWORD parsing for catalog tokens. Returns nullopt instead of
// throwing, so a corrupt catalog entry surfaces as an error message rather
// than an unhandled UI exception.
std::optional<std::uint32_t> parse_dword_token(std::string_view token) {
    std::uint32_t value{};
    const auto [ptr, ec] = std::from_chars(token.data(), token.data() + token.size(), value);
    if (ec != std::errc{} || ptr != token.data() + token.size()) return std::nullopt;
    return value;
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
    card.HorizontalAlignment(HorizontalAlignment::Stretch);
    auto content = Controls::StackPanel();
    auto heading = Controls::TextBlock();
    heading.Text(winchisel::ui::tr(title));
    heading.Style(resources.Lookup(box_value(L"BodyStrongTextBlockStyle")).try_as<Style>());
    content.Children().Append(heading);
    auto detail = Controls::TextBlock();
    detail.Text(winchisel::ui::tr(description));
    detail.TextWrapping(TextWrapping::Wrap);
    detail.Style(resources.Lookup(box_value(L"CaptionTextBlockStyle")).try_as<Style>());
    content.Children().Append(detail);
    card.Child(content);
    return card;
}

Controls::Border new_badge() {
    auto resources = Application::Current().Resources();
    auto accent = resources.Lookup(box_value(L"AccentTextFillColorPrimaryBrush")).try_as<Media::Brush>();
    Media::Brush tint{nullptr};
    if (auto solid = accent.try_as<Media::SolidColorBrush>()) {
        auto color = solid.Color();
        color.A = 0x2E;
        tint = Media::SolidColorBrush(color);
    }
    auto badge = Controls::Border();
    badge.CornerRadius({12, 12, 12, 12});
    badge.Padding({10, 3, 10, 3});
    badge.Margin({8, 0, 0, 0});
    badge.VerticalAlignment(VerticalAlignment::Center);
    badge.Background(tint);
    auto row = Controls::StackPanel();
    row.Orientation(Controls::Orientation::Horizontal);
    row.Spacing(6);
    row.VerticalAlignment(VerticalAlignment::Center);
    auto icon = Controls::FontIcon();
    icon.Glyph(hstring{L"\uE735"});
    icon.FontSize(11);
    icon.Foreground(accent);
    icon.VerticalAlignment(VerticalAlignment::Center);
    row.Children().Append(icon);
    auto label = Controls::TextBlock();
    label.Text(winchisel::ui::tr(L"New"));
    label.Foreground(accent);
    label.FontSize(11);
    label.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
    label.VerticalAlignment(VerticalAlignment::Center);
    row.Children().Append(label);
    badge.Child(row);
    return badge;
}

// "NEW" badges are shown until a version newer than the introducing one
// runs. Dev builds without version.txt report "0.0.0" and keep the badges
// visible for testing.
bool show_new_badges() {
    const auto current = winchisel::platform::current_app_version();
    return !winchisel::platform::is_newer_version(current, winchisel::core::k_new_tweaks_version);
}

bool risk_badges_visible() {
    return winchisel::application::Session::instance().settings().show_risk_badges;
}

Controls::Border risk_badge(winchisel::core::TweakRisk risk) {
    auto resources = Application::Current().Resources();
    hstring brush_key = L"TextFillColorTertiaryBrush";
    if (risk == winchisel::core::TweakRisk::moderate) brush_key = L"SystemFillColorCautionBrush";
    else if (risk == winchisel::core::TweakRisk::risky) brush_key = L"SystemFillColorCriticalBrush";
    auto accent = resources.Lookup(box_value(brush_key)).try_as<Media::Brush>();
    Media::Brush tint{nullptr};
    if (auto solid = accent.try_as<Media::SolidColorBrush>()) {
        auto color = solid.Color();
        color.A = 0x2E;
        tint = Media::SolidColorBrush(color);
    }
    auto badge = Controls::Border();
    badge.CornerRadius({12, 12, 12, 12});
    badge.Padding({10, 3, 10, 3});
    badge.Margin({8, 0, 0, 0});
    badge.VerticalAlignment(VerticalAlignment::Center);
    badge.Background(tint);
    auto label = Controls::TextBlock();
    const auto risk_key = winchisel::core::risk_label_key(risk);
    label.Text(winchisel::ui::tr(std::wstring(risk_key.begin(), risk_key.end())));
    label.Foreground(accent);
    label.FontSize(11);
    label.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
    label.VerticalAlignment(VerticalAlignment::Center);
    badge.Child(label);
    return badge;
}

Controls::Border setting_card(hstring const& title, hstring const& description, FrameworkElement const& control, bool is_new = false,
    winchisel::core::TweakRisk risk = winchisel::core::TweakRisk::safe, bool show_risk = false,
    Controls::Border const& rec_badge = nullptr, Controls::Border const& def_badge = nullptr) {
    auto card = category_card(title, description);
    card.Tag(box_value(title + L" " + description));
    auto layout = Controls::Grid();
    layout.HorizontalAlignment(HorizontalAlignment::Stretch);
    layout.ColumnDefinitions().Append(Controls::ColumnDefinition());
    auto trailing = Controls::ColumnDefinition();
    trailing.Width(GridLength{0, GridUnitType::Auto});
    layout.ColumnDefinitions().Append(trailing);
    layout.ColumnSpacing(12);

    auto text = Controls::StackPanel();
    auto resources = Application::Current().Resources();
    auto heading = Controls::TextBlock();
    heading.Text(title);
    heading.Style(resources.Lookup(box_value(L"BodyStrongTextBlockStyle")).try_as<Style>());
    heading.VerticalAlignment(VerticalAlignment::Center);
    if (!is_new && !show_risk && !rec_badge && !def_badge) {
        text.Children().Append(heading);
    } else {
        auto header_row = Controls::StackPanel();
        header_row.Orientation(Controls::Orientation::Horizontal);
        header_row.VerticalAlignment(VerticalAlignment::Center);
        header_row.Children().Append(heading);
        if (is_new) header_row.Children().Append(new_badge());
        if (show_risk) header_row.Children().Append(risk_badge(risk));
        if (rec_badge) header_row.Children().Append(rec_badge);
        if (def_badge) header_row.Children().Append(def_badge);
        text.Children().Append(header_row);
    }
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

// Per-tweak Recommended/Default quick-set buttons (Winhance-style SettingsCardItem).
// Privacy toggles follow the page profile: recommended = Off (hardened),
// default = On (Windows stock). Combos carry explicit rec/def indices from
// build_profile_plan below.
Controls::Button quick_set_button(hstring const& glyph, hstring const& tip, bool recommended) {
    auto resources = Application::Current().Resources();
    auto button = Controls::Button();
    button.Padding({4, 2, 4, 2});
    button.MinWidth(0);
    button.MinHeight(0);
    button.Width(34);
    button.Height(32);
    button.VerticalAlignment(VerticalAlignment::Center);
    auto icon = Controls::FontIcon();
    icon.Glyph(glyph);
    icon.FontSize(14);
    icon.VerticalAlignment(VerticalAlignment::Center);
    const wchar_t* brush_key = recommended ? L"AccentTextFillColorPrimaryBrush" : L"TextFillColorSecondaryBrush";
    if (auto brush = resources.Lookup(box_value(brush_key)).try_as<Media::Brush>()) icon.Foreground(brush);
    button.Content(icon);
    Controls::ToolTipService::SetToolTip(button, box_value(tip));
    Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(button, tip);
    return button;
}

Controls::StackPanel with_quick_set(FrameworkElement const& control, hstring const& rec_tip, hstring const& def_tip,
    std::function<void()> on_rec, std::function<void()> on_def) {
    auto row = Controls::StackPanel();
    row.Orientation(Controls::Orientation::Horizontal);
    row.Spacing(8);
    row.VerticalAlignment(VerticalAlignment::Center);
    auto pair = Controls::StackPanel();
    pair.Orientation(Controls::Orientation::Horizontal);
    pair.Spacing(4);
    pair.VerticalAlignment(VerticalAlignment::Center);
    auto rec = quick_set_button(hstring{L"\uE735"}, rec_tip, true);
    rec.Click([on_rec = std::move(on_rec)](auto&&, auto&&) { on_rec(); });
    auto def = quick_set_button(hstring{L"\uE10E"}, def_tip, false);
    def.Click([on_def = std::move(on_def)](auto&&, auto&&) { on_def(); });
    pair.Children().Append(rec);
    pair.Children().Append(def);
    row.Children().Append(pair);
    row.Children().Append(control);
    return row;
}

hstring toggle_tip(bool recommended, bool state) {
    const auto prefix = recommended ? winchisel::ui::tr(L"Recommended") : winchisel::ui::tr(L"Defaults");
    return hstring{std::wstring(prefix) + L": " + (state ? L"On" : L"Off")};
}

hstring selection_tip(bool recommended, std::wstring_view option) {
    const auto prefix = recommended ? winchisel::ui::tr(L"Recommended") : winchisel::ui::tr(L"Defaults");
    return hstring{std::wstring(prefix) + L": " + std::wstring(option)};
}

// State pill showing whether the current value matches Recommended or the
// Windows default. Starts hidden; apply_snapshot() refreshes it. Profile:
// toggles recommended = Off / default = On, combos carry explicit indices.
Controls::Border state_badge(bool recommended) {
    auto resources = Application::Current().Resources();
    const wchar_t* brush_key = recommended ? L"AccentTextFillColorPrimaryBrush" : L"TextFillColorSecondaryBrush";
    auto accent = resources.Lookup(box_value(brush_key)).try_as<Media::Brush>();
    Media::Brush tint{nullptr};
    if (auto solid = accent.try_as<Media::SolidColorBrush>()) {
        auto color = solid.Color();
        color.A = 0x2E;
        tint = Media::SolidColorBrush(color);
    }
    auto badge = Controls::Border();
    badge.CornerRadius({12, 12, 12, 12});
    badge.Padding({10, 3, 10, 3});
    badge.Margin({8, 0, 0, 0});
    badge.VerticalAlignment(VerticalAlignment::Center);
    badge.Background(tint);
    badge.Visibility(Visibility::Collapsed);
    auto label = Controls::TextBlock();
    label.Text(recommended ? winchisel::ui::tr(L"Recommended") : winchisel::ui::tr(L"Default"));
    label.Foreground(accent);
    label.FontSize(11);
    label.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
    label.VerticalAlignment(VerticalAlignment::Center);
    if (recommended) {
        auto row = Controls::StackPanel();
        row.Orientation(Controls::Orientation::Horizontal);
        row.Spacing(6);
        row.VerticalAlignment(VerticalAlignment::Center);
        auto icon = Controls::FontIcon();
        icon.Glyph(hstring{L"\uE735"});
        icon.FontSize(11);
        icon.Foreground(accent);
        icon.VerticalAlignment(VerticalAlignment::Center);
        row.Children().Append(icon);
        row.Children().Append(label);
        badge.Child(row);
    } else {
        badge.Child(label);
    }
    return badge;
}

bool state_badges_visible() {
    return winchisel::application::Session::instance().settings().show_state_badges;
}

void update_state_badges(Controls::Border const& rec, Controls::Border const& def, bool is_rec, bool is_def) {
    const bool show = state_badges_visible();
    if (rec) rec.Visibility(show && is_rec ? Visibility::Visible : Visibility::Collapsed);
    if (def) def.Visibility(show && is_def ? Visibility::Visible : Visibility::Collapsed);
}

}  // namespace

PrivacyPage::PrivacyPage() {
    InitializeComponent();
    search_timer_ = DispatcherTimer();
    search_timer_.Interval(std::chrono::milliseconds(200));
    auto weak = get_weak();
    search_timer_token_ = search_timer_.Tick([weak](auto&&, auto&&) { if (auto self = weak.get()) { self->search_timer_.Stop(); self->apply_filter(); } });
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
    submit([] { return winchisel::core::Result<void>{}; });
}

PrivacyPage::~PrivacyPage() { search_timer_.Stop(); search_timer_.Tick(search_timer_token_); }

void PrivacyPage::Search_TextChanged(Windows::Foundation::IInspectable const&, Controls::AutoSuggestBoxTextChangedEventArgs const&) {
    search_timer_.Stop();
    search_timer_.Start();
}

void PrivacyPage::render_groups() {
    Groups().Children().Clear();
    privacy_toggles_.clear();
    std::int32_t group_index{};
    for (const auto& group : winchisel::core::k_privacy_security_groups) {
        const auto description = description_for(group.id);
        auto expander = Controls::Expander();
        expander.Header(box_value(winchisel::ui::tr(to_hstring(group.title))));
        expander.Tag(box_value(to_hstring(std::string(group.title) + " " + description)));
        expander.HorizontalAlignment(HorizontalAlignment::Stretch);
        expander.HorizontalContentAlignment(HorizontalAlignment::Stretch);
        expander.IsExpanded(group.id == "security");
        if (group.id == "security") {
            expander.Content(security_content());
        } else {
            expander.Content(privacy_content(group_index));
        }
        Groups().Children().Append(expander);
        ++group_index;
    }
    apply_filter();
}

void PrivacyPage::apply_filter() {
    const auto query = lower(to_string(Search().Text()));
    std::vector<FrameworkElement> stale;
    for (auto const& child : Groups().Children()) {
        if (child.try_as<Controls::InfoBar>()) { if (auto element = child.try_as<FrameworkElement>()) stale.push_back(element); }
    }
    for (auto const& child : stale) {
        std::uint32_t position{};
        if (Groups().Children().IndexOf(child, position)) Groups().Children().RemoveAt(position);
    }
    std::size_t visible_total{};
    for (auto const& child : Groups().Children()) {
        auto expander = child.try_as<Controls::Expander>();
        if (!expander) continue;
        const auto header_text = lower(to_string(unbox_value_or<hstring>(expander.Tag(), L"")));
        const bool header_match = query.empty() || header_text.find(query) != std::string::npos;
        std::size_t visible{};
        if (auto panel = expander.Content().try_as<Controls::StackPanel>()) {
            for (auto const& card : panel.Children()) {
                auto element = card.try_as<FrameworkElement>();
                const bool show = query.empty() || header_match ||
                    lower(to_string(unbox_value_or<hstring>(element ? element.Tag() : nullptr, L""))).find(query) != std::string::npos;
                if (element) element.Visibility(show ? Visibility::Visible : Visibility::Collapsed);
                if (show) ++visible;
            }
        }
        const bool show_group = query.empty() || header_match || visible > 0;
        expander.Visibility(show_group ? Visibility::Visible : Visibility::Collapsed);
        if (show_group) visible_total += visible;
    }
    if (visible_total == 0) {
        auto empty = Controls::InfoBar();
        empty.Title(L"No matching categories");
        empty.Message(L"Try a different search term.");
        empty.Severity(Controls::InfoBarSeverity::Informational);
        empty.IsOpen(true);
        Groups().Children().Append(empty);
    }
}

Controls::StackPanel PrivacyPage::privacy_content(std::int32_t group) {
    auto content = Controls::StackPanel();
    content.Spacing(8);
    content.HorizontalAlignment(HorizontalAlignment::Stretch);
    const bool show_new = show_new_badges();
    if (group == 1) {
        ads_mode_ = Controls::ComboBox();
        Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(ads_mode_, L"Ads, Suggestions and Promotional Content");
        for (auto option : {L"Allow", L"Deny", L"Custom"}) {
            auto row = Controls::ComboBoxItem();
            row.Content(box_value(option));
            ads_mode_.Items().Append(row);
        }
        ads_mode_.SelectionChanged([this](auto&&, auto&&) { save_ads_mode(); });
        // Profile: recommended = Deny (1), default = Allow (0).
        ads_rec_ = state_badge(true);
        ads_def_ = state_badge(false);
        auto ads_trailing = with_quick_set(ads_mode_, selection_tip(true, L"Deny"), selection_tip(false, L"Allow"),
            [this] { if (!loading_uac_ && ads_mode_) ads_mode_.SelectedIndex(1); },
            [this] { if (!loading_uac_ && ads_mode_) ads_mode_.SelectedIndex(0); });
        content.Children().Append(setting_card(L"Ads, Suggestions and Promotional Content",
            L"Controls all advertising, suggestions, and promotional content throughout Windows", ads_trailing,
            false, winchisel::core::TweakRisk::safe, false, ads_rec_, ads_def_));
    }
    for (auto const& item : winchisel::core::get_privacy_catalog()) {
        if (item.group != group) continue;
        auto toggle = Controls::ToggleSwitch();
        Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(toggle, to_hstring(item.name));
        toggle.OnContent(box_value(L""));
        toggle.OffContent(box_value(L""));
        toggle.MinWidth(0);
        toggle.Width(40);
        auto index = privacy_toggles_.size();
        privacy_toggles_.push_back({std::string(item.id), toggle});
        auto& stored = privacy_toggles_.back();
        stored.rec_badge = state_badge(true);
        stored.def_badge = state_badge(false);
        toggle.Toggled([this, index](auto&&, auto&&) { save_privacy_toggle(index); });
        // Profile: recommended = Off (hardened), default = On (Windows stock).
        auto trailing = with_quick_set(toggle, toggle_tip(true, false), toggle_tip(false, true),
            [this, index] { if (!loading_security_) privacy_toggles_[index].control.IsOn(false); },
            [this, index] { if (!loading_security_) privacy_toggles_[index].control.IsOn(true); });
        auto card = setting_card(to_hstring(item.name), to_hstring(item.description), trailing,
            show_new && winchisel::core::is_new_tweak(item.id), winchisel::core::assess_privacy(item.id), risk_badges_visible(),
            stored.rec_badge, stored.def_badge);
        card.Tag(box_value(to_hstring(std::string(item.name) + " " + std::string(item.description) + " " + std::string(item.id))));
        content.Children().Append(card);
    }
    return content;
}

void PrivacyPage::save_privacy_toggle(std::size_t index){if(loading_security_||index>=privacy_toggles_.size())return;auto const& item=privacy_toggles_[index];std::vector<std::pair<Target,Value>> changes;for(auto const& rule:winchisel::core::get_privacy_registry_rules()){if(rule.id!=item.id)continue;auto destination=target(rule.root?Hive::local_machine:Hive::current_user,rule.path.data(),rule.name.data(),rule.kind?Type::string:Type::dword);auto token=item.control.IsOn()?rule.enabled_value:rule.disabled_value;Value value;if(token=="__MISSING__")value=std::monostate{};else if(rule.kind)value=std::string(token);else if(auto parsed=parse_dword_token(std::string_view(token)))value=*parsed;else{show_write_error("A privacy catalog entry is invalid and was not applied.");submit([] { return winchisel::core::Result<void>{}; });return;}changes.emplace_back(destination,value);}submit([changes=std::move(changes)] { return winchisel::platform::write_registry_values_atomic(changes); });}

Controls::StackPanel PrivacyPage::security_content() {
    auto content = Controls::StackPanel();
    content.Spacing(8);
    content.HorizontalAlignment(HorizontalAlignment::Stretch);
    uac_level_ = Controls::ComboBox();
    Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(uac_level_,L"User Account Control level");
    for (auto const& option : {L"Always notify", L"Notify for app changes", L"Default: notify without dimming", L"Notify without secure desktop", L"Never notify"}) {
        auto item = Controls::ComboBoxItem();
        item.Content(box_value(option));
        uac_level_.Items().Append(item);
    }
    uac_level_.SelectionChanged([this](auto&&, auto&&) { save_uac_level(); });
    // Profile: recommended = Never notify (4), default = notify without dimming (2).
    uac_rec_ = state_badge(true);
    uac_def_ = state_badge(false);
    {
        auto trailing = with_quick_set(uac_level_, selection_tip(true, L"Never notify"), selection_tip(false, L"Default: notify without dimming"),
            [this] { if (!loading_uac_) uac_level_.SelectedIndex(4); },
            [this] { if (!loading_uac_) uac_level_.SelectedIndex(2); });
        content.Children().Append(setting_card(L"User Account Control", L"Choose how Windows asks before apps make system-wide changes.", trailing,
            false, winchisel::core::TweakRisk::safe, false, uac_rec_, uac_def_));
    }

    smart_app_control_=Controls::ComboBox();Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(smart_app_control_,L"Smart App Control");for(auto option:{L"Off",L"On (Enforced)",L"Evaluation Mode"}){auto item=Controls::ComboBoxItem();item.Content(box_value(option));smart_app_control_.Items().Append(item);}smart_app_control_.SelectionChanged([this](auto&&,auto&&){save_smart_app_control();});
    // Profile: recommended = Off (0), default = Evaluation Mode (2).
    sac_rec_ = state_badge(true);
    sac_def_ = state_badge(false);
    {
        auto trailing = with_quick_set(smart_app_control_, selection_tip(true, L"Off"), selection_tip(false, L"Evaluation Mode"),
            [this] { if (!loading_uac_ && smart_app_control_) smart_app_control_.SelectedIndex(0); },
            [this] { if (!loading_uac_ && smart_app_control_) smart_app_control_.SelectedIndex(2); });
        content.Children().Append(setting_card(L"Smart App Control",L"Controls the Smart App Control feature which blocks untrusted applications",trailing,
            false, winchisel::core::TweakRisk::safe, false, sac_rec_, sac_def_));
    }
    powershell_policy_=Controls::ComboBox();Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(powershell_policy_,L"PowerShell Execution Policy");for(auto option:{L"Restricted",L"AllSigned",L"RemoteSigned",L"Unrestricted",L"Bypass"}){auto item=Controls::ComboBoxItem();item.Content(box_value(option));powershell_policy_.Items().Append(item);}powershell_policy_.SelectionChanged([this](auto&&,auto&&){save_powershell_policy();});
    // Profile: recommended = RemoteSigned (2), default = Restricted (0).
    ps_rec_ = state_badge(true);
    ps_def_ = state_badge(false);
    {
        auto trailing = with_quick_set(powershell_policy_, selection_tip(true, L"RemoteSigned"), selection_tip(false, L"Restricted"),
            [this] { if (!loading_uac_ && powershell_policy_) powershell_policy_.SelectedIndex(2); },
            [this] { if (!loading_uac_ && powershell_policy_) powershell_policy_.SelectedIndex(0); });
        content.Children().Append(setting_card(L"PowerShell Execution Policy",L"Controls whether PowerShell scripts are allowed to run and under what conditions",trailing,
            false, winchisel::core::TweakRisk::safe, false, ps_rec_, ps_def_));
    }

    for (std::size_t index = 0; index < security_toggles_.size(); ++index) {
        auto& tweak = security_toggles_[index];
        tweak.control = Controls::ToggleSwitch();
        Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(tweak.control,tweak.title);
        tweak.control.OnContent(box_value(L""));
        tweak.control.OffContent(box_value(L""));
        tweak.control.MinWidth(0);
        tweak.control.Width(40);
        tweak.control.Toggled([this, index](auto&&, auto&&) { save_security_toggle(index); });
        tweak.rec_badge = state_badge(true);
        tweak.def_badge = state_badge(false);
        // Profile: recommended = Off, default = On (matches build_profile_plan).
        auto trailing = with_quick_set(tweak.control, toggle_tip(true, false), toggle_tip(false, true),
            [this, index] { if (!loading_security_) security_toggles_[index].control.IsOn(false); },
            [this, index] { if (!loading_security_) security_toggles_[index].control.IsOn(true); });
        content.Children().Append(setting_card(tweak.title, tweak.description, trailing,
            false, winchisel::core::assess_registry_targets(tweak.targets), risk_badges_visible(),
            tweak.rec_badge, tweak.def_badge));
    }
    return content;
}

PrivacyPage::PrivacySnapshot PrivacyPage::read_snapshot() const {
    PrivacySnapshot snapshot;
    snapshot.privacy.reserve(privacy_toggles_.size());
    for (auto const& item : privacy_toggles_) {
        bool enabled = true;
        for (auto const& rule : winchisel::core::get_privacy_registry_rules()) {
            if (rule.id != item.id) continue;
            auto destination = target(rule.root ? Hive::local_machine : Hive::current_user, rule.path.data(), rule.name.data(), rule.kind ? Type::string : Type::dword);
            auto actual = winchisel::platform::read_registry_value(destination);
            bool match = false;
            if (actual) {
                if (rule.enabled_value == "__MISSING__") match = std::holds_alternative<std::monostate>(*actual);
                else if (auto dword = std::get_if<std::uint32_t>(&*actual); dword) match = rule.enabled_value == std::to_string(*dword);
                else if (auto text = std::get_if<std::string>(&*actual); text) match = rule.enabled_value == *text;
            }
            enabled = enabled && match;
        }
        snapshot.privacy.push_back(enabled);
    }
    snapshot.security.reserve(security_toggles_.size());
    for (auto const& tweak : security_toggles_) {
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
        snapshot.security.push_back(tweak.match_any_target ? any_match : all_match);
    }
    {
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
        snapshot.uac = selection;
    }
    {
        int selected = -1;
        auto value = winchisel::platform::read_registry_value(target(Hive::local_machine, "SYSTEM\\CurrentControlSet\\Control\\CI\\Policy", "VerifiedAndReputablePolicyState", Type::dword));
        if (value) if (auto current = std::get_if<std::uint32_t>(&*value); current && *current <= 2) selected = static_cast<int>(*current);
        snapshot.smart_app_control = selected;
    }
    {
        auto user = winchisel::platform::read_registry_value(target(Hive::current_user, "Software\\Microsoft\\PowerShell\\1\\ShellIds\\Microsoft.PowerShell", "ExecutionPolicy", Type::string));
        auto machine = winchisel::platform::read_registry_value(target(Hive::local_machine, "Software\\Microsoft\\PowerShell\\1\\ShellIds\\Microsoft.PowerShell", "ExecutionPolicy", Type::string));
        auto user_text = user ? std::get_if<std::string>(&*user) : nullptr;
        auto machine_text = machine ? std::get_if<std::string>(&*machine) : nullptr;
        std::array<std::string_view, 5> options{"Restricted", "AllSigned", "RemoteSigned", "Unrestricted", "Bypass"};
        int selected = -1;
        if ((!user_text || !machine_text) || *user_text == *machine_text) {
            auto current = user_text ? user_text : machine_text;
            if (current) for (std::size_t i{}; i < options.size(); ++i) if (*current == options[i]) selected = static_cast<int>(i);
        }
        snapshot.powershell = selected;
    }
    snapshot.ads = detect_ads_mode();
    return snapshot;
}

void PrivacyPage::apply_snapshot(PrivacySnapshot const& snapshot) {
    loading_security_ = true;
    for (std::size_t index{}; index < privacy_toggles_.size() && index < snapshot.privacy.size(); ++index) {
        privacy_toggles_[index].control.IsOn(snapshot.privacy[index]);
        const bool on = snapshot.privacy[index];
        update_state_badges(privacy_toggles_[index].rec_badge, privacy_toggles_[index].def_badge, !on, on);
    }
    for (std::size_t index{}; index < security_toggles_.size() && index < snapshot.security.size(); ++index) {
        security_toggles_[index].control.IsOn(snapshot.security[index]);
        const bool on = snapshot.security[index];
        update_state_badges(security_toggles_[index].rec_badge, security_toggles_[index].def_badge, !on, on);
    }
    loading_security_ = false;
    loading_uac_ = true;
    uac_level_.SelectedIndex(snapshot.uac);
    update_state_badges(uac_rec_, uac_def_, snapshot.uac == 4, snapshot.uac == 2);
    if (smart_app_control_) smart_app_control_.SelectedIndex(snapshot.smart_app_control);
    update_state_badges(sac_rec_, sac_def_, snapshot.smart_app_control == 0, snapshot.smart_app_control == 2);
    if (powershell_policy_) powershell_policy_.SelectedIndex(snapshot.powershell);
    update_state_badges(ps_rec_, ps_def_, snapshot.powershell == 2, snapshot.powershell == 0);
    if (ads_mode_) ads_mode_.SelectedIndex(snapshot.ads);
    update_state_badges(ads_rec_, ads_def_, snapshot.ads == 1, snapshot.ads == 0);
    loading_uac_ = false;
}

void PrivacyPage::submit(std::function<winchisel::core::Result<void>()> change) {
    pending_.push_back(std::move(change));
    process_changes();
}

winrt::fire_and_forget PrivacyPage::process_changes() {
    const auto weak = get_weak();
    winrt::Microsoft::UI::Dispatching::DispatcherQueue queue{nullptr};
    try { queue = DispatcherQueue(); } catch (...) {}
    winrt::apartment_context ui;
    if (work_running_) co_return;
    work_running_ = true;
    try {
        auto lifetime = get_strong();
        IsEnabled(false);
        std::string failure;
        while (true) {
            while (!pending_.empty()) {
                auto change = std::move(pending_.front());
                pending_.pop_front();
                co_await winrt::resume_background();
                const auto result = winchisel::ui::result_or_error(change);
                co_await ui;
                if (!result) { failure = result.error().detail; pending_.clear(); break; }
            }
            co_await winrt::resume_background();
            auto snapshot = read_snapshot();
            co_await ui;
            apply_snapshot(snapshot);
            work_running_ = false;
            if (pending_.empty() || !failure.empty()) break;
            work_running_ = true;
            IsEnabled(false);
        }
        IsEnabled(true);
        if (!failure.empty()) show_write_error(failure);
    } catch (...) {
        winchisel::ui::report_async_error(queue, [weak](winrt::hstring const& text) {
            if (auto self = weak.get()) {
                self->pending_.clear();
                self->work_running_ = false;
                self->IsEnabled(true);
                self->loading_security_ = false;
                self->loading_uac_ = false;
                self->show_write_error(to_string(text));
            }
        });
    }
}

void PrivacyPage::save_security_toggle(std::size_t index) {
    if (loading_security_ || index >= security_toggles_.size()) return;
    const auto& tweak = security_toggles_[index];
    const auto& values = tweak.control.IsOn() ? tweak.enabled_values : tweak.disabled_values;
    std::vector<std::pair<Target,Value>> changes;
    for (std::size_t target_index = 0; target_index < tweak.targets.size(); ++target_index) {
        changes.emplace_back(tweak.targets[target_index],values[target_index]);
    }
    submit([changes=std::move(changes)] { return winchisel::platform::write_registry_values_atomic(changes); });
}

void PrivacyPage::save_uac_level() {
    if (loading_uac_) return;
    constexpr std::array<std::pair<std::uint32_t, std::uint32_t>, 5> values{{{1, 1}, {2, 1}, {5, 1}, {5, 0}, {0, 0}}};
    const auto selected = uac_level_.SelectedIndex();
    if (selected < 0 || selected >= static_cast<std::int32_t>(values.size())) return;
    const auto key = "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\System";
    const auto prompt = target(Hive::local_machine, key, "ConsentPromptBehaviorAdmin", Type::dword);
    const auto secure = target(Hive::local_machine, key, "PromptOnSecureDesktop", Type::dword);
    submit([prompt, secure, first = values[selected].first, second = values[selected].second] {
        return winchisel::platform::write_registry_values_atomic({{prompt, first}, {secure, second}});
    });
}

bool PrivacyPage::append_privacy_rules(std::vector<std::pair<Target,Value>>& changes, std::string_view id, bool enabled) const {
    for (auto const& rule : winchisel::core::get_privacy_registry_rules()) {
        if (rule.id != id) continue;
        auto destination = target(rule.root ? Hive::local_machine : Hive::current_user, rule.path.data(), rule.name.data(), rule.kind ? Type::string : Type::dword);
        auto token = enabled ? rule.enabled_value : rule.disabled_value;
        Value value; if (token == "__MISSING__") value = std::monostate{}; else if (rule.kind) value = std::string(token); else if (auto parsed = parse_dword_token(std::string_view(token))) value = *parsed; else return false;
        changes.emplace_back(destination, value);
    }
    return true;
}

bool PrivacyPage::append_ads_rules(std::vector<std::pair<Target,Value>>& changes, int mode) const {
    if (mode != 0 && mode != 1) return true;
    const bool enabled = mode == 0;
    for (auto const& item : winchisel::core::get_privacy_catalog()) if (item.group == 1) if (!append_privacy_rules(changes, item.id, enabled)) return false;
    return true;
}

int PrivacyPage::detect_ads_mode() const {
    bool saw{}; bool all_allow{true}; bool all_deny{true};
    for (auto const& item : winchisel::core::get_privacy_catalog()) {
        if (item.group != 1) continue;
        bool found{}; bool matches_enabled{true};
        for (auto const& rule : winchisel::core::get_privacy_registry_rules()) {
            if (rule.id != item.id) continue;
            found = true;
            auto actual = winchisel::platform::read_registry_value(target(rule.root ? Hive::local_machine : Hive::current_user, rule.path.data(), rule.name.data(), rule.kind ? Type::string : Type::dword));
            bool match = false;
            if (actual) {
                if (rule.enabled_value == "__MISSING__") match = std::holds_alternative<std::monostate>(*actual);
                else if (auto dword = std::get_if<std::uint32_t>(&*actual); dword) match = rule.enabled_value == std::to_string(*dword);
                else if (auto text = std::get_if<std::string>(&*actual); text) match = rule.enabled_value == *text;
            }
            matches_enabled = matches_enabled && match;
        }
        if (!found) continue;
        saw = true;
        all_allow = all_allow && matches_enabled;
        all_deny = all_deny && !matches_enabled;
    }
    if (!saw || (all_allow == all_deny)) return 2;
    return all_allow ? 0 : 1;
}

void PrivacyPage::save_smart_app_control(){if(loading_uac_||!smart_app_control_||smart_app_control_.SelectedIndex()<0)return;auto change=std::pair{target(Hive::local_machine,"SYSTEM\\CurrentControlSet\\Control\\CI\\Policy","VerifiedAndReputablePolicyState",Type::dword),Value{static_cast<std::uint32_t>(smart_app_control_.SelectedIndex())}};submit([change=std::move(change)] { return winchisel::platform::write_registry_values_atomic({change}); });}
void PrivacyPage::save_powershell_policy(){if(loading_uac_||!powershell_policy_||powershell_policy_.SelectedIndex()<0)return;constexpr std::array values{"Restricted","AllSigned","RemoteSigned","Unrestricted","Bypass"};auto selected=powershell_policy_.SelectedIndex();std::vector<std::pair<Target,Value>> changes;for(auto hive:{Hive::current_user,Hive::local_machine})changes.emplace_back(target(hive,"Software\\Microsoft\\PowerShell\\1\\ShellIds\\Microsoft.PowerShell","ExecutionPolicy",Type::string),std::string(values[selected]));submit([changes=std::move(changes)] { return winchisel::platform::write_registry_values_atomic(changes); });}
void PrivacyPage::save_ads_mode(){if(loading_uac_||!ads_mode_)return;const auto mode=ads_mode_.SelectedIndex();if(mode==2)return;std::vector<std::pair<Target,Value>> changes;if(!append_ads_rules(changes,mode)){show_write_error("A privacy catalog entry is invalid and was not applied.");submit([] { return winchisel::core::Result<void>{}; });return;}if(changes.empty())return;submit([changes=std::move(changes)] { return winchisel::platform::write_registry_values_atomic(changes); });}

PrivacyPage::ProfilePlan PrivacyPage::build_profile_plan(bool recommended) const {
    // Explicit per-setting profile specification. `recommended` trades
    // protection features for fewer prompts; `defaults` restores them.
    ProfilePlan plan;
    auto widen_ascii = [](std::string_view text) { return std::wstring(text.begin(), text.end()); };
    plan.security_on = !recommended;
    plan.privacy_on = !recommended;
    plan.uac_index = recommended ? 4 : 2;
    plan.sac_index = recommended ? 0 : 2;
    plan.powershell_index = recommended ? 2 : 0;
    plan.ads_mode = recommended ? 1 : 0;
    auto on_off = [](bool value) { return value ? L"On" : L"Off"; };
    for (auto const& tweak : security_toggles_) {
        const bool current = tweak.control.IsOn();
        if (current != plan.security_on) {
            plan.summary.push_back(L"\u2022 " + std::wstring(tweak.title.c_str()) + L": " + on_off(current) + L" \u2192 " + on_off(plan.security_on));
        }
        auto const& values = plan.security_on ? tweak.enabled_values : tweak.disabled_values;
        for (std::size_t i{}; i < tweak.targets.size(); ++i) plan.changes.emplace_back(tweak.targets[i], values[i]);
    }
    for (auto const& toggle : privacy_toggles_) {
        std::wstring name = widen_ascii(toggle.id);
        for (auto const& item : winchisel::core::get_privacy_catalog()) {
            if (item.id == toggle.id) { name = widen_ascii(item.name); break; }
        }
        const bool current = toggle.control.IsOn();
        if (current != plan.privacy_on) {
            plan.summary.push_back(L"\u2022 " + name + L": " + on_off(current) + L" \u2192 " + on_off(plan.privacy_on));
        }
    }
    for (auto const& item : winchisel::core::get_privacy_catalog()) {
        if (!append_privacy_rules(plan.changes, item.id, plan.privacy_on)) { plan.valid = false; return plan; }
    }
    constexpr std::array<std::pair<std::uint32_t, std::uint32_t>, 5> uac{{{1, 1}, {2, 1}, {5, 1}, {5, 0}, {0, 0}}};
    constexpr std::array uac_labels{L"Always notify", L"Notify for app changes", L"Default: notify without dimming", L"Notify without secure desktop", L"Never notify"};
    {
        const auto current = uac_level_.SelectedIndex();
        if (current != plan.uac_index) {
            std::wstring from = (current >= 0 && current < 5) ? uac_labels[current] : L"Unknown";
            plan.summary.push_back(L"\u2022 User Account Control: " + from + L" \u2192 " + std::wstring(uac_labels[plan.uac_index]));
        }
        const auto key = "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\System";
        plan.changes.emplace_back(target(Hive::local_machine, key, "ConsentPromptBehaviorAdmin", Type::dword), uac[plan.uac_index].first);
        plan.changes.emplace_back(target(Hive::local_machine, key, "PromptOnSecureDesktop", Type::dword), uac[plan.uac_index].second);
    }
    constexpr std::array sac_labels{L"Off", L"On (Enforced)", L"Evaluation Mode"};
    if (smart_app_control_) {
        const auto current = smart_app_control_.SelectedIndex();
        if (current != plan.sac_index) {
            std::wstring from = (current >= 0 && current < 3) ? sac_labels[current] : L"Unknown";
            plan.summary.push_back(L"\u2022 Smart App Control: " + from + L" \u2192 " + std::wstring(sac_labels[plan.sac_index]));
        }
        plan.changes.emplace_back(target(Hive::local_machine, "SYSTEM\\CurrentControlSet\\Control\\CI\\Policy", "VerifiedAndReputablePolicyState", Type::dword), static_cast<std::uint32_t>(plan.sac_index));
    }
    constexpr std::array ps_labels{"Restricted", "AllSigned", "RemoteSigned", "Unrestricted", "Bypass"};
    if (powershell_policy_) {
        const auto current = powershell_policy_.SelectedIndex();
        if (current != plan.powershell_index) {
            std::wstring from = (current >= 0 && current < 5) ? widen_ascii(ps_labels[current]) : L"Unknown";
            plan.summary.push_back(L"• PowerShell Execution Policy: " + from + L" → " + widen_ascii(ps_labels[plan.powershell_index]));
        }
        for (auto hive : {Hive::current_user, Hive::local_machine}) {
            plan.changes.emplace_back(target(hive, "Software\\Microsoft\\PowerShell\\1\\ShellIds\\Microsoft.PowerShell", "ExecutionPolicy", Type::string), std::string(ps_labels[plan.powershell_index]));
        }
    }
    constexpr std::array ads_labels{L"Allow", L"Deny", L"Custom"};
    if (ads_mode_) {
        const auto current = ads_mode_.SelectedIndex();
        if (current != plan.ads_mode) {
            std::wstring from = (current >= 0 && current < 3) ? ads_labels[current] : L"Unknown";
            plan.summary.push_back(L"\u2022 Ads, Suggestions and Promotional Content: " + from + L" \u2192 " + std::wstring(ads_labels[plan.ads_mode]));
        }
    }
    return plan;
}

winrt::fire_and_forget PrivacyPage::preview_profile(bool recommended) {
    auto error_weak = get_weak();
    winrt::Microsoft::UI::Dispatching::DispatcherQueue error_queue{nullptr};
    try { error_queue = DispatcherQueue(); } catch (...) {}
    try {
        auto error_lifetime = get_strong();
        auto plan = build_profile_plan(recommended);
        if (!plan.valid) {
            show_write_error("A privacy catalog entry is invalid and was not applied.");
            submit([] { return winchisel::core::Result<void>{}; });
            co_return;
        }
        if (plan.summary.empty()) {
            winchisel::ui::show_toast(Controls::InfoBarSeverity::Informational,
                recommended ? L"Recommended settings" : L"Default settings",
                L"Everything already matches this profile. No changes were made.");
            co_return;
        }
        winchisel::core::DialogSlot dialog_slot;
        if (!winchisel::ui::dialog_available(dialog_slot)) co_return;
        Controls::ContentDialog dialog;
        dialog.XamlRoot(XamlRoot());
        dialog.Title(box_value(recommended ? L"Apply Recommended settings?" : L"Restore default settings?"));
        auto content = Controls::StackPanel();
        content.Spacing(8);
        if (recommended) {
            auto warning = Controls::TextBlock();
            warning.Text(L"Recommended turns off protection features (all Security toggles, UAC notifications, Smart App Control) and relaxes the script policy. Review each change below.");
            warning.TextWrapping(TextWrapping::Wrap);
            warning.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
            content.Children().Append(warning);
        }
        auto list = Controls::StackPanel();
        list.Spacing(2);
        for (auto const& line : plan.summary) {
            auto row = Controls::TextBlock();
            row.Text(line);
            row.TextWrapping(TextWrapping::Wrap);
            list.Children().Append(row);
        }
        auto scroll = Controls::ScrollViewer();
        scroll.MaxHeight(320);
        scroll.Content(list);
        content.Children().Append(scroll);
        dialog.Content(content);
        dialog.PrimaryButtonText(L"Apply");
        dialog.CloseButtonText(L"Cancel");
        dialog.DefaultButton(Controls::ContentDialogButton::Close);
        if (co_await dialog.ShowAsync() != Controls::ContentDialogResult::Primary) co_return;
        loading_security_ = true;
        loading_uac_ = true;
        for (auto& toggle : security_toggles_) toggle.control.IsOn(plan.security_on);
        for (auto& toggle : privacy_toggles_) toggle.control.IsOn(plan.privacy_on);
        uac_level_.SelectedIndex(plan.uac_index);
        if (smart_app_control_) smart_app_control_.SelectedIndex(plan.sac_index);
        if (powershell_policy_) powershell_policy_.SelectedIndex(plan.powershell_index);
        if (ads_mode_) ads_mode_.SelectedIndex(plan.ads_mode);
        loading_security_ = false;
        loading_uac_ = false;
        submit([changes = std::move(plan.changes)] { return winchisel::platform::write_registry_values_atomic(changes); });
    } catch (...) {
        winchisel::ui::report_async_error(error_queue, [error_weak](winrt::hstring const& text) {
            if (auto self = error_weak.get()) {
                self->loading_security_ = false;
                self->loading_uac_ = false;
                self->show_write_error(to_string(text));
            }
        });
    }
}

void PrivacyPage::Recommended_Click(Windows::Foundation::IInspectable const&,RoutedEventArgs const&){preview_profile(true);}
void PrivacyPage::Defaults_Click(Windows::Foundation::IInspectable const&,RoutedEventArgs const&){preview_profile(false);}

}  // namespace winrt::Winchisel::implementation
