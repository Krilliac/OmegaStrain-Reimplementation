#include "omega/content/game_data_service.h"

#include "omega/archive/hog_archive.h"
#include "omega/asset/source_locator.h"
#include "omega/debug/subsystem_entry_break.h"
#include "omega/retail/col_spatial_mesh_decoder.h"
#include "omega/retail/fnt_v3_decoder.h"
#include "omega/retail/frontend_document_decoder.h"
#include "omega/retail/frontend_tdx_decoder.h"
#include "omega/retail/pop_level_manifest_decoder.h"
#include "omega/retail/retail_string_table_decoder.h"
#include "omega/retail/vum_material_catalog_decoder.h"
#include "omega/vfs/virtual_file_system.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace omega::content
{
namespace
{
constexpr std::string_view kExpectedBootExecutable = "SCUS_972.64";
constexpr std::string_view kExpectedBootValue = "CDROM0:\\SCUS_972.64;1";
constexpr std::string_view kOpeningMovieArchiveGamePath = "ZMEDIA/ZMOVIES.HOG";
constexpr std::string_view kOpeningMovieArchiveMountRoot = "OPENING_MOVIE_ARCHIVE";
constexpr std::size_t kMaximumLevelCodeBytes = 32;
constexpr std::string_view kFrontEndArchiveGamePath = "GAMEDATA/FRONTEND/NTSC.HOG";
constexpr std::string_view kFontArchiveGamePath = "GAMEDATA/COMMON/FONTS.HOG";
constexpr std::string_view kDefaultFrontEndFontMember = "DEFAULT.FNT";
constexpr std::string_view kStringTableGamePath = "GAMEDATA/COMMON/STRINGS.DAT";
constexpr std::uint64_t kFrontEndMaximumAggregateInputBytes = 64ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kFrontEndMaximumAggregateOutputBytes = 128ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kFrontEndMaximumAggregateScratchBytes = 128ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kFrontEndMaximumAggregateItems = 64ULL * 1024ULL * 1024ULL;
constexpr std::uint32_t kFrontEndMaximumNestingDepth = 16U;
constexpr std::size_t kFrontEndMaximumVisualScopes = 20U;

struct FrontEndRoute final
{
    std::string_view scope;
    std::string_view archive_member;
    std::string_view gui_member;
    std::string_view visual_member;
};

[[nodiscard]] std::optional<FrontEndRoute> RouteFor(const FrontEndScreenKey key) noexcept
{
    switch (key)
    {
    case FrontEndScreenKey::Title:
        return FrontEndRoute{
            .scope = "TITLESCR",
            .archive_member = "TITLESCR.HOG",
            .gui_member = "TITLESCR.GUI",
            .visual_member = "TITLESCR.IE",
        };
    case FrontEndScreenKey::CreateAgent:
        return FrontEndRoute{
            .scope = "AGENTNEW",
            .archive_member = "AGENTNEW.HOG",
            .gui_member = "AGENTNEW.GUI",
            .visual_member = "AGENTNEW.IE",
        };
    case FrontEndScreenKey::LoadAgent:
        return FrontEndRoute{
            .scope = "AGENTOPN",
            .archive_member = "AGENTOPN.HOG",
            .gui_member = "AGENTOPN.GUI",
            .visual_member = "AGENTOPN.IE",
        };
    }
    return std::nullopt;
}

[[nodiscard]] bool HasIsoExtension(const std::filesystem::path& path)
{
    const std::string extension = path.extension().string();
    if (extension.size() != 4 || extension.front() != '.')
        return false;
    constexpr std::string_view expected = "ISO";
    for (std::size_t index = 0; index < expected.size(); ++index)
    {
        const unsigned char value = static_cast<unsigned char>(extension[index + 1]);
        if (value >= 'a' && value <= 'z')
        {
            if (static_cast<char>(value - ('a' - 'A')) != expected[index])
                return false;
        }
        else if (static_cast<char>(value) != expected[index])
        {
            return false;
        }
    }
    return true;
}

enum class OpeningMovieArchiveState
{
    Missing,
    Unavailable,
    Ready,
};

struct SourceIdentityToken final
{
};

[[nodiscard]] GameDataError Error(const GameDataErrorCode code, std::string message)
{
    return GameDataError{
        .code = code,
        .message = std::move(message),
        .decode_error = std::nullopt,
    };
}

[[nodiscard]] asset::DecodeError AssetError(
    const asset::DecodeErrorCode code, std::string message)
{
    return asset::DecodeError{
        .code = code,
        .byte_offset = std::nullopt,
        .message = std::move(message),
    };
}

[[nodiscard]] GameDataError DecodeFailure(
    std::string context, asset::DecodeError decode_error)
{
    context += ": ";
    context += decode_error.message;
    return GameDataError{
        .code = GameDataErrorCode::DecodeFailed,
        .message = std::move(context),
        .decode_error = std::move(decode_error),
    };
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

[[nodiscard]] std::uint32_t ReadU32(
    const std::span<const std::byte> bytes, const std::size_t offset) noexcept
{
    return std::to_integer<std::uint32_t>(bytes[offset]) |
           (std::to_integer<std::uint32_t>(bytes[offset + 1U]) << 8U) |
           (std::to_integer<std::uint32_t>(bytes[offset + 2U]) << 16U) |
           (std::to_integer<std::uint32_t>(bytes[offset + 3U]) << 24U);
}

class LevelDecodeBudget final
{
public:
    [[nodiscard]] static asset::DecodeResult<LevelDecodeBudget> Create(
        const asset::LevelManifestIR& manifest, const asset::DecodeLimits limits)
    {
        std::uint64_t initial_items = 0;
        if (!Add(manifest.terrain_cells.size(), manifest.data_hog_source.hog_entries.size(),
                initial_items) ||
            initial_items > limits.maximum_items)
            return std::unexpected(AssetError(asset::DecodeErrorCode::LimitExceeded,
                "level cells and source chain exceed the shared item limit"));

        std::uint64_t cell_storage = 0;
        std::uint64_t minimum_output = 0;
        if (!Multiply(manifest.terrain_cells.size(), sizeof(asset::SpatialMeshIR), cell_storage) ||
            !Add(sizeof(asset::LevelSpatialIR), cell_storage, minimum_output))
            return std::unexpected(AssetError(asset::DecodeErrorCode::Overflow,
                "level spatial result size overflows"));
        if (minimum_output > limits.maximum_output_bytes)
            return std::unexpected(AssetError(asset::DecodeErrorCode::LimitExceeded,
                "level spatial result exceeds the shared output limit"));

        const std::uint64_t cell_depth = manifest.terrain_cells.empty() ? 0U : 1U;
        std::uint64_t required_archive_depth = 0;
        if (!Add(manifest.data_hog_source.hog_entries.size(), cell_depth,
                required_archive_depth) ||
            required_archive_depth > limits.maximum_nesting_depth)
            return std::unexpected(AssetError(asset::DecodeErrorCode::LimitExceeded,
                "level archive chain exceeds the shared nesting-depth limit"));

        return LevelDecodeBudget(limits, initial_items, sizeof(asset::LevelSpatialIR));
    }

    [[nodiscard]] static asset::DecodeResult<LevelDecodeBudget> CreateMaterialCatalogs(
        const asset::LevelManifestIR& manifest, const asset::DecodeLimits limits)
    {
        std::uint64_t initial_items = 0;
        if (!Add(manifest.terrain_cells.size(), manifest.data_hog_source.hog_entries.size(),
                initial_items) ||
            initial_items > limits.maximum_items)
            return std::unexpected(AssetError(asset::DecodeErrorCode::LimitExceeded,
                "level cells and source chain exceed the shared item limit"));

        std::uint64_t cell_storage = 0;
        std::uint64_t minimum_output = 0;
        if (!Multiply(manifest.terrain_cells.size(), sizeof(asset::MaterialCatalogIR),
                cell_storage) ||
            !Add(sizeof(asset::LevelMaterialCatalogsIR), cell_storage, minimum_output))
            return std::unexpected(AssetError(asset::DecodeErrorCode::Overflow,
                "level material-catalog result size overflows"));
        if (minimum_output > limits.maximum_output_bytes)
            return std::unexpected(AssetError(asset::DecodeErrorCode::LimitExceeded,
                "level material-catalog result exceeds the shared output limit"));

        const std::uint64_t cell_depth = manifest.terrain_cells.empty() ? 0U : 1U;
        std::uint64_t required_archive_depth = 0;
        if (!Add(manifest.data_hog_source.hog_entries.size(), cell_depth,
                required_archive_depth) ||
            required_archive_depth > limits.maximum_nesting_depth)
            return std::unexpected(AssetError(asset::DecodeErrorCode::LimitExceeded,
                "level archive chain exceeds the shared nesting-depth limit"));

        return LevelDecodeBudget(
            limits, initial_items, sizeof(asset::LevelMaterialCatalogsIR));
    }

    [[nodiscard]] static asset::DecodeResult<LevelDecodeBudget> CreateContent(
        const asset::LevelManifestIR& manifest, const asset::DecodeLimits limits)
    {
        std::uint64_t initial_items = 0;
        if (!Add(manifest.terrain_cells.size(), manifest.data_hog_source.hog_entries.size(),
                initial_items) ||
            initial_items > limits.maximum_items)
            return std::unexpected(AssetError(asset::DecodeErrorCode::LimitExceeded,
                "level cells and source chain exceed the shared item limit"));

        std::uint64_t mesh_storage = 0;
        std::uint64_t catalog_storage = 0;
        std::uint64_t cell_storage = 0;
        std::uint64_t minimum_output = 0;
        if (!Multiply(manifest.terrain_cells.size(), sizeof(asset::SpatialMeshIR), mesh_storage) ||
            !Multiply(manifest.terrain_cells.size(), sizeof(asset::MaterialCatalogIR),
                catalog_storage) ||
            !Add(mesh_storage, catalog_storage, cell_storage) ||
            !Add(sizeof(asset::LevelContentIR), cell_storage, minimum_output))
            return std::unexpected(AssetError(asset::DecodeErrorCode::Overflow,
                "level content result size overflows"));
        if (minimum_output > limits.maximum_output_bytes)
            return std::unexpected(AssetError(asset::DecodeErrorCode::LimitExceeded,
                "level content result exceeds the shared output limit"));

        const std::uint64_t cell_depth = manifest.terrain_cells.empty() ? 0U : 1U;
        std::uint64_t required_archive_depth = 0;
        if (!Add(manifest.data_hog_source.hog_entries.size(), cell_depth,
                required_archive_depth) ||
            required_archive_depth > limits.maximum_nesting_depth)
            return std::unexpected(AssetError(asset::DecodeErrorCode::LimitExceeded,
                "level archive chain exceeds the shared nesting-depth limit"));

        return LevelDecodeBudget(limits, initial_items, sizeof(asset::LevelContentIR));
    }

    [[nodiscard]] asset::DecodeResult<void> ConsumeInput(
        const std::uint64_t bytes, const std::string_view description)
    {
        if (bytes > remaining_input_bytes_)
            return std::unexpected(AssetError(asset::DecodeErrorCode::LimitExceeded,
                std::string(description) + " exceeds the shared input limit"));
        remaining_input_bytes_ -= bytes;
        return {};
    }

    [[nodiscard]] asset::DecodeResult<void> ConsumeItems(
        const std::uint64_t items, const std::string_view description)
    {
        if (items > remaining_items_)
            return std::unexpected(AssetError(asset::DecodeErrorCode::LimitExceeded,
                std::string(description) + " exceeds the shared item limit"));
        remaining_items_ -= items;
        return {};
    }

    [[nodiscard]] asset::DecodeLimits ChildLimits(const std::uint32_t archive_depth) const noexcept
    {
        asset::DecodeLimits child = limits_;
        child.maximum_input_bytes = remaining_input_bytes_;
        child.maximum_output_bytes = remaining_output_bytes_;
        child.maximum_items = remaining_items_;
        child.maximum_nesting_depth = archive_depth > limits_.maximum_nesting_depth
            ? 0
            : limits_.maximum_nesting_depth - archive_depth;
        return child;
    }

    [[nodiscard]] asset::DecodeResult<void> CommitMesh(
        const asset::SpatialMeshIR& mesh, const std::uint64_t input_bytes)
    {
        std::uint64_t item_count = mesh.root ? mesh.nodes.size() : 1U;
        if (!Add(item_count, mesh.leaves.size(), item_count) ||
            !Add(item_count, mesh.vertices.size(), item_count) ||
            !Add(item_count, mesh.triangles.size(), item_count) ||
            !Add(item_count, mesh.leaf_triangle_references.size(), item_count))
            return std::unexpected(AssetError(asset::DecodeErrorCode::Overflow,
                "decoded level mesh item count overflows"));

        std::uint64_t output_bytes = sizeof(asset::SpatialMeshIR);
        constexpr std::array<std::uint64_t, 5> element_bytes{
            sizeof(asset::SpatialNodeIR), sizeof(asset::SpatialLeafIR),
            sizeof(asset::Float3IR), sizeof(asset::SpatialTriangleIR), sizeof(std::uint32_t)};
        const std::array<std::uint64_t, 5> counts{mesh.nodes.size(), mesh.leaves.size(),
            mesh.vertices.size(), mesh.triangles.size(), mesh.leaf_triangle_references.size()};
        for (std::size_t index = 0; index < counts.size(); ++index)
        {
            std::uint64_t bytes = 0;
            if (!Multiply(counts[index], element_bytes[index], bytes) ||
                !Add(output_bytes, bytes, output_bytes))
                return std::unexpected(AssetError(asset::DecodeErrorCode::Overflow,
                    "decoded level mesh output size overflows"));
        }

        return CommitDecoded(
            input_bytes, item_count, output_bytes, "decoded mesh");
    }

    [[nodiscard]] asset::DecodeResult<void> CommitCatalog(
        const retail::DecodedVumMaterialCatalog& decoded, const std::uint64_t input_bytes)
    {
        return CommitDecoded(input_bytes, decoded.decoded_items,
            decoded.logical_output_bytes, "decoded material catalog");
    }

private:
    LevelDecodeBudget(const asset::DecodeLimits limits, const std::uint64_t initial_items,
        const std::uint64_t root_output_bytes)
        : limits_(limits), remaining_input_bytes_(limits.maximum_input_bytes),
          remaining_output_bytes_(limits.maximum_output_bytes - root_output_bytes),
          remaining_items_(limits.maximum_items - initial_items)
    {
    }

    [[nodiscard]] asset::DecodeResult<void> CommitDecoded(const std::uint64_t input_bytes,
        const std::uint64_t item_count, const std::uint64_t output_bytes,
        const std::string_view description)
    {
        if (input_bytes > remaining_input_bytes_ || item_count > remaining_items_ ||
            output_bytes > remaining_output_bytes_)
            return std::unexpected(AssetError(asset::DecodeErrorCode::LimitExceeded,
                std::string(description) + " exceeded its shared operation budget"));
        remaining_input_bytes_ -= input_bytes;
        remaining_items_ -= item_count;
        remaining_output_bytes_ -= output_bytes;
        return {};
    }

    asset::DecodeLimits limits_;
    std::uint64_t remaining_input_bytes_ = 0;
    std::uint64_t remaining_output_bytes_ = 0;
    std::uint64_t remaining_items_ = 0;
};

class SourceResolveBudget final
{
public:
    explicit SourceResolveBudget(const asset::DecodeLimits limits) noexcept
        : limits_(limits)
    {
    }

    [[nodiscard]] asset::DecodeResult<void> ConsumeAncestorArchive(
        const std::span<const std::byte> bytes)
    {
        std::uint64_t next_input = 0;
        if (!Add(input_bytes_, bytes.size(), next_input))
            return std::unexpected(AssetError(asset::DecodeErrorCode::Overflow,
                "source locator ancestor input size overflows"));
        if (next_input > limits_.maximum_input_bytes)
            return std::unexpected(AssetError(asset::DecodeErrorCode::LimitExceeded,
                "source locator ancestors exceed the input limit"));

        std::uint64_t next_items = directory_items_;
        if (bytes.size() >= 0x14U + sizeof(std::uint32_t))
        {
            if (!Add(directory_items_, ReadU32(bytes, 4U), next_items))
                return std::unexpected(AssetError(asset::DecodeErrorCode::Overflow,
                    "source locator directory item count overflows"));
            if (next_items > limits_.maximum_items)
                return std::unexpected(AssetError(asset::DecodeErrorCode::LimitExceeded,
                    "source locator ancestors exceed the item limit"));
        }

        input_bytes_ = next_input;
        directory_items_ = next_items;
        return {};
    }

    [[nodiscard]] asset::DecodeResult<void> CheckTerminal(
        const std::uint64_t terminal_bytes) const
    {
        if (terminal_bytes > limits_.maximum_input_bytes - input_bytes_)
            return std::unexpected(AssetError(asset::DecodeErrorCode::LimitExceeded,
                "source locator terminal exceeds the remaining input limit"));
        return {};
    }

    [[nodiscard]] asset::DecodeResult<void> ObserveScratch(const std::uint64_t bytes)
    {
        if (bytes > limits_.maximum_scratch_bytes)
            return std::unexpected(AssetError(asset::DecodeErrorCode::LimitExceeded,
                "source locator resolution exceeds the scratch limit"));
        peak_scratch_bytes_ = std::max(peak_scratch_bytes_, bytes);
        return {};
    }

    [[nodiscard]] std::uint64_t input_bytes() const noexcept { return input_bytes_; }
    [[nodiscard]] std::uint64_t directory_items() const noexcept { return directory_items_; }
    [[nodiscard]] std::uint64_t peak_scratch_bytes() const noexcept
    {
        return peak_scratch_bytes_;
    }

private:
    asset::DecodeLimits limits_;
    std::uint64_t input_bytes_ = 0;
    std::uint64_t directory_items_ = 0;
    std::uint64_t peak_scratch_bytes_ = 0;
};

class FrontEndLoadBudget final
{
public:
    [[nodiscard]] static asset::DecodeResult<FrontEndLoadBudget> Create(
        const asset::DecodeLimits caller_limits)
    {
        asset::DecodeLimits limits{
            .maximum_input_bytes = std::min(caller_limits.maximum_input_bytes,
                kFrontEndMaximumAggregateInputBytes),
            .maximum_output_bytes = std::min(caller_limits.maximum_output_bytes,
                kFrontEndMaximumAggregateOutputBytes),
            .maximum_scratch_bytes = std::min(caller_limits.maximum_scratch_bytes,
                kFrontEndMaximumAggregateScratchBytes),
            .maximum_items = std::min(caller_limits.maximum_items,
                kFrontEndMaximumAggregateItems),
            .maximum_string_bytes = std::min(caller_limits.maximum_string_bytes,
                retail::kFrontendMaximumStringBytes),
            .maximum_nesting_depth = std::min(caller_limits.maximum_nesting_depth,
                kFrontEndMaximumNestingDepth),
        };
        FrontEndLoadBudget budget(limits);
        auto bundle = budget.Commit(0U, 1U, sizeof(FrontEndScreenBundle), 0U,
            "front-end bundle");
        if (!bundle)
            return std::unexpected(bundle.error());
        return budget;
    }

    [[nodiscard]] asset::DecodeResult<asset::DecodeLimits> ChildLimits(
        const std::uint32_t container_depth) const
    {
        if (container_depth > limits_.maximum_nesting_depth)
        {
            return std::unexpected(AssetError(asset::DecodeErrorCode::LimitExceeded,
                "front-end route exceeds the nesting-depth limit"));
        }
        return asset::DecodeLimits{
            .maximum_input_bytes = remaining_input_bytes_,
            .maximum_output_bytes = remaining_output_bytes_,
            .maximum_scratch_bytes = remaining_scratch_bytes_,
            .maximum_items = remaining_items_,
            .maximum_string_bytes = limits_.maximum_string_bytes,
            .maximum_nesting_depth = limits_.maximum_nesting_depth - container_depth,
        };
    }

    [[nodiscard]] asset::DecodeResult<void> Commit(const std::uint64_t input_bytes,
        const std::uint64_t item_count, const std::uint64_t output_bytes,
        const std::uint64_t scratch_bytes, const std::string_view description)
    {
        if (input_bytes > remaining_input_bytes_ || item_count > remaining_items_ ||
            output_bytes > remaining_output_bytes_ || scratch_bytes > remaining_scratch_bytes_)
        {
            return std::unexpected(AssetError(asset::DecodeErrorCode::LimitExceeded,
                std::string(description) + " exceeded the shared front-end operation budget"));
        }
        remaining_input_bytes_ -= input_bytes;
        remaining_items_ -= item_count;
        remaining_output_bytes_ -= output_bytes;
        remaining_scratch_bytes_ -= scratch_bytes;
        return {};
    }

private:
    explicit FrontEndLoadBudget(const asset::DecodeLimits limits) noexcept
        : limits_(limits),
          remaining_input_bytes_(limits.maximum_input_bytes),
          remaining_output_bytes_(limits.maximum_output_bytes),
          remaining_scratch_bytes_(limits.maximum_scratch_bytes),
          remaining_items_(limits.maximum_items)
    {
    }

    asset::DecodeLimits limits_;
    std::uint64_t remaining_input_bytes_ = 0;
    std::uint64_t remaining_output_bytes_ = 0;
    std::uint64_t remaining_scratch_bytes_ = 0;
    std::uint64_t remaining_items_ = 0;
};

[[nodiscard]] asset::DecodeResult<void> PreflightArchiveDirectory(
    const std::span<const std::byte> bytes, LevelDecodeBudget& budget,
    const std::string_view description)
{
    // HOG parsing owns one directory entry per header count. Debit that count before the parser
    // reserves its vectors; malformed short headers are left to the archive parser to classify.
    if (bytes.size() < 0x14U + sizeof(std::uint32_t))
        return {};
    return budget.ConsumeItems(ReadU32(bytes, 4U), description);
}

using ArchiveDirectory = std::unordered_map<std::string, const archive::HogEntry*>;

[[nodiscard]] asset::DecodeResult<std::uint64_t> LocatorWorkspaceUpperBound(
    const asset::SourceLocator& locator, const asset::DecodeLimits limits)
{
    std::uint64_t component_objects = 0;
    std::uint64_t workspace =
        sizeof(std::string) + sizeof(std::vector<std::string>);
    if (locator.game_path.size() > limits.maximum_string_bytes)
        return std::unexpected(AssetError(asset::DecodeErrorCode::LimitExceeded,
            "source locator path exceeds the string limit"));
    if (!Multiply(locator.hog_entries.size(), sizeof(std::string), component_objects) ||
        !Add(workspace, component_objects, workspace) ||
        !Add(workspace, locator.game_path.size(), workspace))
        return std::unexpected(AssetError(asset::DecodeErrorCode::Overflow,
            "source locator workspace size overflows"));

    for (const auto& component : locator.hog_entries)
    {
        if (component.size() > limits.maximum_string_bytes)
            return std::unexpected(AssetError(asset::DecodeErrorCode::LimitExceeded,
                "source locator component exceeds the string limit"));
        if (!Add(workspace, component.size(), workspace))
            return std::unexpected(AssetError(asset::DecodeErrorCode::Overflow,
                "source locator workspace size overflows"));
    }
    return workspace;
}

[[nodiscard]] asset::DecodeResult<std::uint64_t> DirectoryWorkspaceUpperBound(
    const archive::HogArchive& archive, const asset::DecodeLimits limits)
{
    // Logical hash storage per entry: the value object, a node link, a bucket link, and one cached
    // hash/metadata word. Spare implementation-selected bucket capacity is deliberately excluded.
    constexpr std::uint64_t entry_objects =
        sizeof(ArchiveDirectory::value_type) + 3U * sizeof(void*);
    std::uint64_t entries = 0;
    std::uint64_t workspace = 0;
    if (!Multiply(archive.entries().size(), entry_objects, entries) ||
        !Add(workspace, entries, workspace))
        return std::unexpected(AssetError(asset::DecodeErrorCode::Overflow,
            "source locator directory workspace size overflows"));
    for (const auto& entry : archive.entries())
    {
        if (entry.name.size() > limits.maximum_string_bytes)
            return std::unexpected(AssetError(asset::DecodeErrorCode::LimitExceeded,
                "source locator directory name exceeds the string limit"));
        if (!Add(workspace, entry.name.size(), workspace))
            return std::unexpected(AssetError(asset::DecodeErrorCode::Overflow,
                "source locator directory workspace size overflows"));
    }
    return workspace;
}

[[nodiscard]] asset::DecodeResult<std::string> NormalizeArchiveName(
    const std::string_view name, const asset::DecodeLimits limits)
{
    if (name.size() > limits.maximum_string_bytes)
        return std::unexpected(AssetError(asset::DecodeErrorCode::LimitExceeded,
            "archive name exceeds the decoder string limit"));
    auto normalized = vfs::NormalizeGamePath(name);
    if (!normalized)
        return std::unexpected(AssetError(asset::DecodeErrorCode::InvalidReference,
            "archive contains an unsafe name"));
    if (normalized->size() > limits.maximum_string_bytes)
        return std::unexpected(AssetError(asset::DecodeErrorCode::LimitExceeded,
            "normalized archive name exceeds the decoder string limit"));
    return std::move(*normalized);
}

[[nodiscard]] asset::DecodeResult<ArchiveDirectory> BuildArchiveDirectory(
    const archive::HogArchive& archive, const asset::DecodeLimits limits)
{
    ArchiveDirectory directory;
    directory.reserve(archive.entries().size());
    for (const auto& entry : archive.entries())
    {
        auto normalized = NormalizeArchiveName(entry.name, limits);
        if (!normalized)
            return std::unexpected(normalized.error());
        if (!directory.emplace(std::move(*normalized), &entry).second)
            return std::unexpected(AssetError(asset::DecodeErrorCode::DuplicateReference,
                "archive contains duplicate normalized names"));
    }
    return directory;
}

[[nodiscard]] asset::DecodeResult<ArchiveDirectory> BuildMeasuredArchiveDirectory(
    const archive::HogArchive& archive, const asset::DecodeLimits limits,
    const std::uint64_t locator_workspace, SourceResolveBudget& budget)
{
    auto directory_workspace = DirectoryWorkspaceUpperBound(archive, limits);
    if (!directory_workspace)
        return std::unexpected(directory_workspace.error());
    std::uint64_t combined_workspace = 0;
    if (!Add(locator_workspace, *directory_workspace, combined_workspace))
        return std::unexpected(AssetError(asset::DecodeErrorCode::Overflow,
            "source locator resolution workspace size overflows"));
    auto scratch = budget.ObserveScratch(combined_workspace);
    if (!scratch)
        return std::unexpected(scratch.error());
    return BuildArchiveDirectory(archive, limits);
}

[[nodiscard]] asset::DecodeResult<void> CommitFrontEndArchiveInput(
    const std::span<const std::byte> bytes, FrontEndLoadBudget& budget,
    const std::string_view description)
{
    const std::uint64_t directory_items = bytes.size() >= 8U ? ReadU32(bytes, 4U) : 0U;
    return budget.Commit(bytes.size(), directory_items, 0U, 0U, description);
}

[[nodiscard]] asset::DecodeResult<ArchiveDirectory> BuildFrontEndArchiveDirectory(
    const archive::HogArchive& archive, const asset::DecodeLimits limits,
    FrontEndLoadBudget& budget, const std::string_view description)
{
    auto workspace = DirectoryWorkspaceUpperBound(archive, limits);
    if (!workspace)
        return std::unexpected(workspace.error());
    auto committed = budget.Commit(0U, 0U, 0U, *workspace, description);
    if (!committed)
        return std::unexpected(committed.error());
    return BuildArchiveDirectory(archive, limits);
}

[[nodiscard]] asset::DecodeResult<std::string> NormalizeFrontEndDependency(
    const std::string_view reference, const std::string_view required_suffix,
    const bool append_missing_suffix, const asset::DecodeLimits limits)
{
    auto normalized = NormalizeArchiveName(reference, limits);
    if (!normalized)
        return std::unexpected(normalized.error());
    if (normalized->find('/') != std::string::npos)
    {
        return std::unexpected(AssetError(asset::DecodeErrorCode::InvalidReference,
            "front-end dependency is not a leaf archive member"));
    }
    if (normalized->ends_with(required_suffix))
        return normalized;
    if (!append_missing_suffix || normalized->find('.') != std::string::npos)
    {
        return std::unexpected(AssetError(asset::DecodeErrorCode::InvalidReference,
            "front-end dependency has an unsupported member suffix"));
    }
    std::uint64_t extended_size = 0;
    if (!Add(normalized->size(), required_suffix.size(), extended_size))
    {
        return std::unexpected(AssetError(asset::DecodeErrorCode::Overflow,
            "front-end dependency name size overflows"));
    }
    if (extended_size > limits.maximum_string_bytes)
    {
        return std::unexpected(AssetError(asset::DecodeErrorCode::LimitExceeded,
            "front-end dependency name exceeds the string limit"));
    }
    normalized->append(required_suffix);
    return normalized;
}

[[nodiscard]] asset::DecodeResult<std::string> NormalizeFrontEndScope(
    const std::string_view scope, const asset::DecodeLimits limits)
{
    auto normalized = NormalizeArchiveName(scope, limits);
    if (!normalized)
        return std::unexpected(normalized.error());
    if (normalized->find('/') != std::string::npos ||
        normalized->find('.') != std::string::npos)
    {
        return std::unexpected(AssetError(asset::DecodeErrorCode::InvalidReference,
            "front-end visual scope is not a leaf archive stem"));
    }
    return normalized;
}

using FrontEndDependencySet = std::set<std::string, std::less<>>;
using FrontEndScopedResourceSet =
    std::map<std::string, FrontEndVisualScope::ResourceSet, std::less<>>;

[[nodiscard]] asset::DecodeResult<void> CollectFrontEndTextureReferences(
    const asset::FrontendVisualNodeIR& node, FrontEndDependencySet& references,
    const asset::DecodeLimits limits)
{
    if (node.texture_member)
    {
        auto normalized = NormalizeFrontEndDependency(
            *node.texture_member, ".TDX", false, limits);
        if (!normalized)
            return std::unexpected(normalized.error());
        references.insert(std::move(*normalized));
    }
    for (const auto& child : node.children)
    {
        auto collected = CollectFrontEndTextureReferences(child, references, limits);
        if (!collected)
            return collected;
    }
    return {};
}

[[nodiscard]] asset::DecodeResult<void> CollectFrontEndFontReferences(
    const asset::FrontendWidgetIR& node, FrontEndDependencySet& references,
    const asset::DecodeLimits limits)
{
    if (node.font_reference)
    {
        if (node.font_reference->empty())
        {
            references.emplace(kDefaultFrontEndFontMember);
        }
        else
        {
            auto normalized = NormalizeFrontEndDependency(
                *node.font_reference, ".FNT", true, limits);
            if (!normalized)
                return std::unexpected(normalized.error());
            references.insert(std::move(*normalized));
        }
    }
    for (const auto& child : node.children)
    {
        auto collected = CollectFrontEndFontReferences(child, references, limits);
        if (!collected)
            return collected;
    }
    return {};
}

[[nodiscard]] asset::DecodeResult<void> CollectFrontEndScopedResources(
    const asset::FrontendWidgetIR& node, const std::string_view primary_scope,
    const bool parentless, FrontEndScopedResourceSet& scopes,
    const asset::DecodeLimits limits)
{
    if (node.binding)
    {
        const std::string_view source_scope = node.binding->scope_reference.empty()
            ? primary_scope
            : std::string_view(node.binding->scope_reference);
        auto normalized_scope = NormalizeFrontEndScope(source_scope, limits);
        if (!normalized_scope)
            return std::unexpected(normalized_scope.error());

        const std::string_view source_resource = node.binding->resource_reference.empty()
            ? std::string_view(node.identifier)
            : std::string_view(node.binding->resource_reference);
        std::uint64_t resource_size = source_resource.size();
        constexpr std::string_view root_suffix = "_root";
        if (parentless && !Add(resource_size, root_suffix.size(), resource_size))
        {
            return std::unexpected(AssetError(asset::DecodeErrorCode::Overflow,
                "front-end visual resource name size overflows"));
        }
        if (resource_size == 0U)
        {
            return std::unexpected(AssetError(asset::DecodeErrorCode::InvalidReference,
                "front-end visual resource name is empty"));
        }
        if (resource_size > limits.maximum_string_bytes)
        {
            return std::unexpected(AssetError(asset::DecodeErrorCode::LimitExceeded,
                "front-end visual resource name exceeds the string limit"));
        }

        std::string resource(source_resource);
        if (parentless)
            resource.append(root_suffix);
        auto [scope, inserted] = scopes.try_emplace(std::move(*normalized_scope));
        if (inserted && scopes.size() > kFrontEndMaximumVisualScopes)
        {
            return std::unexpected(AssetError(asset::DecodeErrorCode::LimitExceeded,
                "front-end visual scopes exceed the fixed cache limit"));
        }
        scope->second.insert(std::move(resource));
    }

    for (const auto& child : node.children)
    {
        auto collected = CollectFrontEndScopedResources(
            child, primary_scope, false, scopes, limits);
        if (!collected)
            return collected;
    }
    return {};
}

[[nodiscard]] const asset::FrontendVisualNodeIR* FindFrontEndVisualResource(
    const asset::FrontendVisualNodeIR& node,
    const std::string_view exact_identifier) noexcept
{
    if (node.identifier == exact_identifier)
        return &node;
    for (const auto& child : node.children)
    {
        if (const auto* match = FindFrontEndVisualResource(child, exact_identifier);
            match != nullptr)
        {
            return match;
        }
    }
    return nullptr;
}

[[nodiscard]] asset::DecodeResult<void> CollectScopedFrontEndTextureReferences(
    const asset::FrontendVisualDocumentIR& document,
    const FrontEndVisualScope::ResourceSet& resources,
    FrontEndDependencySet& references, const asset::DecodeLimits limits)
{
    for (const auto& resource : resources)
    {
        const auto* node = FindFrontEndVisualResource(document.root, resource);
        if (node == nullptr)
        {
            return std::unexpected(AssetError(asset::DecodeErrorCode::InvalidReference,
                "front-end visual resource does not resolve"));
        }
        auto collected = CollectFrontEndTextureReferences(*node, references, limits);
        if (!collected)
            return collected;
    }
    return {};
}

[[nodiscard]] asset::DecodeResult<void> CommitFrontEndDependencySet(
    const FrontEndDependencySet& references, FrontEndLoadBudget& budget,
    const std::string_view description)
{
    constexpr std::uint64_t node_overhead = sizeof(std::string) + 3U * sizeof(void*);
    std::uint64_t scratch_bytes = sizeof(FrontEndDependencySet);
    for (const auto& reference : references)
    {
        if (!Add(scratch_bytes, node_overhead, scratch_bytes) ||
            !Add(scratch_bytes, reference.size(), scratch_bytes))
        {
            return std::unexpected(AssetError(asset::DecodeErrorCode::Overflow,
                "front-end dependency workspace size overflows"));
        }
    }
    return budget.Commit(0U, references.size(), 0U, scratch_bytes, description);
}

[[nodiscard]] asset::DecodeResult<void> CommitFrontEndScopedResourceSet(
    const FrontEndScopedResourceSet& scopes, FrontEndLoadBudget& budget)
{
    constexpr std::uint64_t map_node_bytes =
        sizeof(FrontEndScopedResourceSet::value_type) + 3U * sizeof(void*);
    constexpr std::uint64_t set_node_bytes =
        sizeof(FrontEndVisualScope::ResourceSet::value_type) + 3U * sizeof(void*);
    std::uint64_t items = 0U;
    std::uint64_t scratch_bytes = sizeof(FrontEndScopedResourceSet);
    for (const auto& [scope, resources] : scopes)
    {
        if (!Add(items, 1U, items) || !Add(items, resources.size(), items) ||
            !Add(scratch_bytes, map_node_bytes, scratch_bytes) ||
            !Add(scratch_bytes, scope.size(), scratch_bytes))
        {
            return std::unexpected(AssetError(asset::DecodeErrorCode::Overflow,
                "front-end visual-scope workspace size overflows"));
        }
        for (const auto& resource : resources)
        {
            if (!Add(scratch_bytes, set_node_bytes, scratch_bytes) ||
                !Add(scratch_bytes, resource.size(), scratch_bytes))
            {
                return std::unexpected(AssetError(asset::DecodeErrorCode::Overflow,
                    "front-end visual-resource workspace size overflows"));
            }
        }
    }
    return budget.Commit(
        0U, items, 0U, scratch_bytes, "front-end visual-scope references");
}

[[nodiscard]] asset::DecodeResult<std::uint64_t> FrontEndVisualScopeOutputBytes(
    const std::string_view scope,
    const FrontEndVisualScope::ResourceSet& resources)
{
    constexpr std::uint64_t map_node_bytes =
        sizeof(FrontEndScreenBundle::VisualScopeMap::value_type) + 3U * sizeof(void*);
    constexpr std::uint64_t set_node_bytes =
        sizeof(FrontEndVisualScope::ResourceSet::value_type) + 3U * sizeof(void*);
    std::uint64_t output_bytes = map_node_bytes;
    if (!Add(output_bytes, scope.size(), output_bytes))
    {
        return std::unexpected(AssetError(asset::DecodeErrorCode::Overflow,
            "front-end visual-scope output size overflows"));
    }
    for (const auto& resource : resources)
    {
        if (!Add(output_bytes, set_node_bytes, output_bytes) ||
            !Add(output_bytes, resource.size(), output_bytes))
        {
            return std::unexpected(AssetError(asset::DecodeErrorCode::Overflow,
                "front-end visual-resource output size overflows"));
        }
    }
    return output_bytes;
}

[[nodiscard]] asset::DecodeResult<const archive::HogEntry*> FindFrontEndDependency(
    const ArchiveDirectory& directory, const std::string& normalized_name)
{
    const auto match = directory.find(normalized_name);
    if (match == directory.end())
    {
        return std::unexpected(AssetError(asset::DecodeErrorCode::InvalidReference,
            "front-end dependency does not resolve"));
    }
    return match->second;
}

[[nodiscard]] asset::DecodeResult<std::uint64_t> FntLogicalOutputBytes(
    const retail::FntV3IR& font)
{
    std::uint64_t glyph_bytes = 0;
    std::uint64_t output_bytes = sizeof(retail::FntV3IR);
    if (!Multiply(font.glyphs.size(), sizeof(retail::FntV3GlyphIR), glyph_bytes) ||
        !Add(output_bytes, glyph_bytes, output_bytes) ||
        !Add(output_bytes, font.atlas_reference.size(), output_bytes))
    {
        return std::unexpected(AssetError(asset::DecodeErrorCode::Overflow,
            "front-end font output size overflows"));
    }
    return output_bytes;
}

[[nodiscard]] asset::DecodeResult<std::uint64_t> StringTableLogicalOutputBytes(
    const retail::RetailStringTableIR& table)
{
    std::uint64_t entry_bytes = 0;
    std::uint64_t output_bytes = sizeof(retail::RetailStringTableIR);
    if (!Multiply(table.entries.size(), sizeof(retail::RetailStringEntryIR), entry_bytes) ||
        !Add(output_bytes, entry_bytes, output_bytes))
    {
        return std::unexpected(AssetError(asset::DecodeErrorCode::Overflow,
            "front-end string-table output size overflows"));
    }
    for (const auto& entry : table.entries)
    {
        if (!Add(output_bytes, entry.key.size(), output_bytes) ||
            !Add(output_bytes, entry.value.size(), output_bytes))
        {
            return std::unexpected(AssetError(asset::DecodeErrorCode::Overflow,
                "front-end string-table output size overflows"));
        }
    }
    return output_bytes;
}

[[nodiscard]] asset::DecodeResult<void> ValidateFrontEndTextReferences(
    const asset::FrontendWidgetIR& node, const retail::RetailStringTableIR& strings)
{
    if (node.text_reference && !node.text_reference->empty() &&
        node.text_reference->front() == '$')
    {
        if (node.text_reference->size() == 1U)
        {
            return std::unexpected(AssetError(asset::DecodeErrorCode::InvalidReference,
                "front-end localized text reference has an empty key"));
        }
        if ((*node.text_reference)[1U] != '$' &&
            strings.Find(std::string_view(*node.text_reference).substr(1U)) == nullptr)
        {
            return std::unexpected(AssetError(asset::DecodeErrorCode::InvalidReference,
                "front-end localized text reference does not resolve"));
        }
    }
    for (const auto& child : node.children)
    {
        auto validated = ValidateFrontEndTextReferences(child, strings);
        if (!validated)
            return validated;
    }
    return {};
}

template <typename Map>
[[nodiscard]] asset::DecodeResult<std::uint64_t> FrontEndMapEntryOutputBytes(
    const std::string_view normalized_name)
{
    std::uint64_t output_bytes = sizeof(typename Map::value_type);
    if (!Add(output_bytes, normalized_name.size(), output_bytes))
    {
        return std::unexpected(AssetError(asset::DecodeErrorCode::Overflow,
            "front-end dependency output size overflows"));
    }
    return output_bytes;
}

[[nodiscard]] asset::DecodeResult<const archive::HogEntry*> FindArchiveEntry(
    const ArchiveDirectory& directory, const std::string_view name,
    const asset::DecodeLimits limits)
{
    auto normalized = NormalizeArchiveName(name, limits);
    if (!normalized)
        return std::unexpected(normalized.error());
    const auto match = directory.find(*normalized);
    if (match == directory.end())
        return std::unexpected(AssetError(asset::DecodeErrorCode::InvalidReference,
            "manifest archive reference does not resolve"));
    return match->second;
}

[[nodiscard]] asset::DecodeResult<const archive::HogEntry*> FindUniqueCol(
    const ArchiveDirectory& directory)
{
    const archive::HogEntry* match = nullptr;
    for (const auto& [name, entry] : directory)
    {
        if (!name.ends_with(".COL"))
            continue;
        if (match != nullptr)
            return std::unexpected(AssetError(asset::DecodeErrorCode::DuplicateReference,
                "cell archive contains more than one COL member"));
        match = entry;
    }
    if (match == nullptr)
        return std::unexpected(AssetError(asset::DecodeErrorCode::InvalidReference,
            "cell archive contains no COL member"));
    return match;
}

[[nodiscard]] asset::DecodeResult<const archive::HogEntry*> FindUniqueVum(
    const ArchiveDirectory& directory)
{
    const archive::HogEntry* match = nullptr;
    for (const auto& [name, entry] : directory)
    {
        if (!name.ends_with(".VUM"))
            continue;
        if (match != nullptr)
            return std::unexpected(AssetError(asset::DecodeErrorCode::DuplicateReference,
                "cell archive contains more than one VUM member"));
        match = entry;
    }
    if (match == nullptr)
        return std::unexpected(AssetError(asset::DecodeErrorCode::InvalidReference,
            "cell archive contains no VUM member"));
    return match;
}

[[nodiscard]] asset::DecodeResult<void> ValidateNestedArchiveSize(
    const std::span<const std::byte> bytes, const std::uint64_t maximum_bytes,
    const std::string_view description)
{
    if (bytes.size() > maximum_bytes)
        return std::unexpected(AssetError(asset::DecodeErrorCode::LimitExceeded,
            std::string(description) + " exceeds the configured nested-HOG byte limit"));
    return {};
}

[[nodiscard]] std::string_view TrimAsciiWhitespace(std::string_view value) noexcept
{
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t'))
        value.remove_prefix(1);
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t' ||
                                 value.back() == '\r'))
        value.remove_suffix(1);
    return value;
}

[[nodiscard]] bool EqualsAsciiCaseInsensitive(
    const std::string_view left, const std::string_view right) noexcept
{
    if (left.size() != right.size())
        return false;
    for (std::size_t index = 0; index < left.size(); ++index)
    {
        const auto fold = [](const unsigned char value) {
            return value >= static_cast<unsigned char>('a') &&
                    value <= static_cast<unsigned char>('z')
                ? static_cast<unsigned char>(value - ('a' - 'A'))
                : value;
        };
        if (fold(static_cast<unsigned char>(left[index])) !=
            fold(static_cast<unsigned char>(right[index])))
            return false;
    }
    return true;
}

[[nodiscard]] std::expected<void, GameDataError> ValidateSystemConfig(
    const std::span<const std::byte> bytes)
{
    if (bytes.empty())
        return std::unexpected(Error(GameDataErrorCode::UnsupportedBuild,
            "SYSTEM.CNF is empty"));
    for (std::size_t index = 0; index < bytes.size(); ++index)
    {
        const auto value = std::to_integer<unsigned char>(bytes[index]);
        if (value != '\t' && value != '\r' && value != '\n' &&
            (value < 0x20U || value > 0x7EU))
            return std::unexpected(Error(GameDataErrorCode::UnsupportedBuild,
                "SYSTEM.CNF contains non-ASCII data"));
    }

    const auto* first = reinterpret_cast<const char*>(bytes.data());
    const std::string_view text(first, bytes.size());
    std::optional<std::string_view> boot_value;
    std::size_t cursor = 0;
    while (cursor <= text.size())
    {
        const std::size_t line_end = text.find('\n', cursor);
        const std::size_t count = line_end == std::string_view::npos
            ? text.size() - cursor
            : line_end - cursor;
        const std::string_view line = TrimAsciiWhitespace(text.substr(cursor, count));
        const std::size_t equals = line.find('=');
        if (equals != std::string_view::npos &&
            EqualsAsciiCaseInsensitive(TrimAsciiWhitespace(line.substr(0, equals)), "BOOT2"))
        {
            if (boot_value)
                return std::unexpected(Error(GameDataErrorCode::UnsupportedBuild,
                    "SYSTEM.CNF contains duplicate BOOT2 entries"));
            boot_value = TrimAsciiWhitespace(line.substr(equals + 1U));
        }
        if (line_end == std::string_view::npos)
            break;
        cursor = line_end + 1U;
    }

    if (!boot_value)
        return std::unexpected(Error(GameDataErrorCode::UnsupportedBuild,
            "SYSTEM.CNF has no BOOT2 entry"));
    if (!EqualsAsciiCaseInsensitive(*boot_value, kExpectedBootValue))
        return std::unexpected(Error(GameDataErrorCode::UnsupportedBuild,
            "unsupported retail build: expected NTSC-U SCUS-97264"));
    return {};
}

[[nodiscard]] std::expected<std::string, GameDataError> NormalizeLevelCode(
    const std::string_view level_code)
{
    if (level_code.empty() || level_code.size() > kMaximumLevelCodeBytes)
        return std::unexpected(Error(GameDataErrorCode::InvalidLevelCode,
            "level code must contain 1 to 32 ASCII letters or digits"));

    std::string normalized;
    normalized.reserve(level_code.size());
    for (const unsigned char value : level_code)
    {
        const bool is_upper = value >= static_cast<unsigned char>('A') &&
                              value <= static_cast<unsigned char>('Z');
        const bool is_lower = value >= static_cast<unsigned char>('a') &&
                              value <= static_cast<unsigned char>('z');
        const bool is_digit = value >= static_cast<unsigned char>('0') &&
                              value <= static_cast<unsigned char>('9');
        if (!is_upper && !is_lower && !is_digit)
            return std::unexpected(Error(GameDataErrorCode::InvalidLevelCode,
                "level code must contain only ASCII letters or digits"));
        normalized.push_back(static_cast<char>(is_lower ? value - ('a' - 'A') : value));
    }
    return normalized;
}
} // namespace

struct GameDataService::Impl
{
    GameDataIdentity identity;
    vfs::VirtualFileSystem files;
    GameDataServiceConfig config;
    OpeningMovieArchiveState opening_movie_archive_state =
        OpeningMovieArchiveState::Missing;
    std::shared_ptr<const void> source_identity = std::make_shared<SourceIdentityToken>();
};

std::string_view RetailBuildName(const RetailBuild build) noexcept
{
    switch (build)
    {
    case RetailBuild::NtscUScus97264:
        return "NTSC-U SCUS-97264";
    }
    return "unknown";
}

std::string_view GameDataErrorCodeName(const GameDataErrorCode code) noexcept
{
    switch (code)
    {
    case GameDataErrorCode::InvalidConfiguration:
        return "invalid-configuration";
    case GameDataErrorCode::ForeignService:
        return "foreign-service";
    case GameDataErrorCode::MountFailed:
        return "mount-failed";
    case GameDataErrorCode::MissingRequiredFile:
        return "missing-required-file";
    case GameDataErrorCode::UnsupportedBuild:
        return "unsupported-build";
    case GameDataErrorCode::InvalidLevelCode:
        return "invalid-level-code";
    case GameDataErrorCode::ReadFailed:
        return "read-failed";
    case GameDataErrorCode::MalformedArchive:
        return "malformed-archive";
    case GameDataErrorCode::DecodeFailed:
        return "decode-failed";
    }
    return "unknown";
}

std::expected<GameDataService, GameDataError> GameDataService::Open(
    GameDataServiceConfig config)
{
    OMEGA_DEBUG_BREAK_SUBSYSTEM_ENTRY("omega_content");
    if (config.root.empty() || config.maximum_system_config_bytes == 0 ||
        config.maximum_pop_bytes == 0 || config.maximum_data_hog_bytes == 0 ||
        config.maximum_nested_hog_bytes == 0)
        return std::unexpected(Error(GameDataErrorCode::InvalidConfiguration,
            "game-data root and byte limits must be non-empty"));

    auto impl = std::make_unique<Impl>();
    impl->config = std::move(config);
    std::error_code source_error;
    const auto source_status = std::filesystem::status(impl->config.root, source_error);
    if (source_error)
        return std::unexpected(Error(GameDataErrorCode::MountFailed,
            "unable to mount game-data root"));

    std::expected<void, std::string> mounted = std::unexpected("unsupported game-data source");
    if (std::filesystem::is_directory(source_status))
        mounted = impl->files.MountDirectory(impl->config.root);
    else if (std::filesystem::is_regular_file(source_status) && HasIsoExtension(impl->config.root))
        mounted = impl->files.MountIso9660(impl->config.root);
    if (!mounted)
        return std::unexpected(Error(GameDataErrorCode::MountFailed,
            "unable to mount game-data root"));

    const auto opening_movie_archive_mounted = impl->files.MountHogFromGameFile(
        kOpeningMovieArchiveMountRoot, kOpeningMovieArchiveGamePath);
    impl->files.Freeze();
    if (opening_movie_archive_mounted)
    {
        impl->opening_movie_archive_state = OpeningMovieArchiveState::Ready;
    }
    else if (impl->files.Contains(kOpeningMovieArchiveGamePath))
    {
        impl->opening_movie_archive_state = OpeningMovieArchiveState::Unavailable;
    }

    if (!impl->files.Contains("SYSTEM.CNF"))
        return std::unexpected(Error(GameDataErrorCode::MissingRequiredFile,
            "game-data root is missing SYSTEM.CNF"));
    auto system_config = impl->files.Read(
        "SYSTEM.CNF", impl->config.maximum_system_config_bytes);
    if (!system_config)
        return std::unexpected(Error(GameDataErrorCode::ReadFailed,
            "unable to read SYSTEM.CNF"));
    auto validated = ValidateSystemConfig(*system_config);
    if (!validated)
        return std::unexpected(validated.error());

    if (!impl->files.Contains(kExpectedBootExecutable))
        return std::unexpected(Error(GameDataErrorCode::MissingRequiredFile,
            "NTSC-U data root is missing SCUS_972.64"));

    impl->identity = GameDataIdentity{
        .build = RetailBuild::NtscUScus97264,
        .boot_executable = std::string(kExpectedBootExecutable),
    };
    return GameDataService(std::move(impl));
}

GameDataService::GameDataService(std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl))
{
}

GameDataService::~GameDataService() = default;
GameDataService::GameDataService(GameDataService&&) noexcept = default;
GameDataService& GameDataService::operator=(GameDataService&&) noexcept = default;

const GameDataIdentity& GameDataService::identity() const noexcept
{
    return impl_->identity;
}

std::expected<asset::OpeningMovieSource, GameDataError>
GameDataService::LoadOpeningMovieSource(const std::string_view member_name) const
{
    asset::DecodeLimits limits;
    limits.maximum_input_bytes = asset::kOpeningMovieMaximumSourceBytes;
    limits.maximum_output_bytes = asset::kOpeningMovieMaximumSourceBytes;
    limits.maximum_nesting_depth = 0U;

    auto resolved = ResolveSourceLocator(source_binding(), asset::SourceLocator{
        .game_path = std::string(kOpeningMovieArchiveGamePath),
        .hog_entries = {std::string(member_name)},
    }, limits);
    if (!resolved)
        return std::unexpected(resolved.error());

    auto source = asset::OpeningMovieSource::Create(
        std::move(resolved->terminal_bytes));
    if (!source)
    {
        return std::unexpected(DecodeFailure("unable to load opening movie source",
            AssetError(asset::DecodeErrorCode::LimitExceeded,
                "opening movie source exceeds the input limit")));
    }
    return std::move(*source);
}

std::expected<FrontEndScreenBundle, GameDataError>
GameDataService::LoadFrontEndScreen(const FrontEndScreenKey key) const
{
    const auto route = RouteFor(key);
    if (!route)
    {
        return std::unexpected(Error(GameDataErrorCode::InvalidConfiguration,
            "unknown front-end screen key"));
    }
    if (!impl_)
    {
        return std::unexpected(Error(GameDataErrorCode::ForeignService,
            "game-data service has no mounted source"));
    }

    auto created_budget = FrontEndLoadBudget::Create(impl_->config.front_end_decode_limits);
    if (!created_budget)
    {
        return std::unexpected(DecodeFailure(
            "unable to create front-end operation", created_budget.error()));
    }
    FrontEndLoadBudget budget = std::move(*created_budget);

    const auto read_direct = [this, &budget](const std::string_view game_path,
                                 const std::uint64_t family_maximum_bytes)
        -> std::expected<std::vector<std::byte>, GameDataError> {
        auto limits = budget.ChildLimits(0U);
        if (!limits)
        {
            return std::unexpected(DecodeFailure(
                "unable to read canonical front-end data", limits.error()));
        }
        if (!impl_->files.Contains(game_path))
        {
            return std::unexpected(Error(GameDataErrorCode::MissingRequiredFile,
                "canonical front-end data is unavailable"));
        }
        auto file_size = impl_->files.FileSize(game_path);
        if (!file_size)
        {
            return std::unexpected(Error(GameDataErrorCode::ReadFailed,
                "unable to read canonical front-end data"));
        }
        const std::uint64_t maximum_bytes = std::min({family_maximum_bytes,
            impl_->config.maximum_data_hog_bytes, limits->maximum_input_bytes});
        if (*file_size > maximum_bytes)
        {
            return std::unexpected(DecodeFailure("unable to read canonical front-end data",
                AssetError(asset::DecodeErrorCode::LimitExceeded,
                    "canonical front-end file exceeds the operation input limit")));
        }

        auto resolved = ResolveSourceLocator(source_binding(), asset::SourceLocator{
            .game_path = std::string(game_path),
        }, *limits);
        if (!resolved)
            return std::unexpected(resolved.error());
        return std::move(resolved->terminal_bytes);
    };

    struct RetainedFrontEndTextureMetadata final
    {
        asset::IndexedImageEncoding sampling_encoding;
        FrontEndTextureAlphaMode alpha_mode;
    };
    const auto retained_texture_metadata = [](const retail::DecodedFrontEndTdx& decoded)
        -> asset::DecodeResult<RetainedFrontEndTextureMetadata> {
        asset::IndexedImageEncoding sampling_encoding;
        switch (decoded.upload_plan.sampling_format)
        {
        case retail::TdxGsPixelStorageFormat::Psmt4:
            sampling_encoding = asset::IndexedImageEncoding::Indexed4;
            break;
        case retail::TdxGsPixelStorageFormat::Psmt8:
            sampling_encoding = asset::IndexedImageEncoding::Indexed8;
            break;
        case retail::TdxGsPixelStorageFormat::Psmct32:
        default:
            return std::unexpected(AssetError(asset::DecodeErrorCode::UnsupportedVariant,
                "front-end texture has no supported indexed sampling format"));
        }
        if (decoded.image.source_encoding != sampling_encoding)
        {
            return std::unexpected(AssetError(asset::DecodeErrorCode::Malformed,
                "front-end texture sampling metadata is inconsistent"));
        }
        return RetainedFrontEndTextureMetadata{
            .sampling_encoding = sampling_encoding,
            .alpha_mode = decoded.upload_plan.texture_alpha_enabled
                ? FrontEndTextureAlphaMode::UsesPaletteAlpha
                : FrontEndTextureAlphaMode::IgnoresTextureAlpha,
        };
    };

    auto front_end_bytes = read_direct(
        kFrontEndArchiveGamePath, kFrontEndMaximumAggregateInputBytes);
    if (!front_end_bytes)
        return std::unexpected(front_end_bytes.error());
    auto archive_limits = budget.ChildLimits(0U);
    if (!archive_limits)
    {
        return std::unexpected(DecodeFailure(
            "unable to load front-end archive", archive_limits.error()));
    }
    auto front_end_input = CommitFrontEndArchiveInput(
        *front_end_bytes, budget, "front-end archive");
    if (!front_end_input)
    {
        return std::unexpected(DecodeFailure(
            "unable to load front-end archive", front_end_input.error()));
    }
    auto front_end_archive = archive::HogArchive::FromBytes(std::move(*front_end_bytes));
    if (!front_end_archive)
    {
        return std::unexpected(Error(GameDataErrorCode::MalformedArchive,
            "canonical front-end archive is malformed"));
    }
    auto front_end_directory = BuildFrontEndArchiveDirectory(
        *front_end_archive, *archive_limits, budget, "front-end archive directory");
    if (!front_end_directory)
    {
        return std::unexpected(DecodeFailure(
            "unable to index front-end archive", front_end_directory.error()));
    }

    auto screen_entry = FindArchiveEntry(
        *front_end_directory, route->archive_member, *archive_limits);
    if (!screen_entry)
    {
        return std::unexpected(DecodeFailure(
            "unable to resolve front-end screen archive", screen_entry.error()));
    }
    const auto screen_bytes = front_end_archive->payload(**screen_entry);
    auto screen_size = ValidateNestedArchiveSize(screen_bytes,
        impl_->config.maximum_nested_hog_bytes, "front-end screen archive");
    if (!screen_size)
    {
        return std::unexpected(DecodeFailure(
            "unable to load front-end screen archive", screen_size.error()));
    }
    auto screen_limits = budget.ChildLimits(1U);
    if (!screen_limits)
    {
        return std::unexpected(DecodeFailure(
            "unable to load front-end screen archive", screen_limits.error()));
    }
    auto screen_copy = budget.Commit(
        0U, 0U, 0U, screen_bytes.size(), "front-end screen archive copy");
    if (!screen_copy)
    {
        return std::unexpected(DecodeFailure(
            "unable to load front-end screen archive", screen_copy.error()));
    }
    auto screen_input = CommitFrontEndArchiveInput(
        screen_bytes, budget, "front-end screen archive");
    if (!screen_input)
    {
        return std::unexpected(DecodeFailure(
            "unable to load front-end screen archive", screen_input.error()));
    }
    auto screen_archive = archive::HogArchive::FromSpan(
        screen_bytes, impl_->config.maximum_nested_hog_bytes);
    if (!screen_archive)
    {
        return std::unexpected(Error(GameDataErrorCode::MalformedArchive,
            "canonical front-end screen archive is malformed"));
    }
    auto screen_directory = BuildFrontEndArchiveDirectory(
        *screen_archive, *screen_limits, budget, "front-end screen archive directory");
    if (!screen_directory)
    {
        return std::unexpected(DecodeFailure(
            "unable to index front-end screen archive", screen_directory.error()));
    }

    auto gui_entry = FindArchiveEntry(*screen_directory, route->gui_member, *screen_limits);
    auto visual_entry = FindArchiveEntry(
        *screen_directory, route->visual_member, *screen_limits);
    if (!gui_entry || !visual_entry)
    {
        const asset::DecodeError error = !gui_entry ? gui_entry.error() : visual_entry.error();
        return std::unexpected(DecodeFailure(
            "front-end screen document is unavailable", error));
    }

    const auto gui_bytes = screen_archive->payload(**gui_entry);
    auto gui = retail::DecodeGuiFrontendMeasured(gui_bytes, *screen_limits);
    if (!gui)
    {
        return std::unexpected(DecodeFailure(
            "unable to decode canonical front-end GUI", gui.error()));
    }
    auto gui_commit = budget.Commit(gui_bytes.size(), gui->decoded_items,
        gui->logical_output_bytes, 0U, "front-end GUI");
    if (!gui_commit)
    {
        return std::unexpected(DecodeFailure(
            "unable to decode canonical front-end GUI", gui_commit.error()));
    }

    screen_limits = budget.ChildLimits(1U);
    if (!screen_limits)
    {
        return std::unexpected(DecodeFailure(
            "unable to decode canonical front-end visual document", screen_limits.error()));
    }
    const auto visual_bytes = screen_archive->payload(**visual_entry);
    auto visual = retail::DecodeIeFrontendMeasured(visual_bytes, *screen_limits);
    if (!visual)
    {
        return std::unexpected(DecodeFailure(
            "unable to decode canonical front-end visual document", visual.error()));
    }
    auto visual_commit = budget.Commit(visual_bytes.size(), visual->decoded_items,
        visual->logical_output_bytes, 0U, "front-end visual document");
    if (!visual_commit)
    {
        return std::unexpected(DecodeFailure(
            "unable to decode canonical front-end visual document", visual_commit.error()));
    }

    FrontEndDependencySet texture_references;
    FrontEndDependencySet font_references;
    FrontEndScopedResourceSet scoped_resources;
    auto reference_limits = budget.ChildLimits(1U);
    if (!reference_limits)
    {
        return std::unexpected(DecodeFailure(
            "unable to collect front-end dependencies", reference_limits.error()));
    }
    auto primary_scope = NormalizeFrontEndScope(route->scope, *reference_limits);
    if (!primary_scope)
    {
        return std::unexpected(DecodeFailure(
            "unable to collect front-end visual scopes", primary_scope.error()));
    }
    scoped_resources.try_emplace(*primary_scope);
    auto textures_collected = CollectFrontEndTextureReferences(
        visual->document.root, texture_references, *reference_limits);
    auto fonts_collected = CollectFrontEndFontReferences(
        gui->document.root, font_references, *reference_limits);
    auto scopes_collected = CollectFrontEndScopedResources(
        gui->document.root, *primary_scope, true, scoped_resources, *reference_limits);
    if (!textures_collected || !fonts_collected || !scopes_collected)
    {
        const asset::DecodeError error = !textures_collected
            ? textures_collected.error()
            : !fonts_collected ? fonts_collected.error() : scopes_collected.error();
        return std::unexpected(DecodeFailure(
            "unable to collect front-end dependencies", error));
    }
    const auto primary_resources = scoped_resources.find(*primary_scope);
    if (primary_resources == scoped_resources.end())
    {
        return std::unexpected(DecodeFailure("unable to collect front-end visual scopes",
            AssetError(asset::DecodeErrorCode::InvalidReference,
                "front-end primary visual scope is unavailable")));
    }
    auto primary_resources_collected = CollectScopedFrontEndTextureReferences(
        visual->document, primary_resources->second, texture_references, *reference_limits);
    if (!primary_resources_collected)
    {
        return std::unexpected(DecodeFailure(
            "front-end primary visual resource is unavailable",
            primary_resources_collected.error()));
    }
    auto texture_set_commit = CommitFrontEndDependencySet(
        texture_references, budget, "front-end texture references");
    auto font_set_commit = CommitFrontEndDependencySet(
        font_references, budget, "front-end font references");
    auto scope_set_commit = CommitFrontEndScopedResourceSet(scoped_resources, budget);
    if (!texture_set_commit || !font_set_commit || !scope_set_commit)
    {
        const asset::DecodeError error = !texture_set_commit
            ? texture_set_commit.error()
            : !font_set_commit ? font_set_commit.error() : scope_set_commit.error();
        return std::unexpected(DecodeFailure(
            "unable to collect front-end dependencies", error));
    }

    FrontEndScreenBundle::TextureMap screen_textures;
    for (const auto& reference : texture_references)
    {
        auto entry = FindFrontEndDependency(*screen_directory, reference);
        if (!entry)
        {
            return std::unexpected(DecodeFailure(
                "front-end texture is unavailable", entry.error()));
        }
        auto texture_limits = budget.ChildLimits(1U);
        if (!texture_limits)
        {
            return std::unexpected(DecodeFailure(
                "unable to decode front-end texture", texture_limits.error()));
        }
        const auto bytes = screen_archive->payload(**entry);
        auto decoded = retail::DecodeTdxFrontEnd(bytes, *texture_limits);
        if (!decoded)
        {
            return std::unexpected(DecodeFailure(
                "unable to decode front-end texture", decoded.error()));
        }
        auto retained_metadata = retained_texture_metadata(*decoded);
        if (!retained_metadata)
        {
            return std::unexpected(DecodeFailure(
                "unable to retain front-end texture", retained_metadata.error()));
        }
        auto entry_output = FrontEndMapEntryOutputBytes<
            FrontEndScreenBundle::TextureMap>(reference);
        if (!entry_output)
        {
            return std::unexpected(DecodeFailure(
                "unable to retain front-end texture", entry_output.error()));
        }
        std::uint64_t combined_output = 0;
        if (!Add(decoded->logical_output_bytes, *entry_output, combined_output))
        {
            return std::unexpected(DecodeFailure("unable to retain front-end texture",
                AssetError(asset::DecodeErrorCode::Overflow,
                    "front-end texture output size overflows")));
        }
        auto texture_commit = budget.Commit(bytes.size(), decoded->decoded_items,
            combined_output, decoded->peak_scratch_bytes, "front-end texture");
        if (!texture_commit)
        {
            return std::unexpected(DecodeFailure(
                "unable to retain front-end texture", texture_commit.error()));
        }
        screen_textures.emplace(reference,
            FrontEndTextureBinding(std::move(decoded->image),
                retained_metadata->sampling_encoding, retained_metadata->alpha_mode));
    }

    FrontEndScreenBundle::VisualScopeMap visual_scopes;
    auto primary_scope_output = FrontEndVisualScopeOutputBytes(
        *primary_scope, primary_resources->second);
    if (!primary_scope_output)
    {
        return std::unexpected(DecodeFailure(
            "unable to retain front-end primary visual scope",
            primary_scope_output.error()));
    }
    auto primary_scope_commit = budget.Commit(0U, 0U, *primary_scope_output, 0U,
        "front-end primary visual scope");
    if (!primary_scope_commit)
    {
        return std::unexpected(DecodeFailure(
            "unable to retain front-end primary visual scope",
            primary_scope_commit.error()));
    }
    visual_scopes.emplace(*primary_scope,
        FrontEndVisualScope(std::move(visual->document),
            std::move(primary_resources->second), std::move(screen_textures)));

    for (auto& [scope, resources] : scoped_resources)
    {
        if (scope == *primary_scope)
            continue;

        auto scope_limits = budget.ChildLimits(1U);
        if (!scope_limits)
        {
            return std::unexpected(DecodeFailure(
                "unable to load front-end visual scope", scope_limits.error()));
        }
        auto scope_archive_name = NormalizeFrontEndDependency(
            scope, ".HOG", true, *scope_limits);
        auto scope_visual_name = NormalizeFrontEndDependency(
            scope, ".IE", true, *scope_limits);
        if (!scope_archive_name || !scope_visual_name)
        {
            const asset::DecodeError error = !scope_archive_name
                ? scope_archive_name.error()
                : scope_visual_name.error();
            return std::unexpected(DecodeFailure(
                "front-end visual scope route is invalid", error));
        }
        auto scope_archive_entry = FindFrontEndDependency(
            *front_end_directory, *scope_archive_name);
        if (!scope_archive_entry)
        {
            return std::unexpected(DecodeFailure(
                "front-end visual scope archive is unavailable",
                scope_archive_entry.error()));
        }
        const auto scope_archive_bytes = front_end_archive->payload(**scope_archive_entry);
        auto scope_archive_size = ValidateNestedArchiveSize(scope_archive_bytes,
            impl_->config.maximum_nested_hog_bytes, "front-end visual scope archive");
        if (!scope_archive_size)
        {
            return std::unexpected(DecodeFailure(
                "unable to load front-end visual scope archive",
                scope_archive_size.error()));
        }
        auto scope_copy = budget.Commit(
            0U, 0U, 0U, scope_archive_bytes.size(), "front-end visual scope archive copy");
        if (!scope_copy)
        {
            return std::unexpected(DecodeFailure(
                "unable to load front-end visual scope archive", scope_copy.error()));
        }
        auto scope_input = CommitFrontEndArchiveInput(
            scope_archive_bytes, budget, "front-end visual scope archive");
        if (!scope_input)
        {
            return std::unexpected(DecodeFailure(
                "unable to load front-end visual scope archive", scope_input.error()));
        }
        auto scope_archive = archive::HogArchive::FromSpan(
            scope_archive_bytes, impl_->config.maximum_nested_hog_bytes);
        if (!scope_archive)
        {
            return std::unexpected(Error(GameDataErrorCode::MalformedArchive,
                "canonical front-end visual scope archive is malformed"));
        }
        auto scope_directory = BuildFrontEndArchiveDirectory(*scope_archive,
            *scope_limits, budget, "front-end visual scope archive directory");
        if (!scope_directory)
        {
            return std::unexpected(DecodeFailure(
                "unable to index front-end visual scope archive",
                scope_directory.error()));
        }
        auto scope_visual_entry = FindFrontEndDependency(
            *scope_directory, *scope_visual_name);
        if (!scope_visual_entry)
        {
            return std::unexpected(DecodeFailure(
                "front-end scoped visual document is unavailable",
                scope_visual_entry.error()));
        }

        scope_limits = budget.ChildLimits(1U);
        if (!scope_limits)
        {
            return std::unexpected(DecodeFailure(
                "unable to decode front-end scoped visual document",
                scope_limits.error()));
        }
        const auto scope_visual_bytes = scope_archive->payload(**scope_visual_entry);
        auto scope_visual = retail::DecodeIeFrontendMeasured(
            scope_visual_bytes, *scope_limits);
        if (!scope_visual)
        {
            return std::unexpected(DecodeFailure(
                "unable to decode canonical front-end scoped visual document",
                scope_visual.error()));
        }
        auto scope_visual_commit = budget.Commit(scope_visual_bytes.size(),
            scope_visual->decoded_items, scope_visual->logical_output_bytes, 0U,
            "front-end scoped visual document");
        if (!scope_visual_commit)
        {
            return std::unexpected(DecodeFailure(
                "unable to decode canonical front-end scoped visual document",
                scope_visual_commit.error()));
        }

        FrontEndDependencySet scoped_texture_references;
        auto scoped_textures_collected = CollectScopedFrontEndTextureReferences(
            scope_visual->document, resources, scoped_texture_references, *scope_limits);
        if (!scoped_textures_collected)
        {
            return std::unexpected(DecodeFailure(
                "front-end scoped visual resource is unavailable",
                scoped_textures_collected.error()));
        }
        auto scoped_texture_set_commit = CommitFrontEndDependencySet(
            scoped_texture_references, budget, "front-end scoped texture references");
        if (!scoped_texture_set_commit)
        {
            return std::unexpected(DecodeFailure(
                "unable to collect front-end scoped textures",
                scoped_texture_set_commit.error()));
        }

        FrontEndVisualScope::TextureMap scoped_textures;
        for (const auto& reference : scoped_texture_references)
        {
            auto entry = FindFrontEndDependency(*scope_directory, reference);
            if (!entry)
            {
                return std::unexpected(DecodeFailure(
                    "front-end scoped texture is unavailable", entry.error()));
            }
            auto texture_limits = budget.ChildLimits(1U);
            if (!texture_limits)
            {
                return std::unexpected(DecodeFailure(
                    "unable to decode front-end scoped texture",
                    texture_limits.error()));
            }
            const auto bytes = scope_archive->payload(**entry);
            auto decoded = retail::DecodeTdxScopedFrontEnd(bytes, *texture_limits);
            if (!decoded)
            {
                return std::unexpected(DecodeFailure(
                    "unable to decode front-end scoped texture", decoded.error()));
            }
            auto retained_metadata = retained_texture_metadata(*decoded);
            if (!retained_metadata)
            {
                return std::unexpected(DecodeFailure(
                    "unable to retain front-end scoped texture",
                    retained_metadata.error()));
            }
            auto entry_output = FrontEndMapEntryOutputBytes<
                FrontEndVisualScope::TextureMap>(reference);
            if (!entry_output)
            {
                return std::unexpected(DecodeFailure(
                    "unable to retain front-end scoped texture",
                    entry_output.error()));
            }
            std::uint64_t combined_output = 0U;
            if (!Add(decoded->logical_output_bytes, *entry_output, combined_output))
            {
                return std::unexpected(DecodeFailure(
                    "unable to retain front-end scoped texture",
                    AssetError(asset::DecodeErrorCode::Overflow,
                        "front-end scoped texture output size overflows")));
            }
            auto texture_commit = budget.Commit(bytes.size(), decoded->decoded_items,
                combined_output, decoded->peak_scratch_bytes,
                "front-end scoped texture");
            if (!texture_commit)
            {
                return std::unexpected(DecodeFailure(
                    "unable to retain front-end scoped texture",
                    texture_commit.error()));
            }
            scoped_textures.emplace(reference,
                FrontEndTextureBinding(std::move(decoded->image),
                    retained_metadata->sampling_encoding,
                    retained_metadata->alpha_mode));
        }

        auto scope_output = FrontEndVisualScopeOutputBytes(scope, resources);
        if (!scope_output)
        {
            return std::unexpected(DecodeFailure(
                "unable to retain front-end visual scope", scope_output.error()));
        }
        auto scope_commit = budget.Commit(
            0U, 0U, *scope_output, 0U, "front-end visual scope");
        if (!scope_commit)
        {
            return std::unexpected(DecodeFailure(
                "unable to retain front-end visual scope", scope_commit.error()));
        }
        visual_scopes.emplace(scope,
            FrontEndVisualScope(std::move(scope_visual->document),
                std::move(resources), std::move(scoped_textures)));
    }

    FrontEndScreenBundle::FontMap fonts;
    FrontEndScreenBundle::TextureMap font_atlases;
    FrontEndDependencySet atlas_references;
    if (!font_references.empty())
    {
        auto font_archive_bytes = read_direct(
            kFontArchiveGamePath, kFrontEndMaximumAggregateInputBytes);
        if (!font_archive_bytes)
            return std::unexpected(font_archive_bytes.error());
        auto font_limits = budget.ChildLimits(0U);
        if (!font_limits)
        {
            return std::unexpected(DecodeFailure(
                "unable to load front-end font archive", font_limits.error()));
        }
        auto font_archive_input = CommitFrontEndArchiveInput(
            *font_archive_bytes, budget, "front-end font archive");
        if (!font_archive_input)
        {
            return std::unexpected(DecodeFailure(
                "unable to load front-end font archive", font_archive_input.error()));
        }
        auto font_archive = archive::HogArchive::FromBytes(std::move(*font_archive_bytes));
        if (!font_archive)
        {
            return std::unexpected(Error(GameDataErrorCode::MalformedArchive,
                "canonical front-end font archive is malformed"));
        }
        auto font_directory = BuildFrontEndArchiveDirectory(
            *font_archive, *font_limits, budget, "front-end font archive directory");
        if (!font_directory)
        {
            return std::unexpected(DecodeFailure(
                "unable to index front-end font archive", font_directory.error()));
        }

        for (const auto& reference : font_references)
        {
            auto entry = FindFrontEndDependency(*font_directory, reference);
            if (!entry)
            {
                return std::unexpected(DecodeFailure(
                    "front-end font is unavailable", entry.error()));
            }
            auto decoder_limits = budget.ChildLimits(0U);
            if (!decoder_limits)
            {
                return std::unexpected(DecodeFailure(
                    "unable to decode front-end font", decoder_limits.error()));
            }
            const auto bytes = font_archive->payload(**entry);
            auto decoded = retail::DecodeFntV3(bytes, *decoder_limits);
            if (!decoded)
            {
                return std::unexpected(DecodeFailure(
                    "unable to decode front-end font", decoded.error()));
            }
            auto atlas_name = NormalizeFrontEndDependency(
                decoded->atlas_reference, ".TDX", false, *decoder_limits);
            if (!atlas_name)
            {
                return std::unexpected(DecodeFailure(
                    "front-end font atlas reference is invalid", atlas_name.error()));
            }
            atlas_references.insert(std::move(*atlas_name));

            auto font_output = FntLogicalOutputBytes(*decoded);
            auto entry_output = FrontEndMapEntryOutputBytes<
                FrontEndScreenBundle::FontMap>(reference);
            if (!font_output || !entry_output)
            {
                const asset::DecodeError error = !font_output
                    ? font_output.error()
                    : entry_output.error();
                return std::unexpected(DecodeFailure(
                    "unable to retain front-end font", error));
            }
            std::uint64_t combined_output = 0;
            if (!Add(*font_output, *entry_output, combined_output))
            {
                return std::unexpected(DecodeFailure("unable to retain front-end font",
                    AssetError(asset::DecodeErrorCode::Overflow,
                        "front-end font output size overflows")));
            }
            auto font_commit = budget.Commit(bytes.size(),
                1U + decoded->glyphs.size(), combined_output, 0U, "front-end font");
            if (!font_commit)
            {
                return std::unexpected(DecodeFailure(
                    "unable to retain front-end font", font_commit.error()));
            }
            fonts.emplace(reference, std::move(*decoded));
        }

        auto atlas_set_commit = CommitFrontEndDependencySet(
            atlas_references, budget, "front-end font atlas references");
        if (!atlas_set_commit)
        {
            return std::unexpected(DecodeFailure(
                "unable to collect front-end font atlases", atlas_set_commit.error()));
        }
        for (const auto& reference : atlas_references)
        {
            auto entry = FindFrontEndDependency(*font_directory, reference);
            if (!entry)
            {
                return std::unexpected(DecodeFailure(
                    "front-end font atlas is unavailable", entry.error()));
            }
            auto decoder_limits = budget.ChildLimits(0U);
            if (!decoder_limits)
            {
                return std::unexpected(DecodeFailure(
                    "unable to decode front-end font atlas", decoder_limits.error()));
            }
            const auto bytes = font_archive->payload(**entry);
            auto decoded = retail::DecodeTdxFrontEnd(bytes, *decoder_limits);
            if (!decoded)
            {
                return std::unexpected(DecodeFailure(
                    "unable to decode front-end font atlas", decoded.error()));
            }
            auto retained_metadata = retained_texture_metadata(*decoded);
            if (!retained_metadata)
            {
                return std::unexpected(DecodeFailure(
                    "unable to retain front-end font atlas",
                    retained_metadata.error()));
            }
            auto entry_output = FrontEndMapEntryOutputBytes<
                FrontEndScreenBundle::TextureMap>(reference);
            if (!entry_output)
            {
                return std::unexpected(DecodeFailure(
                    "unable to retain front-end font atlas", entry_output.error()));
            }
            std::uint64_t combined_output = 0;
            if (!Add(decoded->logical_output_bytes, *entry_output, combined_output))
            {
                return std::unexpected(DecodeFailure(
                    "unable to retain front-end font atlas",
                    AssetError(asset::DecodeErrorCode::Overflow,
                        "front-end font-atlas output size overflows")));
            }
            auto atlas_commit = budget.Commit(bytes.size(), decoded->decoded_items,
                combined_output, decoded->peak_scratch_bytes, "front-end font atlas");
            if (!atlas_commit)
            {
                return std::unexpected(DecodeFailure(
                    "unable to retain front-end font atlas", atlas_commit.error()));
            }
            font_atlases.emplace(reference,
                FrontEndTextureBinding(std::move(decoded->image),
                    retained_metadata->sampling_encoding,
                    retained_metadata->alpha_mode));
        }
    }

    auto string_bytes = read_direct(
        kStringTableGamePath, retail::kRetailStringTableMaximumInputBytes);
    if (!string_bytes)
        return std::unexpected(string_bytes.error());
    auto string_limits = budget.ChildLimits(0U);
    if (!string_limits)
    {
        return std::unexpected(DecodeFailure(
            "unable to decode front-end string table", string_limits.error()));
    }
    auto strings = retail::ParseRetailStringTable(*string_bytes, *string_limits);
    if (!strings)
    {
        return std::unexpected(DecodeFailure(
            "unable to decode front-end string table", strings.error()));
    }
    auto text_references = ValidateFrontEndTextReferences(gui->document.root, *strings);
    if (!text_references)
    {
        return std::unexpected(DecodeFailure(
            "front-end localized text is unavailable", text_references.error()));
    }
    auto string_output = StringTableLogicalOutputBytes(*strings);
    std::uint64_t string_scratch = 0;
    if (!Multiply(strings->entries.size(), 5U * sizeof(std::uint64_t), string_scratch))
    {
        return std::unexpected(DecodeFailure("unable to retain front-end string table",
            AssetError(asset::DecodeErrorCode::Overflow,
                "front-end string-table scratch size overflows")));
    }
    if (!string_output)
    {
        return std::unexpected(DecodeFailure(
            "unable to retain front-end string table", string_output.error()));
    }
    auto string_commit = budget.Commit(string_bytes->size(),
        1U + strings->entries.size(), *string_output, string_scratch,
        "front-end string table");
    if (!string_commit)
    {
        return std::unexpected(DecodeFailure(
            "unable to retain front-end string table", string_commit.error()));
    }

    RetailFrontEndPresentationCapability capability{
        RetailFrontEndPresentationCapability::ConstructionKey{}};
    return FrontEndScreenBundle(key, std::move(gui->document),
        std::move(*primary_scope), std::move(visual_scopes), std::move(fonts),
        std::move(font_atlases), std::move(*strings), std::move(capability));
}

GameDataService::SourceBinding GameDataService::source_binding() const noexcept
{
    return SourceBinding{.identity = impl_
            ? std::weak_ptr<const void>{impl_->source_identity}
            : std::weak_ptr<const void>{}};
}

std::expected<GameDataService::ResolvedSourceLocator, GameDataError>
GameDataService::ResolveSourceLocator(const SourceBinding& expected_source,
    const asset::SourceLocator& locator, const asset::DecodeLimits caller_limits) const
{
    const auto expected_identity = expected_source.identity.lock();
    if (!impl_ || !expected_identity || expected_identity != impl_->source_identity)
        return std::unexpected(Error(GameDataErrorCode::ForeignService,
            "source binding does not belong to this game-data service"));

    const std::uint64_t component_count = locator.hog_entries.size();
    const std::uint64_t maximum_components =
        static_cast<std::uint64_t>(caller_limits.maximum_nesting_depth) + 1U;
    const std::uint64_t maximum_representable_components =
        static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()) + 1U;
    if (component_count > maximum_components ||
        component_count > maximum_representable_components)
        return std::unexpected(DecodeFailure("invalid source locator",
            AssetError(asset::DecodeErrorCode::LimitExceeded,
                "source locator exceeds the nesting-depth limit")));
    const std::uint32_t archive_depth = component_count == 0U
        ? 0U
        : static_cast<std::uint32_t>(component_count - 1U);

    SourceResolveBudget budget(caller_limits);
    auto locator_workspace = LocatorWorkspaceUpperBound(locator, caller_limits);
    if (!locator_workspace)
        return std::unexpected(DecodeFailure(
            "invalid source locator", locator_workspace.error()));
    auto locator_scratch = budget.ObserveScratch(*locator_workspace);
    if (!locator_scratch)
        return std::unexpected(DecodeFailure(
            "unable to resolve source locator", locator_scratch.error()));

    auto normalized_game_path = NormalizeArchiveName(locator.game_path, caller_limits);
    if (!normalized_game_path)
        return std::unexpected(DecodeFailure(
            "invalid source locator", normalized_game_path.error()));

    std::vector<std::string> normalized_components;
    normalized_components.reserve(locator.hog_entries.size());
    for (const auto& component : locator.hog_entries)
    {
        auto normalized = NormalizeArchiveName(component, caller_limits);
        if (!normalized)
            return std::unexpected(DecodeFailure(
                "invalid source locator", normalized.error()));
        normalized_components.push_back(std::move(*normalized));
    }

    if (*normalized_game_path == kOpeningMovieArchiveGamePath &&
        normalized_components.size() == 1U)
    {
        if (impl_->opening_movie_archive_state == OpeningMovieArchiveState::Missing)
        {
            return std::unexpected(Error(GameDataErrorCode::MissingRequiredFile,
                "opening movie archive is unavailable"));
        }
        if (impl_->opening_movie_archive_state != OpeningMovieArchiveState::Ready)
        {
            return std::unexpected(Error(GameDataErrorCode::MalformedArchive,
                "opening movie archive is unavailable"));
        }

        std::uint64_t mounted_path_workspace = 0U;
        if (!Add(kOpeningMovieArchiveMountRoot.size() + 1U,
                normalized_components.front().size(), mounted_path_workspace) ||
            !Add(*locator_workspace, mounted_path_workspace, mounted_path_workspace))
        {
            return std::unexpected(DecodeFailure("unable to load opening movie source",
                AssetError(asset::DecodeErrorCode::Overflow,
                    "opening movie member workspace size overflows")));
        }
        auto mounted_path_scratch = budget.ObserveScratch(mounted_path_workspace);
        if (!mounted_path_scratch)
        {
            return std::unexpected(DecodeFailure(
                "unable to load opening movie source", mounted_path_scratch.error()));
        }

        std::string mounted_member_path(kOpeningMovieArchiveMountRoot);
        mounted_member_path.push_back('/');
        mounted_member_path += normalized_components.front();
        if (!impl_->files.Contains(mounted_member_path))
        {
            return std::unexpected(DecodeFailure("unable to load opening movie source",
                AssetError(asset::DecodeErrorCode::InvalidReference,
                    "opening movie member does not resolve")));
        }
        auto selected_size = impl_->files.FileSize(mounted_member_path);
        if (!selected_size)
        {
            return std::unexpected(Error(GameDataErrorCode::ReadFailed,
                "unable to read opening movie member"));
        }
        auto terminal_limit = budget.CheckTerminal(*selected_size);
        if (!terminal_limit)
        {
            return std::unexpected(DecodeFailure(
                "unable to load opening movie source", terminal_limit.error()));
        }
        auto source_bytes = impl_->files.Read(
            mounted_member_path, asset::kOpeningMovieMaximumSourceBytes);
        if (!source_bytes || source_bytes->size() != *selected_size)
        {
            return std::unexpected(Error(GameDataErrorCode::ReadFailed,
                "unable to read opening movie member"));
        }
        return ResolvedSourceLocator{
            .terminal_bytes = std::move(*source_bytes),
            .ancestor_input_bytes = 0U,
            .ancestor_directory_items = 0U,
            .archive_depth = 0U,
            .peak_scratch_bytes = budget.peak_scratch_bytes(),
        };
    }

    if (!impl_->files.Contains(*normalized_game_path))
        return std::unexpected(Error(GameDataErrorCode::MissingRequiredFile,
            "source locator game path is unavailable"));
    auto source_bytes = impl_->files.Read(
        *normalized_game_path, impl_->config.maximum_data_hog_bytes);
    if (!source_bytes)
        return std::unexpected(Error(GameDataErrorCode::ReadFailed,
            "unable to read source locator game path"));

    if (normalized_components.empty())
    {
        auto terminal_limit = budget.CheckTerminal(source_bytes->size());
        if (!terminal_limit)
            return std::unexpected(DecodeFailure(
                "unable to resolve source locator", terminal_limit.error()));
        return ResolvedSourceLocator{
            .terminal_bytes = std::move(*source_bytes),
            .ancestor_input_bytes = 0,
            .ancestor_directory_items = 0,
            .archive_depth = 0,
            .peak_scratch_bytes = budget.peak_scratch_bytes(),
        };
    }

    auto source_budget = budget.ConsumeAncestorArchive(*source_bytes);
    if (!source_budget)
        return std::unexpected(DecodeFailure(
            "unable to resolve source locator", source_budget.error()));
    auto current_archive = archive::HogArchive::FromBytes(std::move(*source_bytes));
    if (!current_archive)
        return std::unexpected(Error(GameDataErrorCode::MalformedArchive,
            "source locator ancestor archive is malformed"));
    auto current_directory = BuildMeasuredArchiveDirectory(
        *current_archive, caller_limits, *locator_workspace, budget);
    if (!current_directory)
        return std::unexpected(DecodeFailure(
            "unable to resolve source locator", current_directory.error()));

    for (std::size_t index = 0; index < normalized_components.size(); ++index)
    {
        const auto match = current_directory->find(normalized_components[index]);
        if (match == current_directory->end())
            return std::unexpected(DecodeFailure(
                "unable to resolve source locator",
                AssetError(asset::DecodeErrorCode::InvalidReference,
                    "manifest archive reference does not resolve")));
        const auto member_bytes = current_archive->payload(*match->second);
        const bool terminal = index + 1U == normalized_components.size();
        if (terminal)
        {
            if (normalized_components[index].ends_with(".HOG"))
            {
                auto terminal_container_size = ValidateNestedArchiveSize(member_bytes,
                    impl_->config.maximum_nested_hog_bytes,
                    "source locator terminal container");
                if (!terminal_container_size)
                    return std::unexpected(DecodeFailure(
                        "unable to resolve source locator", terminal_container_size.error()));
            }
            auto terminal_limit = budget.CheckTerminal(member_bytes.size());
            if (!terminal_limit)
                return std::unexpected(DecodeFailure(
                    "unable to resolve source locator", terminal_limit.error()));
            return ResolvedSourceLocator{
                .terminal_bytes = std::vector<std::byte>(
                    member_bytes.begin(), member_bytes.end()),
                .ancestor_input_bytes = budget.input_bytes(),
                .ancestor_directory_items = budget.directory_items(),
                .archive_depth = archive_depth,
                .peak_scratch_bytes = budget.peak_scratch_bytes(),
            };
        }

        auto nested_size = ValidateNestedArchiveSize(member_bytes,
            impl_->config.maximum_nested_hog_bytes, "source locator ancestor archive");
        if (!nested_size)
            return std::unexpected(DecodeFailure(
                "unable to resolve source locator", nested_size.error()));
        auto nested_budget = budget.ConsumeAncestorArchive(member_bytes);
        if (!nested_budget)
            return std::unexpected(DecodeFailure(
                "unable to resolve source locator", nested_budget.error()));
        // The member span remains backed by current_archive, so the normalized directory can be
        // released before the next parser/directory workspace is created.
        *current_directory = ArchiveDirectory{};
        auto nested_archive = archive::HogArchive::FromSpan(
            member_bytes, impl_->config.maximum_nested_hog_bytes);
        if (!nested_archive)
            return std::unexpected(Error(GameDataErrorCode::MalformedArchive,
                "source locator ancestor archive is malformed"));
        *current_archive = std::move(*nested_archive);
        current_directory = BuildMeasuredArchiveDirectory(
            *current_archive, caller_limits, *locator_workspace, budget);
        if (!current_directory)
            return std::unexpected(DecodeFailure(
                "unable to resolve source locator", current_directory.error()));
    }

    return std::unexpected(Error(GameDataErrorCode::MalformedArchive,
        "source locator resolution ended unexpectedly"));
}

std::expected<asset::LevelManifestIR, GameDataError> GameDataService::LoadLevelManifest(
    const std::string_view level_code) const
{
    auto normalized_level = NormalizeLevelCode(level_code);
    if (!normalized_level)
        return std::unexpected(normalized_level.error());

    const std::string level_root = "GAMEDATA/" + *normalized_level;
    const std::string pop_path = level_root + "/DATA.POP";
    const std::string data_hog_path = level_root + "/DATA.HOG";
    std::array<std::string, 2> texture_paths{
        level_root + "/TEX.HOG",
        level_root + "/MAPTEX.HOG",
    };
    if (!impl_->files.Contains(pop_path))
        return std::unexpected(Error(GameDataErrorCode::MissingRequiredFile,
            "level is missing DATA.POP: " + *normalized_level));
    if (!impl_->files.Contains(data_hog_path))
        return std::unexpected(Error(GameDataErrorCode::MissingRequiredFile,
            "level is missing DATA.HOG: " + *normalized_level));
    if (!impl_->files.Contains(texture_paths[0]))
        return std::unexpected(Error(GameDataErrorCode::MissingRequiredFile,
            "level is missing required primary texture container"));
    if (!impl_->files.Contains(texture_paths[1]))
        return std::unexpected(Error(GameDataErrorCode::MissingRequiredFile,
            "level is missing required map texture container"));

    // DecodePopLevelManifest accounts sizeof(LevelManifestIR), including the empty vector object.
    // Reserve only the two appended SourceLocator elements, their owned path bytes, and two items.
    const auto decode_limits = impl_->config.decode_limits;
    std::uint64_t texture_source_output_bytes = 0;
    if (!Multiply(texture_paths.size(), sizeof(asset::SourceLocator),
            texture_source_output_bytes))
        return std::unexpected(DecodeFailure("unable to decode level manifest",
            AssetError(asset::DecodeErrorCode::Overflow,
                "level texture source output size overflows")));
    for (const auto& path : texture_paths)
    {
        if (path.size() > decode_limits.maximum_string_bytes)
            return std::unexpected(DecodeFailure("unable to decode level manifest",
                AssetError(asset::DecodeErrorCode::LimitExceeded,
                    "level texture source path exceeds decoder string limit")));
        if (!Add(texture_source_output_bytes, path.size(), texture_source_output_bytes))
            return std::unexpected(DecodeFailure("unable to decode level manifest",
                AssetError(asset::DecodeErrorCode::Overflow,
                    "level texture source output size overflows")));
    }
    if (texture_paths.size() > decode_limits.maximum_items ||
        texture_source_output_bytes > decode_limits.maximum_output_bytes)
        return std::unexpected(DecodeFailure("unable to decode level manifest",
            AssetError(asset::DecodeErrorCode::LimitExceeded,
                "level texture sources exceed decoder limits")));

    auto manifest_limits = decode_limits;
    manifest_limits.maximum_items -= texture_paths.size();
    manifest_limits.maximum_output_bytes -= texture_source_output_bytes;

    auto pop_bytes = impl_->files.Read(pop_path, impl_->config.maximum_pop_bytes);
    if (!pop_bytes)
        return std::unexpected(Error(GameDataErrorCode::ReadFailed,
            "unable to read level DATA.POP"));
    auto data_hog_bytes = impl_->files.Read(
        data_hog_path, impl_->config.maximum_data_hog_bytes);
    if (!data_hog_bytes)
        return std::unexpected(Error(GameDataErrorCode::ReadFailed,
            "unable to read level DATA.HOG"));

    auto data_hog = archive::HogArchive::FromBytes(std::move(*data_hog_bytes));
    if (!data_hog)
        return std::unexpected(Error(GameDataErrorCode::MalformedArchive,
            "invalid level DATA.HOG: " + data_hog.error()));

    auto manifest = retail::DecodePopLevelManifest(*pop_bytes, data_hog->entries(),
        asset::SourceLocator{.game_path = data_hog_path, .hog_entries = {}},
        manifest_limits);
    if (!manifest)
    {
        return std::unexpected(GameDataError{
            .code = GameDataErrorCode::DecodeFailed,
            .message = "unable to decode level manifest: " + manifest.error().message,
            .decode_error = std::move(manifest.error()),
        });
    }
    manifest->texture_sources.reserve(texture_paths.size());
    for (auto& path : texture_paths)
    {
        manifest->texture_sources.push_back(
            asset::SourceLocator{.game_path = std::move(path), .hog_entries = {}});
    }
    return std::move(*manifest);
}

std::expected<asset::LevelSpatialIR, GameDataError> GameDataService::LoadLevelSpatial(
    const asset::LevelManifestIR& manifest) const
{
    const asset::DecodeLimits limits = impl_->config.decode_limits;
    auto budget_result = LevelDecodeBudget::Create(manifest, limits);
    if (!budget_result)
        return std::unexpected(DecodeFailure(
            "unable to initialize level spatial decode", budget_result.error()));
    LevelDecodeBudget budget = std::move(*budget_result);

    auto source_path = NormalizeArchiveName(manifest.data_hog_source.game_path, limits);
    if (!source_path)
        return std::unexpected(DecodeFailure(
            "invalid level archive source", source_path.error()));
    auto source_bytes = impl_->files.Read(*source_path, impl_->config.maximum_data_hog_bytes);
    if (!source_bytes)
        return std::unexpected(Error(GameDataErrorCode::ReadFailed,
            "unable to read level archive source"));
    auto source_input = budget.ConsumeInput(source_bytes->size(), "level archive source");
    if (!source_input)
        return std::unexpected(DecodeFailure(
            "unable to load level spatial data", source_input.error()));
    auto source_items = PreflightArchiveDirectory(
        *source_bytes, budget, "level archive directory");
    if (!source_items)
        return std::unexpected(DecodeFailure(
            "unable to index level archive source", source_items.error()));

    auto source_archive = archive::HogArchive::FromBytes(std::move(*source_bytes));
    if (!source_archive)
        return std::unexpected(Error(GameDataErrorCode::MalformedArchive,
            "invalid level archive source: " + source_archive.error()));
    auto source_directory = BuildArchiveDirectory(*source_archive, limits);
    if (!source_directory)
        return std::unexpected(DecodeFailure(
            "unable to index level archive source", source_directory.error()));

    for (const auto& component : manifest.data_hog_source.hog_entries)
    {
        auto entry = FindArchiveEntry(*source_directory, component, limits);
        if (!entry)
            return std::unexpected(DecodeFailure(
                "unable to resolve nested level archive", entry.error()));
        const auto nested_bytes = source_archive->payload(**entry);
        auto nested_input = budget.ConsumeInput(nested_bytes.size(), "nested level archive");
        if (!nested_input)
            return std::unexpected(DecodeFailure(
                "unable to load nested level archive", nested_input.error()));
        auto nested_size = ValidateNestedArchiveSize(nested_bytes,
            impl_->config.maximum_nested_hog_bytes, "nested level archive");
        if (!nested_size)
            return std::unexpected(DecodeFailure(
                "unable to load nested level archive", nested_size.error()));
        auto nested_items = PreflightArchiveDirectory(
            nested_bytes, budget, "nested level archive directory");
        if (!nested_items)
            return std::unexpected(DecodeFailure(
                "unable to index nested level archive", nested_items.error()));
        auto nested = archive::HogArchive::FromSpan(
            nested_bytes, impl_->config.maximum_nested_hog_bytes);
        if (!nested)
            return std::unexpected(Error(GameDataErrorCode::MalformedArchive,
                "invalid nested level archive: " + nested.error()));
        *source_archive = std::move(*nested);
        source_directory = BuildArchiveDirectory(*source_archive, limits);
        if (!source_directory)
            return std::unexpected(DecodeFailure(
                "unable to index nested level archive", source_directory.error()));
    }

    asset::LevelSpatialIR result;
    result.terrain_cells.reserve(manifest.terrain_cells.size());
    const std::uint32_t archive_depth = static_cast<std::uint32_t>(
        manifest.data_hog_source.hog_entries.size() + 1U);
    for (std::size_t cell_index = 0; cell_index < manifest.terrain_cells.size(); ++cell_index)
    {
        const auto& cell = manifest.terrain_cells[cell_index];
        const std::string cell_context = "level cell " + std::to_string(cell_index);
        auto cell_entry = FindArchiveEntry(*source_directory, cell.data_hog_entry, limits);
        if (!cell_entry)
            return std::unexpected(DecodeFailure(
                "unable to resolve " + cell_context, cell_entry.error()));
        const auto cell_bytes = source_archive->payload(**cell_entry);
        auto cell_input = budget.ConsumeInput(cell_bytes.size(), "cell archive");
        if (!cell_input)
            return std::unexpected(DecodeFailure(
                "unable to load " + cell_context, cell_input.error()));
        auto cell_size = ValidateNestedArchiveSize(
            cell_bytes, impl_->config.maximum_nested_hog_bytes, "cell archive");
        if (!cell_size)
            return std::unexpected(DecodeFailure(
                "unable to load " + cell_context, cell_size.error()));
        auto cell_items = PreflightArchiveDirectory(
            cell_bytes, budget, "cell archive directory");
        if (!cell_items)
            return std::unexpected(DecodeFailure(
                "unable to index " + cell_context, cell_items.error()));
        auto cell_archive = archive::HogArchive::FromSpan(
            cell_bytes, impl_->config.maximum_nested_hog_bytes);
        if (!cell_archive)
            return std::unexpected(Error(GameDataErrorCode::MalformedArchive,
                "invalid " + cell_context + " archive: " + cell_archive.error()));
        auto cell_directory = BuildArchiveDirectory(*cell_archive, limits);
        if (!cell_directory)
            return std::unexpected(DecodeFailure(
                "unable to index " + cell_context, cell_directory.error()));
        auto col_entry = FindUniqueCol(*cell_directory);
        if (!col_entry)
            return std::unexpected(DecodeFailure(
                "unable to resolve " + cell_context + " spatial member", col_entry.error()));
        const auto col_bytes = cell_archive->payload(**col_entry);
        auto mesh = retail::DecodeColSpatialMesh(
            col_bytes, budget.ChildLimits(archive_depth));
        if (!mesh)
            return std::unexpected(DecodeFailure(
                "unable to decode " + cell_context + " spatial mesh", mesh.error()));
        auto committed = budget.CommitMesh(*mesh, col_bytes.size());
        if (!committed)
            return std::unexpected(DecodeFailure(
                "unable to commit " + cell_context + " spatial mesh", committed.error()));
        result.terrain_cells.push_back(std::move(*mesh));
    }
    return result;
}

std::expected<asset::LevelMaterialCatalogsIR, GameDataError>
GameDataService::LoadLevelMaterialCatalogs(const asset::LevelManifestIR& manifest) const
{
    const asset::DecodeLimits limits = impl_->config.decode_limits;
    auto budget_result = LevelDecodeBudget::CreateMaterialCatalogs(manifest, limits);
    if (!budget_result)
        return std::unexpected(DecodeFailure(
            "unable to initialize level material-catalog decode", budget_result.error()));
    LevelDecodeBudget budget = std::move(*budget_result);

    auto source_path = NormalizeArchiveName(manifest.data_hog_source.game_path, limits);
    if (!source_path)
        return std::unexpected(DecodeFailure(
            "invalid level archive source", source_path.error()));
    auto source_bytes = impl_->files.Read(*source_path, impl_->config.maximum_data_hog_bytes);
    if (!source_bytes)
        return std::unexpected(Error(GameDataErrorCode::ReadFailed,
            "unable to read level archive source"));
    auto source_input = budget.ConsumeInput(source_bytes->size(), "level archive source");
    if (!source_input)
        return std::unexpected(DecodeFailure(
            "unable to load level material catalogs", source_input.error()));
    auto source_items = PreflightArchiveDirectory(
        *source_bytes, budget, "level archive directory");
    if (!source_items)
        return std::unexpected(DecodeFailure(
            "unable to index level archive source", source_items.error()));

    auto source_archive = archive::HogArchive::FromBytes(std::move(*source_bytes));
    if (!source_archive)
        return std::unexpected(Error(GameDataErrorCode::MalformedArchive,
            "invalid level archive source: " + source_archive.error()));
    auto source_directory = BuildArchiveDirectory(*source_archive, limits);
    if (!source_directory)
        return std::unexpected(DecodeFailure(
            "unable to index level archive source", source_directory.error()));

    for (const auto& component : manifest.data_hog_source.hog_entries)
    {
        auto entry = FindArchiveEntry(*source_directory, component, limits);
        if (!entry)
            return std::unexpected(DecodeFailure(
                "unable to resolve nested level archive", entry.error()));
        const auto nested_bytes = source_archive->payload(**entry);
        auto nested_input = budget.ConsumeInput(nested_bytes.size(), "nested level archive");
        if (!nested_input)
            return std::unexpected(DecodeFailure(
                "unable to load nested level archive", nested_input.error()));
        auto nested_size = ValidateNestedArchiveSize(nested_bytes,
            impl_->config.maximum_nested_hog_bytes, "nested level archive");
        if (!nested_size)
            return std::unexpected(DecodeFailure(
                "unable to load nested level archive", nested_size.error()));
        auto nested_items = PreflightArchiveDirectory(
            nested_bytes, budget, "nested level archive directory");
        if (!nested_items)
            return std::unexpected(DecodeFailure(
                "unable to index nested level archive", nested_items.error()));
        auto nested = archive::HogArchive::FromSpan(
            nested_bytes, impl_->config.maximum_nested_hog_bytes);
        if (!nested)
            return std::unexpected(Error(GameDataErrorCode::MalformedArchive,
                "invalid nested level archive: " + nested.error()));
        *source_archive = std::move(*nested);
        source_directory = BuildArchiveDirectory(*source_archive, limits);
        if (!source_directory)
            return std::unexpected(DecodeFailure(
                "unable to index nested level archive", source_directory.error()));
    }

    asset::LevelMaterialCatalogsIR result;
    result.terrain_cells.reserve(manifest.terrain_cells.size());
    const std::uint32_t archive_depth = static_cast<std::uint32_t>(
        manifest.data_hog_source.hog_entries.size() + 1U);
    for (std::size_t cell_index = 0; cell_index < manifest.terrain_cells.size(); ++cell_index)
    {
        const auto& cell = manifest.terrain_cells[cell_index];
        const std::string cell_context = "level cell " + std::to_string(cell_index);
        auto cell_entry = FindArchiveEntry(*source_directory, cell.data_hog_entry, limits);
        if (!cell_entry)
            return std::unexpected(DecodeFailure(
                "unable to resolve " + cell_context, cell_entry.error()));
        const auto cell_bytes = source_archive->payload(**cell_entry);
        auto cell_input = budget.ConsumeInput(cell_bytes.size(), "cell archive");
        if (!cell_input)
            return std::unexpected(DecodeFailure(
                "unable to load " + cell_context, cell_input.error()));
        auto cell_size = ValidateNestedArchiveSize(
            cell_bytes, impl_->config.maximum_nested_hog_bytes, "cell archive");
        if (!cell_size)
            return std::unexpected(DecodeFailure(
                "unable to load " + cell_context, cell_size.error()));
        auto cell_items = PreflightArchiveDirectory(
            cell_bytes, budget, "cell archive directory");
        if (!cell_items)
            return std::unexpected(DecodeFailure(
                "unable to index " + cell_context, cell_items.error()));
        auto cell_archive = archive::HogArchive::FromSpan(
            cell_bytes, impl_->config.maximum_nested_hog_bytes);
        if (!cell_archive)
            return std::unexpected(Error(GameDataErrorCode::MalformedArchive,
                "invalid " + cell_context + " archive: " + cell_archive.error()));
        auto cell_directory = BuildArchiveDirectory(*cell_archive, limits);
        if (!cell_directory)
            return std::unexpected(DecodeFailure(
                "unable to index " + cell_context, cell_directory.error()));
        auto vum_entry = FindUniqueVum(*cell_directory);
        if (!vum_entry)
            return std::unexpected(DecodeFailure(
                "unable to resolve " + cell_context + " material member", vum_entry.error()));
        const auto vum_bytes = cell_archive->payload(**vum_entry);
        auto decoded = retail::DecodeVumMaterialCatalogMeasured(
            vum_bytes, budget.ChildLimits(archive_depth));
        if (!decoded)
            return std::unexpected(DecodeFailure(
                "unable to decode " + cell_context + " material catalog", decoded.error()));
        auto committed = budget.CommitCatalog(*decoded, vum_bytes.size());
        if (!committed)
            return std::unexpected(DecodeFailure(
                "unable to commit " + cell_context + " material catalog", committed.error()));
        result.terrain_cells.push_back(std::move(decoded->catalog));
    }
    return result;
}

std::expected<asset::LevelContentIR, GameDataError> GameDataService::LoadLevelContent(
    const asset::LevelManifestIR& manifest) const
{
    const asset::DecodeLimits limits = impl_->config.decode_limits;
    auto budget_result = LevelDecodeBudget::CreateContent(manifest, limits);
    if (!budget_result)
        return std::unexpected(DecodeFailure(
            "unable to initialize level content decode", budget_result.error()));
    LevelDecodeBudget budget = std::move(*budget_result);

    auto source_path = NormalizeArchiveName(manifest.data_hog_source.game_path, limits);
    if (!source_path)
        return std::unexpected(DecodeFailure(
            "invalid level archive source", source_path.error()));
    auto source_bytes = impl_->files.Read(*source_path, impl_->config.maximum_data_hog_bytes);
    if (!source_bytes)
        return std::unexpected(Error(GameDataErrorCode::ReadFailed,
            "unable to read level archive source"));
    auto source_input = budget.ConsumeInput(source_bytes->size(), "level archive source");
    if (!source_input)
        return std::unexpected(DecodeFailure(
            "unable to load level content", source_input.error()));
    auto source_items = PreflightArchiveDirectory(
        *source_bytes, budget, "level archive directory");
    if (!source_items)
        return std::unexpected(DecodeFailure(
            "unable to index level archive source", source_items.error()));

    auto source_archive = archive::HogArchive::FromBytes(std::move(*source_bytes));
    if (!source_archive)
        return std::unexpected(Error(GameDataErrorCode::MalformedArchive,
            "invalid level archive source: " + source_archive.error()));
    auto source_directory = BuildArchiveDirectory(*source_archive, limits);
    if (!source_directory)
        return std::unexpected(DecodeFailure(
            "unable to index level archive source", source_directory.error()));

    for (const auto& component : manifest.data_hog_source.hog_entries)
    {
        auto entry = FindArchiveEntry(*source_directory, component, limits);
        if (!entry)
            return std::unexpected(DecodeFailure(
                "unable to resolve nested level archive", entry.error()));
        const auto nested_bytes = source_archive->payload(**entry);
        auto nested_input = budget.ConsumeInput(nested_bytes.size(), "nested level archive");
        if (!nested_input)
            return std::unexpected(DecodeFailure(
                "unable to load nested level archive", nested_input.error()));
        auto nested_size = ValidateNestedArchiveSize(nested_bytes,
            impl_->config.maximum_nested_hog_bytes, "nested level archive");
        if (!nested_size)
            return std::unexpected(DecodeFailure(
                "unable to load nested level archive", nested_size.error()));
        auto nested_items = PreflightArchiveDirectory(
            nested_bytes, budget, "nested level archive directory");
        if (!nested_items)
            return std::unexpected(DecodeFailure(
                "unable to index nested level archive", nested_items.error()));
        auto nested = archive::HogArchive::FromSpan(
            nested_bytes, impl_->config.maximum_nested_hog_bytes);
        if (!nested)
            return std::unexpected(Error(GameDataErrorCode::MalformedArchive,
                "invalid nested level archive: " + nested.error()));
        *source_archive = std::move(*nested);
        source_directory = BuildArchiveDirectory(*source_archive, limits);
        if (!source_directory)
            return std::unexpected(DecodeFailure(
                "unable to index nested level archive", source_directory.error()));
    }

    asset::LevelContentIR result;
    result.spatial.terrain_cells.reserve(manifest.terrain_cells.size());
    result.material_catalogs.terrain_cells.reserve(manifest.terrain_cells.size());
    const std::uint32_t archive_depth = static_cast<std::uint32_t>(
        manifest.data_hog_source.hog_entries.size() + 1U);
    for (std::size_t cell_index = 0; cell_index < manifest.terrain_cells.size(); ++cell_index)
    {
        const auto& cell = manifest.terrain_cells[cell_index];
        const std::string cell_context = "level cell " + std::to_string(cell_index);
        auto cell_entry = FindArchiveEntry(*source_directory, cell.data_hog_entry, limits);
        if (!cell_entry)
            return std::unexpected(DecodeFailure(
                "unable to resolve " + cell_context, cell_entry.error()));
        const auto cell_bytes = source_archive->payload(**cell_entry);
        auto cell_input = budget.ConsumeInput(cell_bytes.size(), "cell archive");
        if (!cell_input)
            return std::unexpected(DecodeFailure(
                "unable to load " + cell_context, cell_input.error()));
        auto cell_size = ValidateNestedArchiveSize(
            cell_bytes, impl_->config.maximum_nested_hog_bytes, "cell archive");
        if (!cell_size)
            return std::unexpected(DecodeFailure(
                "unable to load " + cell_context, cell_size.error()));
        auto cell_items = PreflightArchiveDirectory(
            cell_bytes, budget, "cell archive directory");
        if (!cell_items)
            return std::unexpected(DecodeFailure(
                "unable to index " + cell_context, cell_items.error()));
        auto cell_archive = archive::HogArchive::FromSpan(
            cell_bytes, impl_->config.maximum_nested_hog_bytes);
        if (!cell_archive)
            return std::unexpected(Error(GameDataErrorCode::MalformedArchive,
                "invalid " + cell_context + " archive: " + cell_archive.error()));
        auto cell_directory = BuildArchiveDirectory(*cell_archive, limits);
        if (!cell_directory)
            return std::unexpected(DecodeFailure(
                "unable to index " + cell_context, cell_directory.error()));

        auto col_entry = FindUniqueCol(*cell_directory);
        if (!col_entry)
            return std::unexpected(DecodeFailure(
                "unable to resolve " + cell_context + " spatial member", col_entry.error()));
        auto vum_entry = FindUniqueVum(*cell_directory);
        if (!vum_entry)
            return std::unexpected(DecodeFailure(
                "unable to resolve " + cell_context + " material member", vum_entry.error()));

        const auto col_bytes = cell_archive->payload(**col_entry);
        auto mesh = retail::DecodeColSpatialMesh(
            col_bytes, budget.ChildLimits(archive_depth));
        if (!mesh)
            return std::unexpected(DecodeFailure(
                "unable to decode " + cell_context + " spatial mesh", mesh.error()));
        auto committed_mesh = budget.CommitMesh(*mesh, col_bytes.size());
        if (!committed_mesh)
            return std::unexpected(DecodeFailure(
                "unable to commit " + cell_context + " spatial mesh", committed_mesh.error()));

        const auto vum_bytes = cell_archive->payload(**vum_entry);
        auto catalog = retail::DecodeVumMaterialCatalogMeasured(
            vum_bytes, budget.ChildLimits(archive_depth));
        if (!catalog)
            return std::unexpected(DecodeFailure(
                "unable to decode " + cell_context + " material catalog", catalog.error()));
        auto committed_catalog = budget.CommitCatalog(*catalog, vum_bytes.size());
        if (!committed_catalog)
            return std::unexpected(DecodeFailure(
                "unable to commit " + cell_context + " material catalog",
                committed_catalog.error()));

        result.spatial.terrain_cells.push_back(std::move(*mesh));
        result.material_catalogs.terrain_cells.push_back(std::move(catalog->catalog));
    }
    return result;
}
} // namespace omega::content
