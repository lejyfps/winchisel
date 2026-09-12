#pragma once

#include "winchisel/application/session.hpp"

#include <string>
#include <string_view>

#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>

namespace winchisel::ui {

// Einmalige Hinweise (TeachingTip): verbrauchte IDs stehen space-separiert
// in Settings.tips_seen, z. B. "cadence_admin bulk_profile".
inline bool teaching_tip_seen(std::string_view id) {
    const auto& seen = winchisel::application::Session::instance().settings().tips_seen;
    for (std::size_t pos = 0; pos < seen.size();) {
        const auto end = seen.find(' ', pos);
        if (seen.compare(pos, end == std::string::npos ? end : end - pos, id) == 0) return true;
        if (end == std::string::npos) break;
        pos = end + 1;
    }
    return false;
}

inline void dismiss_teaching_tip(std::string_view id) {
    if (teaching_tip_seen(id)) return;
    auto settings = winchisel::application::Session::instance().settings();
    if (!settings.tips_seen.empty()) settings.tips_seen += ' ';
    settings.tips_seen += std::string(id);
    (void)winchisel::application::Session::instance().set_settings(settings);
}

// Keep the tip alive in `slot` (locals get destroyed before the popup shows).
// Mark the ID seen on Closed, not before Open — failed show stays retryable.
inline void open_teaching_tip(
    winrt::Microsoft::UI::Xaml::Controls::TeachingTip& slot,
    winrt::event_token& closed,
    winrt::Microsoft::UI::Xaml::FrameworkElement const& target,
    std::string_view id,
    winrt::hstring const& title,
    winrt::hstring const& subtitle) {
    if (teaching_tip_seen(id) || !target) return;
    if (slot) {
        slot.IsOpen(false);
        if (closed) slot.Closed(closed);
        closed = {};
    }
    slot = winrt::Microsoft::UI::Xaml::Controls::TeachingTip();
    slot.Title(title);
    slot.Subtitle(subtitle);
    slot.Target(target);
    slot.PreferredPlacement(winrt::Microsoft::UI::Xaml::Controls::TeachingTipPlacementMode::Bottom);
    slot.IsLightDismissEnabled(true);
    if (auto root = target.XamlRoot()) slot.XamlRoot(root);
    closed = slot.Closed([id = std::string(id)](auto&&, auto&&) { dismiss_teaching_tip(id); });
    slot.IsOpen(true);
}

}  // namespace winchisel::ui
