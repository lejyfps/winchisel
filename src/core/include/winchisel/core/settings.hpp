#pragma once

#include "error.hpp"

#include <filesystem>
#include <string>
#include <string_view>

namespace winchisel::core {

enum class Language { english, german, spanish, french, russian, simplified_chinese, portuguese_brazil, polish, turkish, japanese, korean, italian, dutch, ukrainian, czech, indonesian, vietnamese, arabic, traditional_chinese, thai };
enum class Theme { system, light, dark };

struct Settings {
    bool check_updates_on_startup{true};
    bool show_console{false};
    Language language{Language::english};
    Theme theme{Theme::system};
    bool autostart_enabled{false};
    bool show_risk_badges{true};
};

Settings settings_defaults();
Settings parse_settings_json(std::string_view json);
std::string serialize_settings_json(const Settings& settings);

Language language_from_string(std::string_view s);
std::string_view language_to_string(Language language);
Theme theme_from_string(std::string_view value);
std::string_view theme_to_string(Theme theme);

}  // namespace winchisel::core
