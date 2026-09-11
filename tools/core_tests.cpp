#include "winchisel/core/cleanup.hpp"
#include "winchisel/core/revert.hpp"
#include "winchisel/core/i18n.hpp"
#include "winchisel/core/settings.hpp"
#include "winchisel/core/affinity.hpp"
#include "winchisel/core/startup.hpp"
#include "winchisel/core/tweak.hpp"
#include "winchisel/core/risk.hpp"

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
    expect(settings_defaults().show_risk_badges, "risk badges default on");
    expect(parse_settings_json(R"({"show_risk_badges":false})").show_risk_badges == false, "risk badges off");
    expect(serialize_settings_json(parsed).find("show_risk_badges") != std::string::npos, "serialize risk badges");
    expect(settings_defaults().show_state_badges, "state badges default on");
    expect(parse_settings_json(R"({"show_state_badges":false})").show_state_badges == false, "state badges off");
    expect(serialize_settings_json(parsed).find("show_state_badges") != std::string::npos, "serialize state badges");
    expect(!settings_defaults().nightly_updates, "nightly updates default off");
    expect(parse_settings_json(R"({"nightly_updates":true})").nightly_updates, "nightly updates on");
    expect(parse_settings_json(R"({"nightly_updates":false})").nightly_updates == false, "nightly updates off");
    expect(serialize_settings_json(parse_settings_json(R"({"nightly_updates":true})")).find("\"nightly_updates\": true") != std::string::npos, "serialize nightly updates");
    expect(settings_defaults().poll_for_updates, "update polling default on");
    expect(parse_settings_json(R"({"poll_for_updates":false})").poll_for_updates == false, "update polling off");
    expect(serialize_settings_json(parse_settings_json(R"({"poll_for_updates":false})")).find("\"poll_for_updates\": false") != std::string::npos, "serialize update polling");
    expect(settings_defaults().dismissed_update_version.empty(), "dismissed version default empty");
    expect(parse_settings_json(R"({"dismissed_update_version":"1.0.8.1"})").dismissed_update_version == "1.0.8.1", "dismissed version parsed");
    expect(serialize_settings_json(parse_settings_json(R"({"dismissed_update_version":"1.0.8.1"})")).find("\"dismissed_update_version\": \"1.0.8.1\"") != std::string::npos, "serialize dismissed version");
    {
        auto dismissed = parse_settings_json(R"({"dismissed_update_version":"a\"b\\c\n"})");
        expect(dismissed.dismissed_update_version == "a\"b\\c\n", "dismissed version unescapes");
        const auto json = serialize_settings_json(dismissed);
        expect(json.find("\\\"") != std::string::npos && json.find("\\\\") != std::string::npos &&
            json.find("\\n") != std::string::npos, "serialize dismissed version escaped");
        expect(parse_settings_json(json).dismissed_update_version == dismissed.dismissed_update_version,
            "dismissed version escape roundtrip");
    }

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
        {
            StartupEntry ms_task{"task:\\Microsoft\\Windows\\Update\\Scheduler", "Scheduler", "", "", "\\Microsoft\\Windows\\Update\\Scheduler", StartupLocation::scheduled_task, true};
            StartupEntry sys32{"reg:user:SecurityHealth", "SecurityHealth", "C:\\Windows\\System32\\SecurityHealthSystray.exe", "", "SecurityHealth", StartupLocation::registry_run_user, true};
            StartupEntry third_party{"reg:user:Spotify", "Spotify", "C:\\Users\\User\\AppData\\Roaming\\Spotify\\Spotify.exe", "", "Spotify", StartupLocation::registry_run_user, true};
            StartupEntry folder_item{"folder:user:app.lnk", "app", "C:\\Tools\\app.exe", "", "app.lnk", StartupLocation::folder_user, true};
            expect(is_microsoft_startup_entry(ms_task), "microsoft task path");
            expect(is_microsoft_startup_entry(sys32), "system32 command");
            expect(!is_microsoft_startup_entry(third_party), "third party command");
            expect(!is_microsoft_startup_entry(folder_item), "folder item");
            StartupEntry run_key_detail{"reg:user:Discord", "Discord", "C:\\Program Files\\Discord\\Discord.exe", "Software\\Microsoft\\Windows\\CurrentVersion\\Run", "Discord", StartupLocation::registry_run_user, true};
            StartupEntry folder_detail{"folder:user:chat.lnk", "chat", "C:\\Tools\\chat.exe", "C:\\Users\\U\\AppData\\Roaming\\Microsoft\\Windows\\Start Menu\\Programs\\Startup", "chat.lnk", StartupLocation::folder_user, true};
            StartupEntry uwp_detail{"uwp:SpotifyAB.SpotifyMusic_zpdnekdrzrea0:Startup", "SpotifyAB.SpotifyMusic_zpdnekdrzrea0 (Startup)", "SpotifyAB.SpotifyMusic_zpdnekdrzrea0", "HKCU\\Software\\Classes\\Local Settings\\Software\\Microsoft\\Windows\\CurrentVersion\\AppModel\\SystemAppData\\SpotifyAB.SpotifyMusic_zpdnekdrzrea0", "SpotifyAB.SpotifyMusic_zpdnekdrzrea0|Startup", StartupLocation::uwp_task, true};
            expect(!is_microsoft_startup_entry(run_key_detail), "run key path is not microsoft");
            expect(!is_microsoft_startup_entry(folder_detail), "startup folder path is not microsoft");
            expect(!is_microsoft_startup_entry(uwp_detail), "appmodel hive path is not microsoft");
            StartupEntry plain{"id", "name", "cmd", "detail", "key", StartupLocation::registry_run_user, true};
            expect(plain.publisher.empty() && plain.file_path.empty() && plain.task_state == -1, "entry extra defaults");
        }
        expect(is_new_tweak("gaming-gpu-amd-power") && is_new_tweak("privacy-disable-autologger"), "new tweak ids");
        expect(!is_new_tweak("gaming-game-mode") && !is_new_tweak(""), "old tweak ids");
        // Shape-pinned only: the exact badge version moves with each badge
        // generation (see k_new_tweaks_version in tweak.hpp) and must never
        // break the build again. This just rejects empty/garbled values.
        const std::string_view badge_version{k_new_tweaks_version};
        const bool badge_chars_ok = !badge_version.empty() &&
            badge_version.find_first_not_of("0123456789.") == std::string_view::npos;
        std::size_t badge_dots{};
        for (const char c : badge_version) badge_dots += (c == '.');
        expect(badge_chars_ok && (badge_dots == 2 || badge_dots == 3) &&
            badge_version.front() != '.' && badge_version.back() != '.',
            "new tweaks version shape");
        expect(k_new_tweak_ids.size() == 59, "new tweak count");
        expect(assess_performance("gaming-memory-integrity") == TweakRisk::risky, "risk hvci");
        expect(assess_performance("gaming-virtualization-based-security") == TweakRisk::risky, "risk vbs");
        expect(assess_performance("gaming-telemetry-service") == TweakRisk::moderate, "risk service disable");
        expect(assess_performance("gaming-performance-explorer-mouse-precision") == TweakRisk::safe, "risk hkcu");
        expect(assess_performance("gaming-gpu-amd-power") == TweakRisk::moderate, "risk gpu special");
        expect(assess_performance("gaming-keyboard-repeat") == TweakRisk::safe, "risk keyboard");
        expect(assess_performance("visual-effects-mode") == TweakRisk::safe, "risk visual override");
        expect(assess_performance("gaming-win32-priority") == TweakRisk::moderate, "risk win32 priority");
        expect(assess_performance("CompatibilityAppraiserTask") == TweakRisk::safe, "risk telemetry task");
        expect(assess_performance("AutochkProxyTask") == TweakRisk::moderate, "risk system task");
        expect(assess_performance("gaming-dns-server") == TweakRisk::moderate, "risk dns");
        expect(assess_performance("updates-driver-controls") == TweakRisk::moderate, "risk hklm update policy");
        expect(assess_performance("notifications-push") == TweakRisk::safe, "risk hkcu notification");
        expect(assess_performance("notifications-windows-security") == TweakRisk::moderate, "risk notification override");
        expect(assess_service("Dnscache", true) == TweakRisk::risky, "risk critical service");
        expect(assess_service("Spooler", true) == TweakRisk::moderate, "risk plain service");
        expect(assess_service("Spooler", false) == TweakRisk::safe, "risk manual service");
        expect(assess_service("SensrSvc", true) == TweakRisk::moderate, "risk sensor service");
        expect(assess_performance("DiskDiagnosticTask") == TweakRisk::safe, "risk disk diagnostic task");
        expect(assess_privacy("privacy-diagnostics") == TweakRisk::moderate, "risk diagnostics");
        expect(assess_privacy("privacy-advertising-id") == TweakRisk::moderate, "risk advertising");
        expect(assess_privacy("privacy-speech-recognition") == TweakRisk::moderate, "risk speech");
        expect(assess_privacy("security-developer-mode") == TweakRisk::moderate, "risk devmode override");
        expect(assess_extras("timer_resolution") == TweakRisk::safe, "risk extras safe");
        expect(assess_extras("ipv6") == TweakRisk::moderate, "risk extras moderate");
        expect(assess_extras("unknown-key") == TweakRisk::moderate, "risk extras fallback");
        expect(risk_label_key(TweakRisk::risky) == std::string_view{"Risky"}, "risk label");
    }
    {
        expect(k_cleanup_categories.size() == 9, "cleanup category count");
        std::size_t phase_a{}, phase_b{};
        for (auto const& info : k_cleanup_categories) {
            if (info.phase_b) ++phase_b; else ++phase_a;
            expect(cleanup_category_from_id(info.id) == info.category, "cleanup id roundtrip");
            expect(!cleanup_category_id(info.category).empty(), "cleanup id nonempty");
            expect(cleanup_is_phase_b(info.category) == info.phase_b, "cleanup phase flag");
            expect(info.requires_admin == info.phase_b, "cleanup admin matches phase");
            expect(cleanup_is_appdata_located(info.category) == info.appdata_located, "cleanup appdata flag");
        }
        expect(phase_a == k_cleanup_phase_a_count, "cleanup phase a count");
        expect(phase_b == 3, "cleanup phase b count");
        expect(!cleanup_category_from_id("no-such-category").has_value(), "cleanup unknown id");
        expect(cleanup_category_id(CleanupCategory::recycle_bin) == std::string_view{"recycle-bin"}, "cleanup bin id");
        expect(!cleanup_is_phase_b(CleanupCategory::user_temp), "cleanup temp is phase a");
        expect(cleanup_is_phase_b(CleanupCategory::update_cleanup), "cleanup dism is phase b");
        expect(cleanup_is_appdata_located(CleanupCategory::user_temp), "cleanup temp virtualized");
        expect(cleanup_is_appdata_located(CleanupCategory::thumbnails), "cleanup thumbs virtualized");
        expect(cleanup_is_appdata_located(CleanupCategory::shader_cache), "cleanup shaders virtualized");
        expect(!cleanup_is_appdata_located(CleanupCategory::windows_temp), "cleanup win temp real");
        expect(!cleanup_is_appdata_located(CleanupCategory::recycle_bin), "cleanup bin real");
        expect(!cleanup_is_appdata_located(CleanupCategory::delivery_optimization), "cleanup do real");
    }
    {
        RevertEntry entry;
        entry.key = "20260911-120000-1";
        entry.timestamp = "2026-09-11 12:00";
        entry.page = "performance";
        entry.label = "Gaming: Test \"tweak\"\nnewline";
        entry.registry.push_back(RevertRegistryStep{
            RegistryTarget{RegistryHive::local_machine, "SYSTEM\\Test", "Value", RegistryValueType::dword},
            RegistryNativeValue{false, 4, {0x01, 0x00, 0x00, 0x00}}});
        entry.registry.push_back(RevertRegistryStep{
            RegistryTarget{RegistryHive::current_user, "Software\\Test", "Gone", RegistryValueType::string},
            RegistryNativeValue{true, 0, {}}});
        entry.tasks.push_back(RevertTaskStep{"task-id", true});
        StartupEntry startup{"id", "Name", "cmd", "detail", "key", StartupLocation::scheduled_task, true, "pub", 3,
            "C:\\path"};
        entry.startups.push_back(RevertStartupStep{startup, false});
        entry.toggles.push_back(RevertToggleStep{"special", "gpu-id", true});
        entry.ints.push_back(RevertIntStep{"dns", "dns", 1});
        entry.power = RevertPowerStep{"381b4222-fb38-11d3-bd01-00aa00b7b32"};
        expect(!revert_entry_empty(entry), "revert nonempty");
        expect(revert_entry_empty(RevertEntry{}), "revert empty");
        const auto revert_json = serialize_revert_journal({entry});
        const auto revert_parsed = parse_revert_journal(revert_json);
        expect(revert_parsed.size() == 1, "revert roundtrip count");
        expect(revert_parsed[0].key == entry.key, "revert roundtrip key");
        expect(revert_parsed[0].label == entry.label, "revert roundtrip label escapes");
        expect(revert_parsed[0].registry.size() == 2, "revert roundtrip registry");
        expect(revert_parsed[0].registry[0].target.key_path == "SYSTEM\\Test", "revert roundtrip path");
        expect(revert_parsed[0].registry[0].before.type == 4, "revert roundtrip type");
        expect(revert_parsed[0].registry[0].before.data == std::vector<std::uint8_t>({0x01, 0x00, 0x00, 0x00}),
            "revert roundtrip bytes");
        expect(revert_parsed[0].registry[1].before.missing, "revert roundtrip missing");
        expect(revert_parsed[0].tasks[0].id == "task-id" && revert_parsed[0].tasks[0].was_enabled, "revert roundtrip task");
        expect(revert_parsed[0].startups[0].entry.command == "cmd" &&
                revert_parsed[0].startups[0].entry.location == StartupLocation::scheduled_task &&
                !revert_parsed[0].startups[0].was_enabled,
            "revert roundtrip startup");
        expect(revert_parsed[0].toggles[0].domain == "special" && revert_parsed[0].toggles[0].was_enabled, "revert roundtrip toggle");
        expect(revert_parsed[0].ints[0].was_value == 1, "revert roundtrip int");
        expect(revert_parsed[0].power && revert_parsed[0].power->scheme_guid == "381b4222-fb38-11d3-bd01-00aa00b7b32",
            "revert roundtrip power");
        expect(parse_revert_journal("").empty(), "revert empty text");
        expect(parse_revert_journal("{garbage").empty(), "revert garbage");
        expect(parse_revert_journal(R"({"version":1})").empty(), "revert no entries");
        expect(parse_revert_journal(R"({"version":1,"entries":[{"key":"k"}]})").empty(), "revert bad entry skipped");
        expect(parse_revert_journal(R"({"version":1,"entries":[{"key":"k","timestamp":"t","page":"p","label":"l","registry":[{"hive":9,"key":"k","name":"n","missing":false,"type":4,"data":"zz"}]}]})")
                .empty(),
            "revert bad step skipped");
        expect(parse_revert_journal(std::string(2 * 1024 * 1024, 'x')).empty(), "revert oversize rejected");
    }
    return failed ? 1 : 0;
}
