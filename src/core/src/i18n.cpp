#include "winchisel/core/i18n.hpp"

#include <cstdint>
#include <unordered_map>

namespace winchisel::core {
namespace {

Language g_language{Language::english};

std::wstring utf8_to_wide(std::string_view text) {
    std::wstring out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size();) {
        const auto c = static_cast<unsigned char>(text[i]);
        std::uint32_t cp = c;
        std::size_t n = 1;
        if (c >= 0xF0) { n = 4; cp = c & 0x07; }
        else if (c >= 0xE0) { n = 3; cp = c & 0x0F; }
        else if (c >= 0xC0) { n = 2; cp = c & 0x1F; }
        if (i + n > text.size()) break;
        for (std::size_t j = 1; j < n; ++j) cp = (cp << 6) | (static_cast<unsigned char>(text[i + j]) & 0x3F);
        i += n;
        if (cp >= 0x10000) {
            cp -= 0x10000;
            out.push_back(static_cast<wchar_t>(0xD800 + (cp >> 10)));
            out.push_back(static_cast<wchar_t>(0xDC00 + (cp & 0x3FF)));
        } else {
            out.push_back(static_cast<wchar_t>(cp));
        }
    }
    return out;
}

std::string wide_to_utf8(std::wstring_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        std::uint32_t cp = static_cast<std::uint16_t>(text[i]);
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < text.size()) {
            const auto low = static_cast<std::uint16_t>(text[i + 1]);
            if (low >= 0xDC00 && low <= 0xDFFF) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                ++i;
            }
        }
        if (cp < 0x80) out.push_back(static_cast<char>(cp));
        else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}

std::unordered_map<std::wstring, std::wstring> const& german() {
    static const std::unordered_map<std::wstring, std::wstring> table{
        {L"Home", L"Start"},
        {L"Debloater", L"Debloater"},
        {L"Performance", L"Leistung"},
        {L"Privacy & Security", L"Datenschutz & Sicherheit"},
        {L"Downloads", L"Downloads"},
        {L"Processes", L"Prozesse"},
        {L"Latency", L"Latenz"},
        {L"Extras", L"Extras"},
        {L"Settings", L"Einstellungen"},
        {L"Hardware", L"Hardware"},
        {L"System", L"System"},
        {L"Processor", L"Prozessor"},
        {L"Graphics", L"Grafik"},
        {L"Memory", L"Arbeitsspeicher"},
        {L"Storage", L"Speicher"},
        {L"Windows", L"Windows"},
        {L"Motherboard", L"Mainboard"},
        {L"Display", L"Anzeige"},
        {L"Uptime", L"Laufzeit"},
        {L"Keep the app behavior aligned with your workflow.", L"Passe das App-Verhalten an deinen Ablauf an."},
        {L"Application", L"Anwendung"},
        {L"These settings are saved automatically.", L"Diese Einstellungen werden automatisch gespeichert."},
        {L"Language", L"Sprache"},
        {L"Choose the application language.", L"Wähle die Sprache der Anwendung."},
        {L"Check updates on startup", L"Beim Start nach Updates suchen"},
        {L"Look for a newer Winchisel release when the app starts.", L"Beim Start nach einer neueren Winchisel-Version suchen."},
        {L"Show console", L"Konsole anzeigen"},
        {L"Show the debug console window while the app is running.", L"Debug-Konsole anzeigen, während die App läuft."},
        {L"Start with Windows", L"Mit Windows starten"},
        {L"Launch Winchisel automatically when you sign in.", L"Winchisel automatisch nach der Anmeldung starten."},
        {L"System Protection", L"Systemschutz"},
        {L"System Restore Point", L"Systemwiederherstellungspunkt"},
        {L"Remove or restore Windows apps, capabilities, and optional features.", L"Windows-Apps, Funktionen und optionale Features entfernen oder wiederherstellen."},
        {L"Windows Apps", L"Windows-Apps"},
        {L"Capabilities", L"Funktionen"},
        {L"Optional Features", L"Optionale Features"},
        {L"Search", L"Suche"},
        {L"Find and install trusted applications with winget.", L"Vertrauenswürdige Anwendungen mit winget finden und installieren."},
        {L"Search apps, categories, or package IDs", L"Apps, Kategorien oder Paket-IDs suchen"},
        {L"Inspect running processes, priority, and affinity.", L"Laufende Prozesse, Priorität und Affinität prüfen."},
        {L"Analyze USB topology for latency issues.", L"USB-Topologie auf Latenzprobleme analysieren."},
        {L"Ready", L"Bereit"},
        {L"Additional system, browser, networking, and power options.", L"Weitere System-, Browser-, Netzwerk- und Energieoptionen."},
        {L"Installed", L"Installiert"},
        {L"Not installed", L"Nicht installiert"},
        {L"Website", L"Website"},
        {L"Install", L"Installieren"},
        {L"Scan failed", L"Scan fehlgeschlagen"},
        {L"Installation complete", L"Installation abgeschlossen"},
        {L"Update", L"Aktualisieren"},
        {L"Later", L"Später"},
        {L"Winchisel update available", L"Winchisel-Update verfügbar"},
        {L"Create Restore Point", L"Wiederherstellungspunkt erstellen"},
        {L"System Repair", L"Systemreparatur"},
        {L"Temporary Files - Remove", L"Temporäre Dateien entfernen"},
        {L"Close", L"Schließen"},
        {L"Cancel", L"Abbrechen"},
        {L"Apply", L"Übernehmen"},
        {L"Live log", L"Live-Protokoll"},
        {L"Waiting for output...", L"Warte auf Ausgabe..."},
        {L"This can take a while. Keep the window open until it finishes.", L"Das kann eine Weile dauern. Lass das Fenster geöffnet, bis es fertig ist."},
        {L"Could not apply setting", L"Einstellung konnte nicht übernommen werden"},
        {L"Recommended", L"Empfohlen"},
        {L"Defaults", L"Standard"},
        {L"Settings could not be saved", L"Einstellungen konnten nicht gespeichert werden"},
        {L"Refresh", L"Aktualisieren"},
        {L"All", L"Alle"},
        {L"Active", L"Aktiv"},
        {L"User", L"Benutzer"},
    };
    return table;
}

}  // namespace

void set_ui_language(Language language) { g_language = language; }
Language ui_language() { return g_language; }

std::wstring loc(std::wstring_view english) {
    if (g_language != Language::german) return std::wstring(english);
    auto const& table = german();
    if (auto it = table.find(std::wstring(english)); it != table.end()) return it->second;
    return std::wstring(english);
}

std::string loc_utf8(std::string_view english) {
    return wide_to_utf8(loc(utf8_to_wide(english)));
}

}  // namespace winchisel::core
