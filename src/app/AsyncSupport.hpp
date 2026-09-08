#pragma once
#include "Localization.hpp"
#include "AsyncResult.hpp"
#include "winchisel/core/dialog_slot.hpp"
#include "winchisel/core/error.hpp"
#include "winchisel/platform/system.hpp"
#include <winrt/Microsoft.UI.Dispatching.h>
#include <exception>
#include <future>

namespace winchisel::ui {
template<typename Action>
bool enqueue_safe(winrt::Microsoft::UI::Dispatching::DispatcherQueue const& queue, Action action) noexcept {
    try {
        return queue.TryEnqueue([action = std::move(action)]() mutable noexcept {
            try { action(); }
            catch (...) {
                try { winchisel::platform::boot_log(exception_text().c_str()); } catch (...) {}
            }
        });
    } catch (...) { return false; } // A closed window no longer accepts UI work.
}

template<typename Reset>
void report_async_error(winrt::Microsoft::UI::Dispatching::DispatcherQueue const& queue, Reset reset) noexcept {
    try {
        const auto text = winrt::to_hstring(exception_text());
        enqueue_safe(queue, [reset = std::move(reset), text] {
            reset(text);
            show_toast(winrt::Microsoft::UI::Xaml::Controls::InfoBarSeverity::Error, L"Operation failed", std::wstring(text));
        });
    } catch (...) {}
}

inline bool dialog_available(winchisel::core::DialogSlot const& slot) {
    if (slot) return true;
    show_toast(winrt::Microsoft::UI::Xaml::Controls::InfoBarSeverity::Informational,
        L"Dialog already open", L"Close the current dialog and try again.");
    return false;
}
}
