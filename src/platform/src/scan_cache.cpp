#include "scan_cache.hpp"

#include <shlobj.h>

#include <cstdint>
#include <fstream>
#include <iterator>
#include <mutex>
#include <sstream>

namespace winchisel::platform::persistent_cache {
namespace {

constexpr int k_format = 1;
std::mutex g_mutex;

std::filesystem::path cache_path() {
    PWSTR raw{};
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &raw)) || !raw) return {};
    std::filesystem::path dir = raw;
    CoTaskMemFree(raw);
    return dir / L"Winchisel" / L"cache" / L"scan-cache.json";
}

// ---- Minimal JSON (objects, arrays, strings, integers) for our own file.
struct Value {
    enum class Kind { null, number, string, array, object } kind{Kind::null};
    std::int64_t number{};
    std::string text;
    std::vector<Value> items;
    std::vector<std::pair<std::string, Value>> members;
    Value const* get(std::string_view key) const {
        for (auto const& [k, v] : members) if (k == key) return &v;
        return nullptr;
    }
};

class Parser {
public:
    explicit Parser(std::string_view text) : text_(text) {}
    std::optional<Value> parse() {
        auto value = parse_value(0);
        skip();
        if (!value || pos_ != text_.size()) return std::nullopt;
        return value;
    }

private:
    void skip() { while (pos_ < text_.size() && (text_[pos_] == ' ' || text_[pos_] == '\n' || text_[pos_] == '\r' || text_[pos_] == '\t')) ++pos_; }
    bool eat(char c) { skip(); if (pos_ < text_.size() && text_[pos_] == c) { ++pos_; return true; } return false; }
    std::optional<std::string> parse_string() {
        if (!eat('"')) return std::nullopt;
        std::string out;
        while (pos_ < text_.size()) {
            const char c = text_[pos_++];
            if (c == '"') return out;
            if (c != '\\') { out += c; continue; }
            if (pos_ >= text_.size()) return std::nullopt;
            const char e = text_[pos_++];
            switch (e) {
                case '"': case '\\': case '/': out += e; break;
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                case 'u': {
                    // Only \u00XX is ever written (control characters).
                    if (pos_ + 4 > text_.size()) return std::nullopt;
                    unsigned value{};
                    for (int i = 0; i < 4; ++i) {
                        const char h = text_[pos_++];
                        value <<= 4;
                        if (h >= '0' && h <= '9') value |= static_cast<unsigned>(h - '0');
                        else if (h >= 'a' && h <= 'f') value |= static_cast<unsigned>(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') value |= static_cast<unsigned>(h - 'A' + 10);
                        else return std::nullopt;
                    }
                    if (value > 0x7F) return std::nullopt;
                    out += static_cast<char>(value);
                    break;
                }
                default: return std::nullopt;
            }
        }
        return std::nullopt;
    }
    std::optional<Value> parse_value(int depth) {
        if (depth > 16) return std::nullopt;
        skip();
        if (pos_ >= text_.size()) return std::nullopt;
        Value value;
        const char c = text_[pos_];
        if (c == '"') {
            auto s = parse_string();
            if (!s) return std::nullopt;
            value.kind = Value::Kind::string;
            value.text = std::move(*s);
        } else if (c == '[') {
            ++pos_;
            value.kind = Value::Kind::array;
            if (eat(']')) return value;
            do {
                auto item = parse_value(depth + 1);
                if (!item) return std::nullopt;
                value.items.push_back(std::move(*item));
            } while (eat(','));
            if (!eat(']')) return std::nullopt;
        } else if (c == '{') {
            ++pos_;
            value.kind = Value::Kind::object;
            if (eat('}')) return value;
            do {
                auto key = parse_string();
                if (!key || !eat(':')) return std::nullopt;
                auto member = parse_value(depth + 1);
                if (!member) return std::nullopt;
                value.members.emplace_back(std::move(*key), std::move(*member));
            } while (eat(','));
            if (!eat('}')) return std::nullopt;
        } else if (c == '-' || (c >= '0' && c <= '9')) {
            const auto start = pos_;
            if (c == '-') ++pos_;
            while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') ++pos_;
            value.kind = Value::Kind::number;
            try { value.number = std::stoll(std::string(text_.substr(start, pos_ - start))); } catch (...) { return std::nullopt; }
        } else {
            return std::nullopt;
        }
        return value;
    }
    std::string_view text_;
    std::size_t pos_{};
};

void write_string(std::ostream& out, std::string_view text) {
    out << '"';
    for (const char c : text) {
        switch (c) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    static constexpr char hex[] = "0123456789abcdef";
                    out << "\\u00" << hex[(c >> 4) & 0xF] << hex[c & 0xF];
                } else {
                    out << c;
                }
        }
    }
    out << '"';
}

std::int64_t now_seconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

// Whole file as parsed entries; unusable files read as empty.
std::vector<std::pair<std::string, Value>> read_entries() {
    const auto path = cache_path();
    if (path.empty()) return {};
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (text.size() > 8 * 1024 * 1024) return {};
    auto root = Parser(text).parse();
    if (!root || root->kind != Value::Kind::object) return {};
    auto const* format = root->get("format");
    auto const* entries = root->get("entries");
    if (!format || format->kind != Value::Kind::number || format->number != k_format) return {};
    if (!entries || entries->kind != Value::Kind::object) return {};
    return entries->members;
}

void write_entries(std::vector<std::pair<std::string, Value>> const& entries) {
    const auto path = cache_path();
    if (path.empty()) return;
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    std::ostringstream out;
    out << "{\n  \"format\": " << k_format << ",\n  \"entries\": {";
    bool first_entry = true;
    for (auto const& [name, entry] : entries) {
        out << (first_entry ? "\n    " : ",\n    ");
        first_entry = false;
        write_string(out, name);
        out << ": {\"fingerprint\": ";
        auto const* fingerprint = entry.get("fingerprint");
        write_string(out, fingerprint ? fingerprint->text : std::string{});
        auto const* saved = entry.get("saved");
        out << ", \"saved\": " << (saved ? saved->number : 0) << ", \"lists\": {";
        bool first_list = true;
        if (auto const* lists = entry.get("lists")) {
            for (auto const& [list_name, list] : lists->members) {
                out << (first_list ? "" : ", ");
                first_list = false;
                write_string(out, list_name);
                out << ": [";
                for (std::size_t i = 0; i < list.items.size(); ++i) {
                    if (i) out << ", ";
                    write_string(out, list.items[i].text);
                }
                out << ']';
            }
        }
        out << "}}";
    }
    out << "\n  }\n}\n";
    const auto temp = std::filesystem::path(path.wstring() + L".tmp");
    {
        std::ofstream file(temp, std::ios::binary | std::ios::trunc);
        if (!file) return;
        const auto text = out.str();
        file.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!file) { file.close(); std::filesystem::remove(temp, error); return; }
    }
    if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        std::filesystem::remove(temp, error);
}

std::string filetime_text(FILETIME const& time) {
    return std::to_string((static_cast<std::uint64_t>(time.dwHighDateTime) << 32) | time.dwLowDateTime);
}

}  // namespace

std::optional<Hit> load(std::string_view name, std::string_view fingerprint, std::chrono::seconds refresh_after) {
    std::scoped_lock lock(g_mutex);
    for (auto const& [entry_name, entry] : read_entries()) {
        if (entry_name != name || entry.kind != Value::Kind::object) continue;
        auto const* stored = entry.get("fingerprint");
        auto const* saved = entry.get("saved");
        auto const* lists = entry.get("lists");
        if (!stored || stored->text != fingerprint || !saved || !lists || lists->kind != Value::Kind::object) return std::nullopt;
        Hit hit;
        // A timestamp in the future (clock moved back) also counts as stale.
        const auto age = now_seconds() - saved->number;
        hit.stale = age < 0 || age > refresh_after.count();
        Lists& result = hit.lists;
        for (auto const& [list_name, list] : lists->members) {
            if (list.kind != Value::Kind::array) return std::nullopt;
            auto& target = result[list_name];
            target.reserve(list.items.size());
            for (auto const& item : list.items) {
                if (item.kind != Value::Kind::string) return std::nullopt;
                target.push_back(item.text);
            }
        }
        return hit;
    }
    return std::nullopt;
}

void store(std::string_view name, std::string_view fingerprint, Lists const& lists) {
    std::scoped_lock lock(g_mutex);
    auto entries = read_entries();
    std::erase_if(entries, [&](auto const& entry) { return entry.first == name; });
    Value entry;
    entry.kind = Value::Kind::object;
    Value fp; fp.kind = Value::Kind::string; fp.text = std::string(fingerprint);
    Value saved; saved.kind = Value::Kind::number; saved.number = now_seconds();
    Value list_object; list_object.kind = Value::Kind::object;
    for (auto const& [list_name, items] : lists) {
        Value array; array.kind = Value::Kind::array;
        for (auto const& item : items) { Value s; s.kind = Value::Kind::string; s.text = item; array.items.push_back(std::move(s)); }
        list_object.members.emplace_back(list_name, std::move(array));
    }
    entry.members.emplace_back("fingerprint", std::move(fp));
    entry.members.emplace_back("saved", std::move(saved));
    entry.members.emplace_back("lists", std::move(list_object));
    entries.emplace_back(std::string(name), std::move(entry));
    write_entries(entries);
}

void invalidate(std::string_view name) {
    std::scoped_lock lock(g_mutex);
    auto entries = read_entries();
    const auto before = entries.size();
    std::erase_if(entries, [&](auto const& entry) { return entry.first == name; });
    if (entries.size() != before) write_entries(entries);
}

std::string key_stamp(HKEY root, wchar_t const* path) {
    HKEY key{};
    if (RegOpenKeyExW(root, path, 0, KEY_QUERY_VALUE | KEY_ENUMERATE_SUB_KEYS, &key) != ERROR_SUCCESS) return "-";
    DWORD subkeys{}, values{};
    FILETIME written{};
    const auto status = RegQueryInfoKeyW(key, nullptr, nullptr, nullptr, &subkeys, nullptr, nullptr, &values, nullptr, nullptr, nullptr, &written);
    RegCloseKey(key);
    if (status != ERROR_SUCCESS) return "-";
    return filetime_text(written) + ":" + std::to_string(subkeys) + ":" + std::to_string(values);
}

std::string file_stamp(std::filesystem::path const& path) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) return "-";
    const auto size = (static_cast<std::uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
    return filetime_text(data.ftLastWriteTime) + ":" + std::to_string(size);
}

std::string windows_build_stamp() {
    wchar_t build[32]{};
    DWORD size = sizeof(build);
    DWORD revision{};
    DWORD revision_size = sizeof(revision);
    const wchar_t* key = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
    if (RegGetValueW(HKEY_LOCAL_MACHINE, key, L"CurrentBuildNumber", RRF_RT_REG_SZ, nullptr, build, &size) != ERROR_SUCCESS) build[0] = L'?';
    if (RegGetValueW(HKEY_LOCAL_MACHINE, key, L"UBR", RRF_RT_REG_DWORD, nullptr, &revision, &revision_size) != ERROR_SUCCESS) revision = 0;
    std::string text;
    for (const wchar_t* p = build; *p; ++p) text += static_cast<char>(*p < 128 ? *p : '?');
    return text + "." + std::to_string(revision);
}

}  // namespace winchisel::platform::persistent_cache
