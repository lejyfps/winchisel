#include "winchisel/core/revert.hpp"

#include <cctype>
#include <cstdint>
#include <memory>
#include <utility>

namespace winchisel::core {
namespace {

constexpr int k_max_json_depth = 16;
constexpr std::size_t k_max_json_nodes = 100000;

void append_escaped(std::string& out, std::string_view text) {
    out.push_back('"');
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
    out.push_back('"');
}

void append_hex(std::string& out, std::vector<std::uint8_t> const& bytes) {
    constexpr char digits[] = "0123456789abcdef";
    for (const auto byte : bytes) {
        out.push_back(digits[byte >> 4]);
        out.push_back(digits[byte & 0xF]);
    }
}

struct JsonValue {
    enum class Type { null, boolean, integer, string, array, object };
    Type type{Type::null};
    bool boolean{};
    long long integer{};
    std::string str;
    std::vector<std::unique_ptr<JsonValue>> items;
    std::vector<std::pair<std::string, std::unique_ptr<JsonValue>>> fields;
};

class JsonParser {
public:
    explicit JsonParser(std::string_view text) : text_(text) {}
    std::unique_ptr<JsonValue> parse() {
        auto value = parse_value(0);
        if (!value) return nullptr;
        skip();
        if (position_ != text_.size()) return nullptr;
        return value;
    }

private:
    void skip() {
        while (position_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[position_]))) ++position_;
    }
    bool take(char c) {
        skip();
        if (position_ >= text_.size() || text_[position_] != c) return false;
        ++position_;
        return true;
    }
    std::unique_ptr<JsonValue> alloc(JsonValue::Type type, int depth) {
        if (depth > k_max_json_depth || nodes_ >= k_max_json_nodes) return nullptr;
        ++nodes_;
        auto node = std::make_unique<JsonValue>();
        node->type = type;
        return node;
    }
    bool parse_hex4(std::uint32_t& value) {
        if (position_ + 4 > text_.size()) return false;
        value = 0;
        for (int i{}; i < 4; ++i) {
            const char c = text_[position_];
            value <<= 4;
            if (c >= '0' && c <= '9') value |= static_cast<std::uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') value |= static_cast<std::uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') value |= static_cast<std::uint32_t>(c - 'A' + 10);
            else return false;
            ++position_;
        }
        return true;
    }
    void append_utf8(std::string& out, std::uint32_t code) {
        if (code < 0x80) {
            out.push_back(static_cast<char>(code));
        } else if (code < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (code >> 6)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xE0 | (code >> 12)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        }
    }
    bool parse_string_into(std::string& out) {
        if (!take('"')) return false;
        while (position_ < text_.size()) {
            const unsigned char c = static_cast<unsigned char>(text_[position_++]);
            if (c == '"') return true;
            if (c == '\\') {
                if (position_ >= text_.size()) return false;
                const char escaped = text_[position_++];
                switch (escaped) {
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/': out.push_back('/'); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    case 'u': {
                        std::uint32_t code{};
                        if (!parse_hex4(code)) return false;
                        if (code >= 0xD800 && code <= 0xDBFF) {
                            if (position_ + 6 > text_.size() || text_[position_] != '\\' ||
                                text_[position_ + 1] != 'u')
                                return false;
                            position_ += 2;
                            std::uint32_t low{};
                            if (!parse_hex4(low) || low < 0xDC00 || low > 0xDFFF) return false;
                            code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                            out.push_back(static_cast<char>(0xF0 | (code >> 18)));
                            out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
                            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                        } else if (code >= 0xDC00 && code <= 0xDFFF) {
                            return false;
                        } else {
                            append_utf8(out, code);
                        }
                        break;
                    }
                    default: return false;
                }
                continue;
            }
            if (c < 0x20) return false;
            out.push_back(static_cast<char>(c));
        }
        return false;
    }
    std::unique_ptr<JsonValue> parse_value(int depth) {
        skip();
        if (position_ >= text_.size()) return nullptr;
        const char c = text_[position_];
        if (c == '"') {
            auto node = alloc(JsonValue::Type::string, depth);
            if (!node || !parse_string_into(node->str)) return nullptr;
            return node;
        }
        if (c == '{') {
            auto node = alloc(JsonValue::Type::object, depth);
            if (!node) return nullptr;
            ++position_;
            skip();
            if (position_ < text_.size() && text_[position_] == '}') {
                ++position_;
                return node;
            }
            for (;;) {
                std::string key;
                if (!parse_string_into(key)) return nullptr;
                if (!take(':')) return nullptr;
                auto value = parse_value(depth + 1);
                if (!value) return nullptr;
                node->fields.emplace_back(std::move(key), std::move(value));
                skip();
                if (position_ >= text_.size()) return nullptr;
                if (text_[position_] == '}') {
                    ++position_;
                    return node;
                }
                if (text_[position_] != ',') return nullptr;
                ++position_;
            }
        }
        if (c == '[') {
            auto node = alloc(JsonValue::Type::array, depth);
            if (!node) return nullptr;
            ++position_;
            skip();
            if (position_ < text_.size() && text_[position_] == ']') {
                ++position_;
                return node;
            }
            for (;;) {
                auto value = parse_value(depth + 1);
                if (!value) return nullptr;
                node->items.push_back(std::move(value));
                skip();
                if (position_ >= text_.size()) return nullptr;
                if (text_[position_] == ']') {
                    ++position_;
                    return node;
                }
                if (text_[position_] != ',') return nullptr;
                ++position_;
            }
        }
        if (c == 't') {
            if (!text_.substr(position_).starts_with("true")) return nullptr;
            position_ += 4;
            auto node = alloc(JsonValue::Type::boolean, depth);
            if (!node) return nullptr;
            node->boolean = true;
            return node;
        }
        if (c == 'f') {
            if (!text_.substr(position_).starts_with("false")) return nullptr;
            position_ += 5;
            auto node = alloc(JsonValue::Type::boolean, depth);
            if (!node) return nullptr;
            return node;
        }
        if (c == 'n') {
            if (!text_.substr(position_).starts_with("null")) return nullptr;
            position_ += 4;
            return alloc(JsonValue::Type::null, depth);
        }
        if (c == '-' || (c >= '0' && c <= '9')) {
            const auto begin = position_;
            if (c == '-') ++position_;
            const auto digits = position_;
            while (position_ < text_.size() && text_[position_] >= '0' && text_[position_] <= '9') ++position_;
            if (digits == position_ || position_ - begin > 19) return nullptr;
            long long value{};
            for (auto i = begin; i != position_; ++i) {
                const char d = text_[i];
                if (d == '-') continue;
                value = value * 10 + (d - '0');
            }
            auto node = alloc(JsonValue::Type::integer, depth);
            if (!node) return nullptr;
            node->integer = c == '-' ? -value : value;
            return node;
        }
        return nullptr;
    }
    std::string_view text_;
    std::size_t position_{};
    std::size_t nodes_{};
};

JsonValue const* find_field(JsonValue const& object, std::string_view key) {
    if (object.type != JsonValue::Type::object) return nullptr;
    for (auto const& [name, value] : object.fields) {
        if (name == key) return value.get();
    }
    return nullptr;
}
bool as_string(JsonValue const* node, std::string& out) {
    if (!node || node->type != JsonValue::Type::string) return false;
    out = node->str;
    return true;
}
bool as_bool(JsonValue const* node, bool& out) {
    if (!node || node->type != JsonValue::Type::boolean) return false;
    out = node->boolean;
    return true;
}
bool as_int(JsonValue const* node, long long& out) {
    if (!node || node->type != JsonValue::Type::integer) return false;
    out = node->integer;
    return true;
}
bool as_bytes(JsonValue const* node, std::vector<std::uint8_t>& out) {
    std::string hex;
    if (!as_string(node, hex) || hex.size() > 65536 || hex.size() % 2 != 0) return false;
    out.clear();
    out.reserve(hex.size() / 2);
    for (std::size_t i{}; i < hex.size(); i += 2) {
        auto nibble = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        const int high = nibble(hex[i]);
        const int low = nibble(hex[i + 1]);
        if (high < 0 || low < 0) return false;
        out.push_back(static_cast<std::uint8_t>((high << 4) | low));
    }
    return true;
}

bool convert_registry(JsonValue const& node, RevertRegistryStep& step) {
    long long hive{}, type{};
    bool missing{};
    if (!as_int(find_field(node, "hive"), hive) || hive < 0 || hive > 1) return false;
    if (!as_string(find_field(node, "key"), step.target.key_path) || step.target.key_path.empty() ||
        step.target.key_path.size() > 1024)
        return false;
    if (!as_string(find_field(node, "name"), step.target.value_name) || step.target.value_name.size() > 256)
        return false;
    if (!as_bool(find_field(node, "missing"), missing)) return false;
    if (!as_int(find_field(node, "type"), type) || type < 0 || type > 11) return false;
    if (!as_bytes(find_field(node, "data"), step.before.data)) return false;
    step.target.hive = hive == 0 ? RegistryHive::current_user : RegistryHive::local_machine;
    step.target.type = RegistryValueType::dword;
    step.before.missing = missing;
    step.before.type = static_cast<std::uint32_t>(type);
    return true;
}

bool convert_startup(JsonValue const& node, RevertStartupStep& step) {
    auto const* entry = find_field(node, "entry");
    if (!entry || entry->type != JsonValue::Type::object) return false;
    long long location{}, task_state{};
    if (!as_string(find_field(*entry, "id"), step.entry.id) || step.entry.id.size() > 512) return false;
    if (!as_string(find_field(*entry, "name"), step.entry.name) || step.entry.name.size() > 512) return false;
    if (!as_string(find_field(*entry, "command"), step.entry.command) || step.entry.command.size() > 4096)
        return false;
    if (!as_string(find_field(*entry, "detail"), step.entry.detail) || step.entry.detail.size() > 4096)
        return false;
    if (!as_string(find_field(*entry, "key"), step.entry.key) || step.entry.key.size() > 1024) return false;
    if (!as_int(find_field(*entry, "location"), location) || location < 0 || location > 9) return false;
    if (!as_bool(find_field(*entry, "enabled"), step.entry.enabled)) return false;
    if (!as_string(find_field(*entry, "publisher"), step.entry.publisher) || step.entry.publisher.size() > 512)
        return false;
    if (!as_int(find_field(*entry, "task_state"), task_state) || task_state < -1 || task_state > 4) return false;
    if (!as_string(find_field(*entry, "file_path"), step.entry.file_path) || step.entry.file_path.size() > 1024)
        return false;
    step.entry.location = static_cast<StartupLocation>(location);
    step.entry.task_state = static_cast<int>(task_state);
    return as_bool(find_field(node, "was"), step.was_enabled);
}

bool convert_entry(JsonValue const& node, RevertEntry& entry) {
    if (node.type != JsonValue::Type::object) return false;
    if (!as_string(find_field(node, "key"), entry.key) || entry.key.empty() || entry.key.size() > 128)
        return false;
    if (!as_string(find_field(node, "timestamp"), entry.timestamp) || entry.timestamp.size() > 64) return false;
    if (!as_string(find_field(node, "page"), entry.page) || entry.page.empty() || entry.page.size() > 64)
        return false;
    if (!as_string(find_field(node, "label"), entry.label) || entry.label.empty() || entry.label.size() > 512)
        return false;
    auto array = [&](char const* name) -> JsonValue const* {
        auto const* field = find_field(node, name);
        if (!field) return nullptr;
        return field->type == JsonValue::Type::array ? field : nullptr;
    };
    if (auto const* registry = array("registry")) {
        for (auto const& item : registry->items) {
            RevertRegistryStep step;
            if (!convert_registry(*item, step)) return false;
            entry.registry.push_back(std::move(step));
        }
    }
    if (auto const* tasks = array("tasks")) {
        for (auto const& item : tasks->items) {
            RevertTaskStep step;
            if (!as_string(find_field(*item, "id"), step.id) || step.id.empty() || step.id.size() > 256)
                return false;
            if (!as_bool(find_field(*item, "was"), step.was_enabled)) return false;
            entry.tasks.push_back(std::move(step));
        }
    }
    if (auto const* startups = array("startups")) {
        for (auto const& item : startups->items) {
            RevertStartupStep step;
            if (!convert_startup(*item, step)) return false;
            entry.startups.push_back(std::move(step));
        }
    }
    if (auto const* toggles = array("toggles")) {
        for (auto const& item : toggles->items) {
            RevertToggleStep step;
            if (!as_string(find_field(*item, "domain"), step.domain) || step.domain.empty() ||
                step.domain.size() > 32)
                return false;
            if (!as_string(find_field(*item, "id"), step.id) || step.id.empty() || step.id.size() > 256)
                return false;
            if (!as_bool(find_field(*item, "was"), step.was_enabled)) return false;
            entry.toggles.push_back(std::move(step));
        }
    }
    if (auto const* ints = array("ints")) {
        for (auto const& item : ints->items) {
            RevertIntStep step;
            long long was{};
            if (!as_string(find_field(*item, "domain"), step.domain) || step.domain.empty() ||
                step.domain.size() > 32)
                return false;
            if (!as_string(find_field(*item, "id"), step.id) || step.id.empty() || step.id.size() > 256)
                return false;
            if (!as_int(find_field(*item, "was"), was) || was < -1000000 || was > 1000000) return false;
            step.was_value = static_cast<int>(was);
            entry.ints.push_back(std::move(step));
        }
    }
    if (auto const* power = find_field(node, "power")) {
        if (power->type != JsonValue::Type::object) return false;
        RevertPowerStep step;
        if (!as_string(find_field(*power, "guid"), step.scheme_guid) || step.scheme_guid.empty() ||
            step.scheme_guid.size() > 64)
            return false;
        entry.power = std::move(step);
    }
    return !revert_entry_empty(entry);
}

}  // namespace

std::string serialize_revert_journal(std::vector<RevertEntry> const& entries) {
    std::string out = R"({"version":1,"entries":[)";
    bool first_entry = true;
    for (auto const& entry : entries) {
        if (first_entry) first_entry = false;
        else out.push_back(',');
        out += R"({"key":)";
        append_escaped(out, entry.key);
        out += R"(,"timestamp":)";
        append_escaped(out, entry.timestamp);
        out += R"(,"page":)";
        append_escaped(out, entry.page);
        out += R"(,"label":)";
        append_escaped(out, entry.label);
        if (!entry.registry.empty()) {
            out += R"(,"registry":[)";
            bool first = true;
            for (auto const& step : entry.registry) {
                if (first) first = false;
                else out.push_back(',');
                out += R"({"hive":)";
                out += step.target.hive == RegistryHive::current_user ? "0" : "1";
                out += R"(,"key":)";
                append_escaped(out, step.target.key_path);
                out += R"(,"name":)";
                append_escaped(out, step.target.value_name);
                out += R"(,"missing":)";
                out += step.before.missing ? "true" : "false";
                out += R"(,"type":)";
                out += std::to_string(step.before.type);
                out += R"(,"data":")";
                append_hex(out, step.before.data);
                out += "\"}";
            }
            out.push_back(']');
        }
        if (!entry.tasks.empty()) {
            out += R"(,"tasks":[)";
            bool first = true;
            for (auto const& step : entry.tasks) {
                if (first) first = false;
                else out.push_back(',');
                out += R"({"id":)";
                append_escaped(out, step.id);
                out += R"(,"was":)";
                out += step.was_enabled ? "true}" : "false}";
            }
            out.push_back(']');
        }
        if (!entry.startups.empty()) {
            out += R"(,"startups":[)";
            bool first = true;
            for (auto const& step : entry.startups) {
                if (first) first = false;
                else out.push_back(',');
                out += R"({"entry":{"id":)";
                append_escaped(out, step.entry.id);
                out += R"(,"name":)";
                append_escaped(out, step.entry.name);
                out += R"(,"command":)";
                append_escaped(out, step.entry.command);
                out += R"(,"detail":)";
                append_escaped(out, step.entry.detail);
                out += R"(,"key":)";
                append_escaped(out, step.entry.key);
                out += R"(,"location":)";
                out += std::to_string(static_cast<int>(step.entry.location));
                out += R"(,"enabled":)";
                out += step.entry.enabled ? "true" : "false";
                out += R"(,"publisher":)";
                append_escaped(out, step.entry.publisher);
                out += R"(,"task_state":)";
                out += std::to_string(step.entry.task_state);
                out += R"(,"file_path":)";
                append_escaped(out, step.entry.file_path);
                out += R"(},"was":)";
                out += step.was_enabled ? "true}" : "false}";
            }
            out.push_back(']');
        }
        if (!entry.toggles.empty()) {
            out += R"(,"toggles":[)";
            bool first = true;
            for (auto const& step : entry.toggles) {
                if (first) first = false;
                else out.push_back(',');
                out += R"({"domain":)";
                append_escaped(out, step.domain);
                out += R"(,"id":)";
                append_escaped(out, step.id);
                out += R"(,"was":)";
                out += step.was_enabled ? "true}" : "false}";
            }
            out.push_back(']');
        }
        if (!entry.ints.empty()) {
            out += R"(,"ints":[)";
            bool first = true;
            for (auto const& step : entry.ints) {
                if (first) first = false;
                else out.push_back(',');
                out += R"({"domain":)";
                append_escaped(out, step.domain);
                out += R"(,"id":)";
                append_escaped(out, step.id);
                out += R"(,"was":)";
                out += std::to_string(step.was_value);
                out += "}";
            }
            out.push_back(']');
        }
        if (entry.power) {
            out += R"(,"power":{"guid":)";
            append_escaped(out, entry.power->scheme_guid);
            out += "}";
        }
        out.push_back('}');
    }
    out += "]}";
    return out;
}

std::vector<RevertEntry> parse_revert_journal(std::string_view text) {
    std::vector<RevertEntry> entries;
    if (text.empty() || text.size() > 1024 * 1024) return entries;
    JsonParser parser(text);
    auto root = parser.parse();
    if (!root || root->type != JsonValue::Type::object) return entries;
    auto const* list = find_field(*root, "entries");
    if (!list || list->type != JsonValue::Type::array) return entries;
    for (auto const& item : list->items) {
        RevertEntry entry;
        if (convert_entry(*item, entry)) entries.push_back(std::move(entry));
    }
    return entries;
}

}  // namespace winchisel::core
