#pragma once

#include <cstdint>

namespace winchisel::core {

enum class Screen : std::uint8_t {
    home,
    debloater,
    performance,
    privacy_security,
    downloads,
    processes,
    latency,
    startup,
    extras,
    settings,
};

}  // namespace winchisel::core
