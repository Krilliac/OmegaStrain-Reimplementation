#pragma once

#include "omega/asset/decode.h"
#include "omega/asset/geometry_ir.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace omega::retail
{
// One project-owned visual mesh reconstructed from a VUM final-payload geometry batch. Positions,
// uvs, colors, and triangle_indices share a single per-batch vertex count. This carries no retail
// material, GS register, or draw-order semantics -- only decoded per-vertex geometry.
struct VumVisualMeshIR
{
    std::vector<asset::Float3IR> positions;
    // Texture coordinates, one per position, in the source signed16 / 4096 domain
    // (approximately [-8, 8), without clamping or normalization).
    std::vector<std::array<float, 2>> uvs;
    // Per-vertex diffuse color, one per position, each component 0..1 (source u8 / 255).
    std::vector<asset::Float3IR> colors;
    // Flat sequence of complete three-index triangles addressing this mesh's positions. Assembled
    // as a single triangle strip per batch (strip-break topology is a documented follow-up).
    std::vector<std::uint32_t> triangle_indices;

    bool operator==(const VumVisualMeshIR&) const = default;
};

struct VumVisualGeometryIR
{
    std::vector<VumVisualMeshIR> meshes;

    bool operator==(const VumVisualGeometryIR&) const = default;
};

// [any worker thread; reentrant] Decodes the geometry batches from a VUM final-payload VIF packet
// stream span (already isolated to [final_payload_begin, primary_end)). Handles NOP, non-fill
// STCYCL, STMOD, STMASK, STROW, FLUSH, MSCAL, and the unpacked VIF attribute forms used below.
// Fill-mode STCYCL and packed V4_5 return UnsupportedVariant before their payload semantics can
// desynchronize the walk. Per batch, reconstructs per-vertex positions from the two V4-32 anchor
// points (bounding box) and the V3-16 signed box fractions
// (pos = center + (s16/32768) * halfext), UVs (V2-16 / 4096), and colors (V3-8 / 255). Fail-soft: a
// batch that cannot be parsed coherently stops further batch decoding for that stream and the
// coherent batches decoded so far are returned. Reconstructed position bounds match the
// collision-cell bounds (validated). Caller limits bound the complete input span, cumulative
// retained meshes/attributes/indices, parsed items, and peak dynamic BatchState storage.
[[nodiscard]] asset::DecodeResult<VumVisualGeometryIR> DecodeVumVisualGeometryBatches(
    std::span<const std::byte> final_payload, asset::DecodeLimits limits = {});

// [any worker thread; reentrant] Convenience wrapper: validates the full VUM byte layout, isolates
// the final payload region, and decodes its geometry batches.
[[nodiscard]] asset::DecodeResult<VumVisualGeometryIR> DecodeVumVisualGeometry(
    std::span<const std::byte> vum_bytes, asset::DecodeLimits limits = {});
} // namespace omega::retail
