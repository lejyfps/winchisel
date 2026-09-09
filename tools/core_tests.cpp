#include "winchisel/core/i18n.hpp"
#include "winchisel/core/settings.hpp"
#include "winchisel/core/affinity.hpp"
#include "winchisel/core/startup.hpp"
#include "winchisel/core/tweak.hpp"

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
    expect(!parse_settings_json(R"({"nested":{"show_console":true},"show_console":false})").show_console, "nested key ignored");
    expect(parse_settings_json(R"({"language":"\u0047erman"})").language == Language::german, "unicode escape language");
    expect(parse_settings_json(R"({"language":"\ud83d\ude00","show_console":true})").show_console, "surrogate pair accepted");
    expect(!parse_settings_json(R"({"language":"\ud800","show_console":true})").show_console, "lone high surrogate rejected");
    expect(!parse_settings_json(R"({"language":"\udc00","show_console":true})").show_console, "lone low surrogate rejected");
    expect(!parse_settings_json(R"({"language":"\ud83dX","show_console":true})").show_console, "truncated pair rejected");
    expect(!parse_settings_json(R"({"language":"\ud83d\ude00","show_console":true})").show_console == false, "surrogate pair keeps sibling values");
    expect(!parse_settings_json(R"({"language":"Deutsch","show_console":true,"x":"\u00"})").show_console, "truncated escape rejected");
    expect(!parse_settings_json(R"({"language":"Deutsch","show_console":true,"x":"\q"})").show_console, "bad escape rejected");
    {
        std::string deep;
        for (int i{}; i < 20; ++i) deep += R"({"k":)";
        deep += "true";
        for (int i{}; i < 20; ++i) deep += "}";
        expect(!parse_settings_json(deep).show_console, "deep nesting rejected");
    }
    expect(parse_settings_json(std::string(70000, 'x')).show_console == settings_defaults().show_console, "oversized json rejected");
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
    {
        const auto on = build_approved_data(true, 0x01C0000000000000ULL);
        const auto off = build_approved_data(false, 0x01C0000000000000ULL);
        expect(on[0] == 0x02 && off[0] == 0x03, "approved flag bytes");
        expect(on.size() == 12 && off.size() == 12, "approved flag length");
        bool time_ok = true;
        for (std::size_t i{}; i < 8; ++i) time_ok = time_ok && on[4 + i] == off[4 + i];
        expect(time_ok, "approved flag filetime");
        expect(parse_approved_data({0x02, 0, 0, 0}) == std::optional<bool>{true}, "approved enabled");
        expect(parse_approved_data({0x06, 0, 0, 0}) == std::optional<bool>{true}, "approved enabled variant");
        expect(parse_approved_data({0x03, 0, 0, 0}) == std::optional<bool>{false}, "approved disabled");
        expect(parse_approved_data({0x07, 0, 0, 0}) == std::optional<bool>{false}, "approved disabled variant");
        expect(!parse_approved_data({0x01, 0, 0, 0}).has_value(), "approved unknown flag");
        expect(!parse_approved_data({0x02, 0}).has_value(), "approved truncated");
        expect(parse_approved_data(std::vector<std::uint8_t>(on.begin(), on.end())) == std::optional<bool>{true}, "approved roundtrip");
        expect(parse_uwp_startup_state(2) == std::optional<bool>{true}, "uwp enabled");
        expect(parse_uwp_startup_state(4) == std::optional<bool>{true}, "uwp enabled by policy");
        expect(parse_uwp_startup_state(0) == std::optional<bool>{false}, "uwp disabled");
        expect(parse_uwp_startup_state(1) == std::optional<bool>{false}, "uwp disabled by user");
        expect(!parse_uwp_startup_state(3).has_value(), "uwp unknown state");
        expect(is_startup_trigger(8) && is_startup_trigger(9), "boot and logon triggers");
        expect(!is_startup_trigger(1) && !is_startup_trigger(7) && !is_startup_trigger(0), "non-startup triggers");
        expect(is_new_tweak("gaming-gpu-amd-power") && is_new_tweak("privacy-disable-autologger"), "new tweak ids");
        expect(!is_new_tweak("gaming-game-mode") && !is_new_tweak(""), "old tweak ids");
        expect(k_new_tweaks_version == std::string_view{"1.0.7"}, "new tweaks version");
        expect(k_new_tweak_ids.size() == 9, "new tweak count");
    }
    return failed ? 1 : 0;
}
