#include "level_texture_commands.h"

#include "omega/content/game_data_service.h"
#include "omega/content/level_texture_store.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#endif

namespace omega::tool
{
namespace
{
constexpr std::uint64_t kMaximumTreeEntries = 1ULL << 20U;
constexpr std::uint64_t kMaximumRootEntries = 1ULL << 16U;
constexpr std::uint64_t kMaximumGameDataEntries = 1ULL << 16U;
constexpr std::uint64_t kMaximumLevelEntries = 1ULL << 16U;
constexpr std::size_t kMaximumLevelDirectories = 1U << 12U;
constexpr std::uint32_t kMaximumTreeDepth = 32U;
constexpr std::size_t kMaximumLevelCodeBytes = 32U;

enum class ErrorCategory : std::size_t
{
    DiscoveryInvalidRoot,
    DiscoveryUnsafeEntry,
    DiscoveryLimitExceeded,
    DiscoveryIo,
    DiscoveryMissingGameData,
    DiscoveryDuplicateGameData,
    DiscoveryInvalidLevelCode,
    DiscoveryDuplicateLevelCode,
    DiscoveryDuplicateLevelMarker,
    NoLevels,
    ServiceOpen,
    ManifestLoad,
    StoreOpen,
    EmptyTextureStore,
    HandleLookup,
    TextureLoad,
    AggregateOverflow,
    Count,
};

constexpr std::array<std::string_view, static_cast<std::size_t>(ErrorCategory::Count)>
    kErrorCategoryNames{
        "discovery_invalid_root",
        "discovery_unsafe_entry",
        "discovery_limit_exceeded",
        "discovery_io",
        "discovery_missing_game_data",
        "discovery_duplicate_game_data",
        "discovery_invalid_level_code",
        "discovery_duplicate_level_code",
        "discovery_duplicate_level_marker",
        "no_levels",
        "service_open",
        "manifest_load",
        "store_open",
        "empty_texture_store",
        "handle_lookup",
        "texture_load",
        "aggregate_overflow",
    };

using ErrorCounts =
    std::array<std::uint64_t, static_cast<std::size_t>(ErrorCategory::Count)>;

struct Discovery
{
    std::vector<std::string> level_codes;
    ErrorCounts errors{};
};

struct UsageMaxima
{
    std::uint64_t input_bytes = 0;
    std::uint64_t items = 0;
    std::uint64_t logical_output_bytes = 0;
    std::uint32_t archive_depth = 0;
    std::uint64_t peak_scratch_bytes = 0;

    void Observe(const content::LevelTextureOperationUsage& usage) noexcept
    {
        input_bytes = std::max(input_bytes, usage.input_bytes);
        items = std::max(items, usage.items);
        logical_output_bytes = std::max(logical_output_bytes, usage.logical_output_bytes);
        archive_depth = std::max(archive_depth, usage.archive_depth);
        peak_scratch_bytes = std::max(peak_scratch_bytes, usage.peak_scratch_bytes);
    }
};

struct Totals
{
    std::uint64_t levels_discovered = 0;
    std::uint64_t levels_verified = 0;
    std::uint64_t texture_sources = 0;
    std::uint64_t textures = 0;
    std::uint64_t storage_blocks = 0;
    std::uint64_t storage_planes = 0;
    std::uint64_t storage_palette_entries = 0;
    std::uint64_t storage_plane_bytes = 0;
    std::uint64_t storage_palette_bytes = 0;
    std::uint64_t storage_owned_bytes = 0;
};

struct Aggregate
{
    Totals totals;
    UsageMaxima open_maxima;
    UsageMaxima load_maxima;
    ErrorCounts errors{};
};

[[nodiscard]] bool Add(
    std::uint64_t& target, const std::uint64_t value) noexcept
{
    if (value > std::numeric_limits<std::uint64_t>::max() - target)
        return false;
    target += value;
    return true;
}

[[nodiscard]] bool Multiply(const std::uint64_t left, const std::uint64_t right,
    std::uint64_t& result) noexcept
{
    if (left != 0 && right > std::numeric_limits<std::uint64_t>::max() / left)
        return false;
    result = left * right;
    return true;
}

void RecordError(ErrorCounts& errors, const ErrorCategory category) noexcept
{
    auto& count = errors[static_cast<std::size_t>(category)];
    if (count != std::numeric_limits<std::uint64_t>::max())
        ++count;
}

[[nodiscard]] std::uint64_t ErrorTotal(const ErrorCounts& errors) noexcept
{
    std::uint64_t total = 0;
    for (const std::uint64_t count : errors)
    {
        if (!Add(total, count))
            return std::numeric_limits<std::uint64_t>::max();
    }
    return total;
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

[[nodiscard]] std::expected<bool, ErrorCategory> IsReparsePoint(
    const std::filesystem::path& path) noexcept
{
#ifdef _WIN32
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES)
        return std::unexpected(ErrorCategory::DiscoveryIo);
    return (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
    static_cast<void>(path);
    return false;
#endif
}

[[nodiscard]] std::expected<std::filesystem::file_status, ErrorCategory> SafeStatus(
    const std::filesystem::path& path)
{
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (error)
        return std::unexpected(ErrorCategory::DiscoveryIo);
    auto reparse = IsReparsePoint(path);
    if (!reparse)
        return std::unexpected(reparse.error());
    if (std::filesystem::is_symlink(status) || *reparse)
        return std::unexpected(ErrorCategory::DiscoveryUnsafeEntry);
    return status;
}

[[nodiscard]] std::expected<void, ErrorCategory> PreflightTreeSafety(
    const std::filesystem::path& root)
{
    auto root_status = SafeStatus(root);
    if (!root_status)
        return std::unexpected(root_status.error());
    if (!std::filesystem::is_directory(*root_status))
        return std::unexpected(ErrorCategory::DiscoveryInvalidRoot);

    struct PendingDirectory
    {
        std::filesystem::path path;
        std::uint32_t depth = 0;
    };
    std::vector<PendingDirectory> pending;
    pending.push_back(PendingDirectory{.path = root, .depth = 0});
    std::uint64_t visited_entries = 0;

    while (!pending.empty())
    {
        PendingDirectory directory = std::move(pending.back());
        pending.pop_back();
        std::error_code error;
        std::filesystem::directory_iterator iterator(directory.path, error), end;
        if (error)
            return std::unexpected(ErrorCategory::DiscoveryIo);
        while (iterator != end && !error)
        {
            if (visited_entries >= kMaximumTreeEntries)
                return std::unexpected(ErrorCategory::DiscoveryLimitExceeded);
            ++visited_entries;

            auto status = SafeStatus(iterator->path());
            if (!status)
                return std::unexpected(status.error());
            if (std::filesystem::is_directory(*status))
            {
                if (directory.depth >= kMaximumTreeDepth)
                    return std::unexpected(ErrorCategory::DiscoveryLimitExceeded);
                pending.push_back(PendingDirectory{
                    .path = iterator->path(),
                    .depth = directory.depth + 1U,
                });
            }
            else if (!std::filesystem::is_regular_file(*status))
            {
                return std::unexpected(ErrorCategory::DiscoveryUnsafeEntry);
            }
            iterator.increment(error);
        }
        if (error)
            return std::unexpected(ErrorCategory::DiscoveryIo);
    }
    return {};
}

[[nodiscard]] std::expected<std::string, ErrorCategory> NormalizeLevelCode(
    const std::filesystem::path& directory)
{
    const std::string name = directory.filename().string();
    if (name.empty() || name.size() > kMaximumLevelCodeBytes)
        return std::unexpected(ErrorCategory::DiscoveryInvalidLevelCode);

    std::string normalized;
    normalized.reserve(name.size());
    for (const unsigned char value : name)
    {
        const bool is_upper = value >= static_cast<unsigned char>('A') &&
                              value <= static_cast<unsigned char>('Z');
        const bool is_lower = value >= static_cast<unsigned char>('a') &&
                              value <= static_cast<unsigned char>('z');
        const bool is_digit = value >= static_cast<unsigned char>('0') &&
                              value <= static_cast<unsigned char>('9');
        if (!is_upper && !is_lower && !is_digit)
            return std::unexpected(ErrorCategory::DiscoveryInvalidLevelCode);
        normalized.push_back(static_cast<char>(is_lower ? value - ('a' - 'A') : value));
    }
    return normalized;
}

[[nodiscard]] std::expected<std::uint32_t, ErrorCategory> CountLevelMarkers(
    const std::filesystem::path& directory)
{
    std::uint32_t markers = 0;
    std::uint64_t visited = 0;
    std::error_code error;
    std::filesystem::directory_iterator iterator(directory, error), end;
    if (error)
        return std::unexpected(ErrorCategory::DiscoveryIo);
    while (iterator != end && !error)
    {
        if (visited >= kMaximumLevelEntries)
            return std::unexpected(ErrorCategory::DiscoveryLimitExceeded);
        ++visited;
        if (EqualsAsciiCaseInsensitive(iterator->path().filename().string(), "DATA.POP"))
        {
            auto status = SafeStatus(iterator->path());
            if (!status)
                return std::unexpected(status.error());
            if (!std::filesystem::is_regular_file(*status))
                return std::unexpected(ErrorCategory::DiscoveryUnsafeEntry);
            ++markers;
        }
        iterator.increment(error);
    }
    if (error)
        return std::unexpected(ErrorCategory::DiscoveryIo);
    return markers;
}

[[nodiscard]] std::expected<Discovery, ErrorCategory> DiscoverLevelCodes(
    const std::filesystem::path& root)
{
    auto safe = PreflightTreeSafety(root);
    if (!safe)
        return std::unexpected(safe.error());

    std::optional<std::filesystem::path> game_data;
    std::uint64_t root_entries = 0;
    std::error_code error;
    std::filesystem::directory_iterator root_iterator(root, error), end;
    if (error)
        return std::unexpected(ErrorCategory::DiscoveryIo);
    while (root_iterator != end && !error)
    {
        if (root_entries >= kMaximumRootEntries)
            return std::unexpected(ErrorCategory::DiscoveryLimitExceeded);
        ++root_entries;
        if (EqualsAsciiCaseInsensitive(
                root_iterator->path().filename().string(), "GAMEDATA"))
        {
            auto status = SafeStatus(root_iterator->path());
            if (!status)
                return std::unexpected(status.error());
            if (!std::filesystem::is_directory(*status))
                return std::unexpected(ErrorCategory::DiscoveryUnsafeEntry);
            if (game_data)
                return std::unexpected(ErrorCategory::DiscoveryDuplicateGameData);
            game_data = root_iterator->path();
        }
        root_iterator.increment(error);
    }
    if (error)
        return std::unexpected(ErrorCategory::DiscoveryIo);
    if (!game_data)
        return std::unexpected(ErrorCategory::DiscoveryMissingGameData);

    Discovery discovery;
    std::uint64_t game_data_entries = 0;
    std::size_t level_directories = 0;
    std::filesystem::directory_iterator level_iterator(*game_data, error);
    if (error)
        return std::unexpected(ErrorCategory::DiscoveryIo);
    while (level_iterator != end && !error)
    {
        if (game_data_entries >= kMaximumGameDataEntries)
            return std::unexpected(ErrorCategory::DiscoveryLimitExceeded);
        ++game_data_entries;

        auto status = SafeStatus(level_iterator->path());
        if (!status)
            return std::unexpected(status.error());
        if (std::filesystem::is_directory(*status))
        {
            if (level_directories >= kMaximumLevelDirectories)
                return std::unexpected(ErrorCategory::DiscoveryLimitExceeded);
            ++level_directories;
            auto markers = CountLevelMarkers(level_iterator->path());
            if (!markers)
                return std::unexpected(markers.error());
            if (*markers > 1U)
            {
                RecordError(discovery.errors, ErrorCategory::DiscoveryDuplicateLevelMarker);
            }
            else if (*markers == 1U)
            {
                auto code = NormalizeLevelCode(level_iterator->path());
                if (!code)
                    RecordError(discovery.errors, code.error());
                else
                    discovery.level_codes.push_back(std::move(*code));
            }
        }
        level_iterator.increment(error);
    }
    if (error)
        return std::unexpected(ErrorCategory::DiscoveryIo);

    std::ranges::sort(discovery.level_codes);
    const auto duplicate = std::ranges::adjacent_find(discovery.level_codes);
    if (duplicate != discovery.level_codes.end())
    {
        std::vector<std::string> unique_codes;
        unique_codes.reserve(discovery.level_codes.size());
        for (auto& code : discovery.level_codes)
        {
            if (!unique_codes.empty() && unique_codes.back() == code)
            {
                RecordError(discovery.errors, ErrorCategory::DiscoveryDuplicateLevelCode);
                continue;
            }
            unique_codes.push_back(std::move(code));
        }
        discovery.level_codes = std::move(unique_codes);
    }
    return discovery;
}

[[nodiscard]] bool RecordStorage(
    const asset::TextureStorageIR& storage, Totals& totals) noexcept
{
    if (!Add(totals.storage_blocks, storage.blocks.size()))
        return false;
    for (const auto& block : storage.blocks)
    {
        if (!Add(totals.storage_planes, block.planes.size()))
            return false;
        for (const auto& plane : block.planes)
        {
            if (!Add(totals.storage_plane_bytes, plane.bytes.size()) ||
                !Add(totals.storage_owned_bytes, plane.bytes.size()))
                return false;
        }
        if (block.palette)
        {
            std::uint64_t palette_bytes = 0;
            if (!Multiply(block.palette->entries.size(),
                    sizeof(block.palette->entries.front()), palette_bytes) ||
                !Add(totals.storage_palette_entries, block.palette->entries.size()) ||
                !Add(totals.storage_palette_bytes, palette_bytes) ||
                !Add(totals.storage_owned_bytes, palette_bytes))
                return false;
        }
    }
    return true;
}

[[nodiscard]] bool MergeLevel(Aggregate& aggregate, const Aggregate& level) noexcept
{
    Aggregate next = aggregate;
    if (!Add(next.totals.levels_verified, 1U) ||
        !Add(next.totals.texture_sources, level.totals.texture_sources) ||
        !Add(next.totals.textures, level.totals.textures) ||
        !Add(next.totals.storage_blocks, level.totals.storage_blocks) ||
        !Add(next.totals.storage_planes, level.totals.storage_planes) ||
        !Add(next.totals.storage_palette_entries,
            level.totals.storage_palette_entries) ||
        !Add(next.totals.storage_plane_bytes, level.totals.storage_plane_bytes) ||
        !Add(next.totals.storage_palette_bytes,
            level.totals.storage_palette_bytes) ||
        !Add(next.totals.storage_owned_bytes, level.totals.storage_owned_bytes))
        return false;
    next.open_maxima.input_bytes =
        std::max(next.open_maxima.input_bytes, level.open_maxima.input_bytes);
    next.open_maxima.items = std::max(next.open_maxima.items, level.open_maxima.items);
    next.open_maxima.logical_output_bytes = std::max(
        next.open_maxima.logical_output_bytes, level.open_maxima.logical_output_bytes);
    next.open_maxima.archive_depth =
        std::max(next.open_maxima.archive_depth, level.open_maxima.archive_depth);
    next.open_maxima.peak_scratch_bytes = std::max(
        next.open_maxima.peak_scratch_bytes, level.open_maxima.peak_scratch_bytes);
    next.load_maxima.input_bytes =
        std::max(next.load_maxima.input_bytes, level.load_maxima.input_bytes);
    next.load_maxima.items = std::max(next.load_maxima.items, level.load_maxima.items);
    next.load_maxima.logical_output_bytes = std::max(
        next.load_maxima.logical_output_bytes, level.load_maxima.logical_output_bytes);
    next.load_maxima.archive_depth =
        std::max(next.load_maxima.archive_depth, level.load_maxima.archive_depth);
    next.load_maxima.peak_scratch_bytes = std::max(
        next.load_maxima.peak_scratch_bytes, level.load_maxima.peak_scratch_bytes);
    aggregate = std::move(next);
    return true;
}

void PrintReport(const Aggregate& aggregate)
{
    const std::uint64_t errors = ErrorTotal(aggregate.errors);
    const auto& totals = aggregate.totals;
    const auto& open = aggregate.open_maxima;
    const auto& load = aggregate.load_maxima;
    std::cout
        << "{\"schema_version\":1,"
           "\"scope\":\"native aggregate level texture store verification; independent "
           "field maxima only; no paths, names, hashes, offsets, payloads, per-level rows, "
           "identities, or bindings\","
           "\"totals\":{\"levels_discovered\":"
        << totals.levels_discovered << ",\"levels_verified\":" << totals.levels_verified
        << ",\"texture_sources\":" << totals.texture_sources << ",\"textures\":"
        << totals.textures << ",\"storage_blocks\":" << totals.storage_blocks
        << ",\"storage_planes\":" << totals.storage_planes
        << ",\"storage_palette_entries\":" << totals.storage_palette_entries
        << ",\"storage_plane_bytes\":" << totals.storage_plane_bytes
        << ",\"storage_palette_bytes\":" << totals.storage_palette_bytes
        << ",\"storage_owned_bytes\":" << totals.storage_owned_bytes
        << ",\"errors\":" << errors << "},\"maxima\":{\"open\":{\"input_bytes\":"
        << open.input_bytes << ",\"items\":" << open.items
        << ",\"logical_output_bytes\":" << open.logical_output_bytes
        << ",\"archive_depth\":" << open.archive_depth
        << ",\"peak_scratch_bytes\":" << open.peak_scratch_bytes
        << "},\"load\":{\"input_bytes\":" << load.input_bytes << ",\"items\":"
        << load.items << ",\"logical_output_bytes\":" << load.logical_output_bytes
        << ",\"archive_depth\":" << load.archive_depth
        << ",\"peak_scratch_bytes\":" << load.peak_scratch_bytes
        << "}},\"error_categories\":{";
    for (std::size_t index = 0; index < kErrorCategoryNames.size(); ++index)
    {
        if (index != 0)
            std::cout << ',';
        std::cout << '\"' << kErrorCategoryNames[index] << "\":"
                  << aggregate.errors[index];
    }
    std::cout << "}}\n";
}

void PrintErrors(const ErrorCounts& errors)
{
    for (std::size_t index = 0; index < errors.size(); ++index)
    {
        if (errors[index] != 0)
            std::cerr << "level-texture-store: " << kErrorCategoryNames[index] << '\n';
    }
}

[[nodiscard]] int VerifyTree(const std::filesystem::path& root)
{
    Aggregate aggregate;
    auto discovery = DiscoverLevelCodes(root);
    if (!discovery)
    {
        RecordError(aggregate.errors, discovery.error());
        PrintReport(aggregate);
        PrintErrors(aggregate.errors);
        return 1;
    }
    aggregate.errors = discovery->errors;
    aggregate.totals.levels_discovered = discovery->level_codes.size();
    if (discovery->level_codes.empty())
    {
        RecordError(aggregate.errors, ErrorCategory::NoLevels);
        PrintReport(aggregate);
        PrintErrors(aggregate.errors);
        return 2;
    }

    auto service = content::GameDataService::Open(
        content::GameDataServiceConfig{.root = root});
    if (!service)
    {
        RecordError(aggregate.errors, ErrorCategory::ServiceOpen);
        PrintReport(aggregate);
        PrintErrors(aggregate.errors);
        return 2;
    }

    for (const auto& code : discovery->level_codes)
    {
        Aggregate level;
        bool level_failed = false;
        auto manifest = service->LoadLevelManifest(code);
        if (!manifest)
        {
            RecordError(aggregate.errors, ErrorCategory::ManifestLoad);
            continue;
        }
        if (!Add(level.totals.texture_sources, manifest->texture_sources.size()))
        {
            RecordError(aggregate.errors, ErrorCategory::AggregateOverflow);
            continue;
        }

        auto store = content::LevelTextureStore::Open(*service, *manifest);
        if (!store)
        {
            RecordError(aggregate.errors, ErrorCategory::StoreOpen);
            continue;
        }
        if (store->size() == 0)
        {
            RecordError(aggregate.errors, ErrorCategory::EmptyTextureStore);
            continue;
        }
        if (!Add(level.totals.textures, store->size()))
        {
            RecordError(aggregate.errors, ErrorCategory::AggregateOverflow);
            continue;
        }
        level.open_maxima.Observe(store->open_usage());

        for (std::size_t index = 0; index < store->size(); ++index)
        {
            auto handle = store->HandleAt(index);
            if (!handle)
            {
                RecordError(aggregate.errors, ErrorCategory::HandleLookup);
                level_failed = true;
                continue;
            }
            auto loaded = store->Load(*service, *handle);
            if (!loaded)
            {
                RecordError(aggregate.errors, ErrorCategory::TextureLoad);
                level_failed = true;
                continue;
            }
            level.load_maxima.Observe(loaded->usage);
            if (!RecordStorage(loaded->storage, level.totals))
            {
                RecordError(aggregate.errors, ErrorCategory::AggregateOverflow);
                level_failed = true;
            }
        }

        if (!level_failed && !MergeLevel(aggregate, level))
            RecordError(aggregate.errors, ErrorCategory::AggregateOverflow);
    }

    PrintReport(aggregate);
    PrintErrors(aggregate.errors);
    const bool complete = ErrorTotal(aggregate.errors) == 0 &&
                          aggregate.totals.levels_verified ==
                              aggregate.totals.levels_discovered &&
                          aggregate.totals.textures != 0;
    return complete ? 0 : 2;
}
} // namespace

int LevelTextureStoreVerifyTree(const std::filesystem::path& root)
{
    try
    {
        return VerifyTree(root);
    }
    catch (...)
    {
        Aggregate aggregate;
        RecordError(aggregate.errors, ErrorCategory::DiscoveryIo);
        PrintReport(aggregate);
        PrintErrors(aggregate.errors);
        return 1;
    }
}
} // namespace omega::tool
