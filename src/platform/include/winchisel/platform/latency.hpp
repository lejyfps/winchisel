#pragma once
#include "winchisel/core/error.hpp"
#include <string>
namespace winchisel::platform { winchisel::core::Result<std::string> analyze_usb_topology(); }
