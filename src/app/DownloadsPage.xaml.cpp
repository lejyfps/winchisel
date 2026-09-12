#include "pch.h"
#include "AsyncSupport.hpp"
#include "DownloadsPage.xaml.h"
#include "AsyncLifetime.hpp"
#include "Localization.hpp"
#include "winchisel/platform/shell.hpp"
#include "winchisel/platform/update.hpp"
#include <algorithm>
#include <chrono>
#include <cctype>
#if __has_include("DownloadsPage.g.cpp")
#include "DownloadsPage.g.cpp"
#endif
using namespace winrt;
using namespace Microsoft::UI::Xaml;
namespace winrt::Winchisel::implementation {
namespace { std::string lower(std::string value) { std::ranges::transform(value, value.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); }); return value; }

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
} }

DownloadsPage::DownloadsPage() {
 InitializeComponent(); catalog_=winchisel::core::get_download_catalog(); installed_.assign(catalog_.size(), false);
 auto weak=get_weak();
  timer_=DispatcherTimer(); timer_.Interval(std::chrono::milliseconds(120)); timer_token_=timer_.Tick([weak](auto&&,auto&&){ if(auto self=weak.get()) self->poll_worker(); }); search_timer_=DispatcherTimer();search_timer_.Interval(std::chrono::milliseconds(200));search_timer_token_=search_timer_.Tick([weak](auto&&,auto&&){ if(auto self=weak.get()){ self->search_timer_.Stop(); if(self->operation_==Operation::none)self->apply_filter(); }});ui_ready_=true;
  // #2 RefreshContainer (Pull-to-Refresh): gleicher Pfad wie Refresh-Button.
  refresh_token_=PullRefresh().RefreshRequested([weak](auto const&, auto const&) { if(auto self=weak.get()){ if(self->ui_ready_&&self->operation_==Operation::none) self->start_scan(true); } });
  Loaded([weak](auto&&,auto&&){ if(auto self=weak.get()){ if(self->operation_!=Operation::none) self->timer_.Start(); } }); Unloaded([weak](auto&&,auto&&){ if(auto self=weak.get()) self->timer_.Stop(); }); start_scan();
}
DownloadsPage::~DownloadsPage() { timer_.Stop(); timer_.Tick(timer_token_);search_timer_.Stop();search_timer_.Tick(search_timer_token_); winchisel::ui::finish_in_background(scan_worker_); winchisel::ui::finish_in_background(install_worker_); winchisel::ui::finish_in_background(uninstall_worker_); }
void DownloadsPage::start_scan(bool force, bool clear_notice) {
    auto error_weak=get_weak();
    winrt::Microsoft::UI::Dispatching::DispatcherQueue error_queue{nullptr};
    try { error_queue=DispatcherQueue(); } catch (...) {}
    try {
        auto error_lifetime=get_strong();

  if(operation_!=Operation::none)return; operation_=Operation::scan; Loading().Visibility(Visibility::Visible); Groups().IsHitTestVisible(false); Groups().Opacity(0.6); RefreshButton().IsEnabled(false); InstallButton().IsEnabled(false); UninstallButton().IsEnabled(false); if(clear_notice)Notice().IsOpen(false);
 scan_worker_=std::async(std::launch::async,[catalog=catalog_,force]{return winchisel::platform::scan_downloads_installed(catalog,force);}); timer_.Start();

    } catch (...) {
        winchisel::ui::report_async_error(error_queue, [error_weak](winrt::hstring const& text) {
            if (auto self=error_weak.get()) { self->operation_=Operation::none; self->timer_.Stop(); self->Loading().Visibility(Visibility::Collapsed); self->Groups().IsHitTestVisible(true); self->Groups().Opacity(1.0); self->RefreshButton().IsEnabled(true); self->update_actions(); self->Notice().Title(L"Operation failed"); self->Notice().Message(text); self->Notice().Severity(Controls::InfoBarSeverity::Error); self->Notice().IsOpen(true); }
        });
    }
}
void DownloadsPage::poll_worker() {
    auto error_weak=get_weak();
    winrt::Microsoft::UI::Dispatching::DispatcherQueue error_queue{nullptr};
    try { error_queue=DispatcherQueue(); } catch (...) {}
    try {
        auto error_lifetime=get_strong();

 if(operation_==Operation::scan) {
  if(!scan_worker_.valid()||scan_worker_.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return;
  auto result=scan_worker_.get(); operation_=Operation::none; Loading().Visibility(Visibility::Collapsed); Groups().IsHitTestVisible(true); Groups().Opacity(1.0); RefreshButton().IsEnabled(true);
  if(result){installed_=std::move(*result);render_items();}else{Notice().Title(L"Scan failed");Notice().Message(to_hstring(result.error().detail));Notice().Severity(Controls::InfoBarSeverity::Error);Notice().IsOpen(true);} update_actions();
 } else if(operation_==Operation::install || operation_==Operation::uninstall) {
  const bool is_install=operation_==Operation::install;
  auto& worker=is_install?install_worker_:uninstall_worker_;
  if(!worker.valid()||worker.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return;
  auto result=worker.get(); operation_=Operation::none; Loading().Visibility(Visibility::Collapsed); Groups().IsHitTestVisible(true); Groups().Opacity(1.0); RefreshButton().IsEnabled(true); Notice().Title(is_install?L"Installation complete":L"Uninstallation complete");
  if(result){auto message=std::to_string(result->succeeded)+" succeeded, "+std::to_string(result->failed)+" failed.";if(!result->failure_details.empty()){message+=" ";for(std::size_t index{};index<result->failure_details.size();++index){if(index)message+=" | ";message+=result->failure_details[index];}}Notice().Message(to_hstring(message));Notice().Severity(result->failed?Controls::InfoBarSeverity::Warning:Controls::InfoBarSeverity::Success);}else{Notice().Message(to_hstring(result.error().detail));Notice().Severity(Controls::InfoBarSeverity::Error);} Notice().IsOpen(true); update_actions(); start_scan(false,false);
 } else timer_.Stop();

    } catch (...) {
        winchisel::ui::report_async_error(error_queue, [error_weak](winrt::hstring const& text) {
            if (auto self=error_weak.get()) { self->operation_=Operation::none; self->timer_.Stop(); self->Loading().Visibility(Visibility::Collapsed); self->Groups().IsHitTestVisible(true); self->Groups().Opacity(1.0); self->RefreshButton().IsEnabled(true); self->update_actions(); self->Notice().Title(L"Operation failed"); self->Notice().Message(text); self->Notice().Severity(Controls::InfoBarSeverity::Error); self->Notice().IsOpen(true); }
        });
    }
}
void DownloadsPage::render_items() {
 Groups().Children().Clear(); category_lists_.clear(); detached_content_.clear(); category_base_.assign(16, hstring{});
 search_index_.clear(); search_index_.reserve(catalog_.size());
 for(auto const& item:catalog_){auto category_name=winchisel::core::download_category_name(item.category);search_index_.push_back(lower(std::string(item.name)+" "+std::string(category_name)+" "+std::string(item.winget_ids)));}
 auto resources=Application::Current().Resources(); auto success=resources.Lookup(box_value(L"SystemFillColorSuccessBrush")).try_as<Media::Brush>(); auto secondary=resources.Lookup(box_value(L"TextFillColorSecondaryBrush")).try_as<Media::Brush>(); auto critical=resources.Lookup(box_value(L"SystemFillColorCriticalBrush")).try_as<Media::Brush>(); auto card_background=resources.Lookup(box_value(L"CardBackgroundFillColorDefaultBrush")).try_as<Media::Brush>(); auto card_stroke=resources.Lookup(box_value(L"CardStrokeColorDefaultBrush")).try_as<Media::Brush>(); auto body_strong=resources.Lookup(box_value(L"BodyStrongTextBlockStyle")).try_as<Microsoft::UI::Xaml::Style>(); auto caption=resources.Lookup(box_value(L"CaptionTextBlockStyle")).try_as<Microsoft::UI::Xaml::Style>();
 for(int category_index=0;category_index<16;++category_index){auto category=static_cast<winchisel::core::DownloadCategory>(category_index);auto list=Controls::ListView();list.SelectionMode(Controls::ListViewSelectionMode::Multiple);list.HorizontalContentAlignment(HorizontalAlignment::Stretch);list.SelectionChanged({this,&DownloadsPage::Items_SelectionChanged});
  for(std::size_t index{};index<catalog_.size();++index){auto const& item=catalog_[index];if(item.category!=category)continue;
   auto row=Controls::ListViewItem();row.Tag(box_value(static_cast<std::uint64_t>(index)));Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(row,winrt::hstring{std::wstring(to_hstring(std::string(item.name)))+std::wstring(installed_[index]?winchisel::ui::tr(L", installed"):winchisel::ui::tr(L", not installed"))});row.HorizontalContentAlignment(HorizontalAlignment::Stretch);row.Background(card_background);row.BorderBrush(card_stroke);row.BorderThickness({1,1,1,1});row.CornerRadius({4,4,4,4});row.Padding({12,10,12,10});row.Margin({0,0,0,8});
   auto grid=Controls::Grid();grid.ColumnDefinitions().Append(Controls::ColumnDefinition());auto trailing=Controls::ColumnDefinition();trailing.Width({0,GridUnitType::Auto});grid.ColumnDefinitions().Append(trailing);auto text=Controls::StackPanel();auto title=Controls::TextBlock();title.Text(to_hstring(item.name));title.Style(body_strong);text.Children().Append(title);auto detail=Controls::TextBlock();detail.Text(item.winget_ids.empty()?winchisel::ui::tr(L"Website install"):to_hstring(item.winget_ids));detail.Foreground(secondary);detail.TextWrapping(TextWrapping::Wrap);detail.Style(caption);text.Children().Append(detail);grid.Children().Append(text);
   auto actions=Controls::StackPanel();actions.Orientation(Controls::Orientation::Horizontal);actions.Spacing(12);auto badge=make_status_badge(tint_brush(installed_[index]?success:critical,0x2E),installed_[index]?success:critical,installed_[index]?L"\uE73E":L"\uE896",installed_[index]?winchisel::ui::tr(L"Installed"):winchisel::ui::tr(L"Not installed"));actions.Children().Append(badge);auto website=Controls::Button();website.VerticalAlignment(VerticalAlignment::Center);website.Padding({8,4,8,4});auto website_icon=Controls::FontIcon();website_icon.Glyph(hstring{L"\uE774"});website_icon.FontSize(14);website_icon.VerticalAlignment(VerticalAlignment::Center);website.Content(website_icon);auto website_tip=winchisel::ui::tr(L"Website");Controls::ToolTipService::SetToolTip(website,box_value(website_tip));Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(website,website_tip);website.Tag(box_value(to_hstring(item.website_url)));website.Click({this,&DownloadsPage::Website_Click});actions.Children().Append(website);actions.VerticalAlignment(VerticalAlignment::Center);Controls::Grid::SetColumn(actions,1);grid.Children().Append(actions);row.Content(grid);list.Items().Append(row);
  }
   if(list.Items().Size()){auto base=winrt::hstring{winchisel::ui::tr(winrt::to_hstring(winchisel::core::download_category_name(category)))};category_base_[static_cast<std::size_t>(category_index)]=base;auto expander=Controls::Expander();expander.Tag(box_value(category_index));expander.HorizontalAlignment(HorizontalAlignment::Stretch);expander.HorizontalContentAlignment(HorizontalAlignment::Stretch);expander.IsExpanded(category_index==0);expander.Content(list);category_lists_.push_back(list);
    // #1 Virtualization-light: nur expandierte Kategorie im Visual-Tree.
    auto weak=get_weak();
    expander.Expanding([weak, expander](auto const&, auto const&) { if(auto self=weak.get()) self->attach_group(expander); });
    expander.Collapsed([weak, expander](auto const&, auto const&) { if(auto self=weak.get()) self->detach_group(expander); });
    if(!expander.IsExpanded()){ detached_content_[category_index]=list; expander.Content(nullptr); }
    Groups().Children().Append(expander);}
 }
 apply_filter();
}
void DownloadsPage::attach_group(Controls::Expander const& expander) {
 if(!expander || expander.Content())return;
 const auto index=unbox_value_or<int>(expander.Tag(),-1);
 if(index<0)return;
 if(auto found=detached_content_.find(index);found!=detached_content_.end()){expander.Content(found->second);detached_content_.erase(found);}
}
void DownloadsPage::detach_group(Controls::Expander const& expander) {
 if(!expander || !expander.Content())return;
 const auto index=unbox_value_or<int>(expander.Tag(),-1);
 if(index<0)return;
 detached_content_[index]=expander.Content();
 expander.Content(nullptr);
}
Controls::ListView DownloadsPage::list_for(Controls::Expander const& expander) {
 if(!expander)return nullptr;
 if(auto list=expander.Content().try_as<Controls::ListView>())return list;
 const auto index=unbox_value_or<int>(expander.Tag(),-1);
 if(index<0)return nullptr;
 if(auto found=detached_content_.find(index);found!=detached_content_.end())return found->second.try_as<Controls::ListView>();
 return nullptr;
}
void DownloadsPage::apply_filter(bool scroll_top) {
 auto query=lower(to_string(Search().Text())); auto filter=Filter().SelectedIndex(); std::size_t visible{}; bool first_visible=true;
 Controls::ListView first_list{nullptr}; Windows::Foundation::IInspectable first_row{nullptr};
 for(auto const& child:Groups().Children()){auto expander=child.try_as<Controls::Expander>();if(!expander)continue;auto list=list_for(expander);if(!list)continue;std::size_t count{};
  std::vector<Windows::Foundation::IInspectable> deselect;
  for(auto const& value:list.Items()){auto row=value.try_as<Controls::ListViewItem>();if(!row)continue;const auto index=unbox_value<std::uint64_t>(row.Tag());bool show=index<catalog_.size()&&index<search_index_.size();if(show){if(!query.empty()&&search_index_[static_cast<std::size_t>(index)].find(query)==std::string::npos)show=false;if(show&&filter==1&&!installed_[static_cast<std::size_t>(index)])show=false;if(show&&filter==2&&installed_[static_cast<std::size_t>(index)])show=false;}
   row.Visibility(show?Visibility::Visible:Visibility::Collapsed);if(show){++count;if(!first_row){first_list=list;first_row=value;}}else if(row.IsSelected())deselect.push_back(value);}
  for(auto const& value:deselect){std::uint32_t position{};if(list.SelectedItems().IndexOf(value,position))list.SelectedItems().RemoveAt(position);}
   auto index=unbox_value_or<int>(expander.Tag(),-1);
   auto base=(index>=0 && static_cast<std::size_t>(index)<category_base_.size())?category_base_[static_cast<std::size_t>(index)]:hstring{};
   auto header_panel=Controls::StackPanel();header_panel.Orientation(Controls::Orientation::Horizontal);header_panel.Spacing(8);header_panel.VerticalAlignment(VerticalAlignment::Center);auto header_text=Controls::TextBlock();header_text.Text(base);header_text.VerticalAlignment(VerticalAlignment::Center);header_panel.Children().Append(header_text);auto count_badge=Controls::InfoBadge();count_badge.Value(static_cast<int>(count));count_badge.VerticalAlignment(VerticalAlignment::Center);Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(count_badge,winrt::hstring{std::to_wstring(count)+L" items"});header_panel.Children().Append(count_badge);expander.Header(header_panel);Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(expander,winrt::hstring{std::wstring(base)+L", "+std::to_wstring(count)+L" items"});
  expander.Visibility(count?Visibility::Visible:Visibility::Collapsed);
  // #1: nur expandierte Kategorie anhängen, Rest detached (zählt trotzdem).
  if(count && (!query.empty()||first_visible)){attach_group(expander);expander.IsExpanded(true);}
  else{expander.IsExpanded(false);detach_group(expander);}
  first_visible=first_visible&&!count;
  visible+=count;
 }
 if(!visible){Notice().Title(winchisel::ui::tr(L"No matching apps"));Notice().Message(winchisel::ui::tr(L"Change the search or installed-state filter."));Notice().Severity(Controls::InfoBarSeverity::Informational);Notice().IsOpen(true);} update_actions();
 // #1: gezielter Sprung zum ersten Treffer — nur bei explizitem Filterwechsel.
 if(scroll_top && first_list && first_row) first_list.ScrollIntoView(first_row);
}
void DownloadsPage::update_actions(){std::uint32_t install_count{},uninstall_count{};for(auto const& list:category_lists_)for(auto const& value:list.SelectedItems())if(auto row=value.try_as<Controls::ListViewItem>()){if(row.Visibility()!=Visibility::Visible)continue;auto index=unbox_value<std::uint64_t>(row.Tag());if(index>=catalog_.size()||catalog_[index].winget_ids.empty())continue;if(index<installed_.size()&&installed_[index])++uninstall_count;else ++install_count;}const bool idle=operation_==Operation::none;InstallButton().IsEnabled(idle&&install_count);UninstallButton().IsEnabled(idle&&uninstall_count);InstallButton().Content(box_value(install_count?winrt::hstring{std::wstring(winchisel::ui::tr(L"Install"))+L" ("+std::to_wstring(install_count)+L")"}:winchisel::ui::tr(L"Install")));UninstallButton().Content(box_value(uninstall_count?winrt::hstring{std::wstring(winchisel::ui::tr(L"Uninstall"))+L" ("+std::to_wstring(uninstall_count)+L")"}:winchisel::ui::tr(L"Uninstall")));}
fire_and_forget DownloadsPage::confirm_install(){
    auto error_weak=get_weak();
    winrt::Microsoft::UI::Dispatching::DispatcherQueue error_queue{nullptr};
    try { error_queue=DispatcherQueue(); } catch (...) {}
    try {
        auto error_lifetime=get_strong();
        if (winchisel::platform::is_packaged_install()) {
            // Store policy 10.1.5: packaged builds must not acquire
            // third-party software. The page is hidden there; this covers
            // residual paths (restored sessions, automation). Worded
            // neutrally on purpose: no outside-store pointers.
            Notice().Title(L"Not available");
            Notice().Message(L"Installing apps isn't available in the Microsoft Store version of Winchisel.");
            Notice().Severity(Controls::InfoBarSeverity::Informational);
            Notice().IsOpen(true);
            co_return;
        }
    winchisel::core::DialogSlot dialog_slot; if(!winchisel::ui::dialog_available(dialog_slot))co_return;

auto lifetime=get_strong();std::uint32_t count{};for(auto const& list:category_lists_)for(auto const& value:list.SelectedItems())if(auto row=value.try_as<Controls::ListViewItem>()){if(row.Visibility()!=Visibility::Visible)continue;auto index=unbox_value<std::uint64_t>(row.Tag());if(index<catalog_.size()&&!catalog_[index].winget_ids.empty()&&(index>=installed_.size()||!installed_[index]))++count;}if(!count)co_return;Controls::ContentDialog dialog;dialog.XamlRoot(XamlRoot());dialog.Title(box_value(L"Install selected apps?"));dialog.Content(box_value(to_hstring(std::to_string(count)+" selected apps will be installed with winget.")));dialog.PrimaryButtonText(L"Install");dialog.CloseButtonText(L"Cancel");dialog.DefaultButton(Controls::ContentDialogButton::Close);if(co_await dialog.ShowAsync()==Controls::ContentDialogResult::Primary)start_install();
    } catch (...) {
        winchisel::ui::report_async_error(error_queue, [error_weak](winrt::hstring const& text) {
            if (auto self=error_weak.get()) { self->operation_=Operation::none; self->timer_.Stop(); self->Loading().Visibility(Visibility::Collapsed); self->Groups().IsHitTestVisible(true); self->Groups().Opacity(1.0); self->RefreshButton().IsEnabled(true); self->update_actions(); self->Notice().Title(L"Operation failed"); self->Notice().Message(text); self->Notice().Severity(Controls::InfoBarSeverity::Error); self->Notice().IsOpen(true); }
        });
    }
}
void DownloadsPage::start_install(){
    auto error_weak=get_weak();
    winrt::Microsoft::UI::Dispatching::DispatcherQueue error_queue{nullptr};
    try { error_queue=DispatcherQueue(); } catch (...) {}
    try {
        auto error_lifetime=get_strong();
std::vector<winchisel::core::DownloadCatalogEntry const*> selected;for(auto const& list:category_lists_)for(auto const& value:list.SelectedItems())if(auto row=value.try_as<Controls::ListViewItem>()){if(row.Visibility()!=Visibility::Visible)continue;auto index=unbox_value<std::uint64_t>(row.Tag());if(index<catalog_.size()&&!catalog_[index].winget_ids.empty()&&(index>=installed_.size()||!installed_[index]))selected.push_back(&catalog_[index]);}if(selected.empty())return;operation_=Operation::install;Loading().Visibility(Visibility::Visible);Groups().IsHitTestVisible(false);Groups().Opacity(0.6);RefreshButton().IsEnabled(false);InstallButton().IsEnabled(false);UninstallButton().IsEnabled(false);Notice().IsOpen(false);install_worker_=std::async(std::launch::async,[selected=std::move(selected)]{return winchisel::platform::install_downloads(selected);});timer_.Start();
    } catch (...) {
        winchisel::ui::report_async_error(error_queue, [error_weak](winrt::hstring const& text) {
            if (auto self=error_weak.get()) { self->operation_=Operation::none; self->timer_.Stop(); self->Loading().Visibility(Visibility::Collapsed); self->Groups().IsHitTestVisible(true); self->Groups().Opacity(1.0); self->RefreshButton().IsEnabled(true); self->update_actions(); self->Notice().Title(L"Operation failed"); self->Notice().Message(text); self->Notice().Severity(Controls::InfoBarSeverity::Error); self->Notice().IsOpen(true); }
        });
    }
}
fire_and_forget DownloadsPage::confirm_uninstall(){
    auto error_weak=get_weak();
    winrt::Microsoft::UI::Dispatching::DispatcherQueue error_queue{nullptr};
    try { error_queue=DispatcherQueue(); } catch (...) {}
    try {
        auto error_lifetime=get_strong();
    winchisel::core::DialogSlot dialog_slot; if(!winchisel::ui::dialog_available(dialog_slot))co_return;

auto lifetime=get_strong();std::uint32_t count{};for(auto const& list:category_lists_)for(auto const& value:list.SelectedItems())if(auto row=value.try_as<Controls::ListViewItem>()){if(row.Visibility()!=Visibility::Visible)continue;auto index=unbox_value<std::uint64_t>(row.Tag());if(index<catalog_.size()&&!catalog_[index].winget_ids.empty()&&index<installed_.size()&&installed_[index])++count;}if(!count)co_return;Controls::ContentDialog dialog;dialog.XamlRoot(XamlRoot());dialog.Title(box_value(L"Uninstall selected apps?"));dialog.Content(box_value(to_hstring(std::to_string(count)+" selected apps will be uninstalled with winget.")));dialog.PrimaryButtonText(L"Uninstall");dialog.CloseButtonText(L"Cancel");dialog.DefaultButton(Controls::ContentDialogButton::Close);if(co_await dialog.ShowAsync()==Controls::ContentDialogResult::Primary)start_uninstall();
    } catch (...) {
        winchisel::ui::report_async_error(error_queue, [error_weak](winrt::hstring const& text) {
            if (auto self=error_weak.get()) { self->operation_=Operation::none; self->timer_.Stop(); self->Loading().Visibility(Visibility::Collapsed); self->Groups().IsHitTestVisible(true); self->Groups().Opacity(1.0); self->RefreshButton().IsEnabled(true); self->update_actions(); self->Notice().Title(L"Operation failed"); self->Notice().Message(text); self->Notice().Severity(Controls::InfoBarSeverity::Error); self->Notice().IsOpen(true); }
        });
    }
}
void DownloadsPage::start_uninstall(){
    auto error_weak=get_weak();
    winrt::Microsoft::UI::Dispatching::DispatcherQueue error_queue{nullptr};
    try { error_queue=DispatcherQueue(); } catch (...) {}
    try {
        auto error_lifetime=get_strong();
std::vector<winchisel::core::DownloadCatalogEntry const*> selected;for(auto const& list:category_lists_)for(auto const& value:list.SelectedItems())if(auto row=value.try_as<Controls::ListViewItem>()){if(row.Visibility()!=Visibility::Visible)continue;auto index=unbox_value<std::uint64_t>(row.Tag());if(index<catalog_.size()&&!catalog_[index].winget_ids.empty()&&index<installed_.size()&&installed_[index])selected.push_back(&catalog_[index]);}if(selected.empty())return;operation_=Operation::uninstall;Loading().Visibility(Visibility::Visible);Groups().IsHitTestVisible(false);Groups().Opacity(0.6);RefreshButton().IsEnabled(false);InstallButton().IsEnabled(false);UninstallButton().IsEnabled(false);Notice().IsOpen(false);uninstall_worker_=std::async(std::launch::async,[selected=std::move(selected)]{return winchisel::platform::uninstall_downloads(selected);});timer_.Start();
    } catch (...) {
        winchisel::ui::report_async_error(error_queue, [error_weak](winrt::hstring const& text) {
            if (auto self=error_weak.get()) { self->operation_=Operation::none; self->timer_.Stop(); self->Loading().Visibility(Visibility::Collapsed); self->Groups().IsHitTestVisible(true); self->Groups().Opacity(1.0); self->RefreshButton().IsEnabled(true); self->update_actions(); self->Notice().Title(L"Operation failed"); self->Notice().Message(text); self->Notice().Severity(Controls::InfoBarSeverity::Error); self->Notice().IsOpen(true); }
        });
    }
}
void DownloadsPage::Refresh_Click(IInspectable const&,RoutedEventArgs const&){if(ui_ready_)start_scan(true);} void DownloadsPage::Install_Click(IInspectable const&,RoutedEventArgs const&){if(ui_ready_)confirm_install();} void DownloadsPage::Uninstall_Click(IInspectable const&,RoutedEventArgs const&){if(ui_ready_)confirm_uninstall();} void DownloadsPage::Search_TextChanged(IInspectable const&,Controls::AutoSuggestBoxTextChangedEventArgs const&){if(ui_ready_&&operation_==Operation::none){search_timer_.Stop();search_timer_.Start();}} void DownloadsPage::Filter_SelectionChanged(IInspectable const&,Controls::SelectionChangedEventArgs const&){if(ui_ready_&&operation_==Operation::none)apply_filter(true);} void DownloadsPage::Items_SelectionChanged(IInspectable const&,Controls::SelectionChangedEventArgs const&){if(ui_ready_)update_actions();}
void DownloadsPage::Website_Click(IInspectable const& sender,RoutedEventArgs const&){if(winchisel::platform::is_packaged_install())return;if(auto button=sender.try_as<Controls::Button>()){auto url=unbox_value<hstring>(button.Tag());if(auto result=winchisel::platform::open_https_url(std::wstring(url));!result){Notice().Title(L"Could not open website");Notice().Message(to_hstring(result.error().detail));Notice().Severity(Controls::InfoBarSeverity::Error);Notice().IsOpen(true);}}}
}  // namespace winrt::Winchisel::implementation
