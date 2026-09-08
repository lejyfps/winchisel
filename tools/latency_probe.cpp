#include "winchisel/platform/latency.hpp"

#include <iostream>

int main() {
    auto result = winchisel::platform::analyze_usb_topology();
    if (!result) {
        std::cerr << result.error().detail << '\n';
        return 1;
    }
    for (auto const& line : result->lines) std::cout << line.text << '\n';
}
