#include "omega/retail/col_spatial_mesh_decoder.h"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string_view>
#include <vector>

namespace
{
void WriteU16(std::vector<std::byte>& bytes, const std::size_t offset, const std::uint16_t value)
{
    bytes[offset] = static_cast<std::byte>(value & 0xFFU);
    bytes[offset + 1U] = static_cast<std::byte>((value >> 8U) & 0xFFU);
}

void WriteU32(std::vector<std::byte>& bytes, const std::size_t offset, const std::uint32_t value)
{
    for (unsigned shift = 0; shift < 32; shift += 8)
        bytes[offset + shift / 8U] = static_cast<std::byte>((value >> shift) & 0xFFU);
}

void WriteF32(std::vector<std::byte>& bytes, const std::size_t offset, const float value)
{
    WriteU32(bytes, offset, std::bit_cast<std::uint32_t>(value));
}

void WriteBounds(std::vector<std::byte>& bytes, const std::size_t offset, const float minimum_x,
    const float minimum_y, const float minimum_z, const float maximum_x, const float maximum_y,
    const float maximum_z)
{
    WriteF32(bytes, offset, minimum_x);
    WriteF32(bytes, offset + 4U, minimum_y);
    WriteF32(bytes, offset + 8U, minimum_z);
    WriteF32(bytes, offset + 12U, 1.0F);
    WriteF32(bytes, offset + 16U, maximum_x);
    WriteF32(bytes, offset + 20U, maximum_y);
    WriteF32(bytes, offset + 24U, maximum_z);
    WriteF32(bytes, offset + 28U, 1.0F);
}

void WriteVertex(std::vector<std::byte>& bytes, const std::size_t offset, const float x,
    const float y, const float z)
{
    WriteF32(bytes, offset, x);
    WriteF32(bytes, offset + 4U, y);
    WriteF32(bytes, offset + 8U, z);
    WriteF32(bytes, offset + 12U, 1.0F);
}

void WriteHeader(std::vector<std::byte>& bytes, const std::uint8_t version,
    const std::uint32_t nodes, const std::uint32_t leaves, const std::uint32_t triangles,
    const std::uint32_t vertices)
{
    bytes[0] = std::byte{'C'};
    bytes[1] = std::byte{'O'};
    bytes[2] = std::byte{'L'};
    bytes[3] = static_cast<std::byte>(version);
    const std::uint32_t nodes_end = 48U + 64U * nodes;
    const std::uint32_t leaves_end = nodes_end + 48U * leaves;
    const std::uint32_t triangles_end = leaves_end + 16U * triangles;
    const std::uint32_t vertices_end = triangles_end + 16U * vertices;
    const std::uint32_t index_end = (vertices_end + 4U * triangles + 15U) & ~15U;
    WriteU32(bytes, 4, nodes);
    WriteU32(bytes, 8, 48);
    WriteU32(bytes, 12, leaves);
    WriteU32(bytes, 16, nodes_end);
    WriteU32(bytes, 20, triangles);
    WriteU32(bytes, 24, leaves_end);
    WriteU32(bytes, 28, vertices);
    WriteU32(bytes, 32, triangles_end);
    WriteU32(bytes, 36, vertices_end);
    WriteU32(bytes, 40, 1);
    WriteU32(bytes, 44, index_end);
}

std::vector<std::byte> MakeDirectLeafCol(
    const std::uint8_t version = 5, const bool with_opaque_tail = true)
{
    std::vector<std::byte> bytes(with_opaque_tail ? 192U : 176U, std::byte{0});
    WriteHeader(bytes, version, 0, 1, 1, 3);
    WriteBounds(bytes, 48, 0.0F, 0.0F, 0.0F, 1.0F, 1.0F, 0.0F);
    WriteU32(bytes, 80, 1);
    WriteU32(bytes, 84, 160);
    WriteU32(bytes, 96, 0x7F800000U);
    WriteU16(bytes, 100, 0);
    WriteU16(bytes, 102, 1);
    WriteU16(bytes, 104, 2);
    WriteU16(bytes, 106, 0xFFFFU);
    WriteU32(bytes, 108, 0xA5A55A5AU);
    WriteVertex(bytes, 112, 0.0F, 0.0F, 0.0F);
    WriteVertex(bytes, 128, 1.0F, 0.0F, 0.0F);
    WriteVertex(bytes, 144, 0.0F, 1.0F, 0.0F);
    WriteU32(bytes, 160, 0);
    if (with_opaque_tail)
        std::fill(bytes.begin() + 176, bytes.end(), std::byte{0x5A});
    return bytes;
}

std::vector<std::byte> MakeNodeCol()
{
    std::vector<std::byte> bytes(336, std::byte{0});
    WriteHeader(bytes, 5, 1, 2, 2, 4);
    WriteBounds(bytes, 48, 0.0F, 0.0F, 0.0F, 1.0F, 1.0F, 1.0F);
    WriteU32(bytes, 80, 112);
    WriteU32(bytes, 84, 160);

    WriteBounds(bytes, 112, 0.0F, 0.0F, 0.0F, 1.0F, 1.0F, 1.0F);
    WriteU32(bytes, 144, 1);
    WriteU32(bytes, 148, 304);
    WriteBounds(bytes, 160, 0.0F, 0.0F, 0.0F, 1.0F, 1.0F, 0.0F);
    WriteU32(bytes, 192, 1);
    WriteU32(bytes, 196, 308);

    WriteU32(bytes, 208, 0x12345678U);
    WriteU16(bytes, 212, 0);
    WriteU16(bytes, 214, 1);
    WriteU16(bytes, 216, 2);
    WriteU16(bytes, 218, 0x8002U);
    WriteU32(bytes, 220, 0xCAFEBABEU);
    WriteU32(bytes, 224, 0x87654321U);
    WriteU16(bytes, 228, 1);
    WriteU16(bytes, 230, 2);
    WriteU16(bytes, 232, 3);
    WriteU16(bytes, 234, 0x8003U);
    WriteU32(bytes, 236, 0x0BADF00DU);

    WriteVertex(bytes, 240, 0.0F, 0.0F, 0.0F);
    WriteVertex(bytes, 256, 1.0F, 0.0F, 0.0F);
    WriteVertex(bytes, 272, 0.0F, 1.0F, 0.0F);
    WriteVertex(bytes, 288, 1.0F, 1.0F, 1.0F);
    WriteU32(bytes, 304, 1);
    WriteU32(bytes, 308, 0);
    std::fill(bytes.begin() + 320, bytes.end(), std::byte{0xC3});
    return bytes;
}

std::vector<std::byte> MakeDepthTwoCol()
{
    std::vector<std::byte> bytes(320, std::byte{0});
    WriteHeader(bytes, 5, 2, 1, 1, 3);
    WriteBounds(bytes, 48, 0.0F, 0.0F, 0.0F, 1.0F, 1.0F, 0.0F);
    WriteU32(bytes, 80, 112);
    WriteBounds(bytes, 112, 0.0F, 0.0F, 0.0F, 1.0F, 1.0F, 0.0F);
    WriteU32(bytes, 144, 176);
    WriteBounds(bytes, 176, 0.0F, 0.0F, 0.0F, 1.0F, 1.0F, 0.0F);
    WriteU32(bytes, 208, 1);
    WriteU32(bytes, 212, 288);
    WriteU16(bytes, 228, 0);
    WriteU16(bytes, 230, 1);
    WriteU16(bytes, 232, 2);
    WriteVertex(bytes, 240, 0.0F, 0.0F, 0.0F);
    WriteVertex(bytes, 256, 1.0F, 0.0F, 0.0F);
    WriteVertex(bytes, 272, 0.0F, 1.0F, 0.0F);
    WriteU32(bytes, 288, 0);
    std::fill(bytes.begin() + 304, bytes.end(), std::byte{0x69});
    return bytes;
}

std::vector<std::byte> MakeEmptyCol()
{
    std::vector<std::byte> bytes(128, std::byte{0});
    WriteHeader(bytes, 5, 1, 0, 0, 0);
    for (std::size_t offset : {48U, 52U, 56U})
        WriteU32(bytes, offset, 0x7F7FFFFFU);
    WriteF32(bytes, 60, 1.0F);
    for (std::size_t offset : {64U, 68U, 72U})
        WriteU32(bytes, offset, 0xFF7FFFFFU);
    WriteF32(bytes, 76, 1.0F);
    std::fill(bytes.begin() + 112, bytes.end(), std::byte{0x3C});
    return bytes;
}

int failures = 0;

void Check(const bool condition, const std::string_view message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

template <typename Result>
void CheckError(
    const Result& result, const omega::asset::DecodeErrorCode code, const std::string_view message)
{
    Check(!result && result.error().code == code, message);
}
} // namespace

int ColSpatialMeshDecoderFailureCount()
{
    const auto direct_bytes = MakeDirectLeafCol(3);
    auto direct = omega::retail::DecodeColSpatialMesh(direct_bytes);
    Check(direct.has_value(), "version-3 direct-leaf COL decodes");
    if (direct)
    {
        Check(direct->root ==
                  omega::asset::SpatialElementRefIR{
                      .kind = omega::asset::SpatialElementKind::Leaf, .index = 0},
            "direct-leaf root becomes a typed canonical reference");
        Check(direct->nodes.empty() && direct->leaves.size() == 1,
            "direct-leaf mesh owns only its canonical leaf");
        Check(direct->vertices.size() == 3 && direct->triangles.size() == 1,
            "direct-leaf mesh owns source-order vertices and triangles");
        Check(direct->triangles[0].vertex_indices == std::array<std::uint32_t, 3>{0, 1, 2},
            "triangle widens only the three proven vertex indices");
        Check(direct->leaf_triangle_references == std::vector<std::uint32_t>{0},
            "leaf triangle reference is owned and canonical");
    }

    auto owned_bytes = MakeDirectLeafCol();
    auto owned = omega::retail::DecodeColSpatialMesh(owned_bytes);
    std::fill(owned_bytes.begin(), owned_bytes.end(), std::byte{0});
    Check(owned && owned->vertices[1] == omega::asset::Float3IR{1.0F, 0.0F, 0.0F},
        "decoded mesh owns its data after the input buffer changes");

    auto exact_span = omega::retail::DecodeColSpatialMesh(MakeDirectLeafCol(5, false));
    auto tailed_span = omega::retail::DecodeColSpatialMesh(MakeDirectLeafCol(5, true));
    Check(exact_span && tailed_span && *exact_span == *tailed_span,
        "opaque bytes after the described COL region do not affect canonical "
        "output");

    auto opaque_bytes = MakeDirectLeafCol();
    WriteU32(opaque_bytes, 96, 0xFFFFFFFFU);
    WriteU16(opaque_bytes, 106, 0x1234U);
    WriteU32(opaque_bytes, 108, 0x01020304U);
    std::fill(opaque_bytes.begin() + 176, opaque_bytes.end(), std::byte{0xE7});
    auto opaque = omega::retail::DecodeColSpatialMesh(opaque_bytes);
    Check(tailed_span && opaque && *tailed_span == *opaque,
        "unproven primitive fields and opaque tail bytes are omitted from the "
        "IR");

    auto node_bytes = MakeNodeCol();
    auto node_mesh = omega::retail::DecodeColSpatialMesh(node_bytes);
    Check(node_mesh.has_value(), "node-to-leaf COL decodes");
    if (node_mesh)
    {
        Check(node_mesh->root ==
                  omega::asset::SpatialElementRefIR{
                      .kind = omega::asset::SpatialElementKind::Node, .index = 0},
            "node root becomes a typed canonical reference");
        Check(node_mesh->nodes.size() == 1 && node_mesh->leaves.size() == 2,
            "spatial topology preserves node and leaf order");
        Check(node_mesh->nodes[0].children[0] ==
                      omega::asset::SpatialElementRefIR{
                          .kind = omega::asset::SpatialElementKind::Leaf, .index = 0} &&
                  node_mesh->nodes[0].children[1] ==
                      omega::asset::SpatialElementRefIR{
                          .kind = omega::asset::SpatialElementKind::Leaf, .index = 1},
            "retail child offsets become typed leaf indices");
        Check(node_mesh->leaf_triangle_references == std::vector<std::uint32_t>({1, 0}),
            "nontrivial leaf triangle permutation remains source ordered");
        Check(node_mesh->leaves[1].first_triangle_reference == 1 &&
                  node_mesh->leaves[1].triangle_reference_count == 1,
            "leaf ranges address the packed canonical reference vector");
    }

    auto empty_bytes = MakeEmptyCol();
    auto empty = omega::retail::DecodeColSpatialMesh(empty_bytes);
    Check(empty && !empty->root && empty->vertices.empty() && empty->triangles.empty() &&
              empty->nodes.empty() && empty->leaves.empty(),
        "exact retail empty sentinel normalizes to an empty canonical mesh");
    auto bad_empty = MakeEmptyCol();
    WriteU32(bad_empty, 80, 48);
    CheckError(omega::retail::DecodeColSpatialMesh(bad_empty),
        omega::asset::DecodeErrorCode::Malformed,
        "empty counts require the exact leafless sentinel record");

    auto bad = MakeNodeCol();
    WriteU32(bad, 80, 113);
    CheckError(omega::retail::DecodeColSpatialMesh(bad),
        omega::asset::DecodeErrorCode::InvalidReference, "unaligned child offsets are rejected");
    bad = MakeNodeCol();
    WriteU32(bad, 80, 208);
    CheckError(omega::retail::DecodeColSpatialMesh(bad),
        omega::asset::DecodeErrorCode::InvalidReference,
        "child offsets cannot identify the exact end of the leaf table");
    bad = MakeNodeCol();
    WriteU32(bad, 88, 112);
    CheckError(omega::retail::DecodeColSpatialMesh(bad),
        omega::asset::DecodeErrorCode::DuplicateReference,
        "spatial records cannot have multiple parents");
    bad = MakeNodeCol();
    WriteU32(bad, 84, 0);
    CheckError(omega::retail::DecodeColSpatialMesh(bad),
        omega::asset::DecodeErrorCode::InvalidReference,
        "disconnected spatial records are rejected");
    bad = MakeNodeCol();
    WriteU32(bad, 80, 48);
    CheckError(omega::retail::DecodeColSpatialMesh(bad),
        omega::asset::DecodeErrorCode::InvalidReference, "the root cannot acquire a parent");

    bad = MakeNodeCol();
    WriteU32(bad, 308, 1);
    CheckError(omega::retail::DecodeColSpatialMesh(bad),
        omega::asset::DecodeErrorCode::DuplicateReference,
        "duplicate leaf triangle references are rejected");
    bad = MakeDirectLeafCol();
    WriteU32(bad, 160, 1);
    CheckError(omega::retail::DecodeColSpatialMesh(bad),
        omega::asset::DecodeErrorCode::InvalidReference,
        "out-of-range leaf triangle-reference values are rejected");
    bad = MakeDirectLeafCol();
    WriteU32(bad, 84, 164);
    CheckError(omega::retail::DecodeColSpatialMesh(bad),
        omega::asset::DecodeErrorCode::InvalidReference,
        "leaf reference ranges cannot address alignment padding");
    bad = MakeDirectLeafCol();
    WriteU32(bad, 80, 0);
    CheckError(omega::retail::DecodeColSpatialMesh(bad), omega::asset::DecodeErrorCode::Malformed,
        "populated leaves cannot contain zero triangle references");
    bad = MakeDirectLeafCol();
    WriteU32(bad, 80, std::numeric_limits<std::uint32_t>::max());
    WriteU32(bad, 84, 0xFFFFFFFCU);
    CheckError(omega::retail::DecodeColSpatialMesh(bad),
        omega::asset::DecodeErrorCode::InvalidReference,
        "overflow-shaped leaf reference ranges are rejected before iteration");
    bad = MakeDirectLeafCol();
    WriteU32(bad, 88, 1);
    CheckError(omega::retail::DecodeColSpatialMesh(bad), omega::asset::DecodeErrorCode::Malformed,
        "leaf reserved words must remain zero");
    bad = MakeDirectLeafCol();
    bad[164] = std::byte{1};
    CheckError(omega::retail::DecodeColSpatialMesh(bad), omega::asset::DecodeErrorCode::Malformed,
        "index-list alignment padding must be zero");

    bad = MakeDirectLeafCol();
    WriteU16(bad, 100, 3);
    CheckError(omega::retail::DecodeColSpatialMesh(bad),
        omega::asset::DecodeErrorCode::InvalidReference,
        "out-of-range triangle vertex indices are rejected");
    bad = MakeDirectLeafCol();
    WriteU16(bad, 100, 0x8000U);
    CheckError(omega::retail::DecodeColSpatialMesh(bad),
        omega::asset::DecodeErrorCode::InvalidReference,
        "high bits are rejected in the three proven triangle indices");
    bad = MakeDirectLeafCol();
    WriteU16(bad, 102, 0);
    CheckError(omega::retail::DecodeColSpatialMesh(bad), omega::asset::DecodeErrorCode::Malformed,
        "degenerate source triangle indices are rejected");

    bad = MakeDirectLeafCol();
    WriteU32(bad, 112, 0x7FC00000U);
    CheckError(omega::retail::DecodeColSpatialMesh(bad), omega::asset::DecodeErrorCode::Malformed,
        "non-finite vertex coordinates are rejected");
    bad = MakeDirectLeafCol();
    WriteF32(bad, 124, 0.0F);
    CheckError(omega::retail::DecodeColSpatialMesh(bad), omega::asset::DecodeErrorCode::Malformed,
        "vertex homogeneous coordinates must equal one");
    bad = MakeDirectLeafCol();
    WriteF32(bad, 48, 2.0F);
    CheckError(omega::retail::DecodeColSpatialMesh(bad), omega::asset::DecodeErrorCode::Malformed,
        "leaf bounds must be ordered");
    bad = MakeDirectLeafCol();
    WriteF32(bad, 64, 0.5F);
    CheckError(omega::retail::DecodeColSpatialMesh(bad), omega::asset::DecodeErrorCode::Malformed,
        "leaf bounds must exactly reproduce referenced vertex extents");
    bad = MakeNodeCol();
    WriteF32(bad, 64, 0.5F);
    CheckError(omega::retail::DecodeColSpatialMesh(bad), omega::asset::DecodeErrorCode::Malformed,
        "child bounds must remain inside parent bounds");

    auto limits = omega::asset::DecodeLimits{};
    const auto budget_bytes = MakeDirectLeafCol();
    limits.maximum_input_bytes = budget_bytes.size();
    Check(omega::retail::DecodeColSpatialMesh(budget_bytes, limits).has_value(),
        "exact COL input-byte budget succeeds");
    limits.maximum_input_bytes = budget_bytes.size() - 1U;
    CheckError(omega::retail::DecodeColSpatialMesh(budget_bytes, limits),
        omega::asset::DecodeErrorCode::LimitExceeded, "one-below COL input-byte budget fails");

    limits = omega::asset::DecodeLimits{};
    limits.maximum_items = 6;
    Check(omega::retail::DecodeColSpatialMesh(budget_bytes, limits).has_value(),
        "exact cumulative COL item budget succeeds");
    limits.maximum_items = 5;
    CheckError(omega::retail::DecodeColSpatialMesh(budget_bytes, limits),
        omega::asset::DecodeErrorCode::LimitExceeded, "one-below cumulative COL item budget fails");

    constexpr std::uint64_t output_bytes =
        sizeof(omega::asset::SpatialMeshIR) + 3U * sizeof(omega::asset::Float3IR) +
        sizeof(omega::asset::SpatialTriangleIR) + sizeof(std::uint32_t) +
        sizeof(omega::asset::SpatialLeafIR);
    limits = omega::asset::DecodeLimits{};
    limits.maximum_output_bytes = output_bytes;
    Check(omega::retail::DecodeColSpatialMesh(budget_bytes, limits).has_value(),
        "exact logical COL output budget succeeds");
    limits.maximum_output_bytes = output_bytes - 1U;
    CheckError(omega::retail::DecodeColSpatialMesh(budget_bytes, limits),
        omega::asset::DecodeErrorCode::LimitExceeded, "one-below logical COL output budget fails");

    constexpr std::uint64_t scratch_bytes =
        sizeof(std::uint8_t) + sizeof(omega::asset::SpatialElementRefIR) + sizeof(std::uint32_t);
    limits = omega::asset::DecodeLimits{};
    limits.maximum_scratch_bytes = scratch_bytes;
    Check(omega::retail::DecodeColSpatialMesh(budget_bytes, limits).has_value(),
        "exact COL topology scratch budget succeeds");
    limits.maximum_scratch_bytes = scratch_bytes - 1U;
    CheckError(omega::retail::DecodeColSpatialMesh(budget_bytes, limits),
        omega::asset::DecodeErrorCode::LimitExceeded,
        "one-below COL topology scratch budget fails");

    limits = omega::asset::DecodeLimits{};
    limits.maximum_nesting_depth = 0;
    CheckError(omega::retail::DecodeColSpatialMesh(node_bytes, limits),
        omega::asset::DecodeErrorCode::LimitExceeded,
        "spatial edge depth uses the decoder nesting-depth limit");
    const auto depth_two_bytes = MakeDepthTwoCol();
    limits = omega::asset::DecodeLimits{};
    limits.maximum_nesting_depth = 2;
    Check(omega::retail::DecodeColSpatialMesh(depth_two_bytes, limits).has_value(),
        "exact two-edge spatial depth budget succeeds");
    limits.maximum_nesting_depth = 1;
    CheckError(omega::retail::DecodeColSpatialMesh(depth_two_bytes, limits),
        omega::asset::DecodeErrorCode::LimitExceeded,
        "one-below two-edge spatial depth budget fails");
    bad = MakeDepthTwoCol();
    WriteU32(bad, 80, 176);
    WriteU32(bad, 144, 112);
    CheckError(omega::retail::DecodeColSpatialMesh(bad),
        omega::asset::DecodeErrorCode::InvalidReference,
        "a disconnected self-cycle is rejected without traversal recursion");

    auto truncated = MakeDirectLeafCol();
    truncated.resize(160);
    CheckError(omega::retail::DecodeColSpatialMesh(truncated),
        omega::asset::DecodeErrorCode::Truncated,
        "truncated COL tables are rejected before allocation");
    bad = MakeDirectLeafCol();
    bad[3] = std::byte{4};
    CheckError(omega::retail::DecodeColSpatialMesh(bad),
        omega::asset::DecodeErrorCode::UnsupportedVariant,
        "unknown COL versions remain unsupported");
    bad = MakeDirectLeafCol();
    WriteU32(bad, 4, std::numeric_limits<std::uint32_t>::max());
    WriteU32(bad, 16, 0xFFFFFFF0U);
    CheckError(omega::retail::DecodeColSpatialMesh(bad), omega::asset::DecodeErrorCode::Malformed,
        "hostile table counts are rejected before allocation");

    return failures;
}
