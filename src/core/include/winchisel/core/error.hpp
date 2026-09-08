#pragma once

#include <expected>
#include <string>

namespace winchisel::core {

struct Error {
    std::string detail;
};

template <typename T>
using Result = std::expected<T, Error>;

}  // namespace winchisel::core
