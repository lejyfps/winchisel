#include "pch.h"
#include "AsyncSupport.hpp"
#include "Localization.hpp"
#include "ScheduledTasksPage.xaml.h"
#include "winchisel/platform/startup.hpp"
#include "winchisel/platform/system.hpp"

#if __has_include("ScheduledTasksPage.g.cpp")
#include "ScheduledTasksPage.g.cpp"
#endif

#include <algorithm>
#include <cctype>

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace winrt::Winchisel::implementation {
void ScheduledTasksPage::show_write_error(std::string const& detail){auto message=detail.empty()?"A Windows setting could not be changed. See %APPDATA%\\Winchisel\\logs\\winchisel.log for details.":detail;ResultBar().Title(L"Could not apply setting");ResultBar().Message(to_hstring(message));ResultBar().Severity(Controls::InfoBarSeverity::Error);ResultBar().IsOpen(true);winchisel::platform::boot_log(("scheduled tasks UI: "+message).c_str());}
namespace {

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

ScheduledTasksPage::ScheduledTasksPage() {
    InitializeComponent();
    Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(SortBox(), winchisel::ui::tr(L"Sort by"));
    for (auto const& option : {winchisel::ui::tr(L"Name"), winchisel::ui::tr(L"Status"), winchisel::ui::tr(L"Path")}) {
        auto item = Controls::ComboBoxItem();
        item.Content(box_value(option));
        SortBox().Items().Append(item);
    }
    SortBox().SelectedIndex(0);
    HideMicrosoft().Content(box_value(winchisel::ui::tr(L"Hide Microsoft entries")));
    Status().Text(winchisel::ui::tr(L"Loading scheduled tasks..."));
    submit([] { return winchisel::core::Result<void>{}; });
}

void ScheduledTasksPage::SortBox_SelectionChanged(Windows::Foundation::IInspectable const&,
    Controls::SelectionChangedEventArgs const&) {
    if (loading_) return;
    sort_mode_ = SortBox().SelectedIndex();
    render_list(current_view());
}

void ScheduledTasksPage::HideMicrosoft_Changed(Windows::Foundation::IInspectable const&, RoutedEventArgs const&) {
    if (loading_) return;
    hide_microsoft_ = winrt::unbox_value_or<bool>(HideMicrosoft().IsChecked(), false);
    render_list(current_view());
}

void ScheduledTasksPage::Refresh_Click(Windows::Foundation::IInspectable const&, RoutedEventArgs const&) {
    submit([] { return winchisel::core::Result<void>{}; });
}

void ScheduledTasksPage::Search_TextChanged(Windows::Foundation::IInspectable const&,
    Controls::AutoSuggestBoxTextChangedEventArgs const&) {
    apply_filter();
}

void ScheduledTasksPage::submit(std::function<winchisel::core::Result<void>()> change) {
    pending_changes_.push_back(std::move(change));
    process_changes();
}

winrt::fire_and_forget ScheduledTasksPage::process_changes() {
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
            while (!pending_changes_.empty()) {
                auto change = std::move(pending_changes_.front());
                pending_changes_.pop_front();
                co_await winrt::resume_background();
                const auto result = winchisel::ui::result_or_error(change);
                co_await ui;
                if (!result) { failure = result.error().detail; pending_changes_.clear(); break; }
            }
            if (!failure.empty()) break;
            co_await winrt::resume_background();
            auto scan = winchisel::platform::scan_startup_tasks();
            co_await ui;
            if (scan) {
                entries_ = std::move(*scan);
                load_entries();
            } else {
                failure = scan.error().detail;
                break;
            }
            if (pending_changes_.empty()) break;
        }
        work_running_ = false;
        IsEnabled(true);
        if (!failure.empty()) show_write_error(failure);
    } catch (...) {
        winchisel::ui::report_async_error(queue, [weak](hstring const& text) {
            if (auto self = weak.get()) {
                self->pending_changes_.clear();
                self->work_running_ = false;
                self->IsEnabled(true);
                self->loading_ = false;
                self->show_write_error(to_string(text));
            }
        });
    }
}

void ScheduledTasksPage::load_entries() {
    loading_ = true;
    render_list(current_view());
    loading_ = false;
    render_status();
    apply_filter();
}

std::vector<winchisel::core::StartupEntry> ScheduledTasksPage::current_view() const {
    std::vector<winchisel::core::StartupEntry> view;
    for (auto const& entry : entries_) {
        if (hide_microsoft_ && winchisel::core::is_microsoft_startup_entry(entry)) continue;
        view.push_back(entry);
    }
    const auto lower_name = [](std::string const& text) {
        std::string lowered = text;
        std::ranges::transform(lowered, lowered.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return lowered;
    };
    if (sort_mode_ == 1) {
        std::ranges::sort(view, [&](auto const& left, auto const& right) {
            if (left.enabled != right.enabled) return left.enabled > right.enabled;
            return lower_name(left.name) < lower_name(right.name);
        });
    } else if (sort_mode_ == 2) {
        std::ranges::sort(view, [&](auto const& left, auto const& right) {
            if (left.key != right.key) return lower_name(left.key) < lower_name(right.key);
            return lower_name(left.name) < lower_name(right.name);
        });
    }
    return view;
}

void ScheduledTasksPage::render_list(std::vector<winchisel::core::StartupEntry> const& view) {
    rows_.clear();
    Items().Children().Clear();
    for (auto const& entry : view) {
        auto toggle = Controls::ToggleSwitch();
        Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(toggle, to_hstring(entry.name));
        toggle.OnContent(box_value(L""));
        toggle.OffContent(box_value(L""));
        toggle.MinWidth(0);
        toggle.Width(40);
        toggle.IsOn(entry.enabled);
        const auto index = rows_.size();
        rows_.push_back({entry, toggle, nullptr});
        toggle.Toggled([this, index](auto&&, auto&&) { save_entry(index); });
        std::string description = to_string(winchisel::ui::tr(L"Scheduled task"));
        if (!entry.detail.empty()) {
            description += " | ";
            description += entry.detail;
        }
        auto card = setting_card(to_hstring(entry.name), to_hstring(description), toggle);
        rows_.back().card = card;
        Items().Children().Append(card);
    }
    loading_ = false;
    render_status();
    apply_filter();
}

void ScheduledTasksPage::save_entry(std::size_t index) {
    if (loading_ || index >= rows_.size()) return;
    auto entry = rows_[index].entry;
    const bool enabled = rows_[index].control.IsOn();
    submit([entry = std::move(entry), enabled] {
        return winchisel::platform::set_startup_entry_enabled(entry, enabled);
    });
}

void ScheduledTasksPage::apply_filter() {
    auto query = to_string(Search().Text());
    std::ranges::transform(query, query.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    for (auto const& row : rows_) {
        if (!row.card) continue;
        auto text = row.card.Tag() ? to_string(unbox_value<hstring>(row.card.Tag())) : std::string{};
        std::ranges::transform(text, text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        const bool match = query.empty() || text.find(query) != std::string::npos;
        row.card.Visibility(match ? Visibility::Visible : Visibility::Collapsed);
    }
}

void ScheduledTasksPage::render_status() {
    Status().Text(hstring{std::to_wstring(entries_.size()) + L" " + std::wstring(winchisel::ui::tr(L"scheduled tasks"))});
}

}  // namespace winrt::Winchisel::implementation
