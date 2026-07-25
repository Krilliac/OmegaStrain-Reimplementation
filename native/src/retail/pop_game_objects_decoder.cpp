#include "omega/retail/pop_game_objects_decoder.h"

#include <cmath>
#include <cstddef>
#include <cstring>
#include <optional>
#include <string_view>
#include <utility>

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

// A NOD record's fixed pre-link layout (the dominant variant): class + id +
// field_a + field_b (4 words), then position (3 f32) at +16, then a 6-word
// transform, then link_count(u32) at +52, then link_count (neighbor,weight) u32
// pairs. Nav-node coords sit well inside a level; a tighter bound than the raw
// finite check filters class-delimiter false positives from garbage floats.
constexpr std::size_t kNavNodePositionOffset = 16U;
constexpr std::size_t kNavNodeLinkCountOffset = 52U;
constexpr std::uint32_t kMaximumNavLinks = 64U;
constexpr float kNavCoordinateBound = 4000.0F;

[[nodiscard]] bool IsNavCoordinate(const float value) noexcept
{
    return std::isfinite(value) && std::fabs(value) < kNavCoordinateBound;
}

// Walks the NOD: section, emitting each cleanly-decodable nav node (fixed layout,
// validated position + adjacency). Variant / undecodable records are skipped
// fail-soft (resync on the next class delimiter), so nav_nodes.size() may be less
// than the declared nav_node_count.
void DecodeNavNodes(const std::span<const std::byte> pop_bytes,
    PopGameObjectsIR& result, const asset::DecodeLimits& limits)
{
    const auto nod_offset = FindTag(pop_bytes, "NOD:");
    if (!nod_offset)
        return;
    const std::size_t body_begin = *nod_offset + 8U;
    // GEN: follows NOD: in the fixed section order.
    const auto gen_offset = FindTag(pop_bytes, "GEN:");
    const std::size_t body_end =
        gen_offset && *gen_offset > body_begin ? *gen_offset : pop_bytes.size();
    if (body_begin >= body_end)
        return;
    const std::span<const std::byte> body =
        pop_bytes.subspan(body_begin, body_end - body_begin);
    const std::uint32_t node_count_bound = result.nav_node_count;

    std::size_t i = 0U;
    while (i + 4U <= body.size())
    {
        if (ReadU32(body, i).value_or(0U) != kRecordClass)
        {
            i += 4U;
            continue;
        }
        const std::uint32_t id = ReadU32(body, i + 4U).value_or(0U);
        const auto x = ReadF32(body, i + kNavNodePositionOffset);
        const auto y = ReadF32(body, i + kNavNodePositionOffset + 4U);
        const auto z = ReadF32(body, i + kNavNodePositionOffset + 8U);
        const auto link_count = ReadU32(body, i + kNavNodeLinkCountOffset);
        if (id == 0U || !x || !y || !z || !IsNavCoordinate(*x) ||
            !IsNavCoordinate(*y) || !IsNavCoordinate(*z) || !link_count ||
            *link_count > kMaximumNavLinks)
        {
            i += 4U;
            continue;
        }
        std::vector<PopNavLink> links;
        std::size_t link_offset = i + kNavNodeLinkCountOffset + 4U;
        bool links_valid = true;
        for (std::uint32_t l = 0U; l < *link_count; ++l)
        {
            const auto neighbor = ReadU32(body, link_offset);
            const auto weight = ReadU32(body, link_offset + 4U);
            // Weight 0 and neighbor 0 both occur in the real graph; only an
            // unreadable word or an out-of-range neighbor rejects the record.
            if (!neighbor || !weight)
            {
                links_valid = false; // an unreadable u32 (past the buffer)
                break;
            }
            if (node_count_bound != 0U && *neighbor >= node_count_bound)
            {
                links_valid = false;
                break;
            }
            links.push_back(PopNavLink{.neighbor = *neighbor, .weight = *weight});
            link_offset += 8U;
        }
        if (!links_valid)
        {
            i += 4U;
            continue;
        }
        if (result.nav_nodes.size() >= limits.maximum_items)
            return; // Fail-soft: stop at the item cap, keep what decoded.
        result.nav_nodes.push_back(PopNavNode{
            .id = id,
            .position = asset::Float3IR{.x = *x, .y = *y, .z = *z},
            .links = std::move(links),
        });
        std::size_t next = link_offset;
        while (next + 4U <= body.size() &&
               ReadU32(body, next).value_or(0U) != kRecordClass)
        {
            next += 4U;
        }
        i = next;
    }
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

    DecodeNavNodes(pop_bytes, result, limits);
    return result;
}
} // namespace omega::retail
