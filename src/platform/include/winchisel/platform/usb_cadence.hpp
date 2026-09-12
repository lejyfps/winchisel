#pragma once

// Winchisel USB-Cadence Messvertrag (Phase 2).
//
// Begriffe (verbindlich für UI/Texte/Doku):
//  - "beobachtete USB-Transfer-/Completion-Kadenz": was wir messen.
//  - NICHT "Input-Latenz" und NICHT "garantierte Geräte-Pollrate".
//    Queuing im Treiber-Stack, DPC-Batching, HID-Idle und ETW-Eventverluste
//    können die beobachtete Kadenz von der tatsächlichen Geräte-Pollrate
//    abweichen lassen. `bInterval` ist ein Sollwert, kein Beweis.
//
// Diese Datei enthält ausschließlich reine, testbare Analyse-Logik
// (kein ETW, kein Admin nötig). Capture/Session lebt separat.

#include <cstddef>
#include <optional>
#include <vector>

namespace winchisel::platform::cadence {

enum class UsbSpeed { unknown, low, full, high, super };

enum class CadenceStatus {
    ok,               // Steady-State-Kadenz belastbar
    insufficient_data,// zu wenige Steady-Samples (Warm-up/zu kurz/abgebrochen)
    events_lost,      // ETW meldet Buffer-/Event-Verluste -> Messung UNGÜLTIG
    aborted,          // Capture abgebrochen (User/Fehler)
    unsupported       // Provider/Schema fehlt -> keine scheinbare Messung
};

struct CadenceConfig {
    std::size_t warmup_skip = 200; // Intervalle vom Anfang verwerfen (Driver-Warm-up)
    std::size_t min_samples = 1000;// benötigte Steady-Intervalle für `ok`
};

struct Stall { std::size_t index{}; double interval_us{}; };

// Ein Histogramm-Bin über sortierte Intervalle. Der letzte Bin ist der
// Overflow (alles über hi_us, z.B. Stalls); hi_us == 0 markiert ihn.
struct HistBin {
    double lo_us = 0.0;
    double hi_us = 0.0;
    std::size_t count = 0;
    bool overflow = false;
};

struct CadenceResult {
    CadenceStatus status = CadenceStatus::insufficient_data;
    double rate_hz = 0.0;      // 1e6 / Mittelwert der Steady-Intervalle
    double median_us = 0.0;    // über alle behaltenen Intervalle
    double p95_us = 0.0;
    double p99_us = 0.0;
    double p999_us = 0.0;
    double threshold_us = 0.0; // Tukey-Zaun aus der Verteilung (s. analyze_capture)
    std::size_t kept_samples = 0;
    std::size_t steady_samples = 0;
    std::vector<Stall> stalls;
    std::vector<HistBin> histogram; // begrenzte Bins statt Rohlisten-Export
    double stall_ms = 0.0;
};

// Interpolierte Perzentile über sortierte Daten (p in [0,100]).
double percentile_interpolated(std::vector<double> const& sorted, double p);

// Gleichbreite Bins von min bis P99.9 plus Overflow-Bin. Summe aller
// counts == sorted.size(). bins wird auf [2,16] geclampt.
std::vector<HistBin> histogram_bins(std::vector<double> const& sorted, std::size_t bins = 8);

// Soll-Kadenz aus bInterval + Speed. nullopt = keine Aussage
// (unbekannter Speed oder ungültiges bInterval). Regeln:
//  - HighSpeed: 8000 / 2^(bInterval-1), bInterval 1..16
//  - SuperSpeed: wie HighSpeed (Interrupt), Companion-Deskriptor nur notiert
//  - FullSpeed Interrupt: bInterval = Millisekunden, 1..255
//  - LowSpeed Interrupt: bInterval = Millisekunden, geclampt 10..255
// `bInterval` ohne Speed bedeutet keine feste Zeit (vgl. MS USB-Endpoint-Doku).
std::optional<double> expected_hz(UsbSpeed speed, int b_interval);

// Endpoint-Detail aus einem Konfigurations-Deskriptor-Blob: sucht den
// Endpoint-Deskriptor mit dieser Adresse und liest ggf. den direkt folgenden
// SuperSpeed-Endpoint-Companion (Typ 0x30: bMaxBurst, wBytesPerInterval).
struct EndpointDetail {
    bool found = false;
    int b_interval = 0;
    bool interrupt = false;
    // Zugehöriges Interface: letzter Interface-Deskriptor (Typ 0x04) vor dem
    // Endpoint im Config-Blob. Alt-Settings stehen damit je Endpoint fest,
    // ohne dass ETW sie als eigenes Feld liefern muss.
    bool has_interface = false;
    unsigned long interface_number = 0;
    unsigned long alt_setting = 0;
    bool has_companion = false;
    unsigned long max_burst = 0;         // Companion+1 = Burstgröße
    unsigned long bytes_per_interval = 0;
};

EndpointDetail endpoint_detail(std::vector<unsigned char> const& blob, unsigned long address);

// QPC-Tick-Differenz in Mikrosekunden (Session liefert RAW_TIMESTAMP).
inline double qpc_delta_us(long long first, long long last, long long frequency) {
    if (frequency <= 0 || last <= first) return 0.0;
    return static_cast<double>(last - first) * 1e6 / static_cast<double>(frequency);
}

// Kernanalyse: Intervalle in µs (Completion->Completion, QPC-abgeleitet).
// `events_lost=true` markiert die Messung als UNGÜLTIG (Status events_lost),
// Statistik wird trotzdem zur Einordnung ausgefüllt. Stalls bleiben getrennt
// von der Steady-Rate ausgewiesen (ungültig vs. Stalls nie vermischen).
// Stall-Schwelle: Tukey Q3+3*IQR (bei IQR=0: 0.25*Median), mindestens 2*Median.
CadenceResult analyze_capture(std::vector<double> intervals_us, CadenceConfig const& config, bool events_lost);

} // namespace winchisel::platform::cadence
