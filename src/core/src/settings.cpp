#include "winchisel/core/settings.hpp"

#include <cctype>
#include <sstream>

namespace winchisel::core {
namespace {

std::string_view trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
        s.remove_prefix(1);
    }
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
        s.remove_suffix(1);
    }
    return s;
}

bool extract_bool(std::string_view json, std::string_view key, bool fallback) {
    const std::string needle = "\"" + std::string(key) + "\"";
    const auto pos = json.find(needle);
    if (pos == std::string_view::npos) {
        return fallback;
    }
    const auto colon = json.find(':', pos + needle.size());
    if (colon == std::string_view::npos) {
        return fallback;
    }
    auto rest = trim(json.substr(colon + 1));
    if (rest.starts_with("true")) {
        return true;
    }
    if (rest.starts_with("false")) {
        return false;
    }
    return fallback;
}

std::string extract_string(std::string_view json, std::string_view key, std::string_view fallback) {
    const std::string needle = "\"" + std::string(key) + "\"";
    const auto pos = json.find(needle);
    if (pos == std::string_view::npos) {
        return std::string(fallback);
    }
    const auto colon = json.find(':', pos + needle.size());
    if (colon == std::string_view::npos) {
        return std::string(fallback);
    }
    auto rest = trim(json.substr(colon + 1));
    if (rest.empty() || rest.front() != '"') {
        return std::string(fallback);
    }
    rest.remove_prefix(1);
    const auto end = rest.find('"');
    if (end == std::string_view::npos) {
        return std::string(fallback);
    }
    return std::string(rest.substr(0, end));
}

}  // namespace

Settings settings_defaults() {
    return {};
}

Language language_from_string(std::string_view s) {
    if (s == "German" || s == "german" || s == "de") {
        return Language::german;
    }
    return Language::english;
}

std::string_view language_to_string(Language language) {
    switch (language) {
        case Language::german:
            return "German";
        case Language::english:
        default:
            return "English";
    }
}

Settings parse_settings_json(std::string_view json) {
    Settings s = settings_defaults();
    if (trim(json).empty()) {
        return s;
    }
    s.check_updates_on_startup = extract_bool(json, "check_updates_on_startup", s.check_updates_on_startup);
    s.show_console = extract_bool(json, "show_console", s.show_console);
    s.autostart_enabled = extract_bool(json, "autostart_enabled", s.autostart_enabled);
    s.language = language_from_string(extract_string(json, "language", language_to_string(s.language)));
    return s;
}

std::string serialize_settings_json(const Settings& settings) {
    std::ostringstream out;
    out << "{\n";
    out << "  \"check_updates_on_startup\": " << (settings.check_updates_on_startup ? "true" : "false") << ",\n";
    out << "  \"show_console\": " << (settings.show_console ? "true" : "false") << ",\n";
    out << "  \"language\": \"" << language_to_string(settings.language) << "\",\n";
    out << "  \"autostart_enabled\": " << (settings.autostart_enabled ? "true" : "false") << "\n";
    out << "}\n";
    return out.str();
}

}  // namespace winchisel::core
