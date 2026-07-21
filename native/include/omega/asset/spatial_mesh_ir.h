#pragma once

#include "omega/asset/geometry_ir.h"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace omega::asset
{
struct Bounds3IR
{
    Float3IR minimum;
    Float3IR maximum;

    bool operator==(const Bounds3IR&) const = default;
};

struct SpatialTriangleIR
{
    std::array<std::uint32_t, 3> vertex_indices{};

    bool operator==(const SpatialTriangleIR&) const = default;
};

enum class SpatialElementKind : std::uint8_t
{
    Node,
    Leaf,
};

struct SpatialElementRefIR
{
    SpatialElementKind kind = SpatialElementKind::Node;
    std::uint32_t index = 0;

    bool operator==(const SpatialElementRefIR&) const = default;
};

struct SpatialNodeIR
{
    Bounds3IR bounds;
    // Source slot ordering is preserved. Octant meaning remains intentionally
    // unassigned.
    std::array<std::optional<SpatialElementRefIR>, 8> children{};

    bool operator==(const SpatialNodeIR&) const = default;
};

struct SpatialLeafIR
{
    Bounds3IR bounds;
    std::uint32_t first_triangle_reference = 0;
    std::uint32_t triangle_reference_count = 0;

    bool operator==(const SpatialLeafIR&) const = default;
};

// Canonical, fully owned spatial geometry and its read-only acceleration tree.
// Leaf ranges address leaf_triangle_references, whose values address triangles.
// No retail offsets, padding, opaque fields, console instructions, or input
// views survive decoding.
struct SpatialMeshIR
{
    std::vector<Float3IR> vertices;
    std::vector<SpatialTriangleIR> triangles;
    std::vector<std::uint32_t> leaf_triangle_references;
    std::vector<SpatialNodeIR> nodes;
    std::vector<SpatialLeafIR> leaves;
    std::optional<SpatialElementRefIR> root;

    bool operator==(const SpatialMeshIR&) const = default;
};
} // namespace omega::asset
