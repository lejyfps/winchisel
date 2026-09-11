#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace winchisel::core {

// Disk-cleanup categories for the Cleanup page. Phase A is safe without
// elevation and Store-safe (user temp, caches, recycle bin). Phase B touches
// system locations, needs real elevation, and stays hidden on packaged
// (Store) builds where elevation is unavailable and certification is strict.
enum class CleanupCategory : std::uint8_t {
    user_temp,
    windows_temp,
    recycle_bin,
    thumbnails,
    delivery_optimization,
    shader_cache,
    update_cleanup,
    previous_installations,
    prefetch,
};

struct CleanupCategoryInfo {
    CleanupCategory category;
    std::string_view id;
    bool phase_b;
    bool requires_admin;
};

inline constexpr std::array k_cleanup_categories{
    CleanupCategoryInfo{CleanupCategory::user_temp, "user-temp", false, false},
    CleanupCategoryInfo{CleanupCategory::windows_temp, "windows-temp", false, false},
    CleanupCategoryInfo{CleanupCategory::recycle_bin, "recycle-bin", false, false},
    CleanupCategoryInfo{CleanupCategory::thumbnails, "thumbnails", false, false},
    CleanupCategoryInfo{CleanupCategory::delivery_optimization, "delivery-optimization", false, false},
    CleanupCategoryInfo{CleanupCategory::shader_cache, "shader-cache", false, false},
    CleanupCategoryInfo{CleanupCategory::update_cleanup, "update-cleanup", true, true},
    CleanupCategoryInfo{CleanupCategory::previous_installations, "previous-installations", true, true},
    CleanupCategoryInfo{CleanupCategory::prefetch, "prefetch", true, true},
};

inline constexpr std::size_t k_cleanup_phase_a_count = 6;

inline bool cleanup_is_phase_b(CleanupCategory category) {
    for (auto const& info : k_cleanup_categories) {
        if (info.category == category) return info.phase_b;
    }
    return false;
}

inline std::optional<CleanupCategory> cleanup_category_from_id(std::string_view id) {
    for (auto const& info : k_cleanup_categories) {
        if (info.id == id) return info.category;
    }
    return std::nullopt;
}

inline std::string_view cleanup_category_id(CleanupCategory category) {
    for (auto const& info : k_cleanup_categories) {
        if (info.category == category) return info.id;
    }
    return {};
}

}  // namespace winchisel::core
