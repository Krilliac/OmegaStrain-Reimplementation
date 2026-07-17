#pragma once

#include "omega/asset/spatial_mesh_ir.h"

#include <vector>

namespace omega::asset
{
// Canonical owned spatial meshes in the same order and cardinality as the corresponding
// LevelManifestIR terrain cells. Placement, transforms, visibility, and collision response remain
// separate evidence-driven contracts.
struct LevelSpatialIR
{
    std::vector<SpatialMeshIR> terrain_cells;

    bool operator==(const LevelSpatialIR&) const = default;
};
} // namespace omega::asset
