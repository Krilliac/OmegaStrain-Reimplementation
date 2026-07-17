#pragma once

#include "omega/asset/decode.h"
#include "omega/asset/spatial_mesh_ir.h"

#include <cstddef>
#include <span>

namespace omega::retail
{
// [any worker thread; reentrant] Converts the independently documented COL
// spatial tables into canonical owned geometry. The result retains no retail
// bytes or input spans.
[[nodiscard]] asset::DecodeResult<asset::SpatialMeshIR> DecodeColSpatialMesh(
    std::span<const std::byte> bytes, asset::DecodeLimits limits = {});
} // namespace omega::retail
