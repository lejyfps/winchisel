#include "winchisel/core/i18n.hpp"
#include "winchisel/core/settings.hpp"
#include "winchisel/core/affinity.hpp"

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
    expect(language_from_string("pt-BR") == Language::portuguese_brazil, "brazilian portuguese language");
    expect(language_from_string("pl") == Language::polish, "polish language");
    expect(language_from_string("tr") == Language::turkish, "turkish language");
    expect(language_from_string("ja") == Language::japanese, "japanese language");
    expect(language_from_string("ko") == Language::korean, "korean language");
    expect(language_from_string("it") == Language::italian, "italian language");
    expect(language_from_string("nl") == Language::dutch, "dutch language");
    expect(language_from_string("uk") == Language::ukrainian, "ukrainian language");
    expect(language_from_string("cs") == Language::czech, "czech language");
    expect(language_from_string("id") == Language::indonesian, "indonesian language");
    expect(language_from_string("vi") == Language::vietnamese, "vietnamese language");
    expect(language_from_string("ar") == Language::arabic, "arabic language");
    expect(language_from_string("zh-TW") == Language::traditional_chinese, "traditional chinese language");
    expect(language_from_string("th") == Language::thai, "thai language");
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
    set_ui_language(Language::portuguese_brazil); expect(loc(L"Settings") == L"Configurações", "brazilian portuguese settings label");
    set_ui_language(Language::polish); expect(loc(L"Settings") == L"Ustawienia", "polish settings label");
    set_ui_language(Language::turkish); expect(loc(L"Settings") == L"Ayarlar", "turkish settings label");
    set_ui_language(Language::japanese); expect(loc(L"Settings") == L"設定", "japanese settings label");
    set_ui_language(Language::korean); expect(loc(L"Settings") == L"설정", "korean settings label");
    set_ui_language(Language::italian); expect(loc(L"Settings") == L"Impostazioni", "italian settings label");
    set_ui_language(Language::dutch); expect(loc(L"Settings") == L"Instellingen", "dutch settings label");
    set_ui_language(Language::ukrainian); expect(loc(L"Settings") == L"Налаштування", "ukrainian settings label");
    set_ui_language(Language::czech); expect(loc(L"Settings") == L"Nastavení", "czech settings label");
    set_ui_language(Language::indonesian); expect(loc(L"Settings") == L"Pengaturan", "indonesian settings label");
    set_ui_language(Language::vietnamese); expect(loc(L"Settings") == L"Cài đặt", "vietnamese settings label");
    set_ui_language(Language::arabic); expect(loc(L"Settings") == L"الإعدادات", "arabic settings label");
    set_ui_language(Language::traditional_chinese); expect(loc(L"Settings") == L"設定", "traditional chinese settings label");
    set_ui_language(Language::thai); expect(loc(L"Settings") == L"การตั้งค่า", "thai settings label");

    auto json = serialize_settings_json(parsed);
    expect(json.find("German") != std::string::npos, "serialize language");
    expect(json.find("Dark") != std::string::npos, "serialize theme");
    expect(affinity_mask(255, 0) == 255 && affinity_mask(255, 1) == 85 && affinity_mask(255, 2) == 170 && affinity_mask(255, 3) == 15 && affinity_mask(255, 4) == 240, "all five affinity presets");
    expect(affinity_mask(0x95, 3) == 0x05 && affinity_mask(0x95, 4) == 0x90, "sparse affinity halves");
    expect(affinity_mask(1, 2) == 1 && affinity_mask(0, 3) == 0, "empty affinity fallback");
    return failed ? 1 : 0;
}
