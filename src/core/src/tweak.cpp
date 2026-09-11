#include "winchisel/core/tweak.hpp"
#include "performance_catalog.generated.hpp"
#include "privacy_catalog.generated.hpp"

namespace winchisel::core {
std::span<PerformanceCatalogEntry const> get_performance_catalog() noexcept { return generated_performance_catalog; }
std::span<PerformanceRegistryRule const> get_performance_registry_rules() noexcept { return generated_performance_registry_rules; }
std::span<PerformanceProfileRule const> get_performance_profile_rules() noexcept { return generated_performance_profile_rules; }
std::span<PrivacyCatalogEntry const> get_privacy_catalog() noexcept { return generated_privacy_catalog; }
std::span<PrivacyRegistryRule const> get_privacy_registry_rules() noexcept { return generated_privacy_rules; }

static_assert(k_performance_groups.size() == 16);
static_assert(k_privacy_security_groups.size() == 13);

}  // namespace winchisel::core
