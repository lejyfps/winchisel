#include "winchisel/core/debloater.hpp"
#include "debloater_catalog.generated.hpp"

namespace winchisel::core {

std::span<DebloatCatalogEntry const> get_debloat_catalog() noexcept {
    return generated_debloat_catalog;
}

}  // namespace winchisel::core
