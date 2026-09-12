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

// Swipe-Aktion für eine Row: Text, Symbol, Hintergrund + Invoke-Callback.
Controls::SwipeItem swipe_item(hstring const& text, Controls::Symbol symbol,
                               Media::Brush const& background, std::function<void()> on_invoke) {
    Controls::SwipeItem item;
    item.Text(text);
    Controls::SymbolIconSource icon;
    icon.Symbol(symbol);
    item.IconSource(icon);
    item.Background(background);
    item.Invoked([on_invoke = std::move(on_invoke)](auto const&, auto const&) { on_invoke(); });
    return item;
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

void DebloaterPage::start_scan(bool clear_notice, bool force_refresh) {
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
    scan_worker_ = std::async(std::launch::async, [catalog = catalog_, force_refresh] {
        return winchisel::platform::scan_debloater_installed(catalog, force_refresh);
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
    } else if (operation_ == Operation::install || operation_ == Operation::update || operation_ == Operation::remove) {
        if (!action_worker_.valid() || action_worker_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
        const auto finished = operation_;
        auto result = action_worker_.get();
        operation_ = Operation::none;
        Loading().Visibility(Visibility::Collapsed);
        Items().IsEnabled(true);
        Notice().Title(finished == Operation::install ? L"Installation complete" : finished == Operation::update ? L"Update complete" : L"Removal complete");
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
        start_scan(false, true);
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
    all_rows_.clear();
    all_rows_.reserve(catalog_.size());
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
        // #2 SwipeControl: links Install/Update, rechts Remove — nutzt den
        // bestehenden Confirm-Flow (Row wird zur Einzel-Selektion).
        auto swipe = Controls::SwipeControl();
        swipe.HorizontalAlignment(HorizontalAlignment::Stretch);
        swipe.HorizontalContentAlignment(HorizontalAlignment::Stretch);
        swipe.Content(grid);
        auto swipe_weak = get_weak();
        auto isolate_and_confirm = [swipe_weak, row](bool install) {
            if (auto self = swipe_weak.get()) {
                std::uint32_t position{};
                if (!self->Items().Items().IndexOf(row, position)) return;
                self->Items().SelectedItems().Clear();
                self->Items().SelectedItems().Append(row);
                self->confirm_action(install);
            }
        };
        if (item.can_reinstall) {
            auto left = Controls::SwipeItems();
            left.Mode(Controls::SwipeMode::Reveal);
            const bool is_installed = installed_[index];
            left.Append(swipe_item(is_installed ? winchisel::ui::tr(L"Update") : winchisel::ui::tr(L"Install"),
                Controls::Symbol::Download, tint_brush(success, 0xFF),
                [isolate_and_confirm] { isolate_and_confirm(true); }));
            swipe.LeftItems(left);
        }
        if (installed_[index]) {
            auto right = Controls::SwipeItems();
            right.Mode(Controls::SwipeMode::Reveal);
            right.Append(swipe_item(winchisel::ui::tr(L"Remove"), Controls::Symbol::Delete,
                tint_brush(critical, 0xFF), [isolate_and_confirm] { isolate_and_confirm(false); }));
            swipe.RightItems(right);
        }
        row.Content(swipe);
        all_rows_.push_back(row);
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

void DebloaterPage::apply_filter(bool scroll_top) {
    const auto query = lower(to_string(Search().Text()));
    std::uint32_t tab{};
    if (Tabs().SelectedItem()) {
        Tabs().Items().IndexOf(Tabs().SelectedItem(), tab);
    }
    const auto filter = Filter().SelectedIndex();
    // #1: Selektion über den Rebuild retten (nur noch sichtbare Items).
    std::vector<std::uint64_t> selected;
    for (auto const& value : Items().SelectedItems()) {
        if (auto row = value.try_as<Controls::ListViewItem>()) selected.push_back(unbox_value<std::uint64_t>(row.Tag()));
    }
    Items().Items().Clear();
    visible_indices_.clear();
    for (auto const& row : all_rows_) {
        const auto raw = unbox_value<std::uint64_t>(row.Tag());
        if (raw >= catalog_.size() || raw >= search_index_.size()) continue;
        if (!matches_filter(static_cast<std::size_t>(raw), query, tab, filter)) continue;
        Items().Items().Append(row);
        visible_indices_.push_back(static_cast<std::size_t>(raw));
    }
    for (auto const& value : Items().Items()) {
        if (auto row = value.try_as<Controls::ListViewItem>()) {
            const auto raw = unbox_value<std::uint64_t>(row.Tag());
            if (std::ranges::find(selected, raw) != selected.end()) Items().SelectedItems().Append(value);
        }
    }
    if (visible_indices_.empty()) {
        Notice().Title(winchisel::ui::tr(L"No matching items")); Notice().Message(winchisel::ui::tr(L"Change the search, category, or installed-state filter.")); Notice().Severity(Controls::InfoBarSeverity::Informational); Notice().IsOpen(true);
    }
    update_actions();
    // #1: gezielter Sprung statt Full-Rerender-Flackern — nur bei explizitem
    // Tab-/Filterwechsel, nicht beim Tippen in der Suche.
    if (scroll_top && Items().Items().Size() > 0) Items().ScrollIntoView(Items().Items().GetAt(0));
}

DebloaterPage::ApplySelection DebloaterPage::selected_split() {
    ApplySelection selection;
    for (auto const& value : Items().SelectedItems()) {
        if (auto row = value.try_as<Controls::ListViewItem>()) {
            if (row.Visibility() != Visibility::Visible) continue;
            const auto index = unbox_value<std::uint64_t>(row.Tag());
            if (index >= catalog_.size()) continue;
            auto const* item = &catalog_[static_cast<std::size_t>(index)];
            const bool is_installed = index < installed_.size() && installed_[static_cast<std::size_t>(index)];
            if (is_installed) {
                selection.remove.push_back(item);
                if (item->can_reinstall) selection.update.push_back(item);
            } else if (item->can_reinstall) {
                selection.install.push_back(item);
            }
        }
    }
    return selection;
}

void DebloaterPage::update_actions() {
    const auto selection = selected_split();
    const auto installable = static_cast<std::uint32_t>(selection.install.size());
    const auto updatable = static_cast<std::uint32_t>(selection.update.size());
    const auto removable = static_cast<std::uint32_t>(selection.remove.size());
    const bool idle = operation_ == Operation::none;
    const std::uint32_t actionable = installable + updatable;
    InstallButton().IsEnabled(idle && actionable > 0);
    RemoveButton().IsEnabled(idle && removable > 0);
    if (installable > 0) InstallButton().Content(box_value(winrt::hstring{std::wstring(winchisel::ui::tr(L"Install")) + L" (" + std::to_wstring(actionable) + L")"}));
    else if (updatable > 0) InstallButton().Content(box_value(winrt::hstring{std::wstring(winchisel::ui::tr(L"Update")) + L" (" + std::to_wstring(updatable) + L")"}));
    else InstallButton().Content(box_value(winchisel::ui::tr(L"Install")));
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
    const auto selection = selected_split();
    const auto install_count = selection.install.size();
    const auto update_count = selection.update.size();
    const auto remove_count = selection.remove.size();
    const auto apply_count = install_count + update_count;
    if (install && !apply_count) co_return;
    if (!install && !remove_count) co_return;
    Controls::ContentDialog dialog;
    dialog.XamlRoot(XamlRoot());
    if (install) {
        const bool update_only = install_count == 0;
        const bool mixed = install_count > 0 && update_count > 0;
        dialog.Title(box_value(update_only ? L"Update selected items?" : mixed ? L"Install or update selected items?" : L"Install selected items?"));
        dialog.Content(box_value(to_hstring(std::to_string(apply_count) + (update_only ? " selected items will be updated. Capabilities are reinstalled from the latest payload; Store listings may open for Windows apps." : mixed ? " selected items will be installed or updated. Store listings may open for Windows apps." : " selected items will be installed for the current user. Store listings may open when no local payload remains."))));
        dialog.PrimaryButtonText(update_only ? L"Update" : mixed ? L"Apply" : L"Install");
    } else {
        dialog.Title(box_value(L"Remove selected items?"));
        dialog.Content(box_value(to_hstring(std::to_string(remove_count) + " selected AppX packages will be removed for the current user.")));
        dialog.PrimaryButtonText(L"Remove");
    }
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

    auto selection = selected_split();
    if (install) {
        if (selection.install.empty() && selection.update.empty()) return;
        operation_ = selection.install.empty() ? Operation::update : Operation::install;
        Loading().Visibility(Visibility::Visible); Items().IsEnabled(false); InstallButton().IsEnabled(false); RemoveButton().IsEnabled(false); Notice().IsOpen(false);
        action_worker_ = std::async(std::launch::async, [install_items = std::move(selection.install), update_items = std::move(selection.update)] {
            winchisel::platform::DebloatActionResult merged;
            if (!install_items.empty()) {
                auto installed = winchisel::platform::apply_debloater_action(install_items, true);
                if (!installed) return winchisel::core::Result<winchisel::platform::DebloatActionResult>{std::unexpected(installed.error())};
                merged.succeeded += installed->succeeded;
                merged.failed += installed->failed;
                merged.reboot_required = merged.reboot_required || installed->reboot_required;
                merged.failure_details.insert(merged.failure_details.end(), installed->failure_details.begin(), installed->failure_details.end());
            }
            if (!update_items.empty()) {
                auto updated = winchisel::platform::apply_debloater_update(update_items);
                if (!updated) return winchisel::core::Result<winchisel::platform::DebloatActionResult>{std::unexpected(updated.error())};
                merged.succeeded += updated->succeeded;
                merged.failed += updated->failed;
                merged.reboot_required = merged.reboot_required || updated->reboot_required;
                merged.failure_details.insert(merged.failure_details.end(), updated->failure_details.begin(), updated->failure_details.end());
            }
            return winchisel::core::Result<winchisel::platform::DebloatActionResult>{merged};
        });
        timer_.Start();
    } else {
        if (selection.remove.empty()) return;
        operation_ = Operation::remove;
        Loading().Visibility(Visibility::Visible); Items().IsEnabled(false); InstallButton().IsEnabled(false); RemoveButton().IsEnabled(false); Notice().IsOpen(false);
        action_worker_ = std::async(std::launch::async, [selected = std::move(selection.remove)] {
            return winchisel::platform::apply_debloater_action(selected, false);
        });
        timer_.Start();
    }

    } catch (...) {
        winchisel::ui::report_async_error(error_queue, [error_weak](winrt::hstring const& text) {
            if (auto self=error_weak.get()) { self->operation_=Operation::none; self->timer_.Stop(); self->Loading().Visibility(Visibility::Collapsed); self->Items().IsEnabled(true); self->update_actions(); self->Notice().Title(L"Operation failed"); self->Notice().Message(text); self->Notice().Severity(Controls::InfoBarSeverity::Error); self->Notice().IsOpen(true); }
        });
    }
}

    void DebloaterPage::Tabs_SelectionChanged(IInspectable const&, Controls::SelectorBarSelectionChangedEventArgs const&) { if (ui_ready_ && operation_ == Operation::none) apply_filter(true); }
void DebloaterPage::Refresh_Click(IInspectable const&, RoutedEventArgs const&) { if (ui_ready_) start_scan(true, true); }
void DebloaterPage::Install_Click(IInspectable const&, RoutedEventArgs const&) { if (ui_ready_) confirm_action(true); }
void DebloaterPage::Remove_Click(IInspectable const&, RoutedEventArgs const&) { if (ui_ready_) confirm_action(false); }
void DebloaterPage::Search_TextChanged(IInspectable const&, Controls::AutoSuggestBoxTextChangedEventArgs const&) { if (ui_ready_ && operation_ == Operation::none) { search_timer_.Stop(); search_timer_.Start(); } }
void DebloaterPage::Filter_SelectionChanged(IInspectable const&, Controls::SelectionChangedEventArgs const&) { if (ui_ready_ && operation_ == Operation::none) apply_filter(true); }
void DebloaterPage::Items_SelectionChanged(IInspectable const&, Controls::SelectionChangedEventArgs const&) { if (ui_ready_) update_actions(); }

}  // namespace winrt::Winchisel::implementation
