#include "omega/content/game_data_service.h"

#include "omega/archive/hog_archive.h"
#include "omega/asset/source_locator.h"
#include "omega/retail/col_spatial_mesh_decoder.h"
#include "omega/retail/pop_level_manifest_decoder.h"
#include "omega/retail/vum_material_catalog_decoder.h"
#include "omega/vfs/virtual_file_system.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
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
constexpr std::size_t kMaximumLevelCodeBytes = 32;

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
        const std::uint64_t initial_items = manifest.data_hog_source.hog_entries.size();
        std::uint64_t minimum_items = 0;
        if (!Add(initial_items, manifest.terrain_cells.size(), minimum_items) ||
            minimum_items > limits.maximum_items)
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

    [[nodiscard]] std::uint64_t input_bytes() const noexcept { return input_bytes_; }
    [[nodiscard]] std::uint64_t directory_items() const noexcept { return directory_items_; }

private:
    asset::DecodeLimits limits_;
    std::uint64_t input_bytes_ = 0;
    std::uint64_t directory_items_ = 0;
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
    if (config.root.empty() || config.maximum_system_config_bytes == 0 ||
        config.maximum_pop_bytes == 0 || config.maximum_data_hog_bytes == 0 ||
        config.maximum_nested_hog_bytes == 0)
        return std::unexpected(Error(GameDataErrorCode::InvalidConfiguration,
            "game-data root and byte limits must be non-empty"));

    auto impl = std::make_unique<Impl>();
    impl->config = std::move(config);
    auto mounted = impl->files.MountDirectory(impl->config.root);
    if (!mounted)
        return std::unexpected(Error(GameDataErrorCode::MountFailed,
            "unable to mount game-data root: " + mounted.error()));
    impl->files.Freeze();

    if (!impl->files.Contains("SYSTEM.CNF"))
        return std::unexpected(Error(GameDataErrorCode::MissingRequiredFile,
            "game-data root is missing SYSTEM.CNF"));
    auto system_config = impl->files.Read(
        "SYSTEM.CNF", impl->config.maximum_system_config_bytes);
    if (!system_config)
        return std::unexpected(Error(GameDataErrorCode::ReadFailed,
            "unable to read SYSTEM.CNF: " + system_config.error()));
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

    if (!impl_->files.Contains(*normalized_game_path))
        return std::unexpected(Error(GameDataErrorCode::MissingRequiredFile,
            "source locator game path is unavailable"));
    auto source_bytes = impl_->files.Read(
        *normalized_game_path, impl_->config.maximum_data_hog_bytes);
    if (!source_bytes)
        return std::unexpected(Error(GameDataErrorCode::ReadFailed,
            "unable to read source locator game path"));

    SourceResolveBudget budget(caller_limits);
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
    auto current_directory = BuildArchiveDirectory(*current_archive, caller_limits);
    if (!current_directory)
        return std::unexpected(DecodeFailure(
            "unable to resolve source locator", current_directory.error()));

    for (std::size_t index = 0; index < normalized_components.size(); ++index)
    {
        auto entry = FindArchiveEntry(
            *current_directory, normalized_components[index], caller_limits);
        if (!entry)
            return std::unexpected(DecodeFailure(
                "unable to resolve source locator", entry.error()));
        const auto member_bytes = current_archive->payload(**entry);
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
        auto nested_archive = archive::HogArchive::FromSpan(
            member_bytes, impl_->config.maximum_nested_hog_bytes);
        if (!nested_archive)
            return std::unexpected(Error(GameDataErrorCode::MalformedArchive,
                "source locator ancestor archive is malformed"));
        *current_archive = std::move(*nested_archive);
        current_directory = BuildArchiveDirectory(*current_archive, caller_limits);
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
    if (!impl_->files.Contains(pop_path))
        return std::unexpected(Error(GameDataErrorCode::MissingRequiredFile,
            "level is missing DATA.POP: " + *normalized_level));
    if (!impl_->files.Contains(data_hog_path))
        return std::unexpected(Error(GameDataErrorCode::MissingRequiredFile,
            "level is missing DATA.HOG: " + *normalized_level));

    auto pop_bytes = impl_->files.Read(pop_path, impl_->config.maximum_pop_bytes);
    if (!pop_bytes)
        return std::unexpected(Error(GameDataErrorCode::ReadFailed,
            "unable to read level DATA.POP: " + pop_bytes.error()));
    auto data_hog_bytes = impl_->files.Read(
        data_hog_path, impl_->config.maximum_data_hog_bytes);
    if (!data_hog_bytes)
        return std::unexpected(Error(GameDataErrorCode::ReadFailed,
            "unable to read level DATA.HOG: " + data_hog_bytes.error()));

    auto data_hog = archive::HogArchive::FromBytes(std::move(*data_hog_bytes));
    if (!data_hog)
        return std::unexpected(Error(GameDataErrorCode::MalformedArchive,
            "invalid level DATA.HOG: " + data_hog.error()));

    auto manifest = retail::DecodePopLevelManifest(*pop_bytes, data_hog->entries(),
        asset::SourceLocator{.game_path = data_hog_path, .hog_entries = {}},
        impl_->config.decode_limits);
    if (!manifest)
    {
        return std::unexpected(GameDataError{
            .code = GameDataErrorCode::DecodeFailed,
            .message = "unable to decode level manifest: " + manifest.error().message,
            .decode_error = std::move(manifest.error()),
        });
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
            "unable to read level archive source: " + source_bytes.error()));
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
            "unable to read level archive source: " + source_bytes.error()));
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
            "unable to read level archive source: " + source_bytes.error()));
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
