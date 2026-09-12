#pragma once
#include "winchisel/core/error.hpp"
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace winchisel::platform {

enum class LatencyColor { normal, success, warning, critical, accent, muted, separator };
struct LatencyLine { std::string text; LatencyColor color{LatencyColor::normal}; bool bold{}; };
struct LatencyAnalysis { std::vector<LatencyLine> lines; std::vector<LatencyLine> optimizations; };
using LatencyProgress = std::function<void(int, std::string_view)>;

winchisel::core::Result<LatencyAnalysis> analyze_usb_topology(LatencyProgress progress = {});

// Strukturierter Topologie-Kontext zu einem Gerät (für Kadenz-Report):
// Controller/Plattform, Chip-Ebene, Hubs, IRQ-Modus, Suspend-Flags.
// VID/PID-Vergleich case-insensitiv. found=false = kein Treffer (kein Fehler).
struct TopologyContext {
    bool found = false;
    std::string device_name;
    std::string controller_name;
    std::string platform;
    std::string msi_status;
    int chip_count = 0;
    int hub_count = 0;
    std::vector<std::string> hub_names;
    bool selective_suspend = false;
};

TopologyContext topology_context_for(std::string const& vid, std::string const& pid);

}
