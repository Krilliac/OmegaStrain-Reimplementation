#include "omega/retail/col_spatial_mesh_decoder.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace omega::retail
{
namespace
{
constexpr std::uint64_t kHeaderBytes = 48;
constexpr std::uint64_t kNodeBytes = 64;
constexpr std::uint64_t kLeafBytes = 48;
constexpr std::uint64_t kTriangleBytes = 16;
constexpr std::uint64_t kVertexBytes = 16;
constexpr std::uint32_t kFloatOneBits = 0x3F800000U;
constexpr std::uint32_t kFloatMaximumBits = 0x7F7FFFFFU;
constexpr std::uint32_t kFloatNegativeMaximumBits = 0xFF7FFFFFU;

struct ColLayout
{
    std::uint8_t version = 0;
    std::uint32_t node_count = 0;
    std::uint32_t leaf_count = 0;
    std::uint32_t triangle_count = 0;
    std::uint32_t vertex_count = 0;
    std::uint64_t nodes_begin = kHeaderBytes;
    std::uint64_t nodes_end = kHeaderBytes;
    std::uint64_t leaves_begin = kHeaderBytes;
    std::uint64_t leaves_end = kHeaderBytes;
    std::uint64_t triangles_begin = kHeaderBytes;
    std::uint64_t triangles_end = kHeaderBytes;
    std::uint64_t vertices_begin = kHeaderBytes;
    std::uint64_t vertices_end = kHeaderBytes;
    std::uint64_t index_data_end = kHeaderBytes;
    std::uint64_t index_region_end = kHeaderBytes;
};

struct PendingRecord
{
    asset::SpatialElementRefIR reference;
    std::uint32_t depth = 0;
};

[[nodiscard]] asset::DecodeError Error(const asset::DecodeErrorCode code, std::string message,
    const std::optional<std::uint64_t> byte_offset = std::nullopt)
{
    return asset::DecodeError{
        .code = code,
        .byte_offset = byte_offset,
        .message = std::move(message),
    };
}

[[nodiscard]] std::uint16_t ReadU16(
    const std::span<const std::byte> bytes, const std::size_t offset) noexcept
{
    return std::to_integer<std::uint16_t>(bytes[offset]) |
           (std::to_integer<std::uint16_t>(bytes[offset + 1]) << 8U);
}

[[nodiscard]] std::uint32_t ReadU32(
    const std::span<const std::byte> bytes, const std::size_t offset) noexcept
{
    return std::to_integer<std::uint32_t>(bytes[offset]) |
           (std::to_integer<std::uint32_t>(bytes[offset + 1]) << 8U) |
           (std::to_integer<std::uint32_t>(bytes[offset + 2]) << 16U) |
           (std::to_integer<std::uint32_t>(bytes[offset + 3]) << 24U);
}

[[nodiscard]] float ReadF32(
    const std::span<const std::byte> bytes, const std::size_t offset) noexcept
{
    return std::bit_cast<float>(ReadU32(bytes, offset));
}

[[nodiscard]] bool Add(
    const std::uint64_t left, const std::uint64_t right, std::uint64_t& result) noexcept
{
    if (right > std::numeric_limits<std::uint64_t>::max() - left)
        return false;
    result = left + right;
    return true;
}

[[nodiscard]] bool Multiply(
    const std::uint64_t left, const std::uint64_t right, std::uint64_t& result) noexcept
{
    if (left != 0 && right > std::numeric_limits<std::uint64_t>::max() / left)
        return false;
    result = left * right;
    return true;
}

[[nodiscard]] bool AddWithinLimit(
    std::uint64_t& total, const std::uint64_t amount, const std::uint64_t limit) noexcept
{
    std::uint64_t result = 0;
    if (!Add(total, amount, result) || result > limit)
        return false;
    total = result;
    return true;
}

[[nodiscard]] asset::DecodeResult<ColLayout> ParseLayout(
    const std::span<const std::byte> bytes, const asset::DecodeLimits limits)
{
    if (bytes.size() > limits.maximum_input_bytes)
        return std::unexpected(
            Error(asset::DecodeErrorCode::LimitExceeded, "COL input exceeds decoder byte limit"));
    if (bytes.size() < kHeaderBytes)
        return std::unexpected(Error(asset::DecodeErrorCode::Truncated,
            "COL input is shorter than its observed header", bytes.size()));
    if (bytes.size() % 16U != 0)
        return std::unexpected(
            Error(asset::DecodeErrorCode::Malformed, "COL container span is not 16-byte aligned"));
    if (bytes[0] != std::byte{'C'} || bytes[1] != std::byte{'O'} || bytes[2] != std::byte{'L'})
        return std::unexpected(Error(
            asset::DecodeErrorCode::Malformed, "COL input does not use the observed prefix", 0));

    const std::uint8_t version = std::to_integer<std::uint8_t>(bytes[3]);
    if (version != 3 && version != 5)
        return std::unexpected(Error(
            asset::DecodeErrorCode::UnsupportedVariant, "COL format version is not supported", 3));
    if (ReadU32(bytes, 8) != kHeaderBytes)
        return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
            "COL does not use the observed 48-byte header", 8));

    ColLayout layout{
        .version = version,
        .node_count = ReadU32(bytes, 4),
        .leaf_count = ReadU32(bytes, 12),
        .triangle_count = ReadU32(bytes, 20),
        .vertex_count = ReadU32(bytes, 28),
    };
    constexpr std::array<std::uint64_t, 4> record_bytes{
        kNodeBytes, kLeafBytes, kTriangleBytes, kVertexBytes};
    const std::array<std::uint32_t, 4> counts{
        layout.node_count, layout.leaf_count, layout.triangle_count, layout.vertex_count};
    constexpr std::array<std::size_t, 4> endpoint_offsets{16, 24, 32, 36};
    std::array<std::uint64_t, 5> boundaries{kHeaderBytes, 0, 0, 0, 0};
    for (std::size_t index = 0; index < counts.size(); ++index)
    {
        std::uint64_t table_bytes = 0;
        if (!Multiply(counts[index], record_bytes[index], table_bytes) ||
            !Add(boundaries[index], table_bytes, boundaries[index + 1]))
            return std::unexpected(Error(asset::DecodeErrorCode::Overflow,
                "COL counted table extent overflows", endpoint_offsets[index]));
        if (boundaries[index + 1] != ReadU32(bytes, endpoint_offsets[index]))
            return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
                "COL counted table endpoint contradicts its record count",
                endpoint_offsets[index]));
        if (boundaries[index + 1] > bytes.size())
            return std::unexpected(Error(asset::DecodeErrorCode::Truncated,
                "COL counted table extends past the input", bytes.size()));
    }

    layout.nodes_end = boundaries[1];
    layout.leaves_begin = boundaries[1];
    layout.leaves_end = boundaries[2];
    layout.triangles_begin = boundaries[2];
    layout.triangles_end = boundaries[3];
    layout.vertices_begin = boundaries[3];
    layout.vertices_end = boundaries[4];

    std::uint64_t index_bytes = 0;
    if (!Multiply(layout.triangle_count, sizeof(std::uint32_t), index_bytes) ||
        !Add(layout.vertices_end, index_bytes, layout.index_data_end) ||
        !Add(layout.index_data_end, 15U, layout.index_region_end))
        return std::unexpected(
            Error(asset::DecodeErrorCode::Overflow, "COL index-list extent overflows", 44));
    layout.index_region_end &= ~std::uint64_t{15U};
    if (layout.index_region_end != ReadU32(bytes, 44))
        return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
            "COL index-list endpoint contradicts its triangle count", 44));
    if (layout.index_region_end > bytes.size())
        return std::unexpected(Error(asset::DecodeErrorCode::Truncated,
            "COL index-list region extends past the input", bytes.size()));
    for (std::uint64_t offset = layout.index_data_end; offset < layout.index_region_end; ++offset)
    {
        if (bytes[static_cast<std::size_t>(offset)] != std::byte{0})
            return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
                "COL index-list alignment padding is nonzero", offset));
    }
    return layout;
}

[[nodiscard]] bool IsExactEmptySentinel(
    const std::span<const std::byte> bytes, const ColLayout& layout) noexcept
{
    if (layout.node_count != 1 || layout.leaf_count != 0 || layout.triangle_count != 0 ||
        layout.vertex_count != 0)
        return false;
    constexpr std::array<std::size_t, 3> minimum_offsets{48, 52, 56};
    constexpr std::array<std::size_t, 3> maximum_offsets{64, 68, 72};
    if (!std::ranges::all_of(minimum_offsets,
            [&](const std::size_t offset) {
                return ReadU32(bytes, offset) == kFloatMaximumBits;
            }) ||
        !std::ranges::all_of(maximum_offsets,
            [&](const std::size_t offset) {
                return ReadU32(bytes, offset) == kFloatNegativeMaximumBits;
            }) ||
        ReadU32(bytes, 60) != kFloatOneBits || ReadU32(bytes, 76) != kFloatOneBits)
        return false;
    for (std::size_t offset = 80; offset < 112; offset += sizeof(std::uint32_t))
    {
        if (ReadU32(bytes, offset) != 0)
            return false;
    }
    return true;
}

[[nodiscard]] asset::DecodeResult<asset::Bounds3IR> ReadBounds(
    const std::span<const std::byte> bytes, const std::size_t offset,
    const std::string_view description)
{
    if (ReadU32(bytes, offset + 12U) != kFloatOneBits ||
        ReadU32(bytes, offset + 28U) != kFloatOneBits)
        return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
            std::string(description) + " homogeneous coordinate is not one", offset));
    asset::Bounds3IR bounds{
        .minimum = {ReadF32(bytes, offset), ReadF32(bytes, offset + 4U),
            ReadF32(bytes, offset + 8U)},
        .maximum = {ReadF32(bytes, offset + 16U), ReadF32(bytes, offset + 20U),
            ReadF32(bytes, offset + 24U)},
    };
    const std::array<float, 6> values{bounds.minimum.x, bounds.minimum.y, bounds.minimum.z,
        bounds.maximum.x, bounds.maximum.y, bounds.maximum.z};
    if (!std::ranges::all_of(values, [](const float value) { return std::isfinite(value); }))
        return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
            std::string(description) + " bounds contain a non-finite coordinate", offset));
    if (bounds.minimum.x > bounds.maximum.x || bounds.minimum.y > bounds.maximum.y ||
        bounds.minimum.z > bounds.maximum.z)
        return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
            std::string(description) + " minimum exceeds its maximum", offset));
    return bounds;
}

[[nodiscard]] bool Contains(const asset::Bounds3IR& parent, const asset::Bounds3IR& child) noexcept
{
    return parent.minimum.x <= child.minimum.x && child.maximum.x <= parent.maximum.x &&
           parent.minimum.y <= child.minimum.y && child.maximum.y <= parent.maximum.y &&
           parent.minimum.z <= child.minimum.z && child.maximum.z <= parent.maximum.z;
}

[[nodiscard]] asset::DecodeResult<std::optional<asset::SpatialElementRefIR>> DecodeChild(
    const std::uint32_t offset, const ColLayout& layout, const std::uint64_t field_offset)
{
    if (offset == 0)
        return std::optional<asset::SpatialElementRefIR>{};
    if (offset >= layout.nodes_begin && offset < layout.nodes_end &&
        (offset - layout.nodes_begin) % kNodeBytes == 0)
        return std::optional<asset::SpatialElementRefIR>{asset::SpatialElementRefIR{
            .kind = asset::SpatialElementKind::Node,
            .index = static_cast<std::uint32_t>((offset - layout.nodes_begin) / kNodeBytes),
        }};
    if (offset >= layout.leaves_begin && offset < layout.leaves_end &&
        (offset - layout.leaves_begin) % kLeafBytes == 0)
        return std::optional<asset::SpatialElementRefIR>{asset::SpatialElementRefIR{
            .kind = asset::SpatialElementKind::Leaf,
            .index = static_cast<std::uint32_t>((offset - layout.leaves_begin) / kLeafBytes),
        }};
    return std::unexpected(Error(asset::DecodeErrorCode::InvalidReference,
        "COL child offset does not identify a node or leaf record", field_offset));
}

[[nodiscard]] std::uint64_t GlobalIndex(
    const asset::SpatialElementRefIR reference, const ColLayout& layout) noexcept
{
    return reference.kind == asset::SpatialElementKind::Node
               ? reference.index
               : static_cast<std::uint64_t>(layout.node_count) + reference.index;
}

[[nodiscard]] asset::DecodeResult<void> CheckDecodeBudgets(
    const ColLayout& layout, const bool empty_sentinel, const asset::DecodeLimits limits)
{
    std::uint64_t item_count = 0;
    if (!AddWithinLimit(item_count, layout.node_count, limits.maximum_items) ||
        !AddWithinLimit(item_count, layout.leaf_count, limits.maximum_items) ||
        !AddWithinLimit(item_count, layout.vertex_count, limits.maximum_items) ||
        !AddWithinLimit(item_count, layout.triangle_count, limits.maximum_items) ||
        !AddWithinLimit(item_count, layout.triangle_count, limits.maximum_items))
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
            "COL records and triangle references exceed decoder item limit"));

    std::uint64_t output_bytes = sizeof(asset::SpatialMeshIR);
    const std::uint64_t normalized_nodes = empty_sentinel ? 0U : layout.node_count;
    constexpr std::array<std::uint64_t, 5> element_bytes{sizeof(asset::SpatialNodeIR),
        sizeof(asset::SpatialLeafIR), sizeof(asset::Float3IR), sizeof(asset::SpatialTriangleIR),
        sizeof(std::uint32_t)};
    const std::array<std::uint64_t, 5> counts{normalized_nodes, layout.leaf_count,
        layout.vertex_count, layout.triangle_count, layout.triangle_count};
    for (std::size_t index = 0; index < counts.size(); ++index)
    {
        std::uint64_t bytes = 0;
        if (!Multiply(counts[index], element_bytes[index], bytes))
            return std::unexpected(
                Error(asset::DecodeErrorCode::Overflow, "COL decoded output size overflows"));
        if (!AddWithinLimit(output_bytes, bytes, limits.maximum_output_bytes))
            return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
                "COL decoded mesh exceeds decoder output limit"));
    }

    if (!empty_sentinel)
    {
        std::uint64_t records = 0;
        if (!Add(layout.node_count, layout.leaf_count, records))
            return std::unexpected(
                Error(asset::DecodeErrorCode::Overflow, "COL topology record count overflows"));
        std::uint64_t traversal_bytes = 0;
        if (!Multiply(records, sizeof(std::uint8_t) + sizeof(PendingRecord), traversal_bytes))
            return std::unexpected(
                Error(asset::DecodeErrorCode::Overflow, "COL traversal scratch size overflows"));
        const std::uint64_t scratch_bytes =
            std::max({records, static_cast<std::uint64_t>(layout.triangle_count), traversal_bytes});
        if (scratch_bytes > limits.maximum_scratch_bytes)
            return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
                "COL validation exceeds decoder scratch limit"));
    }
    return {};
}

[[nodiscard]] asset::DecodeResult<void> ValidateLeafBounds(
    const asset::SpatialLeafIR& leaf, const asset::SpatialMeshIR& result)
{
    if (leaf.triangle_reference_count == 0)
        return std::unexpected(Error(
            asset::DecodeErrorCode::Malformed, "COL nonempty leaf has no triangle references"));
    const std::uint32_t first_triangle =
        result.leaf_triangle_references[leaf.first_triangle_reference];
    const auto& first = result.triangles[first_triangle];
    asset::Bounds3IR observed{
        .minimum = result.vertices[first.vertex_indices[0]],
        .maximum = result.vertices[first.vertex_indices[0]],
    };
    const auto include = [&](const asset::Float3IR vertex) {
        observed.minimum.x = std::min(observed.minimum.x, vertex.x);
        observed.minimum.y = std::min(observed.minimum.y, vertex.y);
        observed.minimum.z = std::min(observed.minimum.z, vertex.z);
        observed.maximum.x = std::max(observed.maximum.x, vertex.x);
        observed.maximum.y = std::max(observed.maximum.y, vertex.y);
        observed.maximum.z = std::max(observed.maximum.z, vertex.z);
    };
    for (std::uint32_t index = 0; index < leaf.triangle_reference_count; ++index)
    {
        const std::uint32_t triangle_index =
            result.leaf_triangle_references[leaf.first_triangle_reference + index];
        for (const std::uint32_t vertex_index : result.triangles[triangle_index].vertex_indices)
            include(result.vertices[vertex_index]);
    }
    if (observed.minimum.x != leaf.bounds.minimum.x ||
        observed.minimum.y != leaf.bounds.minimum.y ||
        observed.minimum.z != leaf.bounds.minimum.z ||
        observed.maximum.x != leaf.bounds.maximum.x ||
        observed.maximum.y != leaf.bounds.maximum.y || observed.maximum.z != leaf.bounds.maximum.z)
        return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
            "COL leaf bounds do not equal referenced vertex extents"));
    return {};
}
} // namespace

asset::DecodeResult<asset::SpatialMeshIR> DecodeColSpatialMesh(
    const std::span<const std::byte> bytes, const asset::DecodeLimits limits)
{
    auto layout_result = ParseLayout(bytes, limits);
    if (!layout_result)
        return std::unexpected(layout_result.error());
    const ColLayout& layout = *layout_result;
    const bool empty_counts = layout.node_count == 1 && layout.leaf_count == 0 &&
                              layout.triangle_count == 0 && layout.vertex_count == 0;
    const bool empty_sentinel = IsExactEmptySentinel(bytes, layout);
    if (empty_counts && !empty_sentinel)
        return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
            "COL empty mesh does not use the exact observed sentinel", layout.nodes_begin));
    if (!empty_counts &&
        (layout.leaf_count == 0 || layout.triangle_count == 0 || layout.vertex_count == 0))
        return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
            "COL record counts are outside the observed "
            "empty or populated families",
            4));
    if (layout.node_count == 0 && layout.leaf_count != 1)
        return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
            "COL without nodes does not use the observed single-leaf root", 12));

    auto budget_result = CheckDecodeBudgets(layout, empty_sentinel, limits);
    if (!budget_result)
        return std::unexpected(budget_result.error());
    if (empty_sentinel)
        return asset::SpatialMeshIR{};

    asset::SpatialMeshIR result;
    result.vertices.reserve(layout.vertex_count);
    result.triangles.reserve(layout.triangle_count);
    result.leaf_triangle_references.reserve(layout.triangle_count);
    result.nodes.reserve(layout.node_count);
    result.leaves.reserve(layout.leaf_count);
    result.root =
        layout.node_count != 0
            ? asset::SpatialElementRefIR{.kind = asset::SpatialElementKind::Node, .index = 0}
            : asset::SpatialElementRefIR{.kind = asset::SpatialElementKind::Leaf, .index = 0};

    const std::uint64_t record_count =
        static_cast<std::uint64_t>(layout.node_count) + layout.leaf_count;
    {
        std::vector<std::uint8_t> has_parent(static_cast<std::size_t>(record_count), 0);
        for (std::uint32_t node_index = 0; node_index < layout.node_count; ++node_index)
        {
            const std::uint64_t node_offset =
                layout.nodes_begin + static_cast<std::uint64_t>(node_index) * kNodeBytes;
            auto bounds = ReadBounds(bytes, static_cast<std::size_t>(node_offset), "COL node");
            if (!bounds)
                return std::unexpected(bounds.error());
            asset::SpatialNodeIR node{.bounds = *bounds, .children = {}};
            for (std::size_t slot = 0; slot < node.children.size(); ++slot)
            {
                const std::uint64_t field_offset = node_offset + 32U + slot * sizeof(std::uint32_t);
                auto child = DecodeChild(
                    ReadU32(bytes, static_cast<std::size_t>(field_offset)), layout, field_offset);
                if (!child)
                    return std::unexpected(child.error());
                node.children[slot] = *child;
                if (*child)
                {
                    const std::uint64_t global = GlobalIndex(**child, layout);
                    if (has_parent[static_cast<std::size_t>(global)] != 0)
                        return std::unexpected(Error(asset::DecodeErrorCode::DuplicateReference,
                            "COL spatial record has multiple parents", field_offset));
                    has_parent[static_cast<std::size_t>(global)] = 1;
                }
            }
            result.nodes.push_back(std::move(node));
        }
        if (has_parent[0] != 0)
            return std::unexpected(
                Error(asset::DecodeErrorCode::InvalidReference, "COL root record has a parent"));
        if (!std::ranges::all_of(has_parent.begin() + 1, has_parent.end(),
                [](const std::uint8_t value) { return value == 1; }))
            return std::unexpected(Error(
                asset::DecodeErrorCode::InvalidReference, "COL spatial record has no parent"));
    }

    for (std::uint32_t vertex_index = 0; vertex_index < layout.vertex_count; ++vertex_index)
    {
        const std::uint64_t offset =
            layout.vertices_begin + static_cast<std::uint64_t>(vertex_index) * kVertexBytes;
        asset::Float3IR vertex{ReadF32(bytes, static_cast<std::size_t>(offset)),
            ReadF32(bytes, static_cast<std::size_t>(offset + 4U)),
            ReadF32(bytes, static_cast<std::size_t>(offset + 8U))};
        if (!std::isfinite(vertex.x) || !std::isfinite(vertex.y) || !std::isfinite(vertex.z))
            return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
                "COL vertex contains a non-finite coordinate", offset));
        if (ReadU32(bytes, static_cast<std::size_t>(offset + 12U)) != kFloatOneBits)
            return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
                "COL vertex homogeneous coordinate is not one", offset + 12U));
        result.vertices.push_back(vertex);
    }

    for (std::uint32_t triangle_index = 0; triangle_index < layout.triangle_count; ++triangle_index)
    {
        const std::uint64_t offset =
            layout.triangles_begin + static_cast<std::uint64_t>(triangle_index) * kTriangleBytes;
        const std::array<std::uint16_t, 3> source_indices{
            ReadU16(bytes, static_cast<std::size_t>(offset + 4U)),
            ReadU16(bytes, static_cast<std::size_t>(offset + 6U)),
            ReadU16(bytes, static_cast<std::size_t>(offset + 8U))};
        if (std::ranges::any_of(source_indices, [&](const std::uint16_t index) {
                return (index & 0x8000U) != 0 || index >= layout.vertex_count;
            }))
            return std::unexpected(Error(asset::DecodeErrorCode::InvalidReference,
                "COL triangle vertex index is out of range", offset + 4U));
        if (source_indices[0] == source_indices[1] || source_indices[0] == source_indices[2] ||
            source_indices[1] == source_indices[2])
            return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
                "COL triangle vertex indices are not distinct", offset + 4U));
        result.triangles.push_back(asset::SpatialTriangleIR{
            .vertex_indices = {source_indices[0], source_indices[1], source_indices[2]}});
    }

    {
        std::vector<std::uint8_t> referenced(layout.triangle_count, 0);
        for (std::uint32_t leaf_index = 0; leaf_index < layout.leaf_count; ++leaf_index)
        {
            const std::uint64_t leaf_offset =
                layout.leaves_begin + static_cast<std::uint64_t>(leaf_index) * kLeafBytes;
            auto bounds = ReadBounds(bytes, static_cast<std::size_t>(leaf_offset), "COL leaf");
            if (!bounds)
                return std::unexpected(bounds.error());
            const std::uint32_t reference_count =
                ReadU32(bytes, static_cast<std::size_t>(leaf_offset + 32U));
            const std::uint32_t reference_offset =
                ReadU32(bytes, static_cast<std::size_t>(leaf_offset + 36U));
            if (ReadU32(bytes, static_cast<std::size_t>(leaf_offset + 40U)) != 0 ||
                ReadU32(bytes, static_cast<std::size_t>(leaf_offset + 44U)) != 0)
                return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
                    "COL leaf reserved words are nonzero", leaf_offset + 40U));
            std::uint64_t reference_bytes = 0;
            std::uint64_t reference_end = 0;
            if (reference_offset % sizeof(std::uint32_t) != 0 ||
                !Multiply(reference_count, sizeof(std::uint32_t), reference_bytes) ||
                !Add(reference_offset, reference_bytes, reference_end) ||
                reference_offset < layout.vertices_end || reference_end > layout.index_data_end)
                return std::unexpected(Error(asset::DecodeErrorCode::InvalidReference,
                    "COL leaf triangle-reference range is invalid", leaf_offset + 36U));
            if (result.leaf_triangle_references.size() > std::numeric_limits<std::uint32_t>::max())
                return std::unexpected(Error(asset::DecodeErrorCode::Overflow,
                    "COL canonical leaf-reference offset overflows"));
            asset::SpatialLeafIR leaf{
                .bounds = *bounds,
                .first_triangle_reference =
                    static_cast<std::uint32_t>(result.leaf_triangle_references.size()),
                .triangle_reference_count = reference_count,
            };
            for (std::uint32_t index = 0; index < reference_count; ++index)
            {
                const std::uint64_t offset =
                    static_cast<std::uint64_t>(reference_offset) + index * sizeof(std::uint32_t);
                const std::uint32_t triangle_index =
                    ReadU32(bytes, static_cast<std::size_t>(offset));
                if (triangle_index >= layout.triangle_count)
                    return std::unexpected(Error(asset::DecodeErrorCode::InvalidReference,
                        "COL leaf triangle reference is out of range", offset));
                if (referenced[triangle_index] != 0)
                    return std::unexpected(Error(asset::DecodeErrorCode::DuplicateReference,
                        "COL triangle is referenced by more than one leaf", offset));
                referenced[triangle_index] = 1;
                result.leaf_triangle_references.push_back(triangle_index);
            }
            result.leaves.push_back(leaf);
            auto leaf_bounds = ValidateLeafBounds(result.leaves.back(), result);
            if (!leaf_bounds)
                return std::unexpected(leaf_bounds.error());
        }
        if (result.leaf_triangle_references.size() != layout.triangle_count ||
            !std::ranges::all_of(referenced, [](const std::uint8_t value) { return value == 1; }))
            return std::unexpected(Error(asset::DecodeErrorCode::InvalidReference,
                "COL leaf references do not form a triangle permutation"));
    }

    for (std::uint32_t node_index = 0; node_index < layout.node_count; ++node_index)
    {
        const auto& node = result.nodes[node_index];
        for (const auto& child : node.children)
        {
            if (!child)
                continue;
            const asset::Bounds3IR& child_bounds = child->kind == asset::SpatialElementKind::Node
                                                       ? result.nodes[child->index].bounds
                                                       : result.leaves[child->index].bounds;
            if (!Contains(node.bounds, child_bounds))
                return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
                    "COL child bounds extend outside parent bounds",
                    layout.nodes_begin + static_cast<std::uint64_t>(node_index) * kNodeBytes));
        }
    }

    std::vector<std::uint8_t> visited(static_cast<std::size_t>(record_count), 0);
    std::vector<PendingRecord> pending;
    pending.reserve(static_cast<std::size_t>(record_count));
    pending.push_back(PendingRecord{.reference = *result.root, .depth = 0});
    std::uint64_t visited_count = 0;
    while (!pending.empty())
    {
        const PendingRecord current = pending.back();
        pending.pop_back();
        if (current.depth > limits.maximum_nesting_depth)
            return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
                "COL spatial tree exceeds decoder nesting-depth limit"));
        const std::uint64_t global = GlobalIndex(current.reference, layout);
        if (visited[static_cast<std::size_t>(global)] != 0)
            return std::unexpected(Error(
                asset::DecodeErrorCode::DuplicateReference, "COL spatial tree revisits a record"));
        visited[static_cast<std::size_t>(global)] = 1;
        ++visited_count;
        if (current.reference.kind != asset::SpatialElementKind::Node)
            continue;
        for (const auto& child : result.nodes[current.reference.index].children)
        {
            if (!child)
                continue;
            if (current.depth == std::numeric_limits<std::uint32_t>::max())
                return std::unexpected(
                    Error(asset::DecodeErrorCode::Overflow, "COL spatial tree depth overflows"));
            pending.push_back(PendingRecord{.reference = *child, .depth = current.depth + 1U});
        }
    }
    if (visited_count != record_count)
        return std::unexpected(Error(asset::DecodeErrorCode::InvalidReference,
            "COL spatial tree does not reach every node and leaf"));
    return result;
}
} // namespace omega::retail
