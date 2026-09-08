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
    auto parsed = parse_settings_json(R"({"language":"German","theme":"Dark","show_console":true,"check_updates_on_startup":false,"autostart_enabled":true})");
    expect(parsed.language == Language::german, "german language");
    expect(language_from_string("Spanish") == Language::spanish, "spanish language");
    expect(language_from_string("fr") == Language::french, "french language");
    expect(language_from_string("ru") == Language::russian, "russian language");
    expect(language_from_string("zh-CN") == Language::simplified_chinese, "simplified chinese language");
    expect(parsed.show_console, "show_console true");
    expect(!parsed.check_updates_on_startup, "updates false");
    expect(parsed.autostart_enabled, "autostart true");
    expect(parsed.theme == Theme::dark, "dark theme");

    set_ui_language(Language::german);
    expect(loc(L"Settings") == L"Einstellungen", "german settings label");
    set_ui_language(Language::english);
    expect(loc(L"Settings") == L"Settings", "english identity");
    set_ui_language(Language::spanish);
    expect(loc(L"Settings") == L"Configuración", "spanish settings label");
    set_ui_language(Language::french);
    expect(loc(L"Settings") == L"Paramètres", "french settings label");
    set_ui_language(Language::russian);
    expect(loc(L"Settings") == L"Настройки", "russian settings label");
    set_ui_language(Language::simplified_chinese);
    expect(loc(L"Settings") == L"设置", "simplified chinese settings label");

    auto json = serialize_settings_json(parsed);
    expect(json.find("German") != std::string::npos, "serialize language");
    expect(json.find("Dark") != std::string::npos, "serialize theme");
    return failed ? 1 : 0;
}
