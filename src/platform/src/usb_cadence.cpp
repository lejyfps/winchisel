#include "winchisel/platform/usb_cadence.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace winchisel::platform::cadence {
namespace {

constexpr double kMaxIntervalUs = 50000.0; // >= 50 ms: kein Poll-Intervall, Bus-Pause

double median_of(std::vector<double> const& sorted) {
    return percentile_interpolated(sorted, 50.0);
}

} // namespace

double percentile_interpolated(std::vector<double> const& sorted, double p) {
    if (sorted.empty()) return 0.0;
    if (p <= 0.0) return sorted.front();
    if (p >= 100.0) return sorted.back();
    const double rank = p / 100.0 * static_cast<double>(sorted.size() - 1);
    const std::size_t lo = static_cast<std::size_t>(rank);
    const std::size_t hi = lo + 1 < sorted.size() ? lo + 1 : lo;
    const double frac = rank - static_cast<double>(lo);
    return sorted[lo] + frac * (sorted[hi] - sorted[lo]);
}

std::vector<HistBin> histogram_bins(std::vector<double> const& sorted, std::size_t bins) {
    if (sorted.empty()) return {};
    if (bins < 2) bins = 2;
    if (bins > 16) bins = 16;
    const double lo = sorted.front();
    double hi = percentile_interpolated(sorted, 99.9);
    if (!(hi > lo)) {
        return {{lo, lo, sorted.size(), false}};
    }
    const double width = (hi - lo) / static_cast<double>(bins);
    std::vector<HistBin> result;
    result.reserve(bins + 1);
    for (std::size_t i = 0; i < bins; ++i)
        result.push_back({lo + width * static_cast<double>(i), lo + width * static_cast<double>(i + 1), 0, false});
    result.push_back({hi, 0.0, 0, true});
    for (const double value : sorted) {
        if (value <= lo) {
            ++result.front().count;
            continue;
        }
        if (value > hi) {
            ++result.back().count;
            continue;
        }
        std::size_t index = static_cast<std::size_t>((value - lo) / width);
        if (index >= bins) index = bins - 1;
        ++result[index].count;
    }
    return result;
}

std::optional<double> expected_hz(UsbSpeed speed, int b_interval) {
    switch (speed) {
    case UsbSpeed::high:
        if (b_interval < 1 || b_interval > 16) return std::nullopt;
        return 8000.0 / std::pow(2.0, static_cast<double>(b_interval - 1));
    case UsbSpeed::super:
        // SuperSpeed-Interrupt: bInterval 1..16, gleiche Ableitung wie HighSpeed.
        if (b_interval < 1 || b_interval > 16) return std::nullopt;
        return 8000.0 / std::pow(2.0, static_cast<double>(b_interval - 1));
    case UsbSpeed::full:
        if (b_interval < 1 || b_interval > 255) return std::nullopt;
        return 1000.0 / static_cast<double>(b_interval);
    case UsbSpeed::low: {
        if (b_interval < 1 || b_interval > 255) return std::nullopt;
        const int clamped = b_interval < 10 ? 10 : b_interval;
        return 1000.0 / static_cast<double>(clamped);
    }
    case UsbSpeed::unknown:
        return std::nullopt;
    }
    return std::nullopt;
}

CadenceResult analyze_capture(std::vector<double> intervals_us, CadenceConfig const& config, bool events_lost) {
    CadenceResult result;
    // Warm-up verwerfen, dann auf plausible Poll-Intervalle filtern.
    std::vector<double> kept;
    kept.reserve(intervals_us.size());
    for (std::size_t i = config.warmup_skip; i < intervals_us.size(); ++i) {
        const double value = intervals_us[i];
        if (value >= 0.0 && value < kMaxIntervalUs) kept.push_back(value);
    }
    result.kept_samples = kept.size();
    if (kept.empty()) {
        result.status = events_lost ? CadenceStatus::events_lost : CadenceStatus::insufficient_data;
        return result;
    }

    std::vector<double> sorted = kept;
    std::sort(sorted.begin(), sorted.end());
    result.median_us = median_of(sorted);
    result.p95_us = percentile_interpolated(sorted, 95.0);
    result.p99_us = percentile_interpolated(sorted, 99.0);
    result.p999_us = percentile_interpolated(sorted, 99.9);
    result.histogram = histogram_bins(sorted);

    // Adaptive Stall-Schwelle aus der stabilen Verteilung (Tukey):
    // Q3 + 3*IQR, bei IQR=0 Fallback-Spread 0.25*Median, mindestens 2*Median.
    // Kein fester 1000-µs-Boden (der bei 8 kHz 8 Intervalle verschluckt).
    {
        const double q1 = percentile_interpolated(sorted, 25.0);
        const double q3 = percentile_interpolated(sorted, 75.0);
        const double iqr = q3 - q1;
        const double spread = iqr > 0.0 ? iqr : result.median_us * 0.25;
        const double fence = q3 + 3.0 * spread;
        result.threshold_us = (std::max)(fence, 2.0 * result.median_us);
    }

    double steady_sum = 0.0;
    for (std::size_t i = 0; i < kept.size(); ++i) {
        if (kept[i] < result.threshold_us) {
            steady_sum += kept[i];
            ++result.steady_samples;
        } else {
            result.stalls.push_back({i, kept[i]});
            result.stall_ms += kept[i] / 1000.0;
        }
    }
    if (result.steady_samples > 0) {
        const double mean = steady_sum / static_cast<double>(result.steady_samples);
        if (mean > 0.0) result.rate_hz = 1e6 / mean;
    }

    if (events_lost) {
        result.status = CadenceStatus::events_lost;
    } else if (result.steady_samples < config.min_samples) {
        result.status = CadenceStatus::insufficient_data;
    } else {
        result.status = CadenceStatus::ok;
    }
    return result;
}

EndpointDetail endpoint_detail(std::vector<unsigned char> const& blob, unsigned long address) {
    EndpointDetail detail;
    unsigned long if_number = 0, alt = 0;
    bool have_if = false;
    std::size_t i = 0;
    while (i + 1 < blob.size()) {
        const std::size_t len = blob[i];
        if (len < 2 || i + len > blob.size()) break;
        const unsigned char type = blob[i + 1];
        if (type == 0x04 && len >= 4) { // Interface-Deskriptor
            if_number = blob[i + 2];
            alt = blob[i + 3];
            have_if = true;
        }
        if (type == 0x05 && len >= 7) { // Endpoint-Deskriptor
            if (blob[i + 2] == static_cast<unsigned char>(address & 0xFF)) {
                detail.found = true;
                detail.interrupt = (blob[i + 3] & 0x03) == 0x03;
                detail.b_interval = blob[i + 6];
                detail.has_interface = have_if;
                detail.interface_number = if_number;
                detail.alt_setting = alt;
                // Direkt folgender Companion? (SuperSpeed, Typ 0x30, >= 6 Byte)
                const std::size_t next = i + len;
                if (next + 1 < blob.size() && blob[next + 1] == 0x30 && blob[next] >= 6 &&
                    next + 6 <= blob.size()) {
                    detail.has_companion = true;
                    detail.max_burst = blob[next + 2];
                    detail.bytes_per_interval =
                        static_cast<unsigned long>(blob[next + 4]) |
                        (static_cast<unsigned long>(blob[next + 5]) << 8);
                }
                return detail;
            }
        }
        i += len;
    }
    return detail;
}

} // namespace winchisel::platform::cadence
