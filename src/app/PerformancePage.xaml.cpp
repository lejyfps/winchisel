#include "pch.h"
#include "AsyncSupport.hpp"
#include "Localization.hpp"
#include "TeachingTips.hpp"
#include "PerformancePage.xaml.h"
#include "winchisel/platform/system.hpp"
#include "winchisel/platform/update.hpp"
#include "winchisel/application/session.hpp"

#if __has_include("PerformancePage.g.cpp")
#include "PerformancePage.g.cpp"
#endif

#include "winchisel/core/tweak.hpp"
#include "winchisel/core/risk.hpp"
#include "winchisel/platform/registry.hpp"
#include "winchisel/platform/performance.hpp"

#include <array>
#include <algorithm>
#include <cctype>
#include <charconv>
#include <optional>
#include <vector>

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

Controls::Border setting_card(hstring const& title, hstring const& description, FrameworkElement const& control,
    bool is_new = false, winchisel::core::TweakRisk risk = winchisel::core::TweakRisk::safe, bool show_risk = false,
    Controls::Border const& rec_badge = nullptr, Controls::Border const& def_badge = nullptr) {
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
    layout.ColumnSpacing(12);
    auto text = Controls::StackPanel();
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

// Journal label for catalog tweaks: "Performance: <catalog name>".
std::string performance_tweak_label(std::string_view id) {
    std::string label("Performance: ");
    for (auto const& item : winchisel::core::get_performance_catalog()) {
        if (item.id == id) {
            label += item.name;
            return label;
        }
    }
    label += std::string(id);
    return label;
}
// Profile lookup shared by the bulk Recommended/Defaults actions and the
// per-tweak quick-set buttons (Winhance-style). Returns the toggle state
// (0/1) or combo selection index, or nullopt when the tweak has no profile
// value (e.g. scheduled tasks, which the profiles intentionally skip).
std::optional<std::int32_t> performance_profile_for(std::string_view id, bool selection, bool recommended) {
    const auto rules = winchisel::core::get_performance_profile_rules();
    const auto rule = std::ranges::find_if(rules, [id](auto const& value) { return value.id == id; });
    if (rule == rules.end()) return std::nullopt;
    const auto value = selection ? (recommended ? rule->recommended_selection : rule->default_selection)
                                 : (recommended ? rule->recommended_toggle : rule->default_toggle);
    return value < 0 ? std::nullopt : std::optional<std::int32_t>{value};
}

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

// Wraps a ToggleSwitch/ComboBox with the per-tweak Recommended (star) and
// Default (refresh) quick-set buttons, matching Winhance's SettingsCardItem.
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

// Taskbar/Start menu chrome reads its settings at shell startup, so applied
// values only become visible after an Explorer restart (announced in the
// tweak descriptions). Per-window Explorer view settings apply live.
bool needs_shell_restart(std::string_view id) {
    return id == "taskbar-widgets-button" || id == "taskbar-task-view" ||
        id == "taskbar-search-highlights" || id == "taskbar-copilot-button" ||
        id == "taskbar-end-task" || id == "taskbar-alignment" ||
        id == "taskbar-search-mode" || id == "start-bing-search" ||
        id == "start-recommended-content" || id == "start-layout";
}

hstring toggle_tip(bool recommended_default, bool state) {
    // Localized "Recommended"/"Defaults" prefix plus the target On/Off state,
    // so the tooltip documents what the button will do.
    const auto prefix = recommended_default ? winchisel::ui::tr(L"Recommended") : winchisel::ui::tr(L"Defaults");
    return hstring{std::wstring(prefix) + L": " + (state ? L"On" : L"Off")};
}

// State pill showing whether the current value matches Recommended or the
// Windows default (Winhance-style). Starts hidden; load_*() refreshes it.
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

void PerformancePage::show_bulk_profile_tip(Windows::Foundation::IInspectable const& sender) {
    auto anchor = sender.try_as<FrameworkElement>();
    if (!anchor) return;
    winchisel::ui::open_teaching_tip(bulk_tip_, bulk_tip_closed_, anchor, "bulk_profile",
        L"Applies the full profile at once",
        L"Recommended/Defaults change many settings across all groups. Every change is logged in Change history and can be undone — a restore point first is still recommended.");
}

PerformancePage::PerformancePage() {
    InitializeComponent();
    const bool show_new = show_new_badges();
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
        expander.Tag(box_value(group_index));
        {
            auto weak = get_weak();
            expander.Expanding([weak, expander](auto const&, auto const&) {
                if (auto self = weak.get()) self->ensure_group(expander);
            });
            expander.Collapsed([weak, expander](auto const&, auto const&) {
                if (auto self = weak.get()) self->detach_group(expander);
            });
        }
        Groups().Children().Append(expander);
        ++group_index;
    }
    for (auto const& child : Groups().Children()) {
        auto expander = child.try_as<Controls::Expander>();
        if (expander && expander.IsExpanded()) ensure_group(expander);
    }
    process_changes();
}

void PerformancePage::fill_group(Controls::Expander const& expander) {
    const auto group_index = unbox_value_or<std::int32_t>(expander.Tag(), -1);
    if (group_index < 0 || expander.Content()) return;
    const bool show_new = show_new_badges();
    if (group_index == 0) {
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
                tweak.rec_badge = state_badge(true);
                tweak.def_badge = state_badge(false);
                const bool rec = tweak.recommended, def = tweak.windows_default;
                auto trailing = with_quick_set(tweak.control, toggle_tip(true, rec), toggle_tip(false, def),
                    [this, index, rec] { if (!loading_gaming_toggles_) gaming_toggles_[index].control.IsOn(rec); },
                    [this, index, def] { if (!loading_gaming_toggles_) gaming_toggles_[index].control.IsOn(def); });
                content.Children().Append(setting_card(tweak.title, tweak.description, trailing,
                    false, winchisel::core::assess_registry_targets(tweak.targets), risk_badges_visible(),
                    tweak.rec_badge, tweak.def_badge));
            }
            mouse_hover_time_ = Controls::ComboBox();
            Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(mouse_hover_time_,L"Mouse Hover Time");
            for (auto const& option : {L"1ms (Instant)", L"10ms (Very Fast)", L"50ms (Fast)", L"100ms (Moderate)", L"200ms", L"400ms (Default)"}) {
                auto item = Controls::ComboBoxItem();
                item.Content(box_value(option));
                mouse_hover_time_.Items().Append(item);
            }
            mouse_hover_time_.SelectionChanged([this](auto&&, auto&&) { save_mouse_hover_time(); });
            mouse_hover_rec_ = state_badge(true);
            mouse_hover_def_ = state_badge(false);
            {
                auto rec_tip = hstring{std::wstring(winchisel::ui::tr(L"Recommended")) + L": 1ms (Instant)"};
                auto def_tip = hstring{std::wstring(winchisel::ui::tr(L"Defaults")) + L": 400ms (Default)"};
                auto trailing = with_quick_set(mouse_hover_time_, rec_tip, def_tip,
                    [this] { if (!loading_gaming_selections_) mouse_hover_time_.SelectedIndex(0); },
                    [this] { if (!loading_gaming_selections_) mouse_hover_time_.SelectedIndex(5); });
                content.Children().Append(setting_card(L"Mouse Hover Time", L"Sets how long the pointer must hover before Windows responds.", trailing,
                    false, winchisel::core::assess_registry_targets(std::array{target(Hive::current_user, "Control Panel\\Mouse", "MouseHoverTime", Type::string)}), risk_badges_visible(),
                    mouse_hover_rec_, mouse_hover_def_));
            }
            background_apps_ = Controls::ComboBox();
            Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(background_apps_,L"Let Apps Run in Background");
            for (auto const& option : {L"User in Control (Default)", L"Force Allow", L"Force Deny"}) {
                auto item = Controls::ComboBoxItem();
                item.Content(box_value(option));
                background_apps_.Items().Append(item);
            }
            background_apps_.SelectionChanged([this](auto&&, auto&&) { save_background_apps(); });
            background_rec_ = state_badge(true);
            background_def_ = state_badge(false);
            {
                auto rec_tip = hstring{std::wstring(winchisel::ui::tr(L"Recommended")) + L": Force Deny"};
                auto def_tip = hstring{std::wstring(winchisel::ui::tr(L"Defaults")) + L": User in Control (Default)"};
                auto trailing = with_quick_set(background_apps_, rec_tip, def_tip,
                    [this] { if (!loading_gaming_selections_) background_apps_.SelectedIndex(2); },
                    [this] { if (!loading_gaming_selections_) background_apps_.SelectedIndex(0); });
                content.Children().Append(setting_card(L"Let Apps Run in Background", L"Controls whether apps may continue running in the background.", trailing,
                    false, winchisel::core::assess_registry_targets(std::array{
                        target(Hive::current_user, "SOFTWARE\\Policies\\Microsoft\\Windows\\AppPrivacy", "LetAppsRunInBackground", Type::dword),
                        target(Hive::local_machine, "SOFTWARE\\Policies\\Microsoft\\Windows\\AppPrivacy", "LetAppsRunInBackground", Type::dword)}), risk_badges_visible(),
                    background_rec_, background_def_));
            }
            expander.Content(content);
        } else {
            auto content = Controls::StackPanel();
            content.Spacing(8);
            content.HorizontalAlignment(HorizontalAlignment::Stretch);
            for (auto const& item : winchisel::core::get_performance_catalog()) {
                if (item.group != group_index) continue;
                if (item.id=="gaming-memory-integrity"||item.id=="gaming-performance-prefetch"||item.id=="gaming-disable-mpo-min-fps") continue;
                FrameworkElement control{nullptr};
                Controls::Border card_rec{nullptr}, card_def{nullptr};
                if (item.input == 0) {
                    auto toggle = Controls::ToggleSwitch();
                    Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(toggle,to_hstring(item.name));
                    toggle.OnContent(box_value(L""));
                    toggle.OffContent(box_value(L""));
                    toggle.MinWidth(0);
                    toggle.Width(40);
                    const auto toggle_index = catalog_toggles_.size();
                    const bool supported = item.group==7 || winchisel::platform::is_special_performance_toggle(item.id) || std::ranges::any_of(winchisel::core::get_performance_registry_rules(), [&](auto const& rule) { return rule.id == item.id; });
                    toggle.IsEnabled(supported);
                    catalog_toggles_.push_back({std::string(item.id), toggle});
                    auto& stored_toggle = catalog_toggles_.back();
                    toggle.Toggled([this, toggle_index](auto&&, auto&&) { save_catalog_toggle(toggle_index); });
                    if (auto rec = performance_profile_for(item.id, false, true), def = performance_profile_for(item.id, false, false); rec && def) {
                        const bool rec_on = *rec != 0, def_on = *def != 0;
                        stored_toggle.profile_rec = *rec;
                        stored_toggle.profile_def = *def;
                        stored_toggle.rec_badge = state_badge(true);
                        stored_toggle.def_badge = state_badge(false);
                        card_rec = stored_toggle.rec_badge;
                        card_def = stored_toggle.def_badge;
                        control = with_quick_set(toggle, toggle_tip(true, rec_on), toggle_tip(false, def_on),
                            [this, toggle_index, rec_on] { if (!loading_gaming_toggles_) catalog_toggles_[toggle_index].control.IsOn(rec_on); },
                            [this, toggle_index, def_on] { if (!loading_gaming_toggles_) catalog_toggles_[toggle_index].control.IsOn(def_on); });
                    } else {
                        control = toggle;
                    }
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
                    combo.SelectedIndex(-1);
                    const auto selection_index=catalog_selections_.size();
                    combo.IsEnabled(true);
                    catalog_selections_.push_back({std::string(item.id),std::move(options),combo});
                    combo.SelectionChanged([this,selection_index](auto&&,auto&&){save_catalog_selection(selection_index);});
                    {
                        auto& stored = catalog_selections_.back();
                        auto rec = performance_profile_for(item.id, true, true);
                        auto def = performance_profile_for(item.id, true, false);
                        const bool has = rec && def && *rec >= 0 && *def >= 0 &&
                            *rec < static_cast<std::int32_t>(stored.options.size()) &&
                            *def < static_cast<std::int32_t>(stored.options.size());
                        if (has) {
                            auto label = [&](std::int32_t v) { return to_hstring(stored.options[static_cast<std::size_t>(v)]); };
                            auto rec_tip = hstring{std::wstring(winchisel::ui::tr(L"Recommended")) + L": " + std::wstring(label(*rec))};
                            auto def_tip = hstring{std::wstring(winchisel::ui::tr(L"Defaults")) + L": " + std::wstring(label(*def))};
                            const auto rec_idx = *rec, def_idx = *def;
                            stored.profile_rec = rec_idx;
                            stored.profile_def = def_idx;
                            stored.rec_badge = state_badge(true);
                            stored.def_badge = state_badge(false);
                            card_rec = stored.rec_badge;
                            card_def = stored.def_badge;
                            control = with_quick_set(combo, rec_tip, def_tip,
                                [this, selection_index, rec_idx] { if (!loading_gaming_selections_) catalog_selections_[selection_index].control.SelectedIndex(rec_idx); },
                                [this, selection_index, def_idx] { if (!loading_gaming_selections_) catalog_selections_[selection_index].control.SelectedIndex(def_idx); });
                        } else {
                            control = combo;
                        }
                    }
                }
                auto card=setting_card(to_hstring(item.name),to_hstring(item.description),control,show_new&&winchisel::core::is_new_tweak(item.id),winchisel::core::assess_performance(item.id),risk_badges_visible(),card_rec,card_def);
                std::string_view child_id;
                if(item.id=="gaming-virtualization-based-security")child_id="gaming-memory-integrity";
                else if(item.id=="gaming-sysmain-service")child_id="gaming-performance-prefetch";
                else if(item.id=="gaming-disable-mpo")child_id="gaming-disable-mpo-min-fps";
                if(child_id.empty())content.Children().Append(card);
                else {
                    auto nested=Controls::Expander();nested.Header(card);nested.HorizontalAlignment(HorizontalAlignment::Stretch);nested.HorizontalContentAlignment(HorizontalAlignment::Stretch);
                    auto child=std::ranges::find_if(winchisel::core::get_performance_catalog(),[&](auto const& candidate){return candidate.id==child_id;});
                    if(child!=winchisel::core::get_performance_catalog().end()){
                        auto toggle=Controls::ToggleSwitch();Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(toggle,to_hstring(child->name));toggle.OnContent(box_value(L""));toggle.OffContent(box_value(L""));toggle.MinWidth(0);toggle.Width(40);auto child_index=catalog_toggles_.size();toggle.IsEnabled(true);catalog_toggles_.push_back({std::string(child->id),toggle});auto& stored_child=catalog_toggles_.back();toggle.Toggled([this,child_index](auto&&,auto&&){save_catalog_toggle(child_index);});FrameworkElement child_control{nullptr};Controls::Border child_rec{nullptr},child_def{nullptr};if(auto rec=performance_profile_for(child->id,false,true),def=performance_profile_for(child->id,false,false);rec&&def){const bool rec_on=*rec!=0,def_on=*def!=0;stored_child.profile_rec=*rec;stored_child.profile_def=*def;stored_child.rec_badge=state_badge(true);stored_child.def_badge=state_badge(false);child_rec=stored_child.rec_badge;child_def=stored_child.def_badge;child_control=with_quick_set(toggle,toggle_tip(true,rec_on),toggle_tip(false,def_on),[this,child_index,rec_on]{if(!loading_gaming_toggles_)catalog_toggles_[child_index].control.IsOn(rec_on);},[this,child_index,def_on]{if(!loading_gaming_toggles_)catalog_toggles_[child_index].control.IsOn(def_on);});}else{child_control=toggle;}auto child_card=setting_card(to_hstring(child->name),to_hstring(child->description),child_control,show_new&&winchisel::core::is_new_tweak(child->id),winchisel::core::assess_performance(child->id),risk_badges_visible(),child_rec,child_def);child_card.Margin({24,8,0,0});nested.Content(child_card);
                    }
                    content.Children().Append(nested);
                }
            }
            expander.Content(content);
        }
}

void PerformancePage::ensure_group(Controls::Expander const& expander) {
    if (!expander) return;
    if (expander.Content()) return;
    attach_group(expander);
    if (expander.Content()) return;
    fill_group(expander);
    if (registry_state_.empty()) return;
    const auto index = unbox_value_or<std::int32_t>(expander.Tag(), -1);
    if (index == 0) {
        load_gaming_toggles();
        load_gaming_selections();
    } else {
        load_catalog_toggles();
        load_catalog_selections();
    }
}

void PerformancePage::ensure_all_groups() {
    for (auto const& child : Groups().Children()) {
        if (auto expander = child.try_as<Controls::Expander>()) ensure_group(expander);
    }
}

bool PerformancePage::group_matches_query(int index, std::string const& query) const {
    if (query.empty() || index < 0 || static_cast<std::size_t>(index) >= winchisel::core::k_performance_groups.size()) return true;
    auto hay = std::string(winchisel::core::k_performance_groups[static_cast<std::size_t>(index)].title);
    std::ranges::transform(hay, hay.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (hay.find(query) != std::string::npos) return true;
    if (index == 0) {
        for (auto const& tweak : gaming_toggles_) {
            auto text = to_string(tweak.title) + " " + to_string(tweak.description);
            std::ranges::transform(text, text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (text.find(query) != std::string::npos) return true;
        }
        if (std::string("mouse hover time").find(query) != std::string::npos) return true;
        if (std::string("let apps run in background").find(query) != std::string::npos) return true;
    }
    for (auto const& item : winchisel::core::get_performance_catalog()) {
        if (item.group != index) continue;
        auto text = std::string(item.name) + " " + std::string(item.description) + " " + std::string(item.id);
        std::ranges::transform(text, text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (text.find(query) != std::string::npos) return true;
    }
    return false;
}

void PerformancePage::attach_group(Controls::Expander const& expander) {
    if (!expander || expander.Content()) return;
    const auto index = unbox_value_or<std::int32_t>(expander.Tag(), -1);
    if (index < 0) return;
    if (auto found = detached_content_.find(index); found != detached_content_.end()) {
        expander.Content(found->second);
        detached_content_.erase(found);
    }
}

void PerformancePage::detach_group(Controls::Expander const& expander) {
    if (!expander || !expander.Content()) return;
    const auto index = unbox_value_or<std::int32_t>(expander.Tag(), -1);
    if (index < 0) return;
    detached_content_[index] = expander.Content();
    expander.Content(nullptr);
}

void PerformancePage::load_catalog_toggles() {
    loading_gaming_toggles_ = true;
    for (auto& item : catalog_toggles_) {
        if (!item.control) continue;
        if (winchisel::platform::is_special_performance_toggle(item.id)) {
            if (auto state = cached_special(item.id)) item.control.IsOn(*state);
            if (auto available = cached_available(item.id)) item.control.IsEnabled(*available);
            if (item.rec_badge) {
                const bool on = item.control.IsOn();
                update_state_badges(item.rec_badge, item.def_badge, on == (item.profile_rec != 0), on == (item.profile_def != 0));
            }
            continue;
        }
        bool found{}, enabled = true;
        for (auto const& rule : winchisel::core::get_performance_registry_rules()) {
            if (rule.id != item.id) continue;
            found = true;
            auto type = rule.kind == 0 ? Type::dword : rule.kind == 1 ? Type::string : Type::binary;
            auto actual = cached_value(target(rule.root == 0 ? Hive::current_user : Hive::local_machine, rule.path.data(), rule.name.data(), type));
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
        else if(auto state=cached_task(item.id);state)item.control.IsOn(*state);
        if (item.rec_badge) {
            const bool on = item.control.IsOn();
            update_state_badges(item.rec_badge, item.def_badge, on == (item.profile_rec != 0), on == (item.profile_def != 0));
        }
    }
    loading_gaming_toggles_ = false;
}

std::optional<Value> catalog_desired(auto const& rule, bool enabled, auto&& read) {
    auto type = rule.kind == 0 ? Type::dword : rule.kind == 1 ? Type::string : Type::binary;
    auto destination = target(rule.root == 0 ? Hive::current_user : Hive::local_machine, rule.path.data(), rule.name.data(), type);
    auto values = enabled ? rule.enabled_values : rule.disabled_values; auto separator = values.find('|'); auto value = values.substr(0, separator);
    Value desired;
    if (rule.kind == 0) {
        if (value == "__MISSING__") desired = Value{std::monostate{}};
        else if (auto parsed = parse_dword_token(value)) desired = Value{*parsed};
        else return std::nullopt;
    }
    else if (rule.kind == 1) desired = value == "__MISSING__" ? Value{std::monostate{}} : Value{std::string(value)};
    else {
        auto current=read(destination);
        auto bytes=current?std::get_if<std::vector<std::uint8_t>>(&*current):nullptr;
        std::vector<std::uint8_t> data=bytes?*bytes:std::vector<std::uint8_t>{};
        if(rule.byte_index<0) return desired;
        if(data.size()<=static_cast<std::size_t>(rule.byte_index))data.resize(rule.byte_index+1);
        if(enabled)data[rule.byte_index]|=rule.bit_mask;else data[rule.byte_index]&=static_cast<std::uint8_t>(~rule.bit_mask);
        desired=std::move(data);
    }
    return desired;
}

void PerformancePage::save_catalog_toggle(std::size_t index) {
    if (loading_gaming_toggles_ || index >= catalog_toggles_.size()) return;
    auto const& item = catalog_toggles_[index]; const bool enabled = item.control.IsOn();
    if (winchisel::platform::is_special_performance_toggle(item.id)) {
        if (auto available = cached_available(item.id); available && !*available) {
            show_write_error("This tweak is unavailable on the detected hardware and was not applied.");
            load_catalog_toggles();
            return;
        }
        submit([id = item.id, enabled] { return winchisel::platform::write_special_performance_toggle(id, enabled); });
        return;
    }
    std::vector<std::pair<Target,Value>> registry;
    std::vector<std::pair<std::string,bool>> tasks;
    for (auto const& rule : winchisel::core::get_performance_registry_rules()) {
        if (rule.id != item.id) continue;
        auto type = rule.kind == 0 ? Type::dword : rule.kind == 1 ? Type::string : Type::binary;
        auto destination = target(rule.root == 0 ? Hive::current_user : Hive::local_machine, rule.path.data(), rule.name.data(), type);
        auto desired = catalog_desired(rule, enabled, [this](auto const& destination){return cached_value(destination);});
        if (!desired) { show_write_error("A performance catalog entry is invalid and was not applied."); load_catalog_toggles(); return; }
        registry.emplace_back(destination, std::move(*desired));
    }
    const bool has_registry=std::ranges::any_of(winchisel::core::get_performance_registry_rules(),[&](auto const& rule){return rule.id==item.id;}) || winchisel::platform::is_special_performance_toggle(item.id);
    if(!has_registry) tasks.emplace_back(std::string(item.id), enabled);
    const bool shell = needs_shell_restart(item.id);
    const std::string label = performance_tweak_label(item.id);
    submit([registry=std::move(registry),tasks=std::move(tasks),shell,label]{ auto applied = winchisel::platform::apply_registry_and_tasks(registry,tasks,std::nullopt,std::nullopt,"performance",label); if (applied && shell) winchisel::platform::restart_shell(); return applied; });
}

void PerformancePage::load_catalog_selections() {
    loading_gaming_selections_ = true;
    for (auto& item : catalog_selections_) {
        if (!item.control) continue;
        if(item.id=="gaming-dns-server"){auto profile=dns_state_;item.control.SelectedIndex(profile&&*profile<static_cast<int>(item.options.size())?*profile:-1);if(item.rec_badge){const auto selected=item.control.SelectedIndex();update_state_badges(item.rec_badge,item.def_badge,selected==item.profile_rec,selected==item.profile_def);}continue;}
        if(item.id=="updates-policy-mode"){auto policy=update_policy_state_;int selected=4;if(policy&&*policy>=0&&*policy<static_cast<int>(item.options.size()))selected=*policy;item.control.SelectedIndex(selected);if(item.rec_badge)update_state_badges(item.rec_badge,item.def_badge,selected==item.profile_rec,selected==item.profile_def);continue;}
        if(item.id=="updates-delivery-optimization"){
            const auto user_value=cached_value(target(Hive::current_user,"SOFTWARE\\Policies\\Microsoft\\Windows\\DeliveryOptimization","DODownloadMode",Type::dword));
            const auto machine_value=cached_value(target(Hive::local_machine,"SOFTWARE\\Policies\\Microsoft\\Windows\\DeliveryOptimization","DODownloadMode",Type::dword));
            const auto* mode=user_value?std::get_if<std::uint32_t>(&*user_value):nullptr;
            if(!mode&&machine_value)mode=std::get_if<std::uint32_t>(&*machine_value);
            int selected=0;
            if(mode)selected=*mode==1?1:*mode==3?2:*mode==99?3:-1;
            item.control.SelectedIndex(selected);
            if(item.rec_badge)update_state_badges(item.rec_badge,item.def_badge,selected==item.profile_rec,selected==item.profile_def);
            continue;
        }
        if(item.id=="taskbar-alignment"||item.id=="taskbar-search-mode"||item.id=="start-layout"){
            Target destination=item.id=="taskbar-search-mode"
                ?target(Hive::current_user,"Software\\Microsoft\\Windows\\CurrentVersion\\Search","SearchBoxTaskbarMode",Type::dword)
                :item.id=="start-layout"
                ?target(Hive::current_user,"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced","Start_Layout",Type::dword)
                :target(Hive::current_user,"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced","TaskbarAl",Type::dword);
            const int fallback=item.id=="taskbar-search-mode"?2:item.id=="start-layout"?0:1;
            int selected=fallback;
            if(auto current=cached_value(destination))if(auto dword=std::get_if<std::uint32_t>(&*current))selected=static_cast<int>(std::min(*dword,2u));
            if(selected<0||selected>=static_cast<int>(item.options.size()))selected=fallback;
            item.control.SelectedIndex(selected);
            if(item.rec_badge)update_state_badges(item.rec_badge,item.def_badge,selected==item.profile_rec,selected==item.profile_def);
            continue;
        }
        std::uint32_t value{}; bool found{};
        Target destination;
        if (item.id=="gaming-win32-priority") destination=target(Hive::local_machine,"SYSTEM\\CurrentControlSet\\Control\\PriorityControl","Win32PrioritySeparation",Type::dword);
        else if(item.id=="gaming-performance-svchost-split-threshold") destination=target(Hive::local_machine,"SYSTEM\\CurrentControlSet\\Control","SvcHostSplitThresholdInKB",Type::dword);
        else if(item.id=="visual-effects-mode") destination=target(Hive::current_user,"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\VisualEffects","VisualFXSetting",Type::dword);
        else if(auto service=winchisel::core::service_name_for_id(item.id);!service.empty()) destination=target(Hive::local_machine,("SYSTEM\\CurrentControlSet\\Services\\"+std::string(service)).c_str(),"Start",Type::dword);
        else continue;
        auto current=cached_value(destination);
        if(current)if(auto dword=std::get_if<std::uint32_t>(&*current)){value=*dword;found=true;}
        // Service Start values are DWORDs, but tolerate a stray REG_SZ ("2"/"3"/"4") for display.
        if(!found&&current)if(auto text=std::get_if<std::string>(&*current)){if(*text=="4"||*text=="3"||*text=="2"){value=static_cast<std::uint32_t>((*text)[0]-'0');found=true;}}
        if(!found)continue;
        int selected=0;
        if(item.id=="gaming-win32-priority")selected=value==24?1:value==38?0:-1;
        else if(item.id=="gaming-performance-svchost-split-threshold"){selected=10;constexpr std::array<std::uint32_t,10> values{380000,327680,491520,655360,983040,1310720,1966080,2621440,5242880,10485760};for(std::size_t i{};i<values.size();++i)if(values[i]==value)selected=static_cast<int>(i);}
        else if(item.id=="visual-effects-mode")selected=static_cast<int>(std::min(value,3u));
        else {selected=-1;for(std::size_t i{};i<item.options.size();++i){auto option=lower(item.options[i]);if((value==4&&option.find("disabled")!=std::string::npos)||(value==3&&option.find("manual")!=std::string::npos)||(value==2&&option.find("automatic")!=std::string::npos)){selected=static_cast<int>(i);break;}}}
        item.control.SelectedIndex(selected);
        if (item.rec_badge) update_state_badges(item.rec_badge, item.def_badge, selected == item.profile_rec, selected == item.profile_def);
    }
    loading_gaming_selections_ = false;
}

void PerformancePage::save_catalog_selection(std::size_t index) {
    if(loading_gaming_selections_||index>=catalog_selections_.size())return;auto const& item=catalog_selections_[index];auto selected=item.control.SelectedIndex();if(selected<0)return;Target destination;std::uint32_t value{};
    if(item.id=="gaming-dns-server"){submit([selected]{return winchisel::platform::write_dns_profile(selected);});return;}
    if(item.id=="updates-policy-mode"){submit([selected]{return winchisel::platform::write_update_policy(selected);});return;}
    if(item.id=="updates-delivery-optimization"){const auto user=target(Hive::current_user,"SOFTWARE\\Policies\\Microsoft\\Windows\\DeliveryOptimization","DODownloadMode",Type::dword);const auto machine=target(Hive::local_machine,"SOFTWARE\\Policies\\Microsoft\\Windows\\DeliveryOptimization","DODownloadMode",Type::dword);const Value mode=selected==1?Value{std::uint32_t{1}}:selected==2?Value{std::uint32_t{3}}:selected==3?Value{std::uint32_t{99}}:Value{std::monostate{}};submit([user,machine,mode]{return winchisel::platform::write_registry_values_atomic({{user,mode},{machine,mode}},"performance",performance_tweak_label("updates-delivery-optimization"));});return;}
    if(item.id=="gaming-win32-priority"){destination=target(Hive::local_machine,"SYSTEM\\CurrentControlSet\\Control\\PriorityControl","Win32PrioritySeparation",Type::dword);value=selected==0?38:24;}
    else if(item.id=="gaming-performance-svchost-split-threshold"){constexpr std::array<std::uint32_t,10> values{380000,327680,491520,655360,983040,1310720,1966080,2621440,5242880,10485760};if(selected>=static_cast<int>(values.size()))return;destination=target(Hive::local_machine,"SYSTEM\\CurrentControlSet\\Control","SvcHostSplitThresholdInKB",Type::dword);value=values[selected];}
        else if(item.id=="visual-effects-mode"){destination=target(Hive::current_user,"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\VisualEffects","VisualFXSetting",Type::dword);value=static_cast<std::uint32_t>(selected);}
        else if(item.id=="taskbar-alignment"){destination=target(Hive::current_user,"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced","TaskbarAl",Type::dword);value=static_cast<std::uint32_t>(selected);}
        else if(item.id=="taskbar-search-mode"){destination=target(Hive::current_user,"Software\\Microsoft\\Windows\\CurrentVersion\\Search","SearchBoxTaskbarMode",Type::dword);value=static_cast<std::uint32_t>(selected);}
        else if(item.id=="start-layout"){destination=target(Hive::current_user,"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced","Start_Layout",Type::dword);value=static_cast<std::uint32_t>(selected);}
        else if(auto service=winchisel::core::service_name_for_id(item.id);!service.empty()){destination=target(Hive::local_machine,("SYSTEM\\CurrentControlSet\\Services\\"+std::string(service)).c_str(),"Start",Type::dword);auto option=lower(item.options[static_cast<std::size_t>(selected)]);value=option.find("disabled")!=std::string::npos?4:option.find("manual")!=std::string::npos?3:2;}
    else return;
    const bool shell = needs_shell_restart(item.id);
    const std::string label = performance_tweak_label(item.id);
    submit([destination,value,shell,label]{ auto applied = winchisel::platform::write_registry_values_atomic({{destination,Value{value}}},"performance",label); if (applied && shell) winchisel::platform::restart_shell(); return applied; });
}

void PerformancePage::load_gaming_toggles() {
    loading_gaming_toggles_ = true;
    for (auto& tweak : gaming_toggles_) {
        if (!tweak.control) continue;
        bool has_match = false;
        bool all_match = true;
        for (std::size_t index = 0; index < tweak.targets.size(); ++index) {
            const auto actual = cached_value(tweak.targets[index]);
            const bool missing = actual && std::holds_alternative<std::monostate>(*actual);
            const bool matches = actual && (winchisel::core::registry_value_matches(*actual, tweak.enabled_values[index]) ||
                (tweak.missing_counts_as_enabled && missing));
            has_match = has_match || matches;
            all_match = all_match && matches;
        }
        tweak.control.IsOn(tweak.match_any_target ? has_match : all_match);
        const bool on = tweak.control.IsOn();
        update_state_badges(tweak.rec_badge, tweak.def_badge, on == tweak.recommended, on == tweak.windows_default);
    }
    loading_gaming_toggles_ = false;
}

void PerformancePage::save_gaming_toggle(std::size_t index) {
    if (loading_gaming_toggles_ || index >= gaming_toggles_.size()) {
        return;
    }
    const auto& tweak = gaming_toggles_[index];
    const auto& values = tweak.control.IsOn() ? tweak.enabled_values : tweak.disabled_values;
    std::vector<std::pair<Target,Value>> changes;
    for (std::size_t target_index = 0; target_index < tweak.targets.size(); ++target_index) {
        changes.emplace_back(tweak.targets[target_index],values[target_index]);
    }
    const std::string label = "Performance: " + to_string(tweak.title);
    submit([changes=std::move(changes),label]{return winchisel::platform::write_registry_values_atomic(changes,"performance",label);});
}

void PerformancePage::apply_gaming_profile(bool recommended) {
    ensure_all_groups();
    loading_gaming_toggles_ = true;
    for (auto& tweak : gaming_toggles_) {
        if (!tweak.control) continue;
        tweak.control.IsOn(recommended ? tweak.recommended : tweak.windows_default);
    }
    loading_gaming_toggles_ = false;
    loading_gaming_selections_ = true;
    mouse_hover_time_.SelectedIndex(recommended ? 0 : 5);
    background_apps_.SelectedIndex(recommended ? 2 : 0);
    loading_gaming_selections_ = false;
    apply_catalog_profile(recommended);
}

void PerformancePage::apply_catalog_profile(bool recommended) {
    const auto profile_for = [recommended](std::string_view id, bool selection) -> std::optional<std::int32_t> {
        return performance_profile_for(id, selection, recommended);
    };
    loading_gaming_toggles_ = true;
    for (auto& item : catalog_toggles_) if (item.control) if (auto value = profile_for(item.id, false)) item.control.IsOn(*value != 0);
    loading_gaming_toggles_ = false;
    std::vector<std::pair<Target,Value>> registry;
    std::vector<std::pair<std::string,bool>> tasks;
    std::vector<std::pair<std::string,bool>> specials;
    bool shell_restart = false;
    for (auto const& tweak : gaming_toggles_) {
        auto const& values = tweak.control.IsOn() ? tweak.enabled_values : tweak.disabled_values;
        for (std::size_t i{}; i < tweak.targets.size(); ++i) registry.emplace_back(tweak.targets[i], values[i]);
    }
    constexpr std::array hover_values{"1", "10", "50", "100", "200", "400"};
    if (auto index = mouse_hover_time_.SelectedIndex(); index >= 0 && index < static_cast<int>(hover_values.size()))
        registry.emplace_back(target(Hive::current_user,"Control Panel\\Mouse","MouseHoverTime",Type::string), std::string(hover_values[index]));
    const auto bg = background_apps_.SelectedIndex();
    const Value bg_value = bg == 1 ? Value{std::uint32_t{1}} : bg == 2 ? Value{std::uint32_t{2}} : Value{std::monostate{}};
    registry.emplace_back(target(Hive::current_user, "SOFTWARE\\Policies\\Microsoft\\Windows\\AppPrivacy", "LetAppsRunInBackground", Type::dword), bg_value);
    registry.emplace_back(target(Hive::local_machine, "SOFTWARE\\Policies\\Microsoft\\Windows\\AppPrivacy", "LetAppsRunInBackground", Type::dword), bg_value);
    for (auto& item : catalog_toggles_) {
        if (!item.control || !profile_for(item.id, false)) continue;
        const bool enabled = item.control.IsOn();
        if (winchisel::platform::is_special_performance_toggle(item.id)) {
            if (auto available = cached_available(item.id); available && !*available) continue;
            specials.emplace_back(item.id, enabled);
            continue;
        }
        shell_restart = shell_restart || needs_shell_restart(item.id);
        bool has_registry = false;
        for (auto const& rule : winchisel::core::get_performance_registry_rules()) {
            if (rule.id != item.id) continue;
            has_registry = true;
            auto type = rule.kind == 0 ? Type::dword : rule.kind == 1 ? Type::string : Type::binary;
            auto destination = target(rule.root == 0 ? Hive::current_user : Hive::local_machine, rule.path.data(), rule.name.data(), type);
            auto desired = catalog_desired(rule, enabled, [this](auto const& destination){return cached_value(destination);});
            if (!desired) { show_write_error("A performance catalog entry is invalid and was not applied."); load_catalog_toggles(); load_catalog_selections(); return; }
            registry.emplace_back(destination, std::move(*desired));
        }
        if (!has_registry) tasks.emplace_back(item.id, enabled);
    }
    loading_gaming_selections_ = true;
    for (auto& item : catalog_selections_) {
        if (!item.control) continue;
        if (auto value = profile_for(item.id, true); value && *value >= 0 && *value < static_cast<std::int32_t>(item.options.size())) item.control.SelectedIndex(*value);
    }
    loading_gaming_selections_ = false;
    std::optional<int> dns;
    std::optional<int> update_policy;
    for (auto const& item : catalog_selections_) {
        if (!item.control) continue;
        auto selected = item.control.SelectedIndex();
        if (selected < 0 || !profile_for(item.id, true)) continue;
        if (item.id == "gaming-dns-server") { if (selected != 7) dns = selected; continue; }
        if (item.id == "updates-policy-mode") { if (selected != 4) update_policy = selected; continue; }
        shell_restart = shell_restart || needs_shell_restart(item.id);
        if (item.id == "updates-delivery-optimization") {
            const Value mode = selected == 1 ? Value{std::uint32_t{1}} : selected == 2 ? Value{std::uint32_t{3}} : selected == 3 ? Value{std::uint32_t{99}} : Value{std::monostate{}};
            registry.emplace_back(target(Hive::current_user, "SOFTWARE\\Policies\\Microsoft\\Windows\\DeliveryOptimization", "DODownloadMode", Type::dword), mode);
            registry.emplace_back(target(Hive::local_machine, "SOFTWARE\\Policies\\Microsoft\\Windows\\DeliveryOptimization", "DODownloadMode", Type::dword), mode);
            continue;
        }
        Target destination; std::uint32_t value{};
        if (item.id=="gaming-win32-priority"){destination=target(Hive::local_machine,"SYSTEM\\CurrentControlSet\\Control\\PriorityControl","Win32PrioritySeparation",Type::dword);value=selected==0?38:24;}
        else if(item.id=="gaming-performance-svchost-split-threshold"){constexpr std::array<std::uint32_t,10> values{380000,327680,491520,655360,983040,1310720,1966080,2621440,5242880,10485760};if(selected>=static_cast<int>(values.size()))continue;destination=target(Hive::local_machine,"SYSTEM\\CurrentControlSet\\Control","SvcHostSplitThresholdInKB",Type::dword);value=values[selected];}
    else if(item.id=="visual-effects-mode"){destination=target(Hive::current_user,"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\VisualEffects","VisualFXSetting",Type::dword);value=static_cast<std::uint32_t>(selected);}
    else if(item.id=="taskbar-alignment"){destination=target(Hive::current_user,"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced","TaskbarAl",Type::dword);value=static_cast<std::uint32_t>(selected);}
    else if(item.id=="taskbar-search-mode"){destination=target(Hive::current_user,"Software\\Microsoft\\Windows\\CurrentVersion\\Search","SearchBoxTaskbarMode",Type::dword);value=static_cast<std::uint32_t>(selected);}
    else if(item.id=="start-layout"){destination=target(Hive::current_user,"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced","Start_Layout",Type::dword);value=static_cast<std::uint32_t>(selected);}
        else if(auto service=winchisel::core::service_name_for_id(item.id);!service.empty()){destination=target(Hive::local_machine,("SYSTEM\\CurrentControlSet\\Services\\"+std::string(service)).c_str(),"Start",Type::dword);auto option=lower(item.options[static_cast<std::size_t>(selected)]);value=option.find("disabled")!=std::string::npos?4:option.find("manual")!=std::string::npos?3:2;}
        else continue;
        registry.emplace_back(destination, Value{value});
    }
    submit([registry=std::move(registry),tasks=std::move(tasks),dns,update_policy,specials=std::move(specials),shell_restart,recommended]{
        const std::string label = (recommended ? "Recommended profile (Performance)" : "Defaults profile (Performance)");
        if (auto applied = winchisel::platform::apply_registry_and_tasks(registry,tasks,dns,update_policy,"performance",label); !applied) return applied;
        for (auto const& [id, enabled] : specials) {
            if (auto written = winchisel::platform::write_special_performance_toggle(id, enabled); !written) {
                auto faulty = written.error();
                faulty.detail += "; applied registry changes were kept";
                return winchisel::core::Result<void>{std::unexpected(std::move(faulty))};
            }
        }
        // The classic context menu special already restarts Explorer itself;
        // restart once for any other applied shell-chrome tweaks.
        const bool context_restarted = std::ranges::any_of(specials,
            [](auto const& entry) { return entry.first == "explorer-classic-context-menu"; });
        if (shell_restart && !context_restarted) winchisel::platform::restart_shell();
        return winchisel::core::Result<void>{};
    });
}

void PerformancePage::load_gaming_selections() {
    if (!mouse_hover_time_ || !background_apps_) { loading_gaming_selections_ = false; return; }
    loading_gaming_selections_ = true;
    const auto hover = cached_value(
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
    update_state_badges(mouse_hover_rec_, mouse_hover_def_, hover_index == 0, hover_index == 5);

    const auto user_value = cached_value(
        target(Hive::current_user, "SOFTWARE\\Policies\\Microsoft\\Windows\\AppPrivacy", "LetAppsRunInBackground", Type::dword));
    const auto machine_value = cached_value(
        target(Hive::local_machine, "SOFTWARE\\Policies\\Microsoft\\Windows\\AppPrivacy", "LetAppsRunInBackground", Type::dword));
    const auto* value = user_value ? std::get_if<std::uint32_t>(&*user_value) : nullptr;
    if (!value && machine_value) value = std::get_if<std::uint32_t>(&*machine_value);
    background_apps_.SelectedIndex(value && *value == 1 ? 1 : value && *value == 2 ? 2 : 0);
    {
        const auto selected = background_apps_.SelectedIndex();
        update_state_badges(background_rec_, background_def_, selected == 2, selected == 0);
    }
    loading_gaming_selections_ = false;
}

void PerformancePage::save_mouse_hover_time() {
    if (loading_gaming_selections_) return;
    constexpr std::array values{"1", "10", "50", "100", "200", "400"};
    const auto index = mouse_hover_time_.SelectedIndex();
    if(index < 0 || index >= static_cast<int>(values.size()))return;
    const auto destination=target(Hive::current_user,"Control Panel\\Mouse","MouseHoverTime",Type::string);
    const std::string value=values[index];
    submit([destination,value]{return winchisel::platform::write_registry_values_atomic({{destination,Value{value}}},"performance","Performance: Mouse Hover Time");});
}
void PerformancePage::save_background_apps() {
    if (loading_gaming_selections_) return;
    const auto index = background_apps_.SelectedIndex();
    const Value value = index == 1 ? Value{std::uint32_t{1}} : index == 2 ? Value{std::uint32_t{2}} : Value{std::monostate{}};
    const auto user = target(Hive::current_user, "SOFTWARE\\Policies\\Microsoft\\Windows\\AppPrivacy", "LetAppsRunInBackground", Type::dword);
    const auto machine = target(Hive::local_machine, "SOFTWARE\\Policies\\Microsoft\\Windows\\AppPrivacy", "LetAppsRunInBackground", Type::dword);
    submit([user,machine,value]{return winchisel::platform::write_registry_values_atomic({{user,value},{machine,value}},"performance","Performance: Let Apps Run in Background");});
}

void PerformancePage::Recommended_Click(Windows::Foundation::IInspectable const& sender, RoutedEventArgs const&) {
    show_bulk_profile_tip(sender);
    apply_gaming_profile(true);
}

void PerformancePage::Defaults_Click(Windows::Foundation::IInspectable const& sender, RoutedEventArgs const&) {
    show_bulk_profile_tip(sender);
    apply_gaming_profile(false);
}

void PerformancePage::Search_TextChanged(Windows::Foundation::IInspectable const&,
    Controls::AutoSuggestBoxTextChangedEventArgs const&) {
    auto query = to_string(Search().Text());
    std::ranges::transform(query, query.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    for (auto const& child : Groups().Children()) {
        auto expander = child.try_as<Controls::Expander>();
        if (!expander) continue;
        const auto index = unbox_value_or<std::int32_t>(expander.Tag(), -1);
        const bool any_group = query.empty() || group_matches_query(index, query);
        if (!any_group) {
            expander.Visibility(Visibility::Collapsed);
            continue;
        }
        if (!query.empty()) ensure_group(expander);
        auto header = to_string(unbox_value_or<hstring>(expander.Header(), L""));
        std::ranges::transform(header, header.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        const bool category_match = !query.empty() && header.find(query) != std::string::npos;
        bool any = query.empty() || category_match;
        if (auto content = expander.Content().try_as<Controls::Panel>()) {
            for (auto const& row : content.Children()) {
                auto element = row.try_as<FrameworkElement>();
                auto text = element && element.Tag() ? to_string(unbox_value_or<hstring>(element.Tag(), L"")) : std::string{};
                std::ranges::transform(text, text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                const bool match = query.empty() || category_match || text.find(query) != std::string::npos;
                if (element) element.Visibility(match ? Visibility::Visible : Visibility::Collapsed);
                any = any || match;
            }
        }
        expander.Visibility(any ? Visibility::Visible : Visibility::Collapsed);
        if (!query.empty() && any) expander.IsExpanded(true);
        else if (query.empty() && index != 0) { expander.IsExpanded(false); detach_group(expander); }
    }
}

winchisel::core::Result<Value> PerformancePage::cached_value(Target const& destination) const {
    const auto found=registry_state_.find({destination.hive,destination.key_path,destination.value_name,destination.type});
    if(found==registry_state_.end())return std::unexpected(winchisel::core::Error{"State not loaded"});
    return found->second;
}

winchisel::core::Result<bool> PerformancePage::cached_task(std::string const& id) const {
    const auto found=task_state_.find(id);
    if(found==task_state_.end())return std::unexpected(winchisel::core::Error{"Task state not loaded"});
    return found->second;
}

winchisel::core::Result<bool> PerformancePage::cached_special(std::string const& id) const {
    const auto found=special_state_.find(id);
    if(found==special_state_.end())return std::unexpected(winchisel::core::Error{"State not loaded"});
    return found->second;
}

winchisel::core::Result<bool> PerformancePage::cached_available(std::string const& id) const {
    const auto found=special_available_.find(id);
    if(found==special_available_.end())return std::unexpected(winchisel::core::Error{"State not loaded"});
    return found->second;
}

void PerformancePage::submit(std::function<winchisel::core::Result<void>()> change) {
    pending_changes_.push_back(std::move(change));
    process_changes();
}

winrt::fire_and_forget PerformancePage::process_changes() {
    const auto weak=get_weak();
    winrt::Microsoft::UI::Dispatching::DispatcherQueue queue{nullptr};
    try { queue=DispatcherQueue(); } catch (...) {}
    winrt::apartment_context ui;
    if(work_running_)co_return;
    work_running_=true;
    try {
        auto lifetime=get_strong();
        IsEnabled(false);
        std::string failure;
        while(!pending_changes_.empty()) {
            auto change=std::move(pending_changes_.front());
            pending_changes_.pop_front();
            co_await winrt::resume_background();
            const auto result=winchisel::ui::result_or_error(change);
            co_await ui;
            if(!result) { failure=result.error().detail; pending_changes_.clear(); break; }
        }
        // Only plain registry targets and IDs cross the worker boundary.
        std::vector<Target> targets{
            target(Hive::local_machine,"SYSTEM\\CurrentControlSet\\Control\\PriorityControl","Win32PrioritySeparation",Type::dword),
            target(Hive::local_machine,"SYSTEM\\CurrentControlSet\\Control","SvcHostSplitThresholdInKB",Type::dword),
            target(Hive::current_user,"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\VisualEffects","VisualFXSetting",Type::dword),
            target(Hive::current_user,"Control Panel\\Mouse","MouseHoverTime",Type::string),
            target(Hive::current_user,"SOFTWARE\\Policies\\Microsoft\\Windows\\AppPrivacy","LetAppsRunInBackground",Type::dword),
            target(Hive::local_machine,"SOFTWARE\\Policies\\Microsoft\\Windows\\AppPrivacy","LetAppsRunInBackground",Type::dword),
            target(Hive::current_user,"SOFTWARE\\Policies\\Microsoft\\Windows\\DeliveryOptimization","DODownloadMode",Type::dword),
            target(Hive::local_machine,"SOFTWARE\\Policies\\Microsoft\\Windows\\DeliveryOptimization","DODownloadMode",Type::dword),
            target(Hive::current_user,"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced","TaskbarAl",Type::dword),
            target(Hive::current_user,"Software\\Microsoft\\Windows\\CurrentVersion\\Search","SearchBoxTaskbarMode",Type::dword),
            target(Hive::current_user,"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced","Start_Layout",Type::dword)};
        for(auto const& item:gaming_toggles_)targets.insert(targets.end(),item.targets.begin(),item.targets.end());
        for(auto const& rule:winchisel::core::get_performance_registry_rules())
            targets.push_back(target(rule.root==0?Hive::current_user:Hive::local_machine,rule.path.data(),rule.name.data(),rule.kind==0?Type::dword:rule.kind==1?Type::string:Type::binary));
        for (auto const& item : winchisel::core::get_performance_catalog()) {
            if (auto service = winchisel::core::service_name_for_id(item.id); !service.empty())
                targets.push_back(target(Hive::local_machine, ("SYSTEM\\CurrentControlSet\\Services\\" + std::string(service)).c_str(), "Start", Type::dword));
        }
        std::vector<std::string> task_ids;
        std::vector<std::string> special_ids;
        for (auto const& item : winchisel::core::get_performance_catalog()) {
            if (item.input != 0) continue;
            if (winchisel::platform::is_special_performance_toggle(item.id)) special_ids.emplace_back(item.id);
            else if (!std::ranges::any_of(winchisel::core::get_performance_registry_rules(), [&](auto const& rule) { return rule.id == item.id; }))
                task_ids.emplace_back(item.id);
        }
        co_await winrt::resume_background();
        std::map<RegistryKey,winchisel::core::Result<Value>> registry;
        for(auto const& destination:targets) {
            RegistryKey key{destination.hive,destination.key_path,destination.value_name,destination.type};
            if(!registry.contains(key))registry.emplace(std::move(key),winchisel::platform::read_registry_value(destination));
        }
        std::map<std::string,winchisel::core::Result<bool>> tasks;
        for(auto const& id:task_ids)tasks.emplace(id,winchisel::platform::read_scheduled_task(id));
        std::map<std::string,winchisel::core::Result<bool>> specials;
        for(auto const& id:special_ids)specials.emplace(id,winchisel::platform::read_special_performance_toggle(id));
        std::map<std::string,winchisel::core::Result<bool>> available;
        for(auto const& id:special_ids)available.emplace(id,winchisel::platform::is_special_available(id));
        auto dns=winchisel::platform::read_dns_profile();
        auto update_policy=winchisel::platform::read_update_policy();
        co_await ui;
        registry_state_=std::move(registry); task_state_=std::move(tasks); special_state_=std::move(specials); special_available_=std::move(available); dns_state_=std::move(dns); update_policy_state_=std::move(update_policy);
        load_gaming_toggles(); load_gaming_selections(); load_catalog_toggles(); load_catalog_selections();
        work_running_=false; IsEnabled(true);
        if(!failure.empty())show_write_error(failure);
    } catch(...) {
        winchisel::ui::report_async_error(queue,[weak](hstring const& text){if(auto self=weak.get()){
            self->pending_changes_.clear(); self->work_running_=false; self->IsEnabled(true);
            self->loading_gaming_toggles_=false; self->loading_gaming_selections_=false;
            self->show_write_error(to_string(text));
        }});
    }
}

}  // namespace winrt::Winchisel::implementation
