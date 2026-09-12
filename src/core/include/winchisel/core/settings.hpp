#pragma once

#include "error.hpp"

#include <filesystem>
#include <string>
#include <string_view>

namespace winchisel::core {

enum class Language { english, german, spanish, french, russian, simplified_chinese, portuguese_brazil, polish, turkish, japanese, korean, italian, dutch, ukrainian, czech, indonesian, vietnamese, arabic, traditional_chinese, thai };
enum class Theme { system, light, dark };
enum class Backdrop { mica, mica_alt, acrylic, solid };

struct Settings {
    bool check_updates_on_startup{true};
    bool nightly_updates{false};
    // Background update poller: silent check every 5 minutes (title bar
    // only, never a dialog). Found updates download automatically.
    bool poll_for_updates{true};
    // Version whose update prompt was dismissed with "Later". Automatic
    // (startup) checks skip it quietly; manual checks still offer it.
    std::string dismissed_update_version;
    bool show_console{false};
    Language language{Language::english};
    Theme theme{Theme::system};
    bool autostart_enabled{false};
    bool show_risk_badges{true};
    bool show_state_badges{true};
    // UI-Animationen (Smooth Scrolling u.a.): Opt-out für ältere Hardware.
    bool smooth_scrolling{true};
    // Fenster-Backdrop (Mica/MicaAlt/Acrylic/Solid), Default = bisheriges Mica.
    Backdrop backdrop{Backdrop::mica};
    // Einmal-Hinweise (TeachingTip): IDs, space-separiert, z. B. "cadence_admin".
    std::string tips_seen;
};

Settings settings_defaults();
Settings parse_settings_json(std::string_view json);
std::string serialize_settings_json(const Settings& settings);

Language language_from_string(std::string_view s);
std::string_view language_to_string(Language language);
Theme theme_from_string(std::string_view value);
std::string_view theme_to_string(Theme theme);
Backdrop backdrop_from_string(std::string_view value);
std::string_view backdrop_to_string(Backdrop backdrop);

}  // namespace winchisel::core
