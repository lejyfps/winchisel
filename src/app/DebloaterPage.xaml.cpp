#include "pch.h"
#include "DebloaterPage.xaml.h"
#include "AsyncLifetime.hpp"

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

}  // namespace

DebloaterPage::DebloaterPage() {
    InitializeComponent();
    catalog_ = winchisel::core::get_debloat_catalog();
    installed_.assign(catalog_.size(), false);
    timer_ = DispatcherTimer();
    timer_.Interval(std::chrono::milliseconds(120));
    timer_token_ = timer_.Tick([this](auto&&, auto&&) { poll_worker(); });
    search_timer_ = DispatcherTimer();
    search_timer_.Interval(std::chrono::milliseconds(200));
    search_timer_token_ = search_timer_.Tick([this](auto&&, auto&&) { search_timer_.Stop(); if (operation_ == Operation::none) render_items(); });
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
}

void DebloaterPage::poll_worker() {
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
            Notice().Message(to_hstring(std::to_string(result->succeeded) + " succeeded, " + std::to_string(result->failed) + " failed."));
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
}

void DebloaterPage::render_items() {
    Items().Items().Clear();
    visible_indices_.clear();
    const auto query = lower(to_string(Search().Text()));
    std::uint32_t selected_tab{};
    if (Tabs().SelectedItem()) {
        Tabs().Items().IndexOf(Tabs().SelectedItem(), selected_tab);
    }
    const auto filter = Filter().SelectedIndex();
    auto resources = Application::Current().Resources();
    auto success = resources.Lookup(box_value(L"SystemFillColorSuccessBrush")).try_as<Media::Brush>();
    auto secondary = resources.Lookup(box_value(L"TextFillColorSecondaryBrush")).try_as<Media::Brush>();
    auto critical = resources.Lookup(box_value(L"SystemFillColorCriticalBrush")).try_as<Media::Brush>();

    for (std::size_t index{}; index < catalog_.size(); ++index) {
        auto const& item = catalog_[index];
        const int category = item.category == winchisel::core::DebloatCategory::windows_apps ? 0 : item.category == winchisel::core::DebloatCategory::capabilities ? 1 : 2;
        const auto searchable = lower(std::string(item.name) + " " + std::string(item.description_key) + " " + std::string(item.group) + " " + std::string(item.package_name) + " " + std::string(item.package_aliases));
        if (query.empty() && static_cast<std::uint32_t>(category) != selected_tab) continue;
        if (!query.empty() && searchable.find(query) == std::string::npos) continue;
        if (filter == 1 && !installed_[index]) continue;
        if (filter == 2 && installed_[index]) continue;

        auto row = Controls::ListViewItem();
        row.Tag(box_value(static_cast<std::uint64_t>(index)));
        Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(row,to_hstring(std::string(item.name)+(installed_[index]?", installed":", not installed")));
        row.HorizontalContentAlignment(HorizontalAlignment::Stretch);
        row.Background(resources.Lookup(box_value(L"CardBackgroundFillColorDefaultBrush")).try_as<Media::Brush>());
        row.BorderBrush(resources.Lookup(box_value(L"CardStrokeColorDefaultBrush")).try_as<Media::Brush>());
        row.BorderThickness({1, 1, 1, 1});
        row.CornerRadius({4, 4, 4, 4});
        row.Padding({12, 10, 16, 10});
        row.Margin({0, 0, 0, 8});
        auto grid = Controls::Grid();
        grid.ColumnDefinitions().Append(Controls::ColumnDefinition());
        auto trailing = Controls::ColumnDefinition(); trailing.Width(GridLength{0, GridUnitType::Auto}); grid.ColumnDefinitions().Append(trailing);
        auto text = Controls::StackPanel();
        auto title = Controls::TextBlock(); title.Text(to_hstring(item.name)); title.Style(resources.Lookup(box_value(L"BodyStrongTextBlockStyle")).try_as<Microsoft::UI::Xaml::Style>()); text.Children().Append(title);
        auto detail_text = std::string(item.group) + " | " + std::string(item.package_name);
        if (!item.can_reinstall) detail_text += " | Cannot be reinstalled automatically";
        auto detail = Controls::TextBlock(); detail.Text(to_hstring(detail_text)); detail.TextWrapping(TextWrapping::Wrap); detail.Foreground(item.can_reinstall ? secondary : critical); detail.Style(resources.Lookup(box_value(L"CaptionTextBlockStyle")).try_as<Microsoft::UI::Xaml::Style>()); text.Children().Append(detail);
        grid.Children().Append(text);
        auto status = Controls::TextBlock(); status.Text(installed_[index] ? L"Installed" : L"Not installed"); status.Foreground(installed_[index] ? success : secondary); status.VerticalAlignment(VerticalAlignment::Center);
        Controls::Grid::SetColumn(status, 1); grid.Children().Append(status);
        row.Content(grid);
        Items().Items().Append(row);
        visible_indices_.push_back(index);
    }
    if (visible_indices_.empty()) {
        Notice().Title(L"No matching items"); Notice().Message(L"Change the search, category, or installed-state filter."); Notice().Severity(Controls::InfoBarSeverity::Informational); Notice().IsOpen(true);
    }
    update_actions();
}

void DebloaterPage::update_actions() {
    const auto count = Items().SelectedItems().Size();
    const bool enabled = count > 0 && operation_ == Operation::none;
    InstallButton().IsEnabled(enabled);
    RemoveButton().IsEnabled(enabled);
    InstallButton().Content(box_value(count ? L"Install (" + to_hstring(count) + L")" : L"Install"));
    RemoveButton().Content(box_value(count ? L"Remove (" + to_hstring(count) + L")" : L"Remove"));
}

fire_and_forget DebloaterPage::confirm_action(bool install) {
    auto lifetime = get_strong();
    const auto count = Items().SelectedItems().Size();
    if (!count) co_return;
    Controls::ContentDialog dialog;
    dialog.XamlRoot(XamlRoot());
    dialog.Title(box_value(install ? L"Install selected items?" : L"Remove selected items?"));
    dialog.Content(box_value(to_hstring(std::to_string(count) + (install ? " selected items will be installed." : " selected items will be removed."))));
    dialog.PrimaryButtonText(install ? L"Install" : L"Remove");
    dialog.CloseButtonText(L"Cancel");
    dialog.DefaultButton(Controls::ContentDialogButton::Close);
    if (co_await dialog.ShowAsync() == Controls::ContentDialogResult::Primary) start_action(install);
}

void DebloaterPage::start_action(bool install) {
    std::vector<winchisel::core::DebloatCatalogEntry const*> selected;
    for (auto const& value : Items().SelectedItems()) {
        if (auto row = value.try_as<Controls::ListViewItem>()) {
            const auto index = unbox_value<std::uint64_t>(row.Tag());
            if (index < catalog_.size()) selected.push_back(&catalog_[static_cast<std::size_t>(index)]);
        }
    }
    if (selected.empty()) return;
    operation_ = install ? Operation::install : Operation::remove;
    Loading().Visibility(Visibility::Visible); Items().IsEnabled(false); InstallButton().IsEnabled(false); RemoveButton().IsEnabled(false); Notice().IsOpen(false);
    action_worker_ = std::async(std::launch::async, [selected = std::move(selected), install] {
        return winchisel::platform::apply_debloater_action(selected, install);
    });
    timer_.Start();
}

void DebloaterPage::Tabs_SelectionChanged(IInspectable const&, Controls::SelectorBarSelectionChangedEventArgs const&) { if (ui_ready_ && operation_ == Operation::none) render_items(); }
void DebloaterPage::Refresh_Click(IInspectable const&, RoutedEventArgs const&) { if (ui_ready_) start_scan(); }
void DebloaterPage::Install_Click(IInspectable const&, RoutedEventArgs const&) { if (ui_ready_) confirm_action(true); }
void DebloaterPage::Remove_Click(IInspectable const&, RoutedEventArgs const&) { if (ui_ready_) confirm_action(false); }
void DebloaterPage::Search_TextChanged(IInspectable const&, Controls::AutoSuggestBoxTextChangedEventArgs const&) { if (ui_ready_ && operation_ == Operation::none) { search_timer_.Stop(); search_timer_.Start(); } }
void DebloaterPage::Filter_SelectionChanged(IInspectable const&, Controls::SelectionChangedEventArgs const&) { if (ui_ready_ && operation_ == Operation::none) render_items(); }
void DebloaterPage::Items_SelectionChanged(IInspectable const&, Controls::SelectionChangedEventArgs const&) { if (ui_ready_) update_actions(); }

}  // namespace winrt::Winchisel::implementation
