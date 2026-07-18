#include "pop_commands.h"

#include "omega/archive/hog_archive.h"
#include "omega/asset/pop_terrain_index.h"
#include "omega/content/game_data_service.h"
#include "omega/retail/pop_level_manifest_decoder.h"

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
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace omega::tool
{
namespace
{
constexpr std::uint64_t kMaximumPopBytes = 64ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kMaximumPopFiles = 1ULL << 20U;
constexpr std::uint64_t kMaximumSiblingEntriesPerDirectory = 1ULL << 16U;
constexpr std::uint64_t kMaximumSiblingEntriesTotal = 1ULL << 20U;
constexpr std::uint64_t kMaximumCorpusEntries = 1ULL << 20U;
constexpr std::uint64_t kMaximumCachedHogEntries = 1ULL << 20U;
constexpr std::uint64_t kMaximumCachedHogNameBytes = 64ULL * 1024ULL * 1024ULL;
constexpr std::size_t kMaximumCachedLevelDirectories = 1U << 12U;
constexpr int kMaximumCorpusDepth = 32;

[[nodiscard]] bool IsPop(const std::filesystem::path& path)
{
    std::string extension = path.extension().string();
    std::ranges::transform(extension, extension.begin(),
        [](const unsigned char value) { return static_cast<char>(std::tolower(value)); });
    return extension == ".pop";
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

[[nodiscard]] std::expected<std::filesystem::path, std::string> FindDataHog(
    const std::filesystem::path& directory, std::uint64_t& remaining_sibling_entries)
{
    std::optional<std::filesystem::path> match;
    std::uint64_t visited = 0;
    std::error_code error;
    std::filesystem::directory_iterator iterator(directory, error), end;
    while (iterator != end && !error)
    {
        if (visited >= kMaximumSiblingEntriesPerDirectory)
            return std::unexpected("level directory entry count exceeds safety limit");
        if (remaining_sibling_entries == 0)
            return std::unexpected("total sibling scan exceeds safety limit");
        ++visited;
        --remaining_sibling_entries;

        if (EqualsAsciiCaseInsensitive(iterator->path().filename().string(), "DATA.HOG"))
        {
            const auto status = iterator->symlink_status(error);
            if (error)
                break;
            if (std::filesystem::is_symlink(status))
                return std::unexpected("symbolic-link DATA.HOG inputs are not allowed");
            if (!std::filesystem::is_regular_file(status))
                return std::unexpected("DATA.HOG sibling is not a regular file");
            if (match)
                return std::unexpected("level directory contains duplicate DATA.HOG names");
            match = iterator->path();
        }
        iterator.increment(error);
    }
    if (error)
        return std::unexpected("unable to enumerate level directory: " + error.message());
    if (!match)
        return std::unexpected("level POP has no sibling DATA.HOG");
    return *match;
}

[[nodiscard]] std::expected<std::vector<std::byte>, std::string> ReadPop(
    const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream)
        return std::unexpected("unable to open POP file");
    const std::streampos end = stream.tellg();
    if (end < 0)
        return std::unexpected("unable to determine POP file size");
    const auto size = static_cast<std::uint64_t>(end);
    if (size > kMaximumPopBytes ||
        size > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()) ||
        size > static_cast<std::uint64_t>(std::numeric_limits<std::streamsize>::max()))
        return std::unexpected("POP file exceeds the tool's read limit");

    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    stream.seekg(0, std::ios::beg);
    if (!bytes.empty() &&
        !stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
        return std::unexpected("unable to read complete POP file");
    return bytes;
}

[[nodiscard]] bool Add(std::uint64_t& target, const std::uint64_t amount)
{
    if (amount > std::numeric_limits<std::uint64_t>::max() - target)
        return false;
    target += amount;
    return true;
}

struct CachedLevelData
{
    archive::HogIndex data_hog;
    asset::SourceLocator source;
};

[[nodiscard]] std::expected<CachedLevelData, std::string> LoadLevelData(
    const std::filesystem::path& directory, const std::filesystem::path& root,
    std::uint64_t& remaining_sibling_entries)
{
    auto data_hog_path = FindDataHog(directory, remaining_sibling_entries);
    if (!data_hog_path)
        return std::unexpected(data_hog_path.error());
    auto data_hog = archive::HogIndex::Open(*data_hog_path);
    if (!data_hog)
        return std::unexpected(data_hog.error());
    const auto relative_data_hog = data_hog_path->lexically_relative(root);
    if (relative_data_hog.empty())
        return std::unexpected("unable to form a relative DATA.HOG game path");
    return CachedLevelData{
        .data_hog = std::move(*data_hog),
        .source = asset::SourceLocator{
            .game_path = relative_data_hog.generic_string(),
            .hog_entries = {},
        },
    };
}
} // namespace

int PopVerifyTree(const std::filesystem::path& root)
{
    std::uint64_t file_count = 0;
    std::uint64_t valid_count = 0;
    std::uint64_t error_count = 0;
    std::uint64_t terrain_record_count = 0;
    std::uint64_t nonzero_alignment_record_count = 0;
    std::error_code error;

    std::filesystem::recursive_directory_iterator iterator(root, error), end;
    while (iterator != end && !error)
    {
        const auto status = iterator->symlink_status(error);
        if (error)
            break;
        if (std::filesystem::is_symlink(status))
        {
            if (IsPop(iterator->path()))
            {
                if (file_count >= kMaximumPopFiles)
                {
                    ++error_count;
                    std::cerr << "POP file count exceeds safety limit\n";
                    break;
                }
                ++file_count;
                ++error_count;
                std::cerr << iterator->path().generic_string()
                          << ": symbolic-link corpus inputs are not allowed\n";
            }
            iterator.increment(error);
            continue;
        }
        const bool is_regular_file = std::filesystem::is_regular_file(status);
        if (is_regular_file && IsPop(iterator->path()))
        {
            if (file_count >= kMaximumPopFiles)
            {
                ++error_count;
                std::cerr << "POP file count exceeds safety limit\n";
                break;
            }
            ++file_count;
            auto bytes = ReadPop(iterator->path());
            if (!bytes)
            {
                ++error_count;
                std::cerr << iterator->path().generic_string() << ": " << bytes.error() << '\n';
            }
            else
            {
                auto index = asset::PopTerrainIndex::Parse(*bytes);
                if (!index)
                {
                    ++error_count;
                    std::cerr << iterator->path().generic_string() << ": "
                              << index.error().message << '\n';
                }
                else if (!Add(terrain_record_count, index->records().size()) ||
                         !Add(nonzero_alignment_record_count,
                             index->nonzero_alignment_record_count()))
                {
                    ++error_count;
                    std::cerr << iterator->path().generic_string()
                              << ": POP corpus counter overflow\n";
                    break;
                }
                else
                {
                    ++valid_count;
                }
            }
        }
        iterator.increment(error);
    }
    if (error)
    {
        std::cerr << "unable to enumerate tree: " << error.message() << '\n';
        return 1;
    }

    std::cout << std::format(
        "{{\"files\":{},\"valid\":{},\"errors\":{},\"terrain_records\":{},"
        "\"nonzero_alignment_records\":{}}}\n",
        file_count, valid_count, error_count, terrain_record_count,
        nonzero_alignment_record_count);
    if (file_count == 0)
        std::cerr << "no POP files were found\n";
    return error_count == 0 && file_count != 0 ? 0 : 2;
}

int LevelManifestVerifyTree(const std::filesystem::path& root)
{
    std::uint64_t file_count = 0;
    std::uint64_t valid_count = 0;
    std::uint64_t error_count = 0;
    std::uint64_t terrain_cell_count = 0;
    std::uint64_t visited_entry_count = 0;
    std::uint64_t cached_hog_entry_count = 0;
    std::uint64_t cached_hog_name_bytes = 0;
    std::uint64_t remaining_sibling_entries = kMaximumSiblingEntriesTotal;
    std::error_code error;
    std::unordered_map<std::string, CachedLevelData> level_cache;
    std::unordered_set<std::string> level_failure_cache;

    std::filesystem::recursive_directory_iterator iterator(root, error), end;
    while (iterator != end && !error)
    {
        if (visited_entry_count >= kMaximumCorpusEntries)
        {
            ++error_count;
            std::cerr << "level-manifest corpus entry count exceeds safety limit\n";
            break;
        }
        ++visited_entry_count;
        if (iterator.depth() > kMaximumCorpusDepth)
        {
            ++error_count;
            std::cerr << "level-manifest corpus depth exceeds safety limit\n";
            break;
        }

        const auto status = iterator->symlink_status(error);
        if (error)
            break;
        if (std::filesystem::is_symlink(status))
        {
            if (IsPop(iterator->path()))
            {
                if (file_count >= kMaximumPopFiles)
                {
                    ++error_count;
                    std::cerr << "POP file count exceeds safety limit\n";
                    break;
                }
                ++file_count;
                ++error_count;
                std::cerr << iterator->path().generic_string()
                          << ": symbolic-link corpus inputs are not allowed\n";
            }
            iterator.increment(error);
            continue;
        }
        if (std::filesystem::is_regular_file(status) && IsPop(iterator->path()))
        {
            if (file_count >= kMaximumPopFiles)
            {
                ++error_count;
                std::cerr << "POP file count exceeds safety limit\n";
                break;
            }
            ++file_count;

            auto bytes = ReadPop(iterator->path());
            if (!bytes)
            {
                ++error_count;
                std::cerr << iterator->path().generic_string() << ": " << bytes.error() << '\n';
            }
            else
            {
                const std::string cache_key =
                    iterator->path().parent_path().lexically_normal().generic_string();
                auto cached = level_cache.find(cache_key);
                const bool had_prior_failure = level_failure_cache.contains(cache_key);
                if (cached == level_cache.end() && !had_prior_failure)
                {
                    if (level_cache.size() + level_failure_cache.size() >=
                        kMaximumCachedLevelDirectories)
                    {
                        ++error_count;
                        std::cerr << "cached level directory count exceeds safety limit\n";
                        break;
                    }
                    auto loaded = LoadLevelData(
                        iterator->path().parent_path(), root, remaining_sibling_entries);
                    if (!loaded)
                    {
                        ++error_count;
                        level_failure_cache.emplace(cache_key);
                        std::cerr << iterator->path().generic_string() << ": "
                                  << loaded.error() << '\n';
                    }
                    else if (loaded->data_hog.entries().size() >
                        kMaximumCachedHogEntries - cached_hog_entry_count)
                    {
                        ++error_count;
                        std::cerr << "cached DATA.HOG entries exceed safety limit\n";
                        break;
                    }
                    else
                    {
                        std::uint64_t loaded_name_bytes = 0;
                        bool names_fit = true;
                        for (const auto& entry : loaded->data_hog.entries())
                        {
                            if (!Add(loaded_name_bytes, entry.name.size()))
                            {
                                names_fit = false;
                                break;
                            }
                        }
                        if (!names_fit || loaded_name_bytes >
                                kMaximumCachedHogNameBytes - cached_hog_name_bytes)
                        {
                            ++error_count;
                            std::cerr << "cached DATA.HOG names exceed safety limit\n";
                            break;
                        }
                        cached_hog_entry_count += loaded->data_hog.entries().size();
                        cached_hog_name_bytes += loaded_name_bytes;
                        cached = level_cache.emplace(cache_key, std::move(*loaded)).first;
                    }
                }

                if (had_prior_failure)
                {
                    ++error_count;
                    std::cerr << iterator->path().generic_string() << ": "
                              << "cached level data load failure\n";
                }

                if (cached != level_cache.end())
                {
                    auto manifest = retail::DecodePopLevelManifest(*bytes,
                        cached->second.data_hog.entries(), cached->second.source);
                    if (!manifest)
                    {
                        ++error_count;
                        std::cerr << iterator->path().generic_string() << ": "
                                  << manifest.error().message << '\n';
                    }
                    else if (!Add(terrain_cell_count, manifest->terrain_cells.size()))
                    {
                        ++error_count;
                        std::cerr << iterator->path().generic_string()
                                  << ": level-manifest corpus counter overflow\n";
                        break;
                    }
                    else
                    {
                        ++valid_count;
                    }
                }
            }
        }
        iterator.increment(error);
    }
    if (error)
    {
        std::cerr << "unable to enumerate tree: " << error.message() << '\n';
        return 1;
    }

    std::cout << std::format(
        "{{\"files\":{},\"valid\":{},\"errors\":{},\"terrain_cells\":{}}}\n",
        file_count, valid_count, error_count, terrain_cell_count);
    if (file_count == 0)
        std::cerr << "no POP files were found\n";
    return error_count == 0 && file_count != 0 ? 0 : 2;
}

namespace
{
struct LevelDiscovery
{
    std::vector<std::string> codes;
    std::vector<std::string> errors;
};

struct LevelSpatialStats
{
    std::uint64_t valid_levels = 0;
    std::uint64_t terrain_cells = 0;
    std::uint64_t spatial_meshes = 0;
    std::uint64_t nodes = 0;
    std::uint64_t leaves = 0;
    std::uint64_t vertices = 0;
    std::uint64_t triangles = 0;
    std::uint64_t triangle_references = 0;
    std::uint64_t empty_meshes = 0;
};

struct LevelMaterialCatalogStats
{
    std::uint64_t valid_levels = 0;
    std::uint64_t terrain_cells = 0;
    std::uint64_t catalogs = 0;
    std::uint64_t names = 0;
    std::uint64_t materials = 0;
    std::uint64_t name_references = 0;
};

[[nodiscard]] std::expected<std::string, std::string> NormalizeDiscoveredLevelCode(
    const std::filesystem::path& directory)
{
    const std::string name = directory.filename().string();
    if (name.empty() || name.size() > 32U)
        return std::unexpected("invalid-level-code");

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
            return std::unexpected("invalid-level-code");
        normalized.push_back(static_cast<char>(is_lower ? value - ('a' - 'A') : value));
    }
    return normalized;
}

[[nodiscard]] std::expected<bool, std::string> HasDataPop(
    const std::filesystem::path& directory, std::uint64_t& remaining_sibling_entries)
{
    bool found = false;
    std::uint64_t visited = 0;
    std::error_code error;
    std::filesystem::directory_iterator iterator(directory, error), end;
    while (iterator != end && !error)
    {
        if (visited >= kMaximumSiblingEntriesPerDirectory)
            return std::unexpected("directory-entry-limit");
        if (remaining_sibling_entries == 0)
            return std::unexpected("total-directory-entry-limit");
        ++visited;
        --remaining_sibling_entries;

        if (EqualsAsciiCaseInsensitive(iterator->path().filename().string(), "DATA.POP"))
        {
            const auto status = iterator->symlink_status(error);
            if (error)
                break;
            if (std::filesystem::is_symlink(status))
                return std::unexpected("symbolic-link-data-pop");
            if (!std::filesystem::is_regular_file(status))
                return std::unexpected("non-file-data-pop");
            if (found)
                return std::unexpected("duplicate-data-pop");
            found = true;
        }
        iterator.increment(error);
    }
    if (error)
        return std::unexpected("directory-enumeration-failed");
    return found;
}

[[nodiscard]] std::expected<LevelDiscovery, std::string> DiscoverLevelCodes(
    const std::filesystem::path& root)
{
    std::error_code error;
    const auto root_status = std::filesystem::symlink_status(root, error);
    if (error || std::filesystem::is_symlink(root_status) ||
        !std::filesystem::is_directory(root_status))
        return std::unexpected("unsafe-or-unreadable-root");

    std::optional<std::filesystem::path> game_data;
    std::uint64_t root_entries = 0;
    std::filesystem::directory_iterator root_iterator(root, error), end;
    while (root_iterator != end && !error)
    {
        if (root_entries >= kMaximumSiblingEntriesPerDirectory)
            return std::unexpected("root-entry-limit");
        ++root_entries;
        if (EqualsAsciiCaseInsensitive(
                root_iterator->path().filename().string(), "GAMEDATA"))
        {
            const auto status = root_iterator->symlink_status(error);
            if (error)
                break;
            if (std::filesystem::is_symlink(status) ||
                !std::filesystem::is_directory(status))
                return std::unexpected("unsafe-game-data-directory");
            if (game_data)
                return std::unexpected("duplicate-game-data-directory");
            game_data = root_iterator->path();
        }
        root_iterator.increment(error);
    }
    if (error)
        return std::unexpected("root-enumeration-failed");
    if (!game_data)
        return std::unexpected("missing-game-data-directory");

    LevelDiscovery discovery;
    std::unordered_set<std::string> seen_codes;
    std::uint64_t game_data_entries = 0;
    std::uint64_t remaining_sibling_entries = kMaximumSiblingEntriesTotal;
    std::filesystem::directory_iterator level_iterator(*game_data, error);
    while (level_iterator != end && !error)
    {
        if (game_data_entries >= kMaximumCachedLevelDirectories)
            return std::unexpected("level-directory-limit");
        ++game_data_entries;

        const auto status = level_iterator->symlink_status(error);
        if (error)
            break;
        if (!std::filesystem::is_symlink(status) &&
            std::filesystem::is_directory(status))
        {
            auto has_pop = HasDataPop(level_iterator->path(), remaining_sibling_entries);
            if (!has_pop)
            {
                auto code = NormalizeDiscoveredLevelCode(level_iterator->path());
                discovery.errors.push_back(code
                        ? "level " + *code + ": discover: " + has_pop.error()
                        : "game-data: discover: " + has_pop.error());
            }
            else if (*has_pop)
            {
                auto code = NormalizeDiscoveredLevelCode(level_iterator->path());
                if (!code)
                {
                    discovery.errors.emplace_back(
                        "game-data: discover: invalid-level-code");
                }
                else if (!seen_codes.emplace(*code).second)
                {
                    discovery.errors.push_back(
                        "level " + *code + ": discover: duplicate-level-code");
                }
                else
                {
                    discovery.codes.push_back(std::move(*code));
                }
            }
        }
        level_iterator.increment(error);
    }
    if (error)
        return std::unexpected("level-directory-enumeration-failed");

    std::ranges::sort(discovery.codes);
    std::ranges::sort(discovery.errors);
    return discovery;
}

[[nodiscard]] std::string_view DecodeErrorCodeName(
    const asset::DecodeErrorCode code) noexcept
{
    switch (code)
    {
    case asset::DecodeErrorCode::Truncated:
        return "truncated";
    case asset::DecodeErrorCode::Malformed:
        return "malformed";
    case asset::DecodeErrorCode::Overflow:
        return "overflow";
    case asset::DecodeErrorCode::LimitExceeded:
        return "limit-exceeded";
    case asset::DecodeErrorCode::UnsupportedVariant:
        return "unsupported-variant";
    case asset::DecodeErrorCode::InvalidReference:
        return "invalid-reference";
    case asset::DecodeErrorCode::DuplicateReference:
        return "duplicate-reference";
    }
    return "unknown";
}

void PrintGameDataError(const std::string_view level, const std::string_view stage,
    const content::GameDataError& error)
{
    if (!level.empty())
        std::cerr << "level " << level << ": ";
    else
        std::cerr << "game-data: ";
    std::cerr << stage << ": " << content::GameDataErrorCodeName(error.code);
    if (error.decode_error)
        std::cerr << ':' << DecodeErrorCodeName(error.decode_error->code);
    std::cerr << '\n';
}

[[nodiscard]] bool RecordLevelSpatialStats(const asset::LevelManifestIR& manifest,
    const asset::LevelSpatialIR& spatial, LevelSpatialStats& aggregate)
{
    LevelSpatialStats next = aggregate;
    if (!Add(next.valid_levels, 1U) ||
        !Add(next.terrain_cells, manifest.terrain_cells.size()) ||
        !Add(next.spatial_meshes, spatial.terrain_cells.size()))
        return false;

    for (const auto& mesh : spatial.terrain_cells)
    {
        if (!Add(next.nodes, mesh.nodes.size()) ||
            !Add(next.leaves, mesh.leaves.size()) ||
            !Add(next.vertices, mesh.vertices.size()) ||
            !Add(next.triangles, mesh.triangles.size()) ||
            !Add(next.triangle_references, mesh.leaf_triangle_references.size()) ||
            (!mesh.root && !Add(next.empty_meshes, 1U)))
            return false;
    }
    aggregate = next;
    return true;
}

[[nodiscard]] bool RecordLevelMaterialCatalogStats(
    const asset::LevelManifestIR& manifest,
    const asset::LevelMaterialCatalogsIR& catalogs,
    LevelMaterialCatalogStats& aggregate)
{
    LevelMaterialCatalogStats next = aggregate;
    if (!Add(next.valid_levels, 1U) ||
        !Add(next.terrain_cells, manifest.terrain_cells.size()) ||
        !Add(next.catalogs, catalogs.terrain_cells.size()))
        return false;

    for (const auto& catalog : catalogs.terrain_cells)
    {
        if (!Add(next.names, catalog.names.size()) ||
            !Add(next.materials, catalog.materials.size()))
            return false;
        for (const auto& material : catalog.materials)
        {
            if (!Add(next.name_references, material.name_count))
                return false;
        }
    }
    aggregate = next;
    return true;
}
} // namespace

int LevelSpatialVerifyTree(const std::filesystem::path& root)
{
    auto discovery = DiscoverLevelCodes(root);
    if (!discovery)
    {
        std::cerr << "game-data: discover: " << discovery.error() << '\n';
        std::cout << "{\"levels\":0,\"valid\":0,\"errors\":1,"
                     "\"terrain_cells\":0,\"spatial_meshes\":0,\"nodes\":0,"
                     "\"leaves\":0,\"vertices\":0,\"triangles\":0,"
                     "\"triangle_references\":0,\"empty_meshes\":0}\n";
        return 1;
    }

    std::uint64_t error_count = discovery->errors.size();
    for (const auto& discovery_error : discovery->errors)
        std::cerr << discovery_error << '\n';

    LevelSpatialStats stats;
    auto service = content::GameDataService::Open(
        content::GameDataServiceConfig{.root = root});
    if (!service)
    {
        ++error_count;
        PrintGameDataError({}, "open", service.error());
    }
    else
    {
        for (const auto& code : discovery->codes)
        {
            auto manifest = service->LoadLevelManifest(code);
            if (!manifest)
            {
                ++error_count;
                PrintGameDataError(code, "manifest", manifest.error());
                continue;
            }
            auto spatial = service->LoadLevelSpatial(*manifest);
            if (!spatial)
            {
                ++error_count;
                PrintGameDataError(code, "spatial", spatial.error());
                continue;
            }
            if (spatial->terrain_cells.size() != manifest->terrain_cells.size())
            {
                ++error_count;
                std::cerr << "level " << code
                          << ": spatial: terrain-cell-count-mismatch\n";
                continue;
            }
            if (!RecordLevelSpatialStats(*manifest, *spatial, stats))
            {
                ++error_count;
                std::cerr << "level " << code << ": aggregate: counter-overflow\n";
                break;
            }
        }
    }

    std::cout << std::format(
        "{{\"levels\":{},\"valid\":{},\"errors\":{},\"terrain_cells\":{},"
        "\"spatial_meshes\":{},\"nodes\":{},\"leaves\":{},\"vertices\":{},"
        "\"triangles\":{},\"triangle_references\":{},\"empty_meshes\":{}}}\n",
        discovery->codes.size(), stats.valid_levels, error_count, stats.terrain_cells,
        stats.spatial_meshes, stats.nodes, stats.leaves, stats.vertices, stats.triangles,
        stats.triangle_references, stats.empty_meshes);
    if (discovery->codes.empty())
        std::cerr << "no level DATA.POP files were found\n";
    return error_count == 0 && !discovery->codes.empty() &&
            stats.valid_levels == discovery->codes.size()
        ? 0
        : 2;
}

int LevelMaterialCatalogsVerifyTree(const std::filesystem::path& root)
{
    auto discovery = DiscoverLevelCodes(root);
    if (!discovery)
    {
        std::cerr << "game-data: discover: " << discovery.error() << '\n';
        std::cout << "{\"levels\":0,\"valid\":0,\"errors\":1,"
                     "\"terrain_cells\":0,\"catalogs\":0,\"names\":0,"
                     "\"materials\":0,\"name_references\":0}\n";
        return 1;
    }

    std::uint64_t error_count = discovery->errors.size();
    // Discovery details may contain an internal normalized level code. This public aggregate pass
    // deliberately publishes only a generic category and the final error count.
    for (std::size_t index = 0; index < discovery->errors.size(); ++index)
        std::cerr << "game-data: discover: level-entry-error\n";

    LevelMaterialCatalogStats stats;
    auto service = content::GameDataService::Open(
        content::GameDataServiceConfig{.root = root});
    if (!service)
    {
        ++error_count;
        PrintGameDataError({}, "open", service.error());
    }
    else
    {
        for (const auto& code : discovery->codes)
        {
            auto manifest = service->LoadLevelManifest(code);
            if (!manifest)
            {
                ++error_count;
                PrintGameDataError({}, "manifest", manifest.error());
                continue;
            }
            auto catalogs = service->LoadLevelMaterialCatalogs(*manifest);
            if (!catalogs)
            {
                ++error_count;
                PrintGameDataError({}, "materials", catalogs.error());
                continue;
            }
            if (catalogs->terrain_cells.size() != manifest->terrain_cells.size())
            {
                ++error_count;
                std::cerr << "game-data: materials: terrain-cell-count-mismatch\n";
                continue;
            }
            if (!RecordLevelMaterialCatalogStats(*manifest, *catalogs, stats))
            {
                ++error_count;
                std::cerr << "game-data: aggregate: counter-overflow\n";
                break;
            }
        }
    }

    std::cout << std::format(
        "{{\"levels\":{},\"valid\":{},\"errors\":{},\"terrain_cells\":{},"
        "\"catalogs\":{},\"names\":{},\"materials\":{},\"name_references\":{}}}\n",
        discovery->codes.size(), stats.valid_levels, error_count, stats.terrain_cells,
        stats.catalogs, stats.names, stats.materials, stats.name_references);
    if (discovery->codes.empty())
        std::cerr << "no levels were found\n";
    return error_count == 0 && !discovery->codes.empty() &&
            stats.valid_levels == discovery->codes.size()
        ? 0
        : 2;
}
} // namespace omega::tool
