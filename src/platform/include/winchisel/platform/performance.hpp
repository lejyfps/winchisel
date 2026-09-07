#pragma once
#include "winchisel/core/error.hpp"
#include <string_view>
namespace winchisel::platform {
winchisel::core::Result<int> read_dns_profile();
winchisel::core::Result<void> write_dns_profile(int index);
winchisel::core::Result<bool> read_scheduled_task(std::string_view id);
winchisel::core::Result<void> write_scheduled_task(std::string_view id, bool enabled);
}
