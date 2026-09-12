#pragma once

// Winchisel USB-Cadence Capture (Phase 3: Capture + Phase 4: Gerätezuordnung).
//
// Nativer Echtzeit-ETW-Capture ohne fremde Prozesse/Temp-Dateien:
//  - Private Session, prozess-eindeutiger Name, RAII-Stop via ControlTrace.
//  - Provider: Microsoft-Windows-USB-UCX (Transfers + Endpoint/Device-Rundown)
//    und Microsoft-Windows-USB-USBHUB3 (Handle -> Interface-Pfad + Deskriptor).
//  - Zeitbasis: QPC RAW_TIMESTAMP, Intervalle in µs via qpc_delta_us.
//  - Geräteidentität: ETW-Device-Handle + Pipe/Endpoint + Interface-Pfad
//    (VID:PID nur Anzeige) + PortPath + Speed. Korrelation zu SetupDi/
//    Container-ID passiert in der Auswertungsschicht, nicht im Capture.
//  - Ergebnis: rohe Completion-Intervalle je (Device, Pipe); die Bewertung
//    (Rate/Stalls/Status) macht analyze_capture() aus usb_cadence.hpp.
//
// Verträge: braucht Admin (sonst Fehler, kein stilles Fallback); meldet
// ETW-Verluste; stoppt niemals fremde Sessions.

#include "winchisel/core/error.hpp"
#include "winchisel/platform/usb_cadence.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace winchisel::platform::cadence {

struct CaptureConfig {
    int seconds = 7; // 1..30, danach Clamp
    std::function<void(int, std::string const&)> progress; // Prozent + Text, darf null sein
    std::atomic<bool> const* cancel = nullptr;             // Abbruchflag, darf null sein
};

struct DeviceIdentity {
    unsigned long long etw_device = 0; // fid_UsbDevice
    unsigned long long pipe = 0;       // fid_PipeHandle (0 = geräteweit)
    std::string interface_path;        // HUB3-Rundown, enthält ggf. VID_/PID_
    std::string vid;                   // Anzeige, aus Pfad geparst
    std::string pid;                   // Anzeige, aus Pfad geparst
    std::string port_path;             // UCX/HUB3 PortPath (Hub-Port-Kette)
    UsbSpeed speed = UsbSpeed::unknown;
    unsigned long speed_raw = 0;       // Rohwert DeviceSpeed (Best-Effort-Mapping)
    int b_interval = 0;                // schnellster Interrupt-IN des Geräts (0 = unbekannt)
    // Gemessener Endpoint der Haupt-Pipe (UCX Endpoint-Rundown, verifizierte Felder).
    unsigned long endpoint_address = 0;
    unsigned long endpoint_attributes = 0;
    unsigned long max_packet = 0;
    // SuperSpeed-Companion aus dem Config-Deskriptor (0 = nicht vorhanden/unbekannt).
    bool has_companion = false;
    unsigned long max_burst = 0;
    unsigned long bytes_per_interval = 0;
    // Zugehöriges Interface/Alt-Setting aus dem Config-Deskriptor-Walk.
    bool has_interface = false;
    unsigned long interface_number = 0;
    unsigned long alt_setting = 0;
};

struct DeviceCapture {
    DeviceIdentity identity;
    std::vector<double> intervals_us; // Completion->Completion, QPC-abgeleitet
    unsigned long long completions = 0;
    bool truncated = false; // Intervall-Cap erreicht (s. kMaxIntervalsPerPipe)
};

// Grobe Systemlast-Zeitreihe (PDH, ~500 ms Raster). Nur zur zeitlichen
// Einordnung von Stalls — erklärt einzelne µs/ms-Ausreißer NICHT zuverlässig
// und ist niemals ein Ursachenbeweis. Fail-open: leer bei PDH-Fehler.
struct CpuSample {
    double t_ms = 0.0;    // relativ zum Capture-Start
    double busy_pct = 0.0; // 0..100, Prozessor(_Total)
};

struct CaptureOutcome {
    std::vector<DeviceCapture> devices;
    std::vector<CpuSample> cpu;
    unsigned long events_lost = 0;
    unsigned long buffers_written = 0;
    unsigned long realtime_buffers_lost = 0;
    bool aborted = false;
};

winchisel::core::Result<CaptureOutcome> capture_usb_cadence(CaptureConfig const& config);

// Schema-Preflight ohne Admin: Provider registriert + Transfer-/Rundown-
// Felder im Manifest vorhanden. false = unsupported, keine Messung behaupten.
bool etw_providers_available(std::string* detail = nullptr);

} // namespace winchisel::platform::cadence
