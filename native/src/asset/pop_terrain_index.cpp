#include "omega/asset/pop_terrain_index.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace omega::asset
{
namespace
{
constexpr std::uint32_t kObservedHeaderWord = 70;
constexpr std::uint32_t kMaximumTerrainRecords = 1U << 20U;
constexpr std::size_t kMaximumTerrainNameLength = 4096;
constexpr std::size_t kHeaderBytes = 12;
constexpr std::size_t kFixedRecordBytes = 8;

[[nodiscard]] std::uint32_t ReadU32(
    const std::span<const std::byte> bytes, const std::size_t offset) noexcept
{
    return std::to_integer<std::uint32_t>(bytes[offset]) |
           (std::to_integer<std::uint32_t>(bytes[offset + 1]) << 8U) |
           (std::to_integer<std::uint32_t>(bytes[offset + 2]) << 16U) |
           (std::to_integer<std::uint32_t>(bytes[offset + 3]) << 24U);
}

[[nodiscard]] bool HasTag(
    const std::span<const std::byte> bytes, const std::size_t offset,
    const char first, const char second, const char third, const char fourth) noexcept
{
    return offset <= bytes.size() && bytes.size() - offset >= 4U &&
           bytes[offset] == static_cast<std::byte>(first) &&
           bytes[offset + 1] == static_cast<std::byte>(second) &&
           bytes[offset + 2] == static_cast<std::byte>(third) &&
           bytes[offset + 3] == static_cast<std::byte>(fourth);
}
} // namespace

std::expected<PopTerrainIndex, std::string> PopTerrainIndex::Parse(
    const std::span<const std::byte> bytes)
{
    if (bytes.size() < kHeaderBytes + 4U)
        return std::unexpected("POP is too small for the observed TER prefix and next section tag");
    if (ReadU32(bytes, 0) != kObservedHeaderWord)
        return std::unexpected("POP does not use the observed leading header word");
    if (!HasTag(bytes, 4, 'T', 'E', 'R', ':'))
        return std::unexpected("POP does not begin with the observed TER section tag");

    const std::uint32_t count = ReadU32(bytes, 8);
    if (count > kMaximumTerrainRecords)
        return std::unexpected("POP terrain record count exceeds safety limit");
    if (static_cast<std::uint64_t>(count) * (kFixedRecordBytes + 1U) >
        static_cast<std::uint64_t>(bytes.size() - kHeaderBytes))
        return std::unexpected("POP cannot contain its declared minimum terrain records");

    PopTerrainIndex result;
    result.records_.reserve(count);
    std::size_t cursor = kHeaderBytes;
    for (std::uint32_t ordinal = 0; ordinal < count; ++ordinal)
    {
        if (cursor > bytes.size() || bytes.size() - cursor < kFixedRecordBytes)
            return std::unexpected("POP terrain record header is truncated");
        const std::uint32_t kind = ReadU32(bytes, cursor);
        const std::uint32_t index = ReadU32(bytes, cursor + 4U);
        cursor += kFixedRecordBytes;

        const std::size_t name_start = cursor;
        while (cursor < bytes.size() && bytes[cursor] != std::byte{0})
        {
            const auto value = std::to_integer<unsigned char>(bytes[cursor]);
            if (value < 0x20U || value > 0x7EU)
                return std::unexpected("POP terrain name contains non-printable ASCII");
            if (cursor - name_start >= kMaximumTerrainNameLength)
                return std::unexpected("POP terrain name exceeds safety limit");
            ++cursor;
        }
        if (cursor == bytes.size())
            return std::unexpected("POP terrain name is not NUL-terminated");
        if (cursor == name_start)
            return std::unexpected("POP terrain name is empty");

        const auto* first = reinterpret_cast<const char*>(bytes.data() + name_start);
        std::string name(first, cursor - name_start);
        ++cursor;

        if (cursor > std::numeric_limits<std::size_t>::max() - 3U)
            return std::unexpected("POP terrain name alignment overflows host size");
        const std::size_t aligned_cursor = (cursor + 3U) & ~std::size_t{3U};
        if (aligned_cursor > bytes.size())
            return std::unexpected("POP terrain name alignment extends past input");
        if (std::ranges::any_of(bytes.subspan(cursor, aligned_cursor - cursor),
                [](const std::byte value) { return value != std::byte{0}; }))
            ++result.nonzero_alignment_record_count_;
        cursor = aligned_cursor;

        result.records_.push_back(PopTerrainRecord{
            .kind = kind,
            .index = index,
            .name = std::move(name),
        });
    }

    if (!HasTag(bytes, cursor, 'G', 'O', 'B', ':'))
        return std::unexpected("observed GOB section tag does not follow POP terrain records");
    result.next_section_offset_ = cursor;
    return result;
}
} // namespace omega::asset
