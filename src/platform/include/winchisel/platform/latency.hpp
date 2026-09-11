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

}
