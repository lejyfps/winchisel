#pragma once

#include <cstdint>

namespace winchisel::core {

// Ist: CurrentBuildNumber >= 26100 (Windows 11 24H2+).
inline constexpr std::uint32_t k_min_windows_build = 26100;

}  // namespace winchisel::core
