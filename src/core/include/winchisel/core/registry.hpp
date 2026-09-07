#pragma once

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace winchisel::core {

enum class RegistryHive : std::uint8_t {
    current_user,
    local_machine,
};

enum class RegistryValueType : std::uint8_t {
    dword,
    string,
    binary,
};

struct RegistryTarget {
    RegistryHive hive{};
    std::string key_path;
    std::string value_name;
    RegistryValueType type{};
};

using RegistryValue = std::variant<std::monostate, std::uint32_t, std::string, std::vector<std::uint8_t>>;

inline bool registry_value_matches(RegistryValue const& actual, RegistryValue const& expected) {
    return actual == expected;
}

}  // namespace winchisel::core
