#pragma once

#include "omega/asset/level_material_catalogs_ir.h"
#include "omega/asset/level_spatial_ir.h"

namespace omega::asset
{
// Canonical owned content decoded in one archive traversal. Both collections preserve the
// corresponding LevelManifestIR terrain-cell order and cardinality. Keeping them together proves
// only coherent ownership; it assigns no mesh-to-material, texture, placement, visibility, or
// rendering relationship.
struct LevelContentIR
{
    LevelSpatialIR spatial;
    LevelMaterialCatalogsIR material_catalogs;

    bool operator==(const LevelContentIR&) const = default;
};
} // namespace omega::asset
