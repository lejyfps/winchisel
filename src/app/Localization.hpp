#pragma once

#include "winchisel/core/i18n.hpp"

#include <functional>
#include <string>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Windows.Foundation.h>

namespace winchisel::ui {

inline std::function<void()>& language_reload() {
    static std::function<void()> handler;
    return handler;
}

inline std::function<void()>& theme_reload() {
    static std::function<void()> handler;
    return handler;
}

inline winrt::hstring tr(std::wstring_view english) {
    return winrt::hstring{winchisel::core::loc(english)};
}
inline winrt::hstring tr(wchar_t const* english) {
    return tr(std::wstring_view{english});
}
inline winrt::hstring tr(winrt::hstring const& english) {
    return tr(std::wstring_view{english});
}

inline void localize_tree(winrt::Windows::Foundation::IInspectable const& root) {
    using namespace winrt;
    using namespace Microsoft::UI::Xaml;
    if (!root) return;
    if (auto block = root.try_as<Controls::TextBlock>()) {
        block.Text(hstring{winchisel::core::loc(std::wstring(block.Text()))});
        return;
    }
    if (auto box = root.try_as<Controls::AutoSuggestBox>()) {
        box.PlaceholderText(hstring{winchisel::core::loc(std::wstring(box.PlaceholderText()))});
    }
    if (auto item = root.try_as<Controls::NavigationViewItem>()) {
        if (auto text = item.Content().try_as<hstring>()) {
            item.Content(box_value(hstring{winchisel::core::loc(std::wstring(*text))}));
        }
    }
    if (auto selector = root.try_as<Controls::SelectorBarItem>()) {
        selector.Text(hstring{winchisel::core::loc(std::wstring(selector.Text()))});
    }
    if (auto button = root.try_as<Controls::Button>()) {
        if (auto text = button.Content().try_as<hstring>()) {
            button.Content(box_value(hstring{winchisel::core::loc(std::wstring(*text))}));
        }
    }
    if (auto expander = root.try_as<Controls::Expander>()) {
        if (auto text = expander.Header().try_as<hstring>()) {
            expander.Header(box_value(hstring{winchisel::core::loc(std::wstring(*text))}));
        }
    }
    if (auto combo_item = root.try_as<Controls::ComboBoxItem>()) {
        if (auto text = combo_item.Content().try_as<hstring>()) {
            combo_item.Content(box_value(hstring{winchisel::core::loc(std::wstring(*text))}));
        }
    }
    if (auto bar = root.try_as<Controls::InfoBar>()) {
        bar.Title(hstring{winchisel::core::loc(std::wstring(bar.Title()))});
        bar.Message(hstring{winchisel::core::loc(std::wstring(bar.Message()))});
    }
    auto element = root.try_as<DependencyObject>();
    if (!element) return;
    const auto count = Media::VisualTreeHelper::GetChildrenCount(element);
    for (int index = 0; index < count; ++index) {
        localize_tree(Media::VisualTreeHelper::GetChild(element, index));
    }
}

using ToastHandler = std::function<void(winrt::Microsoft::UI::Xaml::Controls::InfoBarSeverity, std::wstring, std::wstring)>;

inline ToastHandler& toast_handler() {
    static ToastHandler handler;
    return handler;
}

inline void show_toast(winrt::Microsoft::UI::Xaml::Controls::InfoBarSeverity severity, std::wstring title, std::wstring message) {
    if (toast_handler()) toast_handler()(severity, std::move(title), std::move(message));
}

}  // namespace winchisel::ui
