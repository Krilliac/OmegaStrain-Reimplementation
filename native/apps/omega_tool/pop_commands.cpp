#include "pop_commands.h"

#include "omega/asset/pop_terrain_index.h"

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
#include <string>
#include <vector>

namespace omega::tool
{
namespace
{
constexpr std::uint64_t kMaximumPopBytes = 64ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kMaximumPopFiles = 1ULL << 20U;

[[nodiscard]] bool IsPop(const std::filesystem::path& path)
{
    std::string extension = path.extension().string();
    std::ranges::transform(extension, extension.begin(),
        [](const unsigned char value) { return static_cast<char>(std::tolower(value)); });
    return extension == ".pop";
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
                    std::cerr << iterator->path().generic_string() << ": " << index.error() << '\n';
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
} // namespace omega::tool
