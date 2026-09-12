// Winchisel Cadence-Messvertrag Tests (Phase 2).
// Reine Analyse-Logik, kein ETW/Admin nötig.
// Bau: cl ... tools\cadence_tests.cpp src\platform\src\usb_cadence.cpp ...
// Lauf: cadence_tests.exe -> exit 0 bei PASS.

#include "winchisel/platform/usb_cadence.hpp"
#include "winchisel/platform/usb_identity.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

namespace {
int failures = 0;
void check(bool cond, char const* name) {
    std::printf("%s %s\n", cond ? "PASS" : "FAIL", name);
    if (!cond) ++failures;
}
bool near(double a, double b, double eps = 1e-6) { return std::fabs(a - b) <= eps; }
} // namespace

int main() {
    using winchisel::platform::cadence::CadenceConfig;
    using winchisel::platform::cadence::CadenceStatus;
    using winchisel::platform::cadence::UsbSpeed;
    using winchisel::platform::cadence::analyze_capture;
    using winchisel::platform::cadence::expected_hz;
    using winchisel::platform::cadence::percentile_interpolated;

    // Perzentile interpoliert
    check(near(percentile_interpolated({0.0, 10.0}, 50.0), 5.0), "p50 interpoliert 0..10");
    check(near(percentile_interpolated({1.0, 2.0, 3.0, 4.0}, 50.0), 2.5), "p50 interpoliert 1..4");
    check(percentile_interpolated({}, 50.0) == 0.0, "p50 leer=0");
    check(near(percentile_interpolated({7.0}, 99.0), 7.0), "p99 einzelwert");

    // bInterval x Speed
    auto hz = expected_hz(UsbSpeed::high, 1);
    check(hz && near(*hz, 8000.0), "high bi=1 -> 8000");
    hz = expected_hz(UsbSpeed::high, 4);
    check(hz && near(*hz, 1000.0), "high bi=4 -> 1000");
    check(!expected_hz(UsbSpeed::high, 0), "high bi=0 -> nullopt");
    check(!expected_hz(UsbSpeed::high, 17), "high bi=17 -> nullopt");
    hz = expected_hz(UsbSpeed::super, 1);
    check(hz && near(*hz, 8000.0), "super bi=1 -> 8000");
    hz = expected_hz(UsbSpeed::full, 1);
    check(hz && near(*hz, 1000.0), "full bi=1 -> 1000ms");
    hz = expected_hz(UsbSpeed::full, 10);
    check(hz && near(*hz, 100.0), "full bi=10 -> 100");
    hz = expected_hz(UsbSpeed::low, 9);
    check(hz && near(*hz, 100.0), "low bi=9 geclampt -> 100");
    hz = expected_hz(UsbSpeed::low, 10);
    check(hz && near(*hz, 100.0), "low bi=10 -> 100");
    check(!expected_hz(UsbSpeed::unknown, 1), "unknown -> nullopt");

    // 8 kHz synthetisch + Stalls
    {
        std::vector<double> iv(5000, 125.0);
        iv.push_back(5000.0);
        iv.push_back(8000.0);
        CadenceConfig cfg;
        cfg.warmup_skip = 0;
        cfg.min_samples = 1000;
        auto r = analyze_capture(iv, cfg, false);
        check(r.status == CadenceStatus::ok, "8kHz status ok");
        check(r.rate_hz > 7900.0 && r.rate_hz < 8100.0, "8kHz rate ~8000");
        check(r.stalls.size() == 2, "2 stalls erkannt");
        check(r.steady_samples == 5000, "steady=5000");
    }
    // Warm-up-Skip
    {
        std::vector<double> iv(300, 9000.0);
        for (int i = 0; i < 2000; ++i) iv.push_back(125.0);
        CadenceConfig cfg;
        cfg.warmup_skip = 300;
        auto r = analyze_capture(iv, cfg, false);
        check(r.status == CadenceStatus::ok && r.stalls.empty(), "warmup verworfen");
    }
    // Zu wenig Daten
    {
        CadenceConfig cfg;
        cfg.warmup_skip = 0;
        auto r = analyze_capture(std::vector<double>(100, 125.0), cfg, false);
        check(r.status == CadenceStatus::insufficient_data, "wenig daten -> insufficient");
    }
    // Eventverlust = ungültig, trotz guter Rate
    {
        CadenceConfig cfg;
        cfg.warmup_skip = 0;
        auto r = analyze_capture(std::vector<double>(5000, 125.0), cfg, true);
        check(r.status == CadenceStatus::events_lost, "verlust -> events_lost");
        check(r.rate_hz > 7900.0, "rate trotzdem zur einordnung gefuellt");
    }
    // Leere Eingabe
    {
        CadenceConfig cfg;
        auto r = analyze_capture({}, cfg, false);
        check(r.status == CadenceStatus::insufficient_data, "leer -> insufficient");
    }

    // Histogramm-Bins: Summe stimmt, Overflow fängt Stalls, P99.9 interpoliert
    using winchisel::platform::cadence::histogram_bins;
    {
        CadenceConfig cfg;
        cfg.warmup_skip = 0;
        std::vector<double> iv(1000, 125.0);
        iv.push_back(9000.0);
        auto r = analyze_capture(iv, cfg, false);
        check(near(r.p999_us, 125.0, 1.0), "p999 bei 8kHz ~125us");
        std::size_t total = 0, overflow = 0;
        for (auto const& bin : r.histogram) {
            total += bin.count;
            if (bin.overflow) overflow = bin.count;
        }
        check(total == r.kept_samples, "bins summieren sich auf kept");
        check(overflow == 1, "stall landet im overflow-bin");
        check(r.histogram.size() == 9, "8 bins + overflow");
    }
    {
        auto empty = histogram_bins({}, 8);
        check(empty.empty(), "histogramm leer=leer");
        auto single = histogram_bins({5.0, 5.0, 5.0}, 8);
        check(single.size() == 1 && single[0].count == 3, "konstante werte=ein bin");
        auto clamped = histogram_bins({1.0, 2.0, 3.0}, 99);
        check(clamped.size() == 17, "bins auf 16+overflow geclampt");
    }
    using winchisel::platform::cadence::UsbInstance;
    using winchisel::platform::cadence::endpoint_detail;
    using winchisel::platform::cadence::interface_path_matches;
    using winchisel::platform::cadence::resolve_container_id;
    {
        const std::string path = R"(\\?\USB#VID_046D&PID_C52B#ABC123#{a5dcbf10-6530-11d2-901f-00c04fb951ed})";
        check(interface_path_matches(path, R"(USB\VID_046D&PID_C52B\ABC123)"), "pfad matcht instanz");
        check(interface_path_matches(path, R"(usb\vid_046d&pid_c52b\abc123)"), "match case-insensitiv");
        check(!interface_path_matches(path, R"(USB\VID_046D&PID_C52B\XYZ999)"), "fremde instanz kein match");
        check(!interface_path_matches("", R"(USB\VID_046D&PID_C52B\ABC123)"), "leerer pfad kein match");
        check(!interface_path_matches(path, ""), "leere instanz kein match");
        // Kind (Interface, MI_) gewinnt gegen Elter (Composite) bei der Container-Auflösung.
        const std::vector<UsbInstance> list = {
            {R"(USB\VID_39AE&PID_400A\DEFG)", "{11111111-1111-1111-1111-111111111111}", "USB\\ROOT_HUB", "39AE", "400A", ""},
            {R"(USB\VID_39AE&PID_400A&MI_00\DEFG&0000)", "{22222222-2222-2222-2222-222222222222}", R"(USB\VID_39AE&PID_400A\DEFG)", "39AE", "400A", ""},
        };
        auto c = resolve_container_id(R"(\\?\USB#VID_39AE&PID_400A&MI_00#DEFG&0000#{guid})", list);
        check(c && *c == "{22222222-2222-2222-2222-222222222222}", "laengster treffer (kind) gewinnt");
        check(!resolve_container_id(R"(\\?\USB#VID_9999&PID_0001#ZZZ#{guid})", list), "unbekannt -> nullopt");
        check(!resolve_container_id("", list), "leerer pfad -> nullopt");
    }
    // Endpoint-Detail + SuperSpeed-Companion aus synthetischem Config-Blob.
    {
        // Config(9) + Interface(9) + Endpoint HS Interrupt IN 0x81 bi=1 (7)
        // + Endpoint SS Interrupt IN 0x82 bi=4 (7) + Companion (6).
        const std::vector<unsigned char> blob = {
            9, 0x02, 0, 0, 0, 0, 0, 0, 0,
            9, 0x04, 0, 0, 0, 0, 0, 0, 0,
            7, 0x05, 0x81, 0x03, 0x00, 0x00, 0x01,
            7, 0x05, 0x82, 0x03, 0x00, 0x04, 0x04,
            6, 0x30, 0x02, 0x00, 0x00, 0x04,
        };
        auto hs = endpoint_detail(blob, 0x81);
        check(hs.found && hs.interrupt && hs.b_interval == 1 && !hs.has_companion, "hs-endpoint ohne companion");
        check(hs.has_interface && hs.interface_number == 0 && hs.alt_setting == 0, "hs interface/alt");
        auto ss = endpoint_detail(blob, 0x82);
        check(ss.found && ss.interrupt && ss.b_interval == 4, "ss-endpoint interval");
        check(ss.has_companion && ss.max_burst == 2 && ss.bytes_per_interval == 0x0400, "companion burst+bytes");
        auto missing = endpoint_detail(blob, 0x83);
        check(!missing.found, "fremde adresse nicht gefunden");
        check(!endpoint_detail({}, 0x81).found, "leerer blob nicht gefunden");
        // Bulk-Endpoint ist kein Interrupt, wird aber gefunden.
        const std::vector<unsigned char> bulk = {7, 0x05, 0x01, 0x02, 0x00, 0x00, 0x00};
        auto b = endpoint_detail(bulk, 0x01);
        check(b.found && !b.interrupt, "bulk-endpoint markiert");
        // Interface/Alt-Setting: zweites Interface mit Alt 1 gewinnt für seinen Endpoint.
        const std::vector<unsigned char> alt = {
            9, 0x04, 0x00, 0x00, 0, 0, 0, 0, 0,
            7, 0x05, 0x81, 0x03, 0x00, 0x00, 0x08,
            9, 0x04, 0x01, 0x01, 0, 0, 0, 0, 0,
            7, 0x05, 0x82, 0x03, 0x00, 0x00, 0x04,
        };
        auto a1 = endpoint_detail(alt, 0x81);
        check(a1.has_interface && a1.interface_number == 0 && a1.alt_setting == 0, "endpoint if0 alt0");
        auto a2 = endpoint_detail(alt, 0x82);
        check(a2.has_interface && a2.interface_number == 1 && a2.alt_setting == 1, "endpoint if1 alt1");
    }
    // Große Eingabe: Analyse bleibt linear schnell und ok.
    {
        CadenceConfig cfg;
        cfg.warmup_skip = 0;
        auto r = analyze_capture(std::vector<double>(60000, 125.0), cfg, false);
        check(r.status == CadenceStatus::ok && r.steady_samples == 60000, "60k samples ok");
    }

    std::printf(failures == 0 ? "ALL PASS\n" : "FAILURES %d\n", failures);
    return failures == 0 ? 0 : 1;
}
