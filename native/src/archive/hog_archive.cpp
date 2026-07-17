#include "omega/archive/hog_archive.h"

#include <algorithm>
#include <fstream>
#include <limits>
#include <sstream>
#include <utility>

namespace omega::archive
{
namespace
{
constexpr std::uint32_t kHeaderSize = 0x14;
constexpr std::uint64_t kMaximumDirectorySize = 64ULL * 1024ULL * 1024ULL;
constexpr std::uint32_t kMaximumEntryCount = 1U << 20U;
constexpr std::size_t kMaximumNameLength = 4096;

enum class TrailingDataPolicy
{
    ExactEnd,
    ZeroPadding,
};

struct ParsedDirectory
{
    HogHeader header;
    std::vector<HogEntry> entries;
    std::uint64_t logical_size = 0;
};

[[nodiscard]] std::uint32_t ReadU32(const std::span<const std::byte> bytes, const std::size_t offset)
{
    return std::to_integer<std::uint32_t>(bytes[offset]) |
           (std::to_integer<std::uint32_t>(bytes[offset + 1]) << 8U) |
           (std::to_integer<std::uint32_t>(bytes[offset + 2]) << 16U) |
           (std::to_integer<std::uint32_t>(bytes[offset + 3]) << 24U);
}

[[nodiscard]] std::string Hex(const std::uint64_t value)
{
    std::ostringstream stream;
    stream << "0x" << std::hex << std::uppercase << value;
    return stream.str();
}

[[nodiscard]] std::expected<ParsedDirectory, std::string> ParseDirectory(
    const std::span<const std::byte> directory, const std::uint64_t archive_size,
    const TrailingDataPolicy trailing_policy)
{
    if (directory.size() < kHeaderSize + sizeof(std::uint32_t))
        return std::unexpected("archive is too small for a HOG header and terminal offset");

    HogHeader header{
        .tag = ReadU32(directory, 0x00),
        .count = ReadU32(directory, 0x04),
        .offsets_offset = ReadU32(directory, 0x08),
        .names_offset = ReadU32(directory, 0x0C),
        .data_offset = ReadU32(directory, 0x10),
    };

    if (header.offsets_offset != kHeaderSize)
        return std::unexpected("unexpected offset-table location: " + Hex(header.offsets_offset));
    if (header.count > kMaximumEntryCount)
        return std::unexpected("HOG entry count exceeds safety limit");
    if (header.data_offset > kMaximumDirectorySize)
        return std::unexpected("directory prefix exceeds safety limit: " + Hex(header.data_offset));

    const std::uint64_t offset_count = static_cast<std::uint64_t>(header.count) + 1U;
    const std::uint64_t expected_names = static_cast<std::uint64_t>(header.offsets_offset) + offset_count * 4U;
    if (expected_names > std::numeric_limits<std::uint32_t>::max() || header.names_offset != expected_names)
        return std::unexpected("name-table location does not follow the offset table");

    if (header.names_offset > header.data_offset || header.data_offset > directory.size() ||
        header.data_offset > archive_size)
        return std::unexpected("invalid name/data table boundaries");

    std::vector<std::uint32_t> offsets;
    offsets.reserve(static_cast<std::size_t>(offset_count));
    for (std::uint64_t index = 0; index < offset_count; ++index)
    {
        const std::uint64_t location = static_cast<std::uint64_t>(header.offsets_offset) + index * 4U;
        if (location + 4U > directory.size())
            return std::unexpected("offset table extends past the directory prefix");
        offsets.push_back(ReadU32(directory, static_cast<std::size_t>(location)));
    }

    if (offsets.front() != 0)
        return std::unexpected("first payload offset is not zero");
    if (!std::is_sorted(offsets.begin(), offsets.end()))
        return std::unexpected("payload offsets are not monotonic");

    const std::uint64_t payload_end = static_cast<std::uint64_t>(header.data_offset) + offsets.back();
    if (payload_end > archive_size)
        return std::unexpected("terminal payload boundary " + Hex(payload_end) +
                               " exceeds archive size " + Hex(archive_size));
    if (trailing_policy == TrailingDataPolicy::ExactEnd && payload_end != archive_size)
        return std::unexpected("terminal payload boundary " + Hex(payload_end) +
                               " does not equal archive size " + Hex(archive_size));
    if (trailing_policy == TrailingDataPolicy::ZeroPadding && payload_end < archive_size)
    {
        if (archive_size > directory.size())
            return std::unexpected("complete archive bytes are required to validate trailing padding");
        const auto padding = directory.subspan(static_cast<std::size_t>(payload_end));
        if (!std::ranges::all_of(padding, [](const std::byte value) { return value == std::byte{0}; }))
            return std::unexpected("archive contains non-zero bytes after its logical payload end");
    }

    std::vector<std::string> names;
    names.reserve(header.count);
    std::size_t cursor = header.names_offset;
    for (std::uint32_t index = 0; index < header.count; ++index)
    {
        const std::size_t start = cursor;
        while (cursor < header.data_offset && directory[cursor] != std::byte{0})
        {
            const auto value = std::to_integer<unsigned char>(directory[cursor]);
            if (value < 0x20U || value > 0x7EU)
                return std::unexpected("filename table contains a non-printable ASCII byte");
            ++cursor;
        }
        if (cursor == header.data_offset)
            return std::unexpected("filename table ended before all names were terminated");
        if (cursor == start)
            return std::unexpected("filename table contains an empty name");
        if (cursor - start > kMaximumNameLength)
            return std::unexpected("filename exceeds safety limit");

        const auto* first = reinterpret_cast<const char*>(directory.data() + start);
        names.emplace_back(first, cursor - start);
        ++cursor;
    }

    const auto name_padding = directory.subspan(cursor, header.data_offset - cursor);
    if (!std::ranges::all_of(name_padding, [](const std::byte value) { return value == std::byte{0}; }))
        return std::unexpected("name table contains non-zero bytes after the final filename");

    std::vector<HogEntry> entries;
    entries.reserve(header.count);
    for (std::uint32_t index = 0; index < header.count; ++index)
    {
        entries.push_back(HogEntry{
            .name = std::move(names[index]),
            .offset = static_cast<std::uint64_t>(header.data_offset) + offsets[index],
            .size = static_cast<std::uint64_t>(offsets[index + 1]) - offsets[index],
        });
    }

    return ParsedDirectory{
        .header = header,
        .entries = std::move(entries),
        .logical_size = payload_end,
    };
}
} // namespace

std::expected<HogIndex, std::string> HogIndex::Open(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream)
        return std::unexpected("unable to open archive: " + path.string());

    const std::streampos end = stream.tellg();
    if (end < 0)
        return std::unexpected("unable to determine archive size: " + path.string());
    const auto archive_size = static_cast<std::uint64_t>(end);
    if (archive_size < kHeaderSize + sizeof(std::uint32_t))
        return std::unexpected("archive is too small for a HOG header and terminal offset");

    std::vector<std::byte> header_bytes(kHeaderSize);
    stream.seekg(0, std::ios::beg);
    if (!stream.read(reinterpret_cast<char*>(header_bytes.data()), static_cast<std::streamsize>(header_bytes.size())))
        return std::unexpected("unable to read HOG header: " + path.string());

    const std::uint32_t data_offset = ReadU32(header_bytes, 0x10);
    if (data_offset < kHeaderSize + sizeof(std::uint32_t) || data_offset > archive_size)
        return std::unexpected("invalid directory prefix size: " + Hex(data_offset));
    if (data_offset > kMaximumDirectorySize)
        return std::unexpected("directory prefix exceeds safety limit: " + Hex(data_offset));

    std::vector<std::byte> directory(data_offset);
    stream.seekg(0, std::ios::beg);
    if (!stream.read(reinterpret_cast<char*>(directory.data()), static_cast<std::streamsize>(directory.size())))
        return std::unexpected("unable to read complete HOG directory: " + path.string());

    auto parsed = ParseDirectory(directory, archive_size, TrailingDataPolicy::ExactEnd);
    if (!parsed)
        return std::unexpected(parsed.error());

    HogIndex result;
    result.header_ = parsed->header;
    result.entries_ = std::move(parsed->entries);
    result.archive_size_ = archive_size;
    return result;
}

const HogEntry* HogIndex::find(const std::string_view name) const noexcept
{
    const auto iterator = std::find_if(entries_.begin(), entries_.end(),
        [name](const HogEntry& entry) { return entry.name == name; });
    return iterator == entries_.end() ? nullptr : &*iterator;
}

std::expected<HogArchive, std::string> HogArchive::Open(
    const std::filesystem::path& path, const std::uint64_t maximum_bytes)
{
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream)
        return std::unexpected("unable to open archive: " + path.string());

    const std::streampos end = stream.tellg();
    if (end < 0)
        return std::unexpected("unable to determine archive size: " + path.string());

    const auto file_size = static_cast<std::uint64_t>(end);
    if (file_size > maximum_bytes)
        return std::unexpected("archive exceeds caller's load limit: " + path.string());
    if (file_size > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()))
        return std::unexpected("archive is too large for this process: " + path.string());
    if (file_size > static_cast<std::uint64_t>(std::numeric_limits<std::streamsize>::max()))
        return std::unexpected("archive is too large for a single stream read: " + path.string());

    std::vector<std::byte> bytes(static_cast<std::size_t>(file_size));
    stream.seekg(0, std::ios::beg);
    if (!bytes.empty() && !stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
        return std::unexpected("unable to read complete archive: " + path.string());

    return FromBytes(std::move(bytes));
}

std::expected<HogArchive, std::string> HogArchive::FromBytes(std::vector<std::byte> bytes)
{
    const std::span<const std::byte> view(bytes);
    auto parsed = ParseDirectory(view, view.size(), TrailingDataPolicy::ZeroPadding);
    if (!parsed)
        return std::unexpected(parsed.error());

    HogArchive result;
    result.header_ = parsed->header;
    result.storage_ = std::move(bytes);
    result.entries_ = std::move(parsed->entries);
    result.logical_size_ = parsed->logical_size;

    return result;
}

std::span<const std::byte> HogArchive::payload(const HogEntry& entry) const noexcept
{
    if (entry.offset > logical_size_ || entry.size > logical_size_ - entry.offset)
        return {};
    return std::span<const std::byte>(storage_).subspan(
        static_cast<std::size_t>(entry.offset), static_cast<std::size_t>(entry.size));
}

const HogEntry* HogArchive::find(const std::string_view name) const noexcept
{
    const auto iterator = std::find_if(entries_.begin(), entries_.end(),
        [name](const HogEntry& entry) { return entry.name == name; });
    return iterator == entries_.end() ? nullptr : &*iterator;
}
} // namespace omega::archive
