#include "winchisel/core/settings.hpp"

#include <cctype>
#include <optional>
#include <sstream>

namespace winchisel::core {
namespace {

constexpr std::size_t k_max_settings_bytes = 65536;
constexpr int k_max_json_depth = 8;

class JsonValidator {
public:
    explicit JsonValidator(std::string_view text) : text_(text) {}
    bool valid() { skip(); return value(0) && (skip(), position_ == text_.size()); }
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
    bool array(int depth) {
        if (depth > k_max_json_depth) return false;
        if (!take('[')) return false; skip(); if (take(']')) return true;
        do { if (!value(depth + 1)) return false; skip(); if (take(']')) return true; } while (take(',')); return false;
    }
    bool object(int depth) {
        if (depth > k_max_json_depth) return false;
        if (!take('{')) return false; skip(); if (take('}')) return true;
        do { if (!string() || !take(':') || !value(depth + 1)) return false; skip(); if (take('}')) return true; } while (take(',')); return false;
    }
    bool value(int depth) {
        if (depth > k_max_json_depth) return false;
        skip(); if (position_ >= text_.size()) return false;
        if (text_[position_] == '{') return object(depth); if (text_[position_] == '[') return array(depth); if (text_[position_] == '"') return string();
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

std::optional<std::string> unescape_json_string(std::string_view body) {
    std::string out;
    for (std::size_t i{}; i < body.size(); ++i) {
        if (body[i] != '\\') { out.push_back(body[i]); continue; }
        if (i + 1 >= body.size()) return std::nullopt;
        const char e = body[++i];
        switch (e) {
        case '"': case '\\': case '/': out.push_back(e); break;
        case 'b': out.push_back('\b'); break;
        case 'f': out.push_back('\f'); break;
        case 'n': out.push_back('\n'); break;
        case 'r': out.push_back('\r'); break;
        case 't': out.push_back('\t'); break;
        case 'u': {
            if (i + 4 >= body.size()) return std::nullopt;
            unsigned code{};
            for (int n{}; n < 4; ++n) {
                const auto c = static_cast<unsigned char>(body[++i]);
                code <<= 4;
                if (c >= '0' && c <= '9') code += c - '0';
                else if (c >= 'a' && c <= 'f') code += 10 + c - 'a';
                else if (c >= 'A' && c <= 'F') code += 10 + c - 'A';
                else return std::nullopt;
            }
            if (code >= 0xD800 && code <= 0xDBFF) {
                if (i + 6 >= body.size() || body[i + 1] != '\\' || body[i + 2] != 'u') return std::nullopt;
                unsigned low{};
                for (int n{}; n < 4; ++n) {
                    const auto c = static_cast<unsigned char>(body[i + 3 + n]);
                    low <<= 4;
                    if (c >= '0' && c <= '9') low += c - '0';
                    else if (c >= 'a' && c <= 'f') low += 10 + c - 'a';
                    else if (c >= 'A' && c <= 'F') low += 10 + c - 'A';
                    else return std::nullopt;
                }
                if (low < 0xDC00 || low > 0xDFFF) return std::nullopt;
                code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                i += 6;
            } else if (code >= 0xDC00 && code <= 0xDFFF) {
                return std::nullopt;
            }
            if (code < 0x80) out.push_back(static_cast<char>(code));
            else if (code < 0x800) { out.push_back(static_cast<char>(0xC0 | (code >> 6))); out.push_back(static_cast<char>(0x80 | (code & 0x3F))); }
            else if (code < 0x10000) { out.push_back(static_cast<char>(0xE0 | (code >> 12))); out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F))); out.push_back(static_cast<char>(0x80 | (code & 0x3F))); }
            else { out.push_back(static_cast<char>(0xF0 | (code >> 18))); out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F))); out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F))); out.push_back(static_cast<char>(0x80 | (code & 0x3F))); }
            break;
        }
        default: return std::nullopt;
        }
    }
    return out;
}

bool parse_root_object(std::string_view json, Settings& settings) {
    auto text = trim(json);
    if (text.empty() || text.front() != '{') return false;
    text.remove_prefix(1);
    while (true) {
        text = trim(text);
        if (text.empty()) return false;
        if (text.front() == '}') return true;
        if (text.front() != '"') return false;
        text.remove_prefix(1);
        std::size_t key_end{};
        bool escaped{};
        for (; key_end < text.size(); ++key_end) {
            if (escaped) { escaped = false; continue; }
            if (text[key_end] == '\\') { escaped = true; continue; }
            if (text[key_end] == '"') break;
        }
        if (key_end >= text.size()) return false;
        auto key = unescape_json_string(text.substr(0, key_end));
        if (!key) return false;
        text.remove_prefix(key_end + 1);
        text = trim(text);
        if (text.empty() || text.front() != ':') return false;
        text.remove_prefix(1);
        text = trim(text);
        if (text.empty()) return false;
        if (*key == "show_console" || *key == "check_updates_on_startup" || *key == "nightly_updates" || *key == "poll_for_updates" || *key == "autostart_enabled" || *key == "show_risk_badges" || *key == "show_state_badges") {
            bool value{};
            if (text.starts_with("true")) { value = true; text.remove_prefix(4); }
            else if (text.starts_with("false")) { value = false; text.remove_prefix(5); }
            else return false;
            if (*key == "show_console") settings.show_console = value;
            else if (*key == "check_updates_on_startup") settings.check_updates_on_startup = value;
            else if (*key == "nightly_updates") settings.nightly_updates = value;
            else if (*key == "poll_for_updates") settings.poll_for_updates = value;
            else if (*key == "show_risk_badges") settings.show_risk_badges = value;
            else if (*key == "show_state_badges") settings.show_state_badges = value;
            else settings.autostart_enabled = value;
        } else if (*key == "language" || *key == "theme" || *key == "dismissed_update_version") {
            if (text.front() != '"') return false;
            text.remove_prefix(1);
            std::size_t end{}; bool esc{};
            for (; end < text.size(); ++end) {
                if (esc) { esc = false; continue; }
                if (text[end] == '\\') { esc = true; continue; }
                if (text[end] == '"') break;
            }
            if (end >= text.size()) return false;
            auto value = unescape_json_string(text.substr(0, end));
            if (!value) return false;
            if (*key == "language") settings.language = language_from_string(*value);
            else if (*key == "theme") settings.theme = theme_from_string(*value);
            else settings.dismissed_update_version = std::string(*value);
            text.remove_prefix(end + 1);
        } else {
            if (text.front() == '{') {
                int depth = 1; text.remove_prefix(1);
                while (!text.empty() && depth) {
                    if (text.front() == '{') ++depth;
                    else if (text.front() == '}') --depth;
                    else if (text.front() == '"') {
                        text.remove_prefix(1);
                        bool esc{};
                        while (!text.empty()) {
                            const char c = text.front(); text.remove_prefix(1);
                            if (esc) { esc = false; continue; }
                            if (c == '\\') { esc = true; continue; }
                            if (c == '"') break;
                        }
                        continue;
                    }
                    if (!text.empty()) text.remove_prefix(1);
                }
            } else if (text.front() == '"') {
                text.remove_prefix(1);
                bool esc{};
                while (!text.empty()) {
                    const char c = text.front(); text.remove_prefix(1);
                    if (esc) { esc = false; continue; }
                    if (c == '\\') { esc = true; continue; }
                    if (c == '"') break;
                }
            } else {
                while (!text.empty() && text.front() != ',' && text.front() != '}') text.remove_prefix(1);
            }
        }
        text = trim(text);
        if (!text.empty() && text.front() == ',') { text.remove_prefix(1); continue; }
        if (!text.empty() && text.front() == '}') return true;
        return false;
    }
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
    if (s == "Arabic" || s == "ar") return Language::arabic;
    if (s == "Traditional Chinese" || s == "traditional_chinese" || s == "zh-TW") return Language::traditional_chinese;
    if (s == "Thai" || s == "th") return Language::thai;
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
        case Language::arabic: return "Arabic";
        case Language::traditional_chinese: return "Traditional Chinese";
        case Language::thai: return "Thai";
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
    if (json.size() > k_max_settings_bytes || trim(json).empty() || !JsonValidator(json).valid()) {
        return s;
    }
    if (!parse_root_object(json, s)) return settings_defaults();
    return s;
}

std::string json_escape(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const unsigned char c : text) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    constexpr char digits[] = "0123456789abcdef";
                    out += "\\u00";
                    out.push_back(digits[c >> 4]);
                    out.push_back(digits[c & 0xF]);
                } else {
                    out.push_back(static_cast<char>(c));
                }
                break;
        }
    }
    return out;
}

std::string serialize_settings_json(const Settings& settings) {
    std::ostringstream out;
    out << "{\n";
    out << "  \"check_updates_on_startup\": " << (settings.check_updates_on_startup ? "true" : "false") << ",\n";
    out << "  \"nightly_updates\": " << (settings.nightly_updates ? "true" : "false") << ",\n";
    out << "  \"poll_for_updates\": " << (settings.poll_for_updates ? "true" : "false") << ",\n";
    out << "  \"dismissed_update_version\": \"" << json_escape(settings.dismissed_update_version) << "\",\n";
    out << "  \"show_console\": " << (settings.show_console ? "true" : "false") << ",\n";
    out << "  \"language\": \"" << language_to_string(settings.language) << "\",\n";
    out << "  \"theme\": \"" << theme_to_string(settings.theme) << "\",\n";
    out << "  \"autostart_enabled\": " << (settings.autostart_enabled ? "true" : "false") << ",\n";
    out << "  \"show_risk_badges\": " << (settings.show_risk_badges ? "true" : "false") << ",\n";
    out << "  \"show_state_badges\": " << (settings.show_state_badges ? "true" : "false") << "\n";
    out << "}\n";
    return out.str();
}

}  // namespace winchisel::core
