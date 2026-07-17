#include "pop_commands.h"

#include "omega/archive/hog_archive.h"
#include "omega/asset/pop_terrain_index.h"
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
} // namespace omega::tool
