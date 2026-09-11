#include "winchisel/platform/latency.hpp"

#include <iostream>

int main() {
    auto result = winchisel::platform::analyze_usb_topology();
    if (!result) {
        std::cerr << result.error().detail << '\n';
        return 1;
    }
    for (auto const& line : result->lines) std::cout << line.text << '\n';
    if (!result->optimizations.empty()) {
        std::cout << '\n' << "  OPTIMIZATIONS AVAILABLE" << '\n';
        std::cout << "  ---------------------------------------------------------------------" << '\n' << '\n';
        for (auto const& opt : result->optimizations) std::cout << "  " << opt.text << '\n';
        std::cout << '\n';
    }
}
