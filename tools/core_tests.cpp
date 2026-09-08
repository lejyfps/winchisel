#include "winchisel/core/i18n.hpp"
#include "winchisel/core/settings.hpp"

#include <iostream>
#include <string>

int main() {
    using namespace winchisel::core;
    int failed = 0;
    auto expect = [&](bool ok, char const* name) {
        if (!ok) {
            std::cerr << "FAIL " << name << '\n';
            ++failed;
        } else {
            std::cout << "ok " << name << '\n';
        }
    };

    expect(parse_settings_json("{").language == Language::english, "invalid json uses defaults");
    expect(!parse_settings_json(R"({"show_console": trueXYZ})").show_console, "fragment json rejected");
    auto parsed = parse_settings_json(R"({"language":"German","show_console":true,"check_updates_on_startup":false,"autostart_enabled":true})");
    expect(parsed.language == Language::german, "german language");
    expect(parsed.show_console, "show_console true");
    expect(!parsed.check_updates_on_startup, "updates false");
    expect(parsed.autostart_enabled, "autostart true");

    set_ui_language(Language::german);
    expect(loc(L"Settings") == L"Einstellungen", "german settings label");
    set_ui_language(Language::english);
    expect(loc(L"Settings") == L"Settings", "english identity");

    auto json = serialize_settings_json(parsed);
    expect(json.find("German") != std::string::npos, "serialize language");
    return failed ? 1 : 0;
}
