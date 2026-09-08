#include "pch.h"
#include "AsyncSupport.hpp"
#include "DownloadsPage.xaml.h"
#include "AsyncLifetime.hpp"
#include "Localization.hpp"
#include "winchisel/platform/shell.hpp"
#include <algorithm>
#include <chrono>
#include <cctype>
#if __has_include("DownloadsPage.g.cpp")
#include "DownloadsPage.g.cpp"
#endif
using namespace winrt;
using namespace Microsoft::UI::Xaml;
namespace winrt::Winchisel::implementation {
namespace { std::string lower(std::string value) { std::ranges::transform(value, value.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); }); return value; } }

DownloadsPage::DownloadsPage() {
 InitializeComponent(); catalog_=winchisel::core::get_download_catalog(); installed_.assign(catalog_.size(), false);
 timer_=DispatcherTimer(); timer_.Interval(std::chrono::milliseconds(120)); timer_token_=timer_.Tick([this](auto&&,auto&&){ poll_worker(); }); search_timer_=DispatcherTimer();search_timer_.Interval(std::chrono::milliseconds(200));search_timer_token_=search_timer_.Tick([this](auto&&,auto&&){search_timer_.Stop();if(operation_==Operation::none)render_items();});ui_ready_=true; start_scan();
}
DownloadsPage::~DownloadsPage() { timer_.Stop(); timer_.Tick(timer_token_);search_timer_.Stop();search_timer_.Tick(search_timer_token_); winchisel::ui::finish_in_background(scan_worker_); winchisel::ui::finish_in_background(install_worker_); }
void DownloadsPage::start_scan(bool force, bool clear_notice) {
    auto error_lifetime=get_strong();
    auto error_queue=DispatcherQueue();
    auto error_weak=get_weak();
    try {

 if(operation_!=Operation::none)return; operation_=Operation::scan; Loading().Visibility(Visibility::Visible); Groups().IsHitTestVisible(false); Groups().Opacity(0.6); RefreshButton().IsEnabled(false); InstallButton().IsEnabled(false); if(clear_notice)Notice().IsOpen(false);
 scan_worker_=std::async(std::launch::async,[catalog=catalog_,force]{return winchisel::platform::scan_downloads_installed(catalog,force);}); timer_.Start();

    } catch (...) {
        winchisel::ui::report_async_error(error_queue, [error_weak](winrt::hstring const& text) {
            if (auto self=error_weak.get()) { self->operation_=Operation::none; self->timer_.Stop(); self->Loading().Visibility(Visibility::Collapsed); self->Groups().IsHitTestVisible(true); self->Groups().Opacity(1.0); self->RefreshButton().IsEnabled(true); self->update_actions(); self->Notice().Title(L"Operation failed"); self->Notice().Message(text); self->Notice().Severity(Controls::InfoBarSeverity::Error); self->Notice().IsOpen(true); }
        });
    }
}
void DownloadsPage::poll_worker() {
    auto error_lifetime=get_strong();
    auto error_queue=DispatcherQueue();
    auto error_weak=get_weak();
    try {

 if(operation_==Operation::scan) {
  if(!scan_worker_.valid()||scan_worker_.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return;
  auto result=scan_worker_.get(); operation_=Operation::none; Loading().Visibility(Visibility::Collapsed); Groups().IsHitTestVisible(true); Groups().Opacity(1.0); RefreshButton().IsEnabled(true);
  if(result){installed_=std::move(*result);render_items();}else{Notice().Title(L"Scan failed");Notice().Message(to_hstring(result.error().detail));Notice().Severity(Controls::InfoBarSeverity::Error);Notice().IsOpen(true);} update_actions();
 } else if(operation_==Operation::install) {
  if(!install_worker_.valid()||install_worker_.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return;
  auto result=install_worker_.get(); operation_=Operation::none; Loading().Visibility(Visibility::Collapsed); Groups().IsHitTestVisible(true); Groups().Opacity(1.0); RefreshButton().IsEnabled(true); Notice().Title(L"Installation complete");
  if(result){auto message=std::to_string(result->succeeded)+" succeeded, "+std::to_string(result->failed)+" failed.";if(!result->failure_details.empty()){message+=" ";for(std::size_t index{};index<result->failure_details.size();++index){if(index)message+=" | ";message+=result->failure_details[index];}}Notice().Message(to_hstring(message));Notice().Severity(result->failed?Controls::InfoBarSeverity::Warning:Controls::InfoBarSeverity::Success);}else{Notice().Message(to_hstring(result.error().detail));Notice().Severity(Controls::InfoBarSeverity::Error);} Notice().IsOpen(true); update_actions(); start_scan(false,false);
 } else timer_.Stop();

    } catch (...) {
        winchisel::ui::report_async_error(error_queue, [error_weak](winrt::hstring const& text) {
            if (auto self=error_weak.get()) { self->operation_=Operation::none; self->timer_.Stop(); self->Loading().Visibility(Visibility::Collapsed); self->Groups().IsHitTestVisible(true); self->Groups().Opacity(1.0); self->RefreshButton().IsEnabled(true); self->update_actions(); self->Notice().Title(L"Operation failed"); self->Notice().Message(text); self->Notice().Severity(Controls::InfoBarSeverity::Error); self->Notice().IsOpen(true); }
        });
    }
}
void DownloadsPage::render_items() {
 Groups().Children().Clear(); category_lists_.clear(); auto query=lower(to_string(Search().Text())); auto filter=Filter().SelectedIndex(); auto resources=Application::Current().Resources(); auto success=resources.Lookup(box_value(L"SystemFillColorSuccessBrush")).try_as<Media::Brush>(); auto secondary=resources.Lookup(box_value(L"TextFillColorSecondaryBrush")).try_as<Media::Brush>(); std::size_t visible{};
 for(int category_index=0;category_index<16;++category_index){auto category=static_cast<winchisel::core::DownloadCategory>(category_index);auto list=Controls::ListView();list.SelectionMode(Controls::ListViewSelectionMode::Multiple);list.HorizontalContentAlignment(HorizontalAlignment::Stretch);list.SelectionChanged({this,&DownloadsPage::Items_SelectionChanged});std::size_t category_count{};
  for(std::size_t index{};index<catalog_.size();++index){auto const& item=catalog_[index];if(item.category!=category)continue;auto category_name=winchisel::core::download_category_name(category);auto searchable=lower(std::string(item.name)+" "+std::string(category_name)+" "+std::string(item.winget_ids));if(!query.empty()&&searchable.find(query)==std::string::npos)continue;if(filter==1&&!installed_[index])continue;if(filter==2&&installed_[index])continue;
   auto row=Controls::ListViewItem();row.Tag(box_value(static_cast<std::uint64_t>(index)));Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(row,winrt::hstring{std::wstring(to_hstring(std::string(item.name)))+std::wstring(installed_[index]?winchisel::ui::tr(L", installed"):winchisel::ui::tr(L", not installed"))});row.HorizontalContentAlignment(HorizontalAlignment::Stretch);row.Background(resources.Lookup(box_value(L"CardBackgroundFillColorDefaultBrush")).try_as<Media::Brush>());row.BorderBrush(resources.Lookup(box_value(L"CardStrokeColorDefaultBrush")).try_as<Media::Brush>());row.BorderThickness({1,1,1,1});row.CornerRadius({4,4,4,4});row.Padding({12,10,12,10});row.Margin({0,0,0,8});
   auto grid=Controls::Grid();grid.ColumnDefinitions().Append(Controls::ColumnDefinition());auto trailing=Controls::ColumnDefinition();trailing.Width({0,GridUnitType::Auto});grid.ColumnDefinitions().Append(trailing);auto text=Controls::StackPanel();auto title=Controls::TextBlock();title.Text(to_hstring(item.name));title.Style(resources.Lookup(box_value(L"BodyStrongTextBlockStyle")).try_as<Microsoft::UI::Xaml::Style>());text.Children().Append(title);auto detail=Controls::TextBlock();detail.Text(item.winget_ids.empty()?winchisel::ui::tr(L"Website install"):to_hstring(item.winget_ids));detail.Foreground(secondary);detail.TextWrapping(TextWrapping::Wrap);detail.Style(resources.Lookup(box_value(L"CaptionTextBlockStyle")).try_as<Microsoft::UI::Xaml::Style>());text.Children().Append(detail);grid.Children().Append(text);
   auto actions=Controls::StackPanel();actions.Orientation(Controls::Orientation::Horizontal);actions.Spacing(12);auto status=Controls::TextBlock();status.Text(installed_[index]?winchisel::ui::tr(L"Installed"):winchisel::ui::tr(L"Not installed"));status.Foreground(installed_[index]?success:secondary);status.VerticalAlignment(VerticalAlignment::Center);actions.Children().Append(status);auto website=Controls::HyperlinkButton();website.Content(box_value(winchisel::ui::tr(L"Website")));website.Tag(box_value(to_hstring(item.website_url)));website.Click({this,&DownloadsPage::Website_Click});actions.Children().Append(website);Controls::Grid::SetColumn(actions,1);grid.Children().Append(actions);row.Content(grid);list.Items().Append(row);++category_count;++visible;
  }
  if(category_count){auto expander=Controls::Expander();expander.Header(box_value(winrt::hstring{std::wstring(winchisel::ui::tr(winrt::to_hstring(winchisel::core::download_category_name(category))))+L" ("+std::to_wstring(category_count)+L")"}));expander.HorizontalAlignment(HorizontalAlignment::Stretch);expander.HorizontalContentAlignment(HorizontalAlignment::Stretch);expander.IsExpanded(!query.empty()||category_index==0);expander.Content(list);category_lists_.push_back(list);Groups().Children().Append(expander);}
 }
 if(!visible){Notice().Title(winchisel::ui::tr(L"No matching apps"));Notice().Message(winchisel::ui::tr(L"Change the search or installed-state filter."));Notice().Severity(Controls::InfoBarSeverity::Informational);Notice().IsOpen(true);} update_actions();
}
void DownloadsPage::update_actions(){std::uint32_t count{};for(auto const& list:category_lists_)for(auto const& value:list.SelectedItems())if(auto row=value.try_as<Controls::ListViewItem>()){auto index=unbox_value<std::uint64_t>(row.Tag());if(index<catalog_.size()&&!catalog_[index].winget_ids.empty())++count;}InstallButton().IsEnabled(count&&operation_==Operation::none);InstallButton().Content(box_value(count?winrt::hstring{std::wstring(winchisel::ui::tr(L"Install"))+L" ("+std::to_wstring(count)+L")"}:winchisel::ui::tr(L"Install")));}
fire_and_forget DownloadsPage::confirm_install(){
    auto error_lifetime=get_strong();
    auto error_queue=DispatcherQueue();
    auto error_weak=get_weak();
    try {
    winchisel::core::DialogSlot dialog_slot; if(!winchisel::ui::dialog_available(dialog_slot))co_return;

auto lifetime=get_strong();std::uint32_t count{};for(auto const& list:category_lists_)for(auto const& value:list.SelectedItems())if(auto row=value.try_as<Controls::ListViewItem>()){auto index=unbox_value<std::uint64_t>(row.Tag());if(index<catalog_.size()&&!catalog_[index].winget_ids.empty())++count;}if(!count)co_return;Controls::ContentDialog dialog;dialog.XamlRoot(XamlRoot());dialog.Title(box_value(L"Install selected apps?"));dialog.Content(box_value(to_hstring(std::to_string(count)+" selected apps will be installed with winget.")));dialog.PrimaryButtonText(L"Install");dialog.CloseButtonText(L"Cancel");dialog.DefaultButton(Controls::ContentDialogButton::Close);if(co_await dialog.ShowAsync()==Controls::ContentDialogResult::Primary)start_install();
    } catch (...) {
        winchisel::ui::report_async_error(error_queue, [error_weak](winrt::hstring const& text) {
            if (auto self=error_weak.get()) { self->operation_=Operation::none; self->timer_.Stop(); self->Loading().Visibility(Visibility::Collapsed); self->Groups().IsHitTestVisible(true); self->Groups().Opacity(1.0); self->RefreshButton().IsEnabled(true); self->update_actions(); self->Notice().Title(L"Operation failed"); self->Notice().Message(text); self->Notice().Severity(Controls::InfoBarSeverity::Error); self->Notice().IsOpen(true); }
        });
    }
}
void DownloadsPage::start_install(){
    auto error_lifetime=get_strong();
    auto error_queue=DispatcherQueue();
    auto error_weak=get_weak();
    try {
std::vector<winchisel::core::DownloadCatalogEntry const*> selected;for(auto const& list:category_lists_)for(auto const& value:list.SelectedItems())if(auto row=value.try_as<Controls::ListViewItem>()){auto index=unbox_value<std::uint64_t>(row.Tag());if(index<catalog_.size()&&!catalog_[index].winget_ids.empty())selected.push_back(&catalog_[index]);}if(selected.empty())return;operation_=Operation::install;Loading().Visibility(Visibility::Visible);Groups().IsHitTestVisible(false);Groups().Opacity(0.6);RefreshButton().IsEnabled(false);InstallButton().IsEnabled(false);Notice().IsOpen(false);install_worker_=std::async(std::launch::async,[selected=std::move(selected)]{return winchisel::platform::install_downloads(selected);});timer_.Start();
    } catch (...) {
        winchisel::ui::report_async_error(error_queue, [error_weak](winrt::hstring const& text) {
            if (auto self=error_weak.get()) { self->operation_=Operation::none; self->timer_.Stop(); self->Loading().Visibility(Visibility::Collapsed); self->Groups().IsHitTestVisible(true); self->Groups().Opacity(1.0); self->RefreshButton().IsEnabled(true); self->update_actions(); self->Notice().Title(L"Operation failed"); self->Notice().Message(text); self->Notice().Severity(Controls::InfoBarSeverity::Error); self->Notice().IsOpen(true); }
        });
    }
}
void DownloadsPage::Refresh_Click(IInspectable const&,RoutedEventArgs const&){if(ui_ready_)start_scan(true);} void DownloadsPage::Install_Click(IInspectable const&,RoutedEventArgs const&){if(ui_ready_)confirm_install();} void DownloadsPage::Search_TextChanged(IInspectable const&,Controls::AutoSuggestBoxTextChangedEventArgs const&){if(ui_ready_&&operation_==Operation::none){search_timer_.Stop();search_timer_.Start();}} void DownloadsPage::Filter_SelectionChanged(IInspectable const&,Controls::SelectionChangedEventArgs const&){if(ui_ready_&&operation_==Operation::none)render_items();} void DownloadsPage::Items_SelectionChanged(IInspectable const&,Controls::SelectionChangedEventArgs const&){if(ui_ready_)update_actions();}
void DownloadsPage::Website_Click(IInspectable const& sender,RoutedEventArgs const&){if(auto button=sender.try_as<Controls::HyperlinkButton>()){auto url=unbox_value<hstring>(button.Tag());if(auto result=winchisel::platform::open_https_url(std::wstring(url));!result){Notice().Title(L"Could not open website");Notice().Message(to_hstring(result.error().detail));Notice().Severity(Controls::InfoBarSeverity::Error);Notice().IsOpen(true);}}}
}  // namespace winrt::Winchisel::implementation
