#pragma once

#include <span>
#include <string_view>

namespace winchisel::core {

enum class DebloatCategory { windows_apps, capabilities, optional_features };

struct DebloatCatalogEntry {
    std::string_view id;
    std::string_view name;
    std::string_view description_key;
    DebloatCategory category;
    std::string_view group;
    std::string_view package_name;
    std::string_view package_aliases;
    bool can_reinstall;
    bool requires_reboot;
    std::string_view store_id;
};

std::span<DebloatCatalogEntry const> get_debloat_catalog() noexcept;

}  // namespace winchisel::core
