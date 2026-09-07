#pragma once

#include <expected>
#include <string>
#include <string_view>

namespace winchisel::core {

enum class ErrorCode {
    ok = 0,
    io,
    parse,
    unsupported_os,
    elevation,
    cancelled,
    platform,
    not_found,
};

struct Error {
    ErrorCode code{ErrorCode::platform};
    std::string message_key;
    std::string detail;
};

template <typename T>
using Result = std::expected<T, Error>;

}  // namespace winchisel::core
