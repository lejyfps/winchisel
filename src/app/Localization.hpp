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

// Fired by SettingsPage when the user opts into nightly updates so the
// newest nightly is offered immediately instead of after a restart.
inline std::function<void()>& update_check() {
    static std::function<void()> handler;
    return handler;
}

// Fired by SettingsPage when the user opens the change history. MainWindow
// owns the dialog (journal UI plus page rebuilds after an undo).
inline std::function<void()>& open_history() {
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
            // The template's presenter mirrors Content through a binding.
            // Do not descend into template parts: pinning a local value there
            // would override the binding and freeze later Content updates
            // (e.g. PowerPlan "Active", Latency "Analyzing...").
            return;
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
        // Title/Message are rendered through template-generated presenters bound
        // to these properties. Do not descend: writing a local value into a
        // template part would override its binding, so later Title/Message
        // updates (e.g. ExtrasPage::show_result "Applied"/"Setting applied.")
        // would no longer reach the screen and the bar would stay empty.
        // InfoBars in this app never host localizable custom Content.
        return;
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
