#pragma once

#include "omega/asset/geometry_ir.h"

#include <cstdint>
#include <vector>

namespace omega::asset
{
// Fully owned, renderer-neutral triangle geometry. triangle_indices is a flat sequence of complete
// three-index triangles addressing positions. Materials, GPU resources, retail offsets, source
// locations, and inferred retail draw semantics are deliberately absent.
struct RenderMeshIR
{
    std::vector<Float3IR> positions;
    std::vector<std::uint32_t> triangle_indices;

    bool operator==(const RenderMeshIR&) const = default;
};
} // namespace omega::asset
