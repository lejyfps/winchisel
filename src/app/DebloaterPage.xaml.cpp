#include "pch.h"
#include "AsyncSupport.hpp"
#include "DebloaterPage.xaml.h"
#include "AsyncLifetime.hpp"
#include "Localization.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>

#if __has_include("DebloaterPage.g.cpp")
#include "DebloaterPage.g.cpp"
#endif

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace winrt::Winchisel::implementation {
namespace {

std::string lower(std::string value) {
    std::ranges::transform(value, value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

hstring category_name(winchisel::core::DebloatCategory category) {
    switch (category) {
    case winchisel::core::DebloatCategory::windows_apps: return L"Windows App";
    case winchisel::core::DebloatCategory::capabilities: return L"Capability";
    default: return L"Optional Feature";
    }
}

// Subtle tinted brush derived from a theme brush (e.g. success green at ~18%
// opacity) for pill badges. Text/icon keep the full-strength theme brush.
Media::SolidColorBrush tint_brush(Media::Brush const& base, std::uint8_t alpha) {
    auto color = Windows::UI::Colors::Transparent();
    if (auto solid = base.try_as<Media::SolidColorBrush>()) color = solid.Color();
    color.A = alpha;
    return Media::SolidColorBrush(color);
}

// Pill badge with a Fluent icon and bold label.
Controls::Border make_status_badge(Media::Brush const& tint, Media::Brush const& foreground,
                                   wchar_t const* glyph, hstring const& text) {
    Controls::Border badge;
    badge.CornerRadius({12, 12, 12, 12});
    badge.Padding({10, 3, 10, 3});
    badge.VerticalAlignment(VerticalAlignment::Center);
    badge.Background(tint);
    Controls::StackPanel row;
    row.Orientation(Controls::Orientation::Horizontal);
    row.Spacing(6);
    row.VerticalAlignment(VerticalAlignment::Center);
    Controls::FontIcon icon;
    icon.Glyph(hstring{glyph});
    icon.FontSize(12);
    icon.Foreground(foreground);
    icon.VerticalAlignment(VerticalAlignment::Center);
    row.Children().Append(icon);
    Controls::TextBlock label;
    label.Text(text);
    label.Foreground(foreground);
    label.FontSize(12);
    label.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
    label.VerticalAlignment(VerticalAlignment::Center);
    row.Children().Append(label);
    badge.Child(row);
    return badge;
}

}  // namespace

DebloaterPage::DebloaterPage() {
    InitializeComponent();
    catalog_ = winchisel::core::get_debloat_catalog();
    installed_.assign(catalog_.size(), false);
    timer_ = DispatcherTimer();
    timer_.Interval(std::chrono::milliseconds(120));
    auto weak = get_weak();
    timer_token_ = timer_.Tick([weak](auto&&, auto&&) { if (auto self = weak.get()) self->poll_worker(); });
    search_timer_ = DispatcherTimer();
    search_timer_.Interval(std::chrono::milliseconds(200));
    search_timer_token_ = search_timer_.Tick([weak](auto&&, auto&&) { if (auto self = weak.get()) { self->search_timer_.Stop(); if (self->operation_ == Operation::none) self->apply_filter(); } });
    Loaded([weak](auto&&, auto&&) { if (auto self = weak.get()) { if (self->operation_ != Operation::none) self->timer_.Start(); } });
    Unloaded([weak](auto&&, auto&&) { if (auto self = weak.get()) self->timer_.Stop(); });
    ui_ready_ = true;
    start_scan();
}

DebloaterPage::~DebloaterPage() {
    timer_.Stop();
    timer_.Tick(timer_token_);
    search_timer_.Stop();
    search_timer_.Tick(search_timer_token_);
    winchisel::ui::finish_in_background(scan_worker_);
    winchisel::ui::finish_in_background(action_worker_);
}

void DebloaterPage::start_scan(bool clear_notice) {
    auto error_weak=get_weak();
    winrt::Microsoft::UI::Dispatching::DispatcherQueue error_queue{nullptr};
    try { error_queue=DispatcherQueue(); } catch (...) {}
    try {
        auto error_lifetime=get_strong();

    if (operation_ != Operation::none) return;
    operation_ = Operation::scan;
    Loading().Visibility(Visibility::Visible);
    Items().IsEnabled(false);
    InstallButton().IsEnabled(false);
    RemoveButton().IsEnabled(false);
    if (clear_notice) Notice().IsOpen(false);
    scan_worker_ = std::async(std::launch::async, [catalog = catalog_] {
        return winchisel::platform::scan_debloater_installed(catalog);
    });
    timer_.Start();

    } catch (...) {
        winchisel::ui::report_async_error(error_queue, [error_weak](winrt::hstring const& text) {
            if (auto self=error_weak.get()) { self->operation_=Operation::none; self->timer_.Stop(); self->Loading().Visibility(Visibility::Collapsed); self->Items().IsEnabled(true); self->update_actions(); self->Notice().Title(L"Operation failed"); self->Notice().Message(text); self->Notice().Severity(Controls::InfoBarSeverity::Error); self->Notice().IsOpen(true); }
        });
    }
}

void DebloaterPage::poll_worker() {
    auto error_weak=get_weak();
    winrt::Microsoft::UI::Dispatching::DispatcherQueue error_queue{nullptr};
    try { error_queue=DispatcherQueue(); } catch (...) {}
    try {
        auto error_lifetime=get_strong();

    if (operation_ == Operation::scan) {
        if (!scan_worker_.valid() || scan_worker_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
        auto result = scan_worker_.get();
        Loading().Visibility(Visibility::Collapsed);
        Items().IsEnabled(true);
        operation_ = Operation::none;
        if (result) {
            installed_ = std::move(*result);
            render_items();
        } else {
            Notice().Title(L"Scan failed");
            Notice().Message(to_hstring(result.error().detail));
            Notice().Severity(Controls::InfoBarSeverity::Error);
            Notice().IsOpen(true);
        }
    } else if (operation_ == Operation::install || operation_ == Operation::remove) {
        if (!action_worker_.valid() || action_worker_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
        const bool installed_action = operation_ == Operation::install;
        auto result = action_worker_.get();
        operation_ = Operation::none;
        Loading().Visibility(Visibility::Collapsed);
        Items().IsEnabled(true);
        Notice().Title(installed_action ? L"Installation complete" : L"Removal complete");
        if (result) {
            auto message = std::to_string(result->succeeded) + " succeeded, " + std::to_string(result->failed) + " failed.";
            if (result->reboot_required) message += " A restart is required.";
            if (!result->failure_details.empty()) {
                message += " ";
                for (std::size_t i{}; i < result->failure_details.size(); ++i) {
                    if (i) message += " | ";
                    message += result->failure_details[i];
                }
            }
            Notice().Message(to_hstring(message));
            Notice().Severity(result->failed == 0 ? Controls::InfoBarSeverity::Success : Controls::InfoBarSeverity::Warning);
        } else {
            Notice().Message(to_hstring(result.error().detail));
            Notice().Severity(Controls::InfoBarSeverity::Error);
        }
        Notice().IsOpen(true);
        Items().SelectedItems().Clear();
        update_actions();
        start_scan(false);
    } else {
        timer_.Stop();
    }

    } catch (...) {
        winchisel::ui::report_async_error(error_queue, [error_weak](winrt::hstring const& text) {
            if (auto self=error_weak.get()) { self->operation_=Operation::none; self->timer_.Stop(); self->Loading().Visibility(Visibility::Collapsed); self->Items().IsEnabled(true); self->update_actions(); self->Notice().Title(L"Operation failed"); self->Notice().Message(text); self->Notice().Severity(Controls::InfoBarSeverity::Error); self->Notice().IsOpen(true); }
        });
    }
}

void DebloaterPage::render_items() {
    Items().Items().Clear();
    search_index_.clear();
    search_index_.reserve(catalog_.size());
    for (auto const& item : catalog_) {
        search_index_.push_back(lower(std::string(item.name) + " " + std::string(item.description_key) + " " + std::string(item.group) + " " + std::string(item.package_name) + " " + std::string(item.package_aliases)));
    }
    auto resources = Application::Current().Resources();
    auto success = resources.Lookup(box_value(L"SystemFillColorSuccessBrush")).try_as<Media::Brush>();
    auto secondary = resources.Lookup(box_value(L"TextFillColorSecondaryBrush")).try_as<Media::Brush>();
    auto critical = resources.Lookup(box_value(L"SystemFillColorCriticalBrush")).try_as<Media::Brush>();
    auto card_background = resources.Lookup(box_value(L"CardBackgroundFillColorDefaultBrush")).try_as<Media::Brush>();
    auto card_stroke = resources.Lookup(box_value(L"CardStrokeColorDefaultBrush")).try_as<Media::Brush>();
    auto body_strong = resources.Lookup(box_value(L"BodyStrongTextBlockStyle")).try_as<Microsoft::UI::Xaml::Style>();
    auto caption = resources.Lookup(box_value(L"CaptionTextBlockStyle")).try_as<Microsoft::UI::Xaml::Style>();

    for (std::size_t index{}; index < catalog_.size(); ++index) {
        auto const& item = catalog_[index];
        auto row = Controls::ListViewItem();
        row.Tag(box_value(static_cast<std::uint64_t>(index)));
        Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(row,winrt::hstring{std::wstring(to_hstring(std::string(item.name)))+std::wstring(installed_[index]?winchisel::ui::tr(L", installed"):winchisel::ui::tr(L", not installed"))});
        row.HorizontalContentAlignment(HorizontalAlignment::Stretch);
        row.Background(card_background);
        row.BorderBrush(card_stroke);
        row.BorderThickness({1, 1, 1, 1});
        row.CornerRadius({4, 4, 4, 4});
        row.Padding({12, 10, 16, 10});
        row.Margin({0, 0, 0, 8});
        auto grid = Controls::Grid();
        grid.ColumnDefinitions().Append(Controls::ColumnDefinition());
        auto trailing = Controls::ColumnDefinition(); trailing.Width(GridLength{0, GridUnitType::Auto}); grid.ColumnDefinitions().Append(trailing);
        auto text = Controls::StackPanel();
        auto title = Controls::TextBlock(); title.Text(to_hstring(item.name)); title.Style(body_strong); text.Children().Append(title);
        auto detail_text = std::string(item.group) + " | " + std::string(item.package_name);
        if (!item.can_reinstall) detail_text += " | " + winrt::to_string(winchisel::ui::tr(L"Cannot be reinstalled automatically"));
        auto detail = Controls::TextBlock(); detail.Text(to_hstring(detail_text)); detail.TextWrapping(TextWrapping::Wrap); detail.Foreground(item.can_reinstall ? secondary : critical); detail.Style(caption); text.Children().Append(detail);
        grid.Children().Append(text);
        auto badge = make_status_badge(
            tint_brush(installed_[index] ? success : critical, 0x2E),
            installed_[index] ? success : critical,
            installed_[index] ? L"\uE73E" : L"\uE896",
            installed_[index] ? winchisel::ui::tr(L"Installed") : winchisel::ui::tr(L"Not installed"));
        Controls::Grid::SetColumn(badge, 1); grid.Children().Append(badge);
        row.Content(grid);
        Items().Items().Append(row);
    }
    apply_filter();
}

bool DebloaterPage::matches_filter(std::size_t index, std::string const& query, std::uint32_t tab, int filter) const {
    auto const& item = catalog_[index];
    const int category = item.category == winchisel::core::DebloatCategory::windows_apps ? 0 : item.category == winchisel::core::DebloatCategory::capabilities ? 1 : 2;
    if (query.empty() && static_cast<std::uint32_t>(category) != tab) return false;
    if (!query.empty() && search_index_[index].find(query) == std::string::npos) return false;
    if (filter == 1 && !installed_[index]) return false;
    if (filter == 2 && installed_[index]) return false;
    return true;
}

void DebloaterPage::apply_filter() {
    const auto query = lower(to_string(Search().Text()));
    std::uint32_t tab{};
    if (Tabs().SelectedItem()) {
        Tabs().Items().IndexOf(Tabs().SelectedItem(), tab);
    }
    const auto filter = Filter().SelectedIndex();
    visible_indices_.clear();
    std::vector<Windows::Foundation::IInspectable> deselect;
    for (auto const& value : Items().Items()) {
        auto row = value.try_as<Controls::ListViewItem>();
        if (!row) continue;
        const auto raw = unbox_value<std::uint64_t>(row.Tag());
        const bool show = raw < catalog_.size() && raw < search_index_.size() && matches_filter(static_cast<std::size_t>(raw), query, tab, filter);
        row.Visibility(show ? Visibility::Visible : Visibility::Collapsed);
        if (show) {
            visible_indices_.push_back(static_cast<std::size_t>(raw));
        } else if (row.IsSelected()) {
            deselect.push_back(value);
        }
    }
    for (auto const& value : deselect) {
        std::uint32_t position{};
        if (Items().SelectedItems().IndexOf(value, position)) Items().SelectedItems().RemoveAt(position);
    }
    if (visible_indices_.empty()) {
        Notice().Title(winchisel::ui::tr(L"No matching items")); Notice().Message(winchisel::ui::tr(L"Change the search, category, or installed-state filter.")); Notice().Severity(Controls::InfoBarSeverity::Informational); Notice().IsOpen(true);
    }
    update_actions();
}

void DebloaterPage::update_actions() {
    std::uint32_t installable{};
    std::uint32_t removable{};
    for (auto const& value : Items().SelectedItems()) {
        if (auto row = value.try_as<Controls::ListViewItem>()) {
            if (row.Visibility() != Visibility::Visible) continue;
            const auto index = unbox_value<std::uint64_t>(row.Tag());
            if (index >= catalog_.size()) continue;
            if (catalog_[static_cast<std::size_t>(index)].can_reinstall) ++installable;
            ++removable;
        }
    }
    const bool idle = operation_ == Operation::none;
    InstallButton().IsEnabled(idle && installable > 0);
    RemoveButton().IsEnabled(idle && removable > 0);
    InstallButton().Content(box_value(installable ? winrt::hstring{std::wstring(winchisel::ui::tr(L"Install")) + L" (" + std::to_wstring(installable) + L")"} : winchisel::ui::tr(L"Install")));
    RemoveButton().Content(box_value(removable ? winrt::hstring{std::wstring(winchisel::ui::tr(L"Remove")) + L" (" + std::to_wstring(removable) + L")"} : winchisel::ui::tr(L"Remove")));
}

fire_and_forget DebloaterPage::confirm_action(bool install) {
    auto error_weak=get_weak();
    winrt::Microsoft::UI::Dispatching::DispatcherQueue error_queue{nullptr};
    try { error_queue=DispatcherQueue(); } catch (...) {}
    try {
        auto error_lifetime=get_strong();
    winchisel::core::DialogSlot dialog_slot; if(!winchisel::ui::dialog_available(dialog_slot))co_return;


    auto lifetime = get_strong();
    const auto count = Items().SelectedItems().Size();
    if (!count) co_return;
    Controls::ContentDialog dialog;
    dialog.XamlRoot(XamlRoot());
    dialog.Title(box_value(install ? L"Install selected items?" : L"Remove selected items?"));
    dialog.Content(box_value(to_hstring(std::to_string(count) + (install ? " selected items will be installed for the current user. Store listings may open when no local payload remains." : " selected AppX packages will be removed for the current user."))));
    dialog.PrimaryButtonText(install ? L"Install" : L"Remove");
    dialog.CloseButtonText(L"Cancel");
    dialog.DefaultButton(Controls::ContentDialogButton::Close);
    if (co_await dialog.ShowAsync() == Controls::ContentDialogResult::Primary) start_action(install);

    } catch (...) {
        winchisel::ui::report_async_error(error_queue, [error_weak](winrt::hstring const& text) {
            if (auto self=error_weak.get()) { self->operation_=Operation::none; self->timer_.Stop(); self->Loading().Visibility(Visibility::Collapsed); self->Items().IsEnabled(true); self->update_actions(); self->Notice().Title(L"Operation failed"); self->Notice().Message(text); self->Notice().Severity(Controls::InfoBarSeverity::Error); self->Notice().IsOpen(true); }
        });
    }
}

void DebloaterPage::start_action(bool install) {
    auto error_weak=get_weak();
    winrt::Microsoft::UI::Dispatching::DispatcherQueue error_queue{nullptr};
    try { error_queue=DispatcherQueue(); } catch (...) {}
    try {
        auto error_lifetime=get_strong();

    std::vector<winchisel::core::DebloatCatalogEntry const*> selected;
    for (auto const& value : Items().SelectedItems()) {
        if (auto row = value.try_as<Controls::ListViewItem>()) {
            if (row.Visibility() != Visibility::Visible) continue;
            const auto index = unbox_value<std::uint64_t>(row.Tag());
            if (index < catalog_.size() && (!install || catalog_[static_cast<std::size_t>(index)].can_reinstall)) selected.push_back(&catalog_[static_cast<std::size_t>(index)]);
        }
    }
    if (selected.empty()) return;
    operation_ = install ? Operation::install : Operation::remove;
    Loading().Visibility(Visibility::Visible); Items().IsEnabled(false); InstallButton().IsEnabled(false); RemoveButton().IsEnabled(false); Notice().IsOpen(false);
    action_worker_ = std::async(std::launch::async, [selected = std::move(selected), install] {
        return winchisel::platform::apply_debloater_action(selected, install);
    });
    timer_.Start();

    } catch (...) {
        winchisel::ui::report_async_error(error_queue, [error_weak](winrt::hstring const& text) {
            if (auto self=error_weak.get()) { self->operation_=Operation::none; self->timer_.Stop(); self->Loading().Visibility(Visibility::Collapsed); self->Items().IsEnabled(true); self->update_actions(); self->Notice().Title(L"Operation failed"); self->Notice().Message(text); self->Notice().Severity(Controls::InfoBarSeverity::Error); self->Notice().IsOpen(true); }
        });
    }
}

void DebloaterPage::Tabs_SelectionChanged(IInspectable const&, Controls::SelectorBarSelectionChangedEventArgs const&) { if (ui_ready_ && operation_ == Operation::none) apply_filter(); }
void DebloaterPage::Refresh_Click(IInspectable const&, RoutedEventArgs const&) { if (ui_ready_) start_scan(); }
void DebloaterPage::Install_Click(IInspectable const&, RoutedEventArgs const&) { if (ui_ready_) confirm_action(true); }
void DebloaterPage::Remove_Click(IInspectable const&, RoutedEventArgs const&) { if (ui_ready_) confirm_action(false); }
void DebloaterPage::Search_TextChanged(IInspectable const&, Controls::AutoSuggestBoxTextChangedEventArgs const&) { if (ui_ready_ && operation_ == Operation::none) { search_timer_.Stop(); search_timer_.Start(); } }
void DebloaterPage::Filter_SelectionChanged(IInspectable const&, Controls::SelectionChangedEventArgs const&) { if (ui_ready_ && operation_ == Operation::none) apply_filter(); }
void DebloaterPage::Items_SelectionChanged(IInspectable const&, Controls::SelectionChangedEventArgs const&) { if (ui_ready_) update_actions(); }

}  // namespace winrt::Winchisel::implementation
