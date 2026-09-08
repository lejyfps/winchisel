#pragma once

#include "winchisel/core/error.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace winchisel::platform {

winchisel::core::Result<void> set_ifeo_dword(std::wstring const& image, std::wstring_view value_name, std::uint32_t value);
winchisel::core::Result<void> remove_ifeo_dword(std::wstring const& image, std::wstring_view value_name);
std::optional<std::uint32_t> read_ifeo_dword(std::wstring const& image, std::wstring_view value_name);

}  // namespace winchisel::platform
