#include "winchisel/core/settings.hpp"

#include <cctype>
#include <sstream>

namespace winchisel::core {
namespace {

class JsonValidator {
public:
    explicit JsonValidator(std::string_view text) : text_(text) {}
    bool valid() { skip(); return value() && (skip(), position_ == text_.size()); }
private:
    void skip() { while (position_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[position_]))) ++position_; }
    bool take(char c) { skip(); if (position_ >= text_.size() || text_[position_] != c) return false; ++position_; return true; }
    bool string() {
        if (!take('"')) return false;
        while (position_ < text_.size()) {
            const unsigned char c = static_cast<unsigned char>(text_[position_++]);
            if (c == '"') return true;
            if (c < 0x20) return false;
            if (c == '\\') {
                if (position_ >= text_.size()) return false;
                const char escaped = text_[position_++];
                if (std::string_view{"\"\\/bfnrt"}.find(escaped) != std::string_view::npos) continue;
                if (escaped != 'u' || position_ + 4 > text_.size()) return false;
                for (int i = 0; i < 4; ++i) if (!std::isxdigit(static_cast<unsigned char>(text_[position_++]))) return false;
            }
        }
        return false;
    }
    bool literal(std::string_view token) {
        skip(); if (!text_.substr(position_).starts_with(token)) return false; position_ += token.size(); return true;
    }
    bool number() {
        skip(); const auto begin = position_;
        if (position_ < text_.size() && text_[position_] == '-') ++position_;
        if (position_ >= text_.size()) return false;
        if (text_[position_] == '0') ++position_;
        else { if (!std::isdigit(static_cast<unsigned char>(text_[position_]))) return false; while (position_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[position_]))) ++position_; }
        if (position_ < text_.size() && text_[position_] == '.') { ++position_; const auto digits = position_; while (position_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[position_]))) ++position_; if (digits == position_) return false; }
        if (position_ < text_.size() && (text_[position_] == 'e' || text_[position_] == 'E')) { ++position_; if (position_ < text_.size() && (text_[position_] == '+' || text_[position_] == '-')) ++position_; const auto digits = position_; while (position_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[position_]))) ++position_; if (digits == position_) return false; }
        return position_ > begin;
    }
    bool array() {
        if (!take('[')) return false; skip(); if (take(']')) return true;
        do { if (!value()) return false; skip(); if (take(']')) return true; } while (take(',')); return false;
    }
    bool object() {
        if (!take('{')) return false; skip(); if (take('}')) return true;
        do { if (!string() || !take(':') || !value()) return false; skip(); if (take('}')) return true; } while (take(',')); return false;
    }
    bool value() {
        skip(); if (position_ >= text_.size()) return false;
        if (text_[position_] == '{') return object(); if (text_[position_] == '[') return array(); if (text_[position_] == '"') return string();
        return literal("true") || literal("false") || literal("null") || number();
    }
    std::string_view text_; std::size_t position_{};
};

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
    if (s == "Spanish" || s == "spanish" || s == "es") return Language::spanish;
    if (s == "French" || s == "french" || s == "fr") return Language::french;
    if (s == "Russian" || s == "russian" || s == "ru") return Language::russian;
    if (s == "Simplified Chinese" || s == "simplified_chinese" || s == "zh-CN") return Language::simplified_chinese;
    if (s == "Portuguese (Brazil)" || s == "pt-BR") return Language::portuguese_brazil;
    if (s == "Polish" || s == "pl") return Language::polish;
    if (s == "Turkish" || s == "tr") return Language::turkish;
    if (s == "Japanese" || s == "ja") return Language::japanese;
    if (s == "Korean" || s == "ko") return Language::korean;
    if (s == "Italian" || s == "it") return Language::italian;
    if (s == "Dutch" || s == "nl") return Language::dutch;
    if (s == "Ukrainian" || s == "uk") return Language::ukrainian;
    if (s == "Czech" || s == "cs") return Language::czech;
    if (s == "Indonesian" || s == "id") return Language::indonesian;
    if (s == "Vietnamese" || s == "vi") return Language::vietnamese;
    return Language::english;
}

std::string_view language_to_string(Language language) {
    switch (language) {
        case Language::german:
            return "German";
        case Language::spanish:
            return "Spanish";
        case Language::french:
            return "French";
        case Language::russian:
            return "Russian";
        case Language::simplified_chinese:
            return "Simplified Chinese";
        case Language::portuguese_brazil: return "Portuguese (Brazil)";
        case Language::polish: return "Polish";
        case Language::turkish: return "Turkish";
        case Language::japanese: return "Japanese";
        case Language::korean: return "Korean";
        case Language::italian: return "Italian";
        case Language::dutch: return "Dutch";
        case Language::ukrainian: return "Ukrainian";
        case Language::czech: return "Czech";
        case Language::indonesian: return "Indonesian";
        case Language::vietnamese: return "Vietnamese";
        case Language::english:
        default:
            return "English";
    }
}

Theme theme_from_string(std::string_view value) {
    if (value == "Light" || value == "light") return Theme::light;
    if (value == "Dark" || value == "dark") return Theme::dark;
    return Theme::system;
}

std::string_view theme_to_string(Theme theme) {
    if (theme == Theme::light) return "Light";
    if (theme == Theme::dark) return "Dark";
    return "System";
}

Settings parse_settings_json(std::string_view json) {
    Settings s = settings_defaults();
    if (trim(json).empty() || !JsonValidator(json).valid()) {
        return s;
    }
    s.check_updates_on_startup = extract_bool(json, "check_updates_on_startup", s.check_updates_on_startup);
    s.show_console = extract_bool(json, "show_console", s.show_console);
    s.autostart_enabled = extract_bool(json, "autostart_enabled", s.autostart_enabled);
    s.language = language_from_string(extract_string(json, "language", language_to_string(s.language)));
    s.theme = theme_from_string(extract_string(json, "theme", theme_to_string(s.theme)));
    return s;
}

std::string serialize_settings_json(const Settings& settings) {
    std::ostringstream out;
    out << "{\n";
    out << "  \"check_updates_on_startup\": " << (settings.check_updates_on_startup ? "true" : "false") << ",\n";
    out << "  \"show_console\": " << (settings.show_console ? "true" : "false") << ",\n";
    out << "  \"language\": \"" << language_to_string(settings.language) << "\",\n";
    out << "  \"theme\": \"" << theme_to_string(settings.theme) << "\",\n";
    out << "  \"autostart_enabled\": " << (settings.autostart_enabled ? "true" : "false") << "\n";
    out << "}\n";
    return out.str();
}

}  // namespace winchisel::core
