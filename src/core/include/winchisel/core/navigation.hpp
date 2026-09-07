#pragma once

#include <cstdint>
#include <string_view>

namespace winchisel::core {

enum class Screen : std::uint8_t {
    home,
    debloater,
    performance,
    privacy_security,
    downloads,
    processes,
    latency,
    extras,
    settings,
};

inline constexpr std::string_view screen_id(Screen screen) {
    switch (screen) {
        case Screen::home:
            return "home";
        case Screen::debloater:
            return "debloater";
        case Screen::performance:
            return "performance";
        case Screen::privacy_security:
            return "privacy_security";
        case Screen::downloads:
            return "downloads";
        case Screen::processes:
            return "processes";
        case Screen::latency:
            return "latency";
        case Screen::extras:
            return "extras";
        case Screen::settings:
            return "settings";
    }
    return "home";
}

}  // namespace winchisel::core
