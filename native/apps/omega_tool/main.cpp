#include "omega/archive/hog_archive.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <format>
#include <iostream>
#include <string>
#include <string_view>

namespace
{
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
              << "  omega_tool hog-verify-tree <root>\n";
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
        const bool is_regular_file = iterator->is_regular_file(error);
        if (error)
            break;
        if (is_regular_file && IsHog(iterator->path()))
        {
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
                entry_count += archive->entries().size();
                total_bytes += archive->archive_size();
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
    return error_count == 0 ? 0 : 2;
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

    PrintUsage();
    return 64;
}
