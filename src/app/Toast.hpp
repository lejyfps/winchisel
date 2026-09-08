#pragma once

#include <functional>
#include <string>

#include <winrt/Microsoft.UI.Xaml.Controls.h>

namespace winchisel::ui {

using ToastHandler = std::function<void(winrt::Microsoft::UI::Xaml::Controls::InfoBarSeverity, std::wstring, std::wstring)>;

inline ToastHandler& toast_handler() {
    static ToastHandler handler;
    return handler;
}

inline void show_toast(winrt::Microsoft::UI::Xaml::Controls::InfoBarSeverity severity, std::wstring title, std::wstring message) {
    if (toast_handler()) toast_handler()(severity, std::move(title), std::move(message));
}

}  // namespace winchisel::ui
