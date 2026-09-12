#pragma once

#include "winchisel/application/session.hpp"

#include <string>
#include <string_view>

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

}  // namespace winchisel::ui
