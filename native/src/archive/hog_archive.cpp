#include "omega/archive/hog_archive.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <sstream>
#include <string_view>
#include <utility>

namespace omega::archive
{
namespace
{
constexpr std::uint32_t kHeaderSize = 0x14;
constexpr std::uint64_t kMaximumDirectorySize = 64ULL * 1024ULL * 1024ULL;
constexpr std::uint32_t kMaximumEntryCount = 1U << 20U;
constexpr std::size_t kMaximumNameLength = 4096;
constexpr std::size_t kPaddingValidationChunkSize = 64U * 1024U;

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

[[nodiscard]] std::expected<std::uint64_t, std::string> CheckedAdd(
    const std::uint64_t left, const std::uint64_t right, const std::string_view description)
{
    if (right > std::numeric_limits<std::uint64_t>::max() - left)
        return std::unexpected(std::string(description) + " overflows a 64-bit offset");
    return left + right;
}

[[nodiscard]] std::expected<std::uint64_t, std::string> CheckedMultiply(
    const std::uint64_t left, const std::uint64_t right, const std::string_view description)
{
    if (left != 0 && right > std::numeric_limits<std::uint64_t>::max() / left)
        return std::unexpected(std::string(description) + " overflows a 64-bit size");
    return left * right;
}

[[nodiscard]] std::expected<std::uint64_t, std::string> FileSize(
    std::ifstream& stream, const std::filesystem::path& path)
{
    const std::streampos end = stream.tellg();
    if (end < 0)
        return std::unexpected("unable to determine archive size: " + path.string());
    return static_cast<std::uint64_t>(end);
}

[[nodiscard]] std::expected<void, std::string> ValidateFileRange(
    const HogFileRange range, const std::uint64_t file_size, const std::filesystem::path& path,
    const std::uint64_t maximum_bytes)
{
    if (range.size > maximum_bytes)
        return std::unexpected("archive range exceeds caller's load limit: " + path.string());
    if (range.offset > file_size || range.size > file_size - range.offset)
        return std::unexpected("archive range extends past the backing file: " + path.string());

    constexpr auto maximum_stream_offset =
        static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max());
    if (range.offset > maximum_stream_offset || range.size > maximum_stream_offset - range.offset)
        return std::unexpected("archive range is too large for stream offsets: " + path.string());
    return {};
}

[[nodiscard]] std::expected<void, std::string> ReadExact(
    std::ifstream& stream, const std::filesystem::path& path, const std::uint64_t offset,
    const std::span<std::byte> output, const std::string_view description)
{
    if (offset > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max()) ||
        output.size() > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max()))
        return std::unexpected(std::string(description) + " is too large for a stream read: " + path.string());

    stream.clear();
    stream.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!stream)
        return std::unexpected("unable to seek to " + std::string(description) + ": " + path.string());
    if (!output.empty() &&
        !stream.read(reinterpret_cast<char*>(output.data()), static_cast<std::streamsize>(output.size())))
        return std::unexpected("unable to read complete " + std::string(description) + ": " + path.string());
    return {};
}

[[nodiscard]] std::expected<void, std::string> ValidateZeroFileRange(
    std::ifstream& stream, const std::filesystem::path& path, const HogFileRange range)
{
    std::array<std::byte, kPaddingValidationChunkSize> buffer{};
    std::uint64_t cursor = range.offset;
    std::uint64_t remaining = range.size;
    while (remaining != 0)
    {
        const auto chunk_size = static_cast<std::size_t>(
            std::min<std::uint64_t>(remaining, buffer.size()));
        auto read = ReadExact(stream, path, cursor,
            std::span<std::byte>(buffer).first(chunk_size), "HOG trailing padding");
        if (!read)
            return read;
        if (!std::ranges::all_of(std::span<const std::byte>(buffer).first(chunk_size),
                [](const std::byte value) { return value == std::byte{0}; }))
            return std::unexpected("archive range contains non-zero bytes after its logical payload end: " +
                                   path.string());

        auto next_cursor = CheckedAdd(cursor, chunk_size, "HOG padding cursor");
        if (!next_cursor)
            return std::unexpected(next_cursor.error());
        cursor = *next_cursor;
        remaining -= chunk_size;
    }
    return {};
}

[[nodiscard]] std::expected<void, std::string> ValidateSourceRange(
    const HogReadSource& source, const HogFileRange range, const std::uint64_t maximum_bytes)
{
    if (source.read_exact == nullptr)
        return std::unexpected("HOG read source has no read callback");
    if (range.size > maximum_bytes)
        return std::unexpected("archive range exceeds caller's load limit");
    if (range.offset > source.size || range.size > source.size - range.offset)
        return std::unexpected("archive range extends past the byte source");
    return {};
}

[[nodiscard]] std::expected<void, std::string> ReadExact(
    const HogReadSource& source, const std::uint64_t offset,
    const std::span<std::byte> output, const std::string_view description)
{
    if (source.read_exact == nullptr)
        return std::unexpected("HOG read source has no read callback");
    if (offset > source.size || output.size() > source.size - offset)
        return std::unexpected(std::string(description) + " extends past the byte source");
    auto read = source.read_exact(source.context, offset, output);
    if (!read)
        return std::unexpected(read.error());
    return {};
}

[[nodiscard]] std::expected<void, std::string> ValidateZeroSourceRange(
    const HogReadSource& source, const HogFileRange range)
{
    std::array<std::byte, kPaddingValidationChunkSize> buffer{};
    std::uint64_t cursor = range.offset;
    std::uint64_t remaining = range.size;
    while (remaining != 0)
    {
        const auto chunk_size = static_cast<std::size_t>(
            std::min<std::uint64_t>(remaining, buffer.size()));
        auto read = ReadExact(source, cursor,
            std::span<std::byte>(buffer).first(chunk_size), "HOG trailing padding");
        if (!read)
            return read;
        if (!std::ranges::all_of(std::span<const std::byte>(buffer).first(chunk_size),
                [](const std::byte value) { return value == std::byte{0}; }))
            return std::unexpected(
                "archive range contains non-zero bytes after its logical payload end");

        auto next_cursor = CheckedAdd(cursor, chunk_size, "HOG padding cursor");
        if (!next_cursor)
            return std::unexpected(next_cursor.error());
        cursor = *next_cursor;
        remaining -= chunk_size;
    }
    return {};
}

[[nodiscard]] std::expected<ParsedDirectory, std::string> ParseDirectory(
    const std::span<const std::byte> directory, const std::uint64_t archive_size)
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

    auto offset_count = CheckedAdd(header.count, 1U, "HOG offset count");
    if (!offset_count)
        return std::unexpected(offset_count.error());
    auto offset_table_size = CheckedMultiply(*offset_count, sizeof(std::uint32_t), "HOG offset table");
    if (!offset_table_size)
        return std::unexpected(offset_table_size.error());
    auto expected_names = CheckedAdd(header.offsets_offset, *offset_table_size, "HOG name-table offset");
    if (!expected_names)
        return std::unexpected(expected_names.error());
    if (*expected_names > std::numeric_limits<std::uint32_t>::max() ||
        header.names_offset != *expected_names)
        return std::unexpected("name-table location does not follow the offset table");

    if (header.names_offset > header.data_offset || header.data_offset > directory.size() ||
        header.data_offset > archive_size)
        return std::unexpected("invalid name/data table boundaries");

    std::vector<std::uint32_t> offsets;
    offsets.reserve(static_cast<std::size_t>(*offset_count));
    for (std::uint64_t index = 0; index < *offset_count; ++index)
    {
        auto index_bytes = CheckedMultiply(index, sizeof(std::uint32_t), "HOG offset-table index");
        if (!index_bytes)
            return std::unexpected(index_bytes.error());
        auto location = CheckedAdd(header.offsets_offset, *index_bytes, "HOG offset-table location");
        if (!location)
            return std::unexpected(location.error());
        auto location_end = CheckedAdd(*location, sizeof(std::uint32_t), "HOG offset-table read");
        if (!location_end)
            return std::unexpected(location_end.error());
        if (*location_end > directory.size())
            return std::unexpected("offset table extends past the directory prefix");
        offsets.push_back(ReadU32(directory, static_cast<std::size_t>(*location)));
    }

    if (offsets.front() != 0)
        return std::unexpected("first payload offset is not zero");
    if (!std::is_sorted(offsets.begin(), offsets.end()))
        return std::unexpected("payload offsets are not monotonic");

    auto payload_end = CheckedAdd(header.data_offset, offsets.back(), "HOG terminal payload boundary");
    if (!payload_end)
        return std::unexpected(payload_end.error());
    if (*payload_end > archive_size)
        return std::unexpected("terminal payload boundary " + Hex(*payload_end) +
                               " exceeds archive size " + Hex(archive_size));

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
        auto entry_offset = CheckedAdd(header.data_offset, offsets[index], "HOG entry offset");
        if (!entry_offset)
            return std::unexpected(entry_offset.error());
        entries.push_back(HogEntry{
            .name = std::move(names[index]),
            .offset = *entry_offset,
            .size = static_cast<std::uint64_t>(offsets[index + 1]) - offsets[index],
        });
    }

    return ParsedDirectory{
        .header = header,
        .entries = std::move(entries),
        .logical_size = *payload_end,
    };
}

[[nodiscard]] std::expected<ParsedDirectory, std::string> ReadDirectoryFromRange(
    std::ifstream& stream, const std::filesystem::path& path, const HogFileRange range)
{
    if (range.size < kHeaderSize + sizeof(std::uint32_t))
        return std::unexpected("archive range is too small for a HOG header and terminal offset");

    std::array<std::byte, kHeaderSize> header_bytes{};
    auto header_read = ReadExact(stream, path, range.offset, header_bytes, "HOG header");
    if (!header_read)
        return std::unexpected(header_read.error());

    const std::uint32_t data_offset = ReadU32(header_bytes, 0x10);
    if (data_offset < kHeaderSize + sizeof(std::uint32_t) || data_offset > range.size)
        return std::unexpected("invalid directory prefix size: " + Hex(data_offset));
    if (data_offset > kMaximumDirectorySize)
        return std::unexpected("directory prefix exceeds safety limit: " + Hex(data_offset));

    std::vector<std::byte> directory(data_offset);
    auto directory_read = ReadExact(stream, path, range.offset, directory, "HOG directory");
    if (!directory_read)
        return std::unexpected(directory_read.error());
    return ParseDirectory(directory, range.size);
}

[[nodiscard]] std::expected<ParsedDirectory, std::string> ReadDirectoryFromRange(
    const HogReadSource& source, const HogFileRange range)
{
    if (range.size < kHeaderSize + sizeof(std::uint32_t))
        return std::unexpected("archive range is too small for a HOG header and terminal offset");

    std::array<std::byte, kHeaderSize> header_bytes{};
    auto header_read = ReadExact(source, range.offset, header_bytes, "HOG header");
    if (!header_read)
        return std::unexpected(header_read.error());

    const std::uint32_t data_offset = ReadU32(header_bytes, 0x10);
    if (data_offset < kHeaderSize + sizeof(std::uint32_t) || data_offset > range.size)
        return std::unexpected("invalid directory prefix size: " + Hex(data_offset));
    if (data_offset > kMaximumDirectorySize)
        return std::unexpected("directory prefix exceeds safety limit: " + Hex(data_offset));

    std::vector<std::byte> directory(data_offset);
    auto directory_read = ReadExact(source, range.offset, directory, "HOG directory");
    if (!directory_read)
        return std::unexpected(directory_read.error());
    return ParseDirectory(directory, range.size);
}

[[nodiscard]] std::expected<std::vector<std::byte>, std::string> ReadOwnedRange(
    std::ifstream& stream, const std::filesystem::path& path, const HogFileRange range)
{
    if (range.size > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()) ||
        range.size > static_cast<std::uint64_t>(std::numeric_limits<std::streamsize>::max()))
        return std::unexpected("archive range is too large for a single allocation/read: " + path.string());

    std::vector<std::byte> bytes(static_cast<std::size_t>(range.size));
    auto read = ReadExact(stream, path, range.offset, bytes, "HOG archive range");
    if (!read)
        return std::unexpected(read.error());
    return bytes;
}

[[nodiscard]] std::expected<void, std::string> ValidateZeroMemoryTail(
    const std::span<const std::byte> bytes, const std::uint64_t logical_size)
{
    if (logical_size > bytes.size())
        return std::unexpected("logical HOG size exceeds its owning byte span");
    const auto padding = bytes.subspan(static_cast<std::size_t>(logical_size));
    if (!std::ranges::all_of(padding, [](const std::byte value) { return value == std::byte{0}; }))
        return std::unexpected("archive contains non-zero bytes after its logical payload end");
    return {};
}
} // namespace

std::expected<HogIndex, std::string> HogIndex::Open(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream)
        return std::unexpected("unable to open archive: " + path.string());

    auto file_size = FileSize(stream, path);
    if (!file_size)
        return std::unexpected(file_size.error());
    const HogFileRange range{.offset = 0, .size = *file_size};
    auto valid_range = ValidateFileRange(range, *file_size, path, *file_size);
    if (!valid_range)
        return std::unexpected(valid_range.error());

    auto parsed = ReadDirectoryFromRange(stream, path, range);
    if (!parsed)
        return std::unexpected(parsed.error());
    if (parsed->logical_size != range.size)
        return std::unexpected("terminal payload boundary " + Hex(parsed->logical_size) +
                               " does not equal archive size " + Hex(range.size));

    HogIndex result;
    result.header_ = parsed->header;
    result.entries_ = std::move(parsed->entries);
    result.archive_size_ = range.size;
    result.logical_size_ = parsed->logical_size;
    return result;
}

std::expected<HogIndex, std::string> HogIndex::Open(const HogReadSource& source)
{
    const HogFileRange range{.offset = 0, .size = source.size};
    auto valid_range = ValidateSourceRange(source, range, source.size);
    if (!valid_range)
        return std::unexpected(valid_range.error());

    auto parsed = ReadDirectoryFromRange(source, range);
    if (!parsed)
        return std::unexpected(parsed.error());
    if (parsed->logical_size != range.size)
        return std::unexpected("terminal payload boundary " + Hex(parsed->logical_size) +
                               " does not equal archive size " + Hex(range.size));

    HogIndex result;
    result.header_ = parsed->header;
    result.entries_ = std::move(parsed->entries);
    result.archive_size_ = range.size;
    result.logical_size_ = parsed->logical_size;
    return result;
}

std::expected<HogIndex, std::string> HogIndex::OpenRange(
    const std::filesystem::path& path, const HogFileRange range, const std::uint64_t maximum_bytes)
{
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream)
        return std::unexpected("unable to open archive: " + path.string());

    auto file_size = FileSize(stream, path);
    if (!file_size)
        return std::unexpected(file_size.error());
    auto valid_range = ValidateFileRange(range, *file_size, path, maximum_bytes);
    if (!valid_range)
        return std::unexpected(valid_range.error());

    auto parsed = ReadDirectoryFromRange(stream, path, range);
    if (!parsed)
        return std::unexpected(parsed.error());

    auto padding_offset = CheckedAdd(range.offset, parsed->logical_size, "HOG padding offset");
    if (!padding_offset)
        return std::unexpected(padding_offset.error());
    auto padding_valid = ValidateZeroFileRange(stream, path, HogFileRange{
        .offset = *padding_offset,
        .size = range.size - parsed->logical_size,
    });
    if (!padding_valid)
        return std::unexpected(padding_valid.error());

    HogIndex result;
    result.header_ = parsed->header;
    result.entries_ = std::move(parsed->entries);
    result.archive_size_ = range.size;
    result.logical_size_ = parsed->logical_size;
    return result;
}

std::expected<HogIndex, std::string> HogIndex::OpenRange(
    const HogReadSource& source, const HogFileRange range, const std::uint64_t maximum_bytes)
{
    auto valid_range = ValidateSourceRange(source, range, maximum_bytes);
    if (!valid_range)
        return std::unexpected(valid_range.error());

    auto parsed = ReadDirectoryFromRange(source, range);
    if (!parsed)
        return std::unexpected(parsed.error());

    auto padding_offset = CheckedAdd(range.offset, parsed->logical_size, "HOG padding offset");
    if (!padding_offset)
        return std::unexpected(padding_offset.error());
    auto padding_valid = ValidateZeroSourceRange(source, HogFileRange{
        .offset = *padding_offset,
        .size = range.size - parsed->logical_size,
    });
    if (!padding_valid)
        return std::unexpected(padding_valid.error());

    HogIndex result;
    result.header_ = parsed->header;
    result.entries_ = std::move(parsed->entries);
    result.archive_size_ = range.size;
    result.logical_size_ = parsed->logical_size;
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

    auto file_size = FileSize(stream, path);
    if (!file_size)
        return std::unexpected(file_size.error());
    const HogFileRange range{.offset = 0, .size = *file_size};
    auto valid_range = ValidateFileRange(range, *file_size, path, maximum_bytes);
    if (!valid_range)
        return std::unexpected(valid_range.error());
    auto bytes = ReadOwnedRange(stream, path, range);
    if (!bytes)
        return std::unexpected(bytes.error());
    return FromOwnedBytes(std::move(*bytes), TrailingDataPolicy::ExactEnd);
}

std::expected<HogArchive, std::string> HogArchive::OpenRange(
    const std::filesystem::path& path, const HogFileRange range, const std::uint64_t maximum_bytes)
{
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream)
        return std::unexpected("unable to open archive: " + path.string());

    auto file_size = FileSize(stream, path);
    if (!file_size)
        return std::unexpected(file_size.error());
    auto valid_range = ValidateFileRange(range, *file_size, path, maximum_bytes);
    if (!valid_range)
        return std::unexpected(valid_range.error());
    auto bytes = ReadOwnedRange(stream, path, range);
    if (!bytes)
        return std::unexpected(bytes.error());
    return FromOwnedBytes(std::move(*bytes), TrailingDataPolicy::ZeroPadding);
}

std::expected<HogArchive, std::string> HogArchive::FromBytes(std::vector<std::byte> bytes)
{
    return FromOwnedBytes(std::move(bytes), TrailingDataPolicy::ExactEnd);
}

std::expected<HogArchive, std::string> HogArchive::FromSpan(
    const std::span<const std::byte> bytes, const std::uint64_t maximum_bytes)
{
    if (bytes.size() > maximum_bytes)
        return std::unexpected("archive span exceeds caller's load limit");
    return FromOwnedBytes(
        std::vector<std::byte>(bytes.begin(), bytes.end()), TrailingDataPolicy::ZeroPadding);
}

std::expected<HogArchive, std::string> HogArchive::FromOwnedBytes(
    std::vector<std::byte> bytes, const TrailingDataPolicy trailing_policy)
{
    const std::span<const std::byte> view(bytes);
    auto parsed = ParseDirectory(view, view.size());
    if (!parsed)
        return std::unexpected(parsed.error());
    if (trailing_policy == TrailingDataPolicy::ZeroPadding)
    {
        auto padding_valid = ValidateZeroMemoryTail(view, parsed->logical_size);
        if (!padding_valid)
            return std::unexpected(padding_valid.error());
    }
    else if (parsed->logical_size != view.size())
    {
        return std::unexpected("terminal payload boundary " + Hex(parsed->logical_size) +
                               " does not equal archive size " + Hex(view.size()));
    }

    HogArchive result;
    result.header_ = parsed->header;
    result.storage_ = std::move(bytes);
    result.entries_ = std::move(parsed->entries);
    result.logical_size_ = parsed->logical_size;

    return result;
}

std::span<const std::byte> HogArchive::payload(const HogEntry& entry) const noexcept
{
    if (entry.offset < header_.data_offset || entry.offset > logical_size_ ||
        entry.size > logical_size_ - entry.offset)
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
