#pragma once
#include <bit>
#include <cstdint>

namespace winchisel::core {
// Modes: all, even logical CPUs, odd logical CPUs, first half, second half.
constexpr std::uint64_t affinity_mask(std::uint64_t system, int mode) {
    if (mode < 1 || mode > 4) return system;
    std::uint64_t target{};
    const int split = (std::popcount(system) + 1) / 2;
    int index{};
    for (unsigned logical{}; logical < 64; ++logical) {
        const auto bit = std::uint64_t{1} << logical;
        if (!(system & bit)) continue;
        if ((mode == 1 && logical % 2 == 0) || (mode == 2 && logical % 2 == 1) ||
            (mode == 3 && index < split) || (mode == 4 && index >= split)) target |= bit;
        ++index;
    }
    return target ? target : system;
}
}
