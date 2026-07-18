#pragma once

#include "omega/asset/material_catalog_ir.h"

#include <vector>

namespace omega::asset
{
// Canonical owned material/name catalogs in the same order and cardinality as the corresponding
// LevelManifestIR terrain cells. Each catalog comes from the cell HOG's unique VUM member. Names
// retain no assigned role, and no relationship to spatial triangles, TDX assets, placement,
// transforms, visibility, or render draws is asserted.
struct LevelMaterialCatalogsIR
{
    std::vector<MaterialCatalogIR> terrain_cells;

    bool operator==(const LevelMaterialCatalogsIR&) const = default;
};
} // namespace omega::asset
