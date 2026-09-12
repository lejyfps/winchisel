#pragma once

// Winchisel USB-Instanzidentität (Gerätezuordnung, Phase 4b).
//
// Problem: VID:PID allein trennt zwei baugleiche Geräte nicht (die
// Bestands-Topologie in latency.cpp dedupliziert danach). Lösung:
//  - Alle präsenten USB-Instanzen aufzählen (KEIN Dedup).
//  - Je Instanz: Instanz-ID, Container-ID, Eltern-Instanz, VID/PID.
//  - Korrelation zu ETW: Der HUB3-Interface-Pfad enthält den Instanzpfad
//    (`USB#VID_...#...`); normalisiert (`#`->`\`, case-insensitiv) ergibt
//    das die exakte Instanz — längster Treffer gewinnt (Kind vor Elter).
//
// Kein Admin nötig, kein ETW, rein lesend. Reine Match-Logik ist testbar.

#include <optional>
#include <string>
#include <vector>

namespace winchisel::platform::cadence {

struct UsbInstance {
    std::string instance_id;  // z.B. USB\VID_046D&PID_C52B\ABC123
    std::string container_id; // GUID-String, ggf. leer
    std::string parent_id;    // Eltern-Instanz (Hub/Controller), ggf. leer
    std::string vid;
    std::string pid;
    std::string bus_desc;
};

// Alle präsenten USB-Geräteinstanzen (HID/USB), ohne Dedup.
std::vector<UsbInstance> enumerate_usb_instances();

// Bezeichnet dieser ETW-Interface-Pfad genau diese Instanz?
bool interface_path_matches(std::string const& interface_path, std::string const& instance_id);

// Container-ID der am besten passenden Instanz (längste Instanz-ID gewinnt).
// nullopt = keine Korrelation (Pfad unbekannt/leer).
std::optional<std::string> resolve_container_id(std::string const& interface_path,
                                                std::vector<UsbInstance> const& instances);

} // namespace winchisel::platform::cadence
