#include "omega/retail/pop_game_objects_decoder.h"

#include <array>
#include <cmath>
#include <cstring>
#include <optional>
#include <string_view>

namespace omega::retail
{
namespace
{
// The GOB record class marker: every placement record starts with this word.
constexpr std::uint32_t kRecordClass = 12U;
// A record's model name never realistically exceeds this; also bounds the search.
constexpr std::size_t kMaximumNameBytes = 48U;
// Reject absurd position magnitudes (level coords are well under this).
constexpr float kMaximumCoordinate = 1.0e6F;
// The three parameter words (flags + two params) between the 4-aligned name end
// and the position triple, observed uniformly across the NPC records.
constexpr std::size_t kParamsBytesBeforePosition = 12U;

[[nodiscard]] std::optional<std::uint32_t> ReadU32(
    const std::span<const std::byte> bytes, const std::size_t offset) noexcept
{
    if (offset + 4U > bytes.size())
        return std::nullopt;
    std::uint32_t value = 0U;
    std::memcpy(&value, bytes.data() + offset, 4U);
    return value;
}

[[nodiscard]] std::optional<float> ReadF32(
    const std::span<const std::byte> bytes, const std::size_t offset) noexcept
{
    if (offset + 4U > bytes.size())
        return std::nullopt;
    float value = 0.0F;
    std::memcpy(&value, bytes.data() + offset, 4U);
    return value;
}

[[nodiscard]] constexpr std::size_t Align4(const std::size_t value) noexcept
{
    return (value + 3U) & ~static_cast<std::size_t>(3U);
}

// First byte offset of a 4-char section tag, or nullopt. Sections are tag-led;
// the first occurrence is the section header (matches the RE-validated table).
[[nodiscard]] std::optional<std::size_t> FindTag(
    const std::span<const std::byte> bytes, const std::string_view tag) noexcept
{
    if (bytes.size() < tag.size())
        return std::nullopt;
    for (std::size_t i = 0U; i + tag.size() <= bytes.size(); ++i)
    {
        if (std::memcmp(bytes.data() + i, tag.data(), tag.size()) == 0)
            return i;
    }
    return std::nullopt;
}

[[nodiscard]] std::uint32_t SectionCount(
    const std::span<const std::byte> bytes, const std::string_view tag) noexcept
{
    const auto offset = FindTag(bytes, tag);
    if (!offset)
        return 0U;
    return ReadU32(bytes, *offset + 4U).value_or(0U);
}

// Reads a NUL-terminated printable-ASCII model name at `offset`. Returns the name
// (without the NUL) if it is at least two printable characters and terminates
// within kMaximumNameBytes; nullopt otherwise.
[[nodiscard]] std::optional<std::string> ReadModelName(
    const std::span<const std::byte> bytes, const std::size_t offset)
{
    std::string name;
    for (std::size_t i = 0U; i < kMaximumNameBytes; ++i)
    {
        if (offset + i >= bytes.size())
            return std::nullopt;
        const auto character = static_cast<unsigned char>(bytes[offset + i]);
        if (character == 0U)
            return name.size() >= 2U ? std::optional<std::string>(std::move(name))
                                     : std::nullopt;
        if (character < 0x20U || character > 0x7EU)
            return std::nullopt;
        name.push_back(static_cast<char>(character));
    }
    return std::nullopt;
}

[[nodiscard]] bool IsFiniteCoordinate(const float value) noexcept
{
    return std::isfinite(value) && std::fabs(value) < kMaximumCoordinate;
}
} // namespace

asset::DecodeResult<PopGameObjectsIR> DecodePopGameObjects(
    const std::span<const std::byte> pop_bytes, const asset::DecodeLimits limits)
{
    PopGameObjectsIR result;
    result.npc_section_count = SectionCount(pop_bytes, "NPC:");
    result.nav_node_count = SectionCount(pop_bytes, "NOD:");
    result.hotbox_count = SectionCount(pop_bytes, "BOX:");

    const auto npc_offset = FindTag(pop_bytes, "NPC:");
    if (!npc_offset)
        return result; // No NPC section -- an empty roster is a valid outcome.

    // The NPC body runs from just after the tag+count to the next section tag
    // (WPN: follows NPC: in the fixed section order).
    const std::size_t body_begin = *npc_offset + 8U;
    const auto wpn_offset = FindTag(pop_bytes, "WPN:");
    const std::size_t body_end =
        wpn_offset && *wpn_offset > body_begin ? *wpn_offset : pop_bytes.size();
    if (body_begin >= body_end)
        return result;
    const std::span<const std::byte> body =
        pop_bytes.subspan(body_begin, body_end - body_begin);

    // Walk records: split on the class delimiter, and emit only records that carry
    // a valid model name and a finite position (fail-soft over variant records).
    std::size_t i = 0U;
    while (i + 4U <= body.size())
    {
        if (ReadU32(body, i).value_or(0U) != kRecordClass)
        {
            i += 4U;
            continue;
        }
        const std::uint32_t id = ReadU32(body, i + 4U).value_or(0U);
        const auto name = ReadModelName(body, i + 8U);
        if (id == 0U || id >= 0x10000000U || !name)
        {
            i += 4U;
            continue;
        }
        const std::size_t position_offset =
            i + Align4(8U + name->size() + 1U) + kParamsBytesBeforePosition;
        const auto x = ReadF32(body, position_offset);
        const auto y = ReadF32(body, position_offset + 4U);
        const auto z = ReadF32(body, position_offset + 8U);
        if (!x || !y || !z || !IsFiniteCoordinate(*x) ||
            !IsFiniteCoordinate(*y) || !IsFiniteCoordinate(*z))
        {
            i += 4U;
            continue;
        }
        if (result.npc_spawns.size() >= limits.maximum_items)
        {
            return std::unexpected(asset::DecodeError{
                .code = asset::DecodeErrorCode::LimitExceeded,
                .byte_offset = body_begin + i,
                .message = "POP NPC spawns exceed the decoder item limit",
            });
        }
        result.npc_spawns.push_back(PopNpcSpawn{
            .id = id,
            .model = *name,
            .position = asset::Float3IR{.x = *x, .y = *y, .z = *z},
        });
        // Advance past this record to the next class delimiter.
        std::size_t next = position_offset + 12U;
        while (next + 4U <= body.size() &&
               ReadU32(body, next).value_or(0U) != kRecordClass)
        {
            next += 4U;
        }
        i = next;
    }
    return result;
}
} // namespace omega::retail
