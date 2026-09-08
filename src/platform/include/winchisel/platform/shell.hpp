#pragma once

#include "winchisel/core/error.hpp"

#include <string_view>

namespace winchisel::platform {

winchisel::core::Result<void> open_https_url(std::wstring_view url);

}  // namespace winchisel::platform
