#include "winchisel/core/tweak.hpp"
#include "winchisel/core/registry.hpp"

namespace winchisel::core {
namespace {

constexpr TweakProfile k_toggle_profile{
    .recommended_toggle = true,
    .default_toggle = false,
};
constexpr TweakProfile k_selection_profile{
    .recommended_selection = 2,
    .default_selection = 0,
};

static_assert(match_toggle_profile(k_toggle_profile, true).recommended);
static_assert(match_toggle_profile(k_toggle_profile, false).windows_default);
static_assert(match_selection_profile(k_selection_profile, 1).custom());
static_assert(k_performance_groups.size() == 10);
static_assert(k_privacy_security_groups.size() == 13);

const RegistryTarget k_registry_target{
    .hive = RegistryHive::current_user,
    .key_path = "Software\\Winchisel",
    .value_name = "Example",
    .type = RegistryValueType::dword,
};

}  // namespace
}  // namespace winchisel::core
