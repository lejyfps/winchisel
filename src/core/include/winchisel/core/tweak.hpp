#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace winchisel::core {

// These types deliberately contain no Win32 or WinUI details. Platform maps a
// definition to registry/services/tasks; UI only renders the resulting state.
enum class TweakInput : std::uint8_t {
    toggle,
    selection,
};

enum class TweakGroup : std::uint8_t {
    performance,
    privacy_security,
};

struct TweakProfile {
    std::optional<bool> recommended_toggle;
    std::optional<bool> default_toggle;
    std::optional<std::int32_t> recommended_selection;
    std::optional<std::int32_t> default_selection;
};

struct TweakDefinition {
    std::int32_t numeric_id{};
    std::string_view id;
    TweakGroup group{};
    TweakInput input{TweakInput::toggle};
    std::string_view title_key;
    std::string_view description_key;
    TweakProfile profile;
    bool is_new{};
};

struct TweakProfileMatch {
    bool recommended{};
    bool windows_default{};

    constexpr bool custom() const noexcept {
        return !recommended && !windows_default;
    }
};

constexpr TweakProfileMatch match_toggle_profile(TweakProfile const& profile, bool current) noexcept {
    return {
        .recommended = profile.recommended_toggle && current == *profile.recommended_toggle,
        .windows_default = profile.default_toggle && current == *profile.default_toggle,
    };
}

constexpr TweakProfileMatch match_selection_profile(TweakProfile const& profile, std::int32_t current) noexcept {
    return {
        .recommended = profile.recommended_selection && current == *profile.recommended_selection,
        .windows_default = profile.default_selection && current == *profile.default_selection,
    };
}

}  // namespace winchisel::core
