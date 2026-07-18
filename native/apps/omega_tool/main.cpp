#include "omega/archive/hog_archive.h"

#include "asset_commands.h"
#include "level_texture_commands.h"
#include "pop_commands.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <format>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

namespace
{
constexpr std::uint64_t kMaximumTopLevelArchiveCount = 1U << 12U;
constexpr std::uint64_t kMaximumNestedArchiveCount = 1U << 15U;
constexpr std::uint64_t kMaximumIndexedEntryCount = 1U << 20U;
constexpr std::size_t kMaximumNestedArchiveDepth = 32;

[[nodiscard]] bool IsHog(const std::filesystem::path& path)
{
    std::string extension = path.extension().string();
    std::ranges::transform(extension, extension.begin(),
        [](const unsigned char value) { return static_cast<char>(std::tolower(value)); });
    return extension == ".hog";
}

void PrintUsage()
{
    std::cerr << "usage:\n"
              << "  omega_tool hog-info <archive>\n"
              << "  omega_tool hog-verify-tree <root>\n"
              << "  omega_tool hog-verify-nested-tree <root>\n"
              << "  omega_tool pop-verify-tree <root>\n"
              << "  omega_tool level-manifest-verify-tree <root>\n"
              << "  omega_tool level-spatial-verify-tree <root>\n"
              << "  omega_tool level-material-catalogs-verify-tree <root>\n"
              << "  omega_tool level-texture-store-verify-tree <root>\n"
              << "  omega_tool asset-metadata-verify-tree <root>\n";
}

[[nodiscard]] std::optional<std::uint64_t> CheckedAdd(
    const std::uint64_t left, const std::uint64_t right)
{
    if (right > std::numeric_limits<std::uint64_t>::max() - left)
        return std::nullopt;
    return left + right;
}

int HogInfo(const std::filesystem::path& path)
{
    auto archive = omega::archive::HogIndex::Open(path);
    if (!archive)
    {
        std::cerr << archive.error() << '\n';
        return 1;
    }

    const auto& header = archive->header();
    std::cout << std::format(
        "{{\"path\":\"{}\",\"tag\":\"0x{:08X}\",\"count\":{},\"data_offset\":{},\"bytes\":{}}}\n",
        path.generic_string(), header.tag, header.count, header.data_offset, archive->archive_size());
    for (const auto& entry : archive->entries())
        std::cout << std::format("{}\t{}\t{}\n", entry.offset, entry.size, entry.name);
    return 0;
}

int HogVerifyTree(const std::filesystem::path& root)
{
    std::error_code error;
    std::uint64_t archive_count = 0;
    std::uint64_t valid_count = 0;
    std::uint64_t error_count = 0;
    std::uint64_t entry_count = 0;
    std::uint64_t total_bytes = 0;

    std::filesystem::recursive_directory_iterator iterator(root, error), end;
    while (iterator != end && !error)
    {
        const auto status = iterator->symlink_status(error);
        if (error)
            break;
        if (std::filesystem::is_symlink(status))
        {
            if (IsHog(iterator->path()))
            {
                if (archive_count >= kMaximumTopLevelArchiveCount)
                {
                    ++error_count;
                    std::cerr << "top-level HOG count exceeds safety limit\n";
                    break;
                }
                ++archive_count;
                ++error_count;
                std::cerr << iterator->path().generic_string()
                          << ": symbolic-link corpus inputs are not allowed\n";
            }
            iterator.increment(error);
            continue;
        }
        const bool is_regular_file = std::filesystem::is_regular_file(status);
        if (is_regular_file && IsHog(iterator->path()))
        {
            if (archive_count >= kMaximumTopLevelArchiveCount)
            {
                ++error_count;
                std::cerr << "top-level HOG count exceeds safety limit\n";
                break;
            }
            ++archive_count;
            auto archive = omega::archive::HogIndex::Open(iterator->path());
            if (!archive)
            {
                ++error_count;
                std::cerr << iterator->path().generic_string() << ": " << archive.error() << '\n';
            }
            else
            {
                ++valid_count;
                auto next_entries = CheckedAdd(entry_count, archive->entries().size());
                auto next_bytes = CheckedAdd(total_bytes, archive->archive_size());
                if (!next_entries || *next_entries > kMaximumIndexedEntryCount || !next_bytes)
                {
                    ++error_count;
                    std::cerr << iterator->path().generic_string()
                              << ": top-level corpus counters exceed safety limits\n";
                    break;
                }
                entry_count = *next_entries;
                total_bytes = *next_bytes;
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
        "{{\"archives\":{},\"valid\":{},\"errors\":{},\"entries\":{},\"bytes\":{}}}\n",
        archive_count, valid_count, error_count, entry_count, total_bytes);
    if (archive_count == 0)
        std::cerr << "no top-level HOG files were found\n";
    return error_count == 0 && archive_count != 0 ? 0 : 2;
}

struct NestedVerificationStats
{
    std::uint64_t top_level_archives = 0;
    std::uint64_t top_level_valid = 0;
    std::uint64_t top_level_errors = 0;
    std::uint64_t top_level_entries = 0;
    std::uint64_t nested_candidates = 0;
    std::uint64_t nested_valid = 0;
    std::uint64_t nested_errors = 0;
    std::uint64_t nested_entries = 0;
    std::uint64_t indexed_entries = 0;
    std::uint64_t nested_bytes = 0;
    std::uint64_t exact_spans = 0;
    std::uint64_t zero_padded_spans = 0;
    bool stopped_at_safety_limit = false;
};

[[nodiscard]] bool AddCounter(
    std::uint64_t& target, const std::uint64_t amount, const std::string_view description)
{
    auto result = CheckedAdd(target, amount);
    if (!result)
    {
        std::cerr << description << " counter overflow\n";
        return false;
    }
    target = *result;
    return true;
}

[[nodiscard]] bool AddBudgetedCounter(std::uint64_t& target, const std::uint64_t amount,
    const std::uint64_t maximum, const std::string_view description)
{
    if (!AddCounter(target, amount, description))
        return false;
    if (target > maximum)
    {
        std::cerr << description << " exceeds safety limit\n";
        return false;
    }
    return true;
}

void VerifyNestedEntries(
    const std::filesystem::path& backing_path,
    const omega::archive::HogIndex& parent,
    const std::uint64_t parent_file_offset,
    const std::string_view parent_name,
    const std::size_t depth,
    NestedVerificationStats& stats)
{
    if (stats.stopped_at_safety_limit)
        return;

    for (const auto& entry : parent.entries())
    {
        if (!IsHog(std::filesystem::path(entry.name)))
            continue;
        if (stats.nested_candidates >= kMaximumNestedArchiveCount)
        {
            std::cerr << "nested HOG count exceeds safety limit\n";
            ++stats.nested_errors;
            stats.stopped_at_safety_limit = true;
            return;
        }
        ++stats.nested_candidates;

        const std::string nested_name = std::string(parent_name) + "::" + entry.name;
        if (depth >= kMaximumNestedArchiveDepth)
        {
            ++stats.nested_errors;
            std::cerr << nested_name << ": nested HOG depth exceeds safety limit\n";
            continue;
        }

        auto absolute_offset = CheckedAdd(parent_file_offset, entry.offset);
        if (!absolute_offset)
        {
            ++stats.nested_errors;
            std::cerr << nested_name << ": nested HOG file offset overflows\n";
            continue;
        }

        auto nested = omega::archive::HogIndex::OpenRange(
            backing_path,
            omega::archive::HogFileRange{.offset = *absolute_offset, .size = entry.size},
            omega::archive::kDefaultMaximumArchiveLoadBytes);
        if (!nested)
        {
            ++stats.nested_errors;
            std::cerr << nested_name << ": " << nested.error() << '\n';
            continue;
        }

        ++stats.nested_valid;
        if (nested->padding_size() == 0)
            ++stats.exact_spans;
        else
            ++stats.zero_padded_spans;
        if (!AddCounter(stats.nested_entries, nested->entries().size(), "nested entry") ||
            !AddBudgetedCounter(stats.indexed_entries, nested->entries().size(),
                kMaximumIndexedEntryCount, "total indexed entry count") ||
            !AddCounter(stats.nested_bytes, nested->archive_size(), "nested byte"))
        {
            ++stats.nested_errors;
            stats.stopped_at_safety_limit = true;
            return;
        }

        VerifyNestedEntries(
            backing_path, *nested, *absolute_offset, nested_name, depth + 1U, stats);
        if (stats.stopped_at_safety_limit)
            return;
    }
}

int HogVerifyNestedTree(const std::filesystem::path& root)
{
    NestedVerificationStats stats;
    std::error_code error;
    std::filesystem::recursive_directory_iterator iterator(root, error), end;
    while (iterator != end && !error && !stats.stopped_at_safety_limit)
    {
        const auto status = iterator->symlink_status(error);
        if (error)
            break;
        if (std::filesystem::is_symlink(status))
        {
            if (IsHog(iterator->path()))
            {
                if (stats.top_level_archives >= kMaximumTopLevelArchiveCount)
                {
                    ++stats.top_level_errors;
                    stats.stopped_at_safety_limit = true;
                    std::cerr << "top-level HOG count exceeds safety limit\n";
                    break;
                }
                ++stats.top_level_archives;
                ++stats.top_level_errors;
                std::cerr << iterator->path().generic_string()
                          << ": symbolic-link corpus inputs are not allowed\n";
            }
            iterator.increment(error);
            continue;
        }
        const bool is_regular_file = std::filesystem::is_regular_file(status);
        if (is_regular_file && IsHog(iterator->path()))
        {
            if (stats.top_level_archives >= kMaximumTopLevelArchiveCount)
            {
                ++stats.top_level_errors;
                stats.stopped_at_safety_limit = true;
                std::cerr << "top-level HOG count exceeds safety limit\n";
                break;
            }
            ++stats.top_level_archives;
            auto archive = omega::archive::HogIndex::Open(iterator->path());
            if (!archive)
            {
                ++stats.top_level_errors;
                std::cerr << iterator->path().generic_string() << ": " << archive.error() << '\n';
            }
            else
            {
                ++stats.top_level_valid;
                if (!AddCounter(stats.top_level_entries, archive->entries().size(), "top-level entry") ||
                    !AddBudgetedCounter(stats.indexed_entries, archive->entries().size(),
                        kMaximumIndexedEntryCount, "total indexed entry count"))
                {
                    ++stats.top_level_errors;
                    stats.stopped_at_safety_limit = true;
                    break;
                }
                VerifyNestedEntries(iterator->path(), *archive, 0,
                    iterator->path().generic_string(), 0, stats);
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
        "{{\"top_level_archives\":{},\"top_level_valid\":{},\"top_level_errors\":{},"
        "\"top_level_entries\":{},\"nested_candidates\":{},\"nested_valid\":{},"
        "\"nested_errors\":{},\"nested_entries\":{},\"nested_bytes\":{},"
        "\"exact_spans\":{},\"zero_padded_spans\":{}}}\n",
        stats.top_level_archives, stats.top_level_valid, stats.top_level_errors,
        stats.top_level_entries, stats.nested_candidates, stats.nested_valid,
        stats.nested_errors, stats.nested_entries, stats.nested_bytes,
        stats.exact_spans, stats.zero_padded_spans);
    if (stats.top_level_archives == 0)
        std::cerr << "no top-level HOG files were found\n";
    if (stats.nested_candidates == 0)
        std::cerr << "no nested HOG spans were found\n";
    return stats.top_level_errors == 0 && stats.nested_errors == 0 &&
            !stats.stopped_at_safety_limit && stats.top_level_archives != 0 &&
            stats.nested_candidates != 0
        ? 0
        : 2;
}
} // namespace

int main(const int argc, char** argv)
{
    if (argc != 3)
    {
        PrintUsage();
        return 64;
    }

    const std::string_view command(argv[1]);
    if (command == "hog-info")
        return HogInfo(argv[2]);
    if (command == "hog-verify-tree")
        return HogVerifyTree(argv[2]);
    if (command == "hog-verify-nested-tree")
        return HogVerifyNestedTree(argv[2]);
    if (command == "pop-verify-tree")
        return omega::tool::PopVerifyTree(argv[2]);
    if (command == "level-manifest-verify-tree")
        return omega::tool::LevelManifestVerifyTree(argv[2]);
    if (command == "level-spatial-verify-tree")
        return omega::tool::LevelSpatialVerifyTree(argv[2]);
    if (command == "level-material-catalogs-verify-tree")
        return omega::tool::LevelMaterialCatalogsVerifyTree(argv[2]);
    if (command == "level-texture-store-verify-tree")
        return omega::tool::LevelTextureStoreVerifyTree(argv[2]);
    if (command == "asset-metadata-verify-tree")
        return omega::tool::AssetMetadataVerifyTree(argv[2]);

    PrintUsage();
    return 64;
}
