#include "asset_commands.h"

#include "omega/archive/hog_archive.h"
#include "omega/retail/col_spatial_mesh_decoder.h"
#include "omega/retail/container_descriptors.h"
#include "omega/retail/tdx_texture_storage_decoder.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace omega::tool
{
namespace
{
constexpr std::uint64_t kMaximumAssetBytes = 64ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kMaximumFilesystemEntries = 1ULL << 20U;
constexpr std::uint64_t kMaximumTopLevelHogs = 1ULL << 12U;
constexpr std::uint64_t kMaximumNestedHogs = 1ULL << 15U;
constexpr std::uint64_t kMaximumIndexedEntries = 1ULL << 20U;
constexpr std::uint64_t kMaximumAssetCandidates = 1ULL << 20U;
constexpr std::uint64_t kMaximumAggregateAssetBytes = 4ULL * 1024ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kMaximumAggregateNestedHogBytes = 16ULL * 1024ULL * 1024ULL * 1024ULL;
constexpr std::size_t kMaximumNestedDepth = 32;
constexpr int kMaximumFilesystemDepth = 32;

enum class InputKind
{
    Other,
    Hog,
    Col,
    Vum,
    Tdx,
};

struct FormatStats
{
    std::uint64_t candidates = 0;
    std::uint64_t valid = 0;
    std::uint64_t errors = 0;
};

struct ExtentStats
{
    std::uint64_t exact = 0;
    std::uint64_t zero_padded_tail = 0;
    std::uint64_t nonzero_tail = 0;
    std::uint64_t exceeds_input = 0;
};

struct ColSemanticStats
{
    std::uint64_t version_3 = 0;
    std::uint64_t version_5 = 0;
    std::uint64_t source_nodes = 0;
    std::uint64_t canonical_nodes = 0;
    std::uint64_t leaves = 0;
    std::uint64_t triangles = 0;
    std::uint64_t vertices = 0;
    std::uint64_t triangle_references = 0;
    std::uint64_t empty_meshes = 0;
    std::uint64_t direct_leaf_roots = 0;
    std::uint32_t maximum_edge_depth = 0;
};

struct TdxSemanticStats
{
    std::uint64_t indexed_4 = 0;
    std::uint64_t indexed_8 = 0;
    std::uint64_t packed_24 = 0;
    std::uint64_t packed_32 = 0;
    std::uint64_t blocks = 0;
    std::uint64_t primary_planes = 0;
    std::uint64_t primary_bytes = 0;
    std::uint64_t palette_blocks = 0;
    std::uint64_t direct_blocks = 0;
    std::uint64_t palette_entries = 0;
    std::uint64_t implicit_zero_textures = 0;
    std::uint64_t implicit_zero_bytes = 0;
};

struct VerificationStats
{
    std::uint64_t filesystem_entries = 0;
    std::uint64_t top_level_hogs = 0;
    std::uint64_t top_level_hog_valid = 0;
    std::uint64_t nested_hogs = 0;
    std::uint64_t nested_hog_valid = 0;
    std::uint64_t indexed_entries = 0;
    std::uint64_t asset_candidates = 0;
    std::uint64_t asset_bytes = 0;
    std::uint64_t nested_hog_bytes = 0;
    std::uint64_t hog_errors = 0;
    std::uint64_t safety_errors = 0;
    FormatStats col;
    FormatStats vum;
    FormatStats tdx;
    ExtentStats col_extents;
    ExtentStats vum_extents;
    ExtentStats tdx_extents;
    ColSemanticStats col_semantic;
    TdxSemanticStats tdx_semantic;
    bool stopped_at_safety_limit = false;
};

[[nodiscard]] InputKind Classify(const std::filesystem::path& path)
{
    std::string extension = path.extension().string();
    std::ranges::transform(extension, extension.begin(),
        [](const unsigned char value) { return static_cast<char>(std::tolower(value)); });
    if (extension == ".hog")
        return InputKind::Hog;
    if (extension == ".col")
        return InputKind::Col;
    if (extension == ".vum")
        return InputKind::Vum;
    if (extension == ".tdx")
        return InputKind::Tdx;
    return InputKind::Other;
}

[[nodiscard]] std::expected<std::uint64_t, std::string> CheckedAdd(
    const std::uint64_t left, const std::uint64_t right, const std::string_view description)
{
    if (right > std::numeric_limits<std::uint64_t>::max() - left)
        return std::unexpected(std::string(description) + " overflows");
    return left + right;
}

[[nodiscard]] std::expected<std::vector<std::byte>, std::string> ReadRange(
    const std::filesystem::path& path, const std::uint64_t offset, const std::uint64_t size)
{
    if (size > kMaximumAssetBytes)
        return std::unexpected("asset span exceeds read safety limit");
    if (size > std::numeric_limits<std::size_t>::max() ||
        offset > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max()) ||
        size > static_cast<std::uint64_t>(std::numeric_limits<std::streamsize>::max()))
        return std::unexpected("asset span does not fit host file APIs");
    auto end = CheckedAdd(offset, size, "asset file range");
    if (!end)
        return std::unexpected(end.error());

    std::ifstream stream(path, std::ios::binary);
    if (!stream)
        return std::unexpected("unable to open backing file");
    stream.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!stream)
        return std::unexpected("unable to seek to asset span");
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    if (!bytes.empty() &&
        !stream.read(reinterpret_cast<char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size())))
        return std::unexpected("unable to read complete asset span");
    return bytes;
}

void StopAtSafetyLimit(VerificationStats& stats, const std::string_view message)
{
    ++stats.safety_errors;
    stats.stopped_at_safety_limit = true;
    std::cerr << message << '\n';
}

[[nodiscard]] bool AddSemanticCounter(std::uint64_t& target, const std::uint64_t amount,
    VerificationStats& stats, const std::string_view description)
{
    auto result = CheckedAdd(target, amount, description);
    if (!result)
    {
        StopAtSafetyLimit(stats, result.error());
        return false;
    }
    target = *result;
    return true;
}

[[nodiscard]] std::uint32_t MaximumEdgeDepth(const asset::SpatialMeshIR& mesh)
{
    if (!mesh.root)
        return 0;
    using Pending = std::pair<asset::SpatialElementRefIR, std::uint32_t>;
    std::vector<Pending> pending;
    pending.reserve(mesh.nodes.size() + mesh.leaves.size());
    pending.emplace_back(*mesh.root, 0);
    std::uint32_t maximum = 0;
    while (!pending.empty())
    {
        const auto [current, depth] = pending.back();
        pending.pop_back();
        maximum = std::max(maximum, depth);
        if (current.kind != asset::SpatialElementKind::Node)
            continue;
        for (const auto& child : mesh.nodes[current.index].children)
        {
            if (child)
                pending.emplace_back(*child, depth + 1U);
        }
    }
    return maximum;
}

[[nodiscard]] bool RecordColSemantics(const retail::ColContainerDescriptor& descriptor,
    const asset::SpatialMeshIR& mesh, VerificationStats& stats)
{
    auto& semantic = stats.col_semantic;
    if (descriptor.format_version == 3)
        ++semantic.version_3;
    else
        ++semantic.version_5;
    const bool counters_fit =
        AddSemanticCounter(semantic.source_nodes, descriptor.observed_record_counts[0], stats,
            "COL source-node count") &&
        AddSemanticCounter(
            semantic.canonical_nodes, mesh.nodes.size(), stats, "COL canonical-node count") &&
        AddSemanticCounter(semantic.leaves, mesh.leaves.size(), stats, "COL leaf count") &&
        AddSemanticCounter(
            semantic.triangles, mesh.triangles.size(), stats, "COL triangle count") &&
        AddSemanticCounter(semantic.vertices, mesh.vertices.size(), stats, "COL vertex count") &&
        AddSemanticCounter(semantic.triangle_references, mesh.leaf_triangle_references.size(),
            stats, "COL triangle-reference count");
    if (!counters_fit)
        return false;
    if (!mesh.root)
        ++semantic.empty_meshes;
    else if (mesh.root->kind == asset::SpatialElementKind::Leaf)
        ++semantic.direct_leaf_roots;
    semantic.maximum_edge_depth = std::max(semantic.maximum_edge_depth, MaximumEdgeDepth(mesh));
    return true;
}

[[nodiscard]] bool RecordTdxSemantics(const retail::TdxContainerDescriptor& descriptor,
    const asset::TextureStorageIR& texture, VerificationStats& stats)
{
    auto& semantic = stats.tdx_semantic;
    switch (texture.sample_encoding)
    {
    case asset::TextureSampleEncoding::Indexed4:
        ++semantic.indexed_4;
        break;
    case asset::TextureSampleEncoding::Indexed8:
        ++semantic.indexed_8;
        break;
    case asset::TextureSampleEncoding::Packed24:
        ++semantic.packed_24;
        break;
    case asset::TextureSampleEncoding::Packed32:
        ++semantic.packed_32;
        break;
    }
    if (!AddSemanticCounter(
            semantic.blocks, texture.blocks.size(), stats, "TDX storage block count"))
        return false;
    for (const auto& block : texture.blocks)
    {
        if (!AddSemanticCounter(semantic.primary_planes, block.planes.size(), stats,
                "TDX primary storage-plane count"))
            return false;
        for (const auto& plane : block.planes)
        {
            if (!AddSemanticCounter(semantic.primary_bytes, plane.bytes.size(), stats,
                    "TDX primary storage byte count"))
                return false;
        }
        if (block.palette)
        {
            ++semantic.palette_blocks;
            if (!AddSemanticCounter(semantic.palette_entries, block.palette->entries.size(), stats,
                    "TDX palette-entry count"))
                return false;
        }
        else
        {
            ++semantic.direct_blocks;
        }
    }
    if (descriptor.counted_blocks_extent.relation ==
        retail::ObservedExtentRelation::ExceedsInput)
    {
        ++semantic.implicit_zero_textures;
        const std::uint64_t missing = descriptor.counted_blocks_extent.observed_bytes -
                                      descriptor.counted_blocks_extent.input_bytes;
        if (!AddSemanticCounter(semantic.implicit_zero_bytes, missing, stats,
                "TDX implicit-zero byte count"))
            return false;
    }
    return true;
}

[[nodiscard]] bool AddIndexedEntries(
    VerificationStats& stats, const std::uint64_t amount)
{
    if (amount > kMaximumIndexedEntries - stats.indexed_entries)
    {
        StopAtSafetyLimit(stats, "asset metadata indexed-entry count exceeds safety limit");
        return false;
    }
    stats.indexed_entries += amount;
    return true;
}

[[nodiscard]] bool AddBudgetedBytes(std::uint64_t& total, const std::uint64_t amount,
    const std::uint64_t maximum, VerificationStats& stats, const std::string_view message)
{
    if (amount > maximum - total)
    {
        StopAtSafetyLimit(stats, message);
        return false;
    }
    total += amount;
    return true;
}

[[nodiscard]] FormatStats& StatsFor(VerificationStats& stats, const InputKind kind)
{
    if (kind == InputKind::Col)
        return stats.col;
    if (kind == InputKind::Vum)
        return stats.vum;
    return stats.tdx;
}

[[nodiscard]] bool BeginAssetCandidate(VerificationStats& stats, const InputKind kind)
{
    if (stats.asset_candidates >= kMaximumAssetCandidates)
    {
        StopAtSafetyLimit(stats, "asset metadata candidate count exceeds safety limit");
        return false;
    }
    ++stats.asset_candidates;
    ++StatsFor(stats, kind).candidates;
    return true;
}

void RecordExtent(ExtentStats& stats, const retail::ObservedExtentRelation relation)
{
    switch (relation)
    {
    case retail::ObservedExtentRelation::Exact:
        ++stats.exact;
        break;
    case retail::ObservedExtentRelation::ZeroPaddedTail:
        ++stats.zero_padded_tail;
        break;
    case retail::ObservedExtentRelation::NonzeroTail:
        ++stats.nonzero_tail;
        break;
    case retail::ObservedExtentRelation::ExceedsInput:
        ++stats.exceeds_input;
        break;
    }
}

void PrintAssetError(
    const std::filesystem::path&, const std::string_view, const std::string_view)
{
    std::cerr << "asset verification error\n";
}

void InspectAssetRange(const std::filesystem::path& backing_path,
    const std::uint64_t offset, const std::uint64_t size, const InputKind kind,
    const std::string_view entry_name, VerificationStats& stats)
{
    if (stats.stopped_at_safety_limit)
        return;
    if (!AddBudgetedBytes(stats.asset_bytes, size, kMaximumAggregateAssetBytes, stats,
            "asset metadata aggregate asset bytes exceed safety limit"))
        return;
    auto bytes = ReadRange(backing_path, offset, size);
    if (!bytes)
    {
        ++StatsFor(stats, kind).errors;
        PrintAssetError(backing_path, entry_name, bytes.error());
        return;
    }

    if (kind == InputKind::Col)
    {
        auto descriptor = retail::InspectColContainer(*bytes);
        if (!descriptor)
        {
            ++stats.col.errors;
            PrintAssetError(backing_path, entry_name, descriptor.error().message);
            return;
        }
        auto mesh = retail::DecodeColSpatialMesh(*bytes);
        if (!mesh)
        {
            ++stats.col.errors;
            PrintAssetError(backing_path, entry_name, mesh.error().message);
            return;
        }
        if (!RecordColSemantics(*descriptor, *mesh, stats))
            return;
        ++stats.col.valid;
        RecordExtent(stats.col_extents, descriptor->described_tables_extent.relation);
        return;
    }
    if (kind == InputKind::Vum)
    {
        auto descriptor = retail::InspectVumContainer(*bytes);
        if (!descriptor)
        {
            ++stats.vum.errors;
            PrintAssetError(backing_path, entry_name, descriptor.error().message);
            return;
        }
        ++stats.vum.valid;
        RecordExtent(stats.vum_extents, descriptor->primary_extent.relation);
        return;
    }

    auto descriptor = retail::InspectTdxContainer(*bytes);
    if (!descriptor)
    {
        ++stats.tdx.errors;
        PrintAssetError(backing_path, entry_name, descriptor.error().message);
        return;
    }
    auto texture = retail::DecodeTdxTextureStorage(*bytes);
    if (!texture)
    {
        ++stats.tdx.errors;
        PrintAssetError(backing_path, entry_name, texture.error().message);
        return;
    }
    if (!RecordTdxSemantics(*descriptor, *texture, stats))
        return;
    ++stats.tdx.valid;
    RecordExtent(stats.tdx_extents, descriptor->counted_blocks_extent.relation);
}

void RecordAssetOffsetError(const std::filesystem::path& backing_path,
    const archive::HogEntry& entry, const InputKind kind, VerificationStats& stats)
{
    ++StatsFor(stats, kind).errors;
    PrintAssetError(backing_path, entry.name, "asset file offset overflows");
}

void InspectHogEntries(const std::filesystem::path& backing_path,
    const archive::HogIndex& parent, const std::uint64_t parent_file_offset,
    const std::size_t depth, VerificationStats& stats)
{
    if (stats.stopped_at_safety_limit || !AddIndexedEntries(stats, parent.entries().size()))
        return;

    for (const auto& entry : parent.entries())
    {
        if (stats.stopped_at_safety_limit)
            return;
        const InputKind kind = Classify(std::filesystem::path(entry.name));
        if (kind == InputKind::Other)
            continue;

        if (kind == InputKind::Hog)
        {
            if (stats.nested_hogs >= kMaximumNestedHogs)
            {
                StopAtSafetyLimit(stats, "asset metadata nested-HOG count exceeds safety limit");
                return;
            }
            ++stats.nested_hogs;
            if (depth >= kMaximumNestedDepth)
            {
                ++stats.hog_errors;
                PrintAssetError(backing_path, entry.name,
                    "nested HOG depth exceeds safety limit");
                continue;
            }
            auto absolute_offset = CheckedAdd(
                parent_file_offset, entry.offset, "nested HOG file offset");
            if (!absolute_offset)
            {
                ++stats.hog_errors;
                PrintAssetError(backing_path, entry.name, absolute_offset.error());
                continue;
            }
            if (!AddBudgetedBytes(stats.nested_hog_bytes, entry.size,
                    kMaximumAggregateNestedHogBytes, stats,
                    "asset metadata aggregate nested-HOG bytes exceed safety limit"))
                return;
            auto nested = archive::HogIndex::OpenRange(backing_path,
                archive::HogFileRange{.offset = *absolute_offset, .size = entry.size});
            if (!nested)
            {
                ++stats.hog_errors;
                PrintAssetError(backing_path, entry.name, nested.error());
                continue;
            }
            ++stats.nested_hog_valid;
            InspectHogEntries(
                backing_path, *nested, *absolute_offset, depth + 1U, stats);
            continue;
        }

        if (!BeginAssetCandidate(stats, kind))
            return;
        auto absolute_offset = CheckedAdd(
            parent_file_offset, entry.offset, "asset file offset");
        if (!absolute_offset)
        {
            RecordAssetOffsetError(backing_path, entry, kind, stats);
            continue;
        }
        InspectAssetRange(
            backing_path, *absolute_offset, entry.size, kind, entry.name, stats);
    }
}

void RecordSymlinkInput(const std::filesystem::path& path,
    const InputKind kind, VerificationStats& stats)
{
    if (kind == InputKind::Hog)
    {
        if (stats.top_level_hogs >= kMaximumTopLevelHogs)
        {
            StopAtSafetyLimit(stats, "asset metadata top-level HOG count exceeds safety limit");
            return;
        }
        ++stats.top_level_hogs;
        ++stats.hog_errors;
    }
    else if (BeginAssetCandidate(stats, kind))
    {
        ++StatsFor(stats, kind).errors;
    }
    PrintAssetError(path, {}, "symbolic-link corpus inputs are not allowed");
}
} // namespace

int AssetMetadataVerifyTree(const std::filesystem::path& root)
{
    VerificationStats stats;
    std::error_code error;
    std::filesystem::recursive_directory_iterator iterator(root, error), end;
    while (iterator != end && !error && !stats.stopped_at_safety_limit)
    {
        if (stats.filesystem_entries >= kMaximumFilesystemEntries)
        {
            StopAtSafetyLimit(stats, "asset metadata filesystem entry count exceeds safety limit");
            break;
        }
        ++stats.filesystem_entries;
        if (iterator.depth() > kMaximumFilesystemDepth)
        {
            StopAtSafetyLimit(stats, "asset metadata filesystem depth exceeds safety limit");
            break;
        }

        const auto status = iterator->symlink_status(error);
        if (error)
            break;
        const InputKind kind = Classify(iterator->path());
        if (std::filesystem::is_symlink(status))
        {
            if (kind != InputKind::Other)
                RecordSymlinkInput(iterator->path(), kind, stats);
            iterator.increment(error);
            continue;
        }
        if (!std::filesystem::is_regular_file(status) || kind == InputKind::Other)
        {
            iterator.increment(error);
            continue;
        }

        if (kind == InputKind::Hog)
        {
            if (stats.top_level_hogs >= kMaximumTopLevelHogs)
            {
                StopAtSafetyLimit(stats,
                    "asset metadata top-level HOG count exceeds safety limit");
                break;
            }
            ++stats.top_level_hogs;
            auto hog = archive::HogIndex::Open(iterator->path());
            if (!hog)
            {
                ++stats.hog_errors;
                PrintAssetError(iterator->path(), {}, hog.error());
            }
            else
            {
                ++stats.top_level_hog_valid;
                InspectHogEntries(iterator->path(), *hog, 0, 0, stats);
            }
        }
        else if (BeginAssetCandidate(stats, kind))
        {
            const std::uintmax_t file_size = std::filesystem::file_size(iterator->path(), error);
            if (error)
            {
                ++StatsFor(stats, kind).errors;
                PrintAssetError(iterator->path(), {},
                    "unable to determine direct asset file size");
                error.clear();
            }
            else if (!std::in_range<std::uint64_t>(file_size))
            {
                ++StatsFor(stats, kind).errors;
                PrintAssetError(iterator->path(), {}, "direct asset file size overflows");
            }
            else
            {
                InspectAssetRange(iterator->path(), 0,
                    static_cast<std::uint64_t>(file_size), kind, {}, stats);
            }
        }
        iterator.increment(error);
    }
    if (error)
    {
        std::cerr << "unable to enumerate asset metadata tree: " << error.message() << '\n';
        return 1;
    }

    const std::uint64_t asset_errors = stats.col.errors + stats.vum.errors + stats.tdx.errors;
    const std::uint64_t total_errors = stats.hog_errors + stats.safety_errors + asset_errors;
    std::cout << std::format(
        "{{\"top_level_hogs\":{},\"top_level_hog_valid\":{},\"nested_hogs\":{},"
        "\"nested_hog_valid\":{},\"nested_hog_bytes\":{},\"indexed_entries\":{},"
        "\"asset_candidates\":{},\"asset_bytes\":{},"
        "\"col\":{{\"candidates\":{},\"valid\":{},\"errors\":{},\"exact\":{},"
        "\"zero_tail\":{},\"nonzero_tail\":{},\"exceeds\":{},"
        "\"version_3\":{},\"version_5\":{},\"source_nodes\":{},"
        "\"canonical_nodes\":{},\"leaves\":{},\"triangles\":{},\"vertices\":{},"
        "\"triangle_references\":{},\"empty_meshes\":{},"
        "\"direct_leaf_roots\":{},\"maximum_edge_depth\":{}}},"
        "\"vum\":{{\"candidates\":{},\"valid\":{},\"errors\":{},\"exact\":{},"
        "\"zero_tail\":{},\"nonzero_tail\":{},\"exceeds\":{}}},"
        "\"tdx\":{{\"candidates\":{},\"valid\":{},\"errors\":{},\"exact\":{},"
        "\"zero_tail\":{},\"nonzero_tail\":{},\"exceeds\":{},"
        "\"indexed_4\":{},\"indexed_8\":{},\"packed_24\":{},\"packed_32\":{},"
        "\"blocks\":{},\"primary_planes\":{},\"primary_bytes\":{},"
        "\"palette_blocks\":{},\"direct_blocks\":{},\"palette_entries\":{},"
        "\"implicit_zero_textures\":{},\"implicit_zero_bytes\":{}}},"
        "\"errors\":{}}}\n",
        stats.top_level_hogs, stats.top_level_hog_valid, stats.nested_hogs,
        stats.nested_hog_valid, stats.nested_hog_bytes, stats.indexed_entries,
        stats.asset_candidates, stats.asset_bytes,
        stats.col.candidates, stats.col.valid, stats.col.errors, stats.col_extents.exact,
        stats.col_extents.zero_padded_tail, stats.col_extents.nonzero_tail,
        stats.col_extents.exceeds_input, stats.col_semantic.version_3, stats.col_semantic.version_5,
        stats.col_semantic.source_nodes, stats.col_semantic.canonical_nodes,
        stats.col_semantic.leaves, stats.col_semantic.triangles, stats.col_semantic.vertices,
        stats.col_semantic.triangle_references, stats.col_semantic.empty_meshes,
        stats.col_semantic.direct_leaf_roots, stats.col_semantic.maximum_edge_depth,
        stats.vum.candidates, stats.vum.valid, stats.vum.errors, stats.vum_extents.exact,
        stats.vum_extents.zero_padded_tail, stats.vum_extents.nonzero_tail,
        stats.vum_extents.exceeds_input, stats.tdx.candidates, stats.tdx.valid, stats.tdx.errors,
        stats.tdx_extents.exact, stats.tdx_extents.zero_padded_tail, stats.tdx_extents.nonzero_tail,
        stats.tdx_extents.exceeds_input, stats.tdx_semantic.indexed_4,
        stats.tdx_semantic.indexed_8, stats.tdx_semantic.packed_24,
        stats.tdx_semantic.packed_32, stats.tdx_semantic.blocks,
        stats.tdx_semantic.primary_planes, stats.tdx_semantic.primary_bytes,
        stats.tdx_semantic.palette_blocks, stats.tdx_semantic.direct_blocks,
        stats.tdx_semantic.palette_entries, stats.tdx_semantic.implicit_zero_textures,
        stats.tdx_semantic.implicit_zero_bytes, total_errors);
    if (stats.asset_candidates == 0)
        std::cerr << "no COL, VUM, or TDX assets were found\n";
    return total_errors == 0 && stats.asset_candidates != 0 &&
            !stats.stopped_at_safety_limit
        ? 0
        : 2;
}
} // namespace omega::tool
