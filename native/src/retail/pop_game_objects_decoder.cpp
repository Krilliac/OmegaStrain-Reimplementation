#include "omega/retail/pop_game_objects_decoder.h"

#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>
#include <new>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace omega::retail
{
namespace
{
// The GOB record class marker: every placement record starts with this word.
constexpr std::uint32_t kRecordClass = 12U;
// A record's model name never realistically exceeds this; also bounds the
// search.
constexpr std::size_t kMaximumNameBytes = 48U;
// Reject absurd position magnitudes (level coords are well under this).
constexpr float kMaximumCoordinate = 1.0e6F;
// The three parameter words (flags + two params) between the 4-aligned name end
// and the position triple, observed uniformly across the NPC records.
constexpr std::size_t kParamsBytesBeforePosition = 12U;

// A NOD record's fixed pre-link layout (the dominant variant): class + id +
// field_a + field_b (4 words), then position (3 f32) at +16, then a 6-word
// transform, then link_count(u32) at +52, then link_count (neighbor,weight) u32
// pairs. Nav-node coords sit well inside a level; a tighter bound than the raw
// finite check filters class-delimiter false positives from garbage floats.
constexpr std::size_t kNavNodePositionOffset = 16U;
constexpr std::size_t kNavNodeLinkCountOffset = 52U;
constexpr std::uint32_t kMaximumNavLinks = 64U;
constexpr float kNavCoordinateBound = 4000.0F;

struct NpcLayout
{
    std::uint32_t id = 0U;
    std::size_t model_offset = 0U;
    std::size_t model_length = 0U;
    asset::Float3IR position;
};

struct NavNodeLayout
{
    std::uint32_t id = 0U;
    std::size_t links_offset = 0U;
    std::uint32_t link_count = 0U;
    asset::Float3IR position;
};

struct PopLayout
{
    std::vector<NpcLayout> npc_spawns;
    std::vector<NavNodeLayout> nav_nodes;
    std::uint32_t npc_section_count = 0U;
    std::uint32_t nav_node_count = 0U;
    std::uint32_t hotbox_count = 0U;
    std::uint64_t decoded_items = 0U;
    std::uint64_t logical_output_bytes = sizeof(PopGameObjectsIR);
    std::uint64_t scratch_bytes = 0U;
};

[[nodiscard]] asset::DecodeError Error(
    const asset::DecodeErrorCode code, std::string message,
    const std::optional<std::uint64_t> byte_offset = std::nullopt)
{
    return asset::DecodeError{
        .code = code,
        .byte_offset = byte_offset,
        .message = std::move(message),
    };
}

[[nodiscard]] bool Add(const std::uint64_t left, const std::uint64_t right,
                       std::uint64_t& result) noexcept
{
    if (right > std::numeric_limits<std::uint64_t>::max() - left)
        return false;
    result = left + right;
    return true;
}

[[nodiscard]] bool Multiply(const std::uint64_t left, const std::uint64_t right,
                            std::uint64_t& result) noexcept
{
    if (left != 0U && right > std::numeric_limits<std::uint64_t>::max() / left)
        return false;
    result = left * right;
    return true;
}

[[nodiscard]] asset::DecodeResult<void> Accumulate(std::uint64_t& total, const std::uint64_t amount,
                                                   const std::uint64_t limit,
                                                   const char* overflow_message,
                                                   const char* limit_message,
                                                   const std::optional<std::uint64_t> byte_offset)
{
    std::uint64_t next = 0U;
    if (!Add(total, amount, next))
        return std::unexpected(
            Error(asset::DecodeErrorCode::Overflow, overflow_message, byte_offset));
    if (next > limit)
        return std::unexpected(
            Error(asset::DecodeErrorCode::LimitExceeded, limit_message, byte_offset));
    total = next;
    return {};
}

[[nodiscard]] std::optional<std::uint32_t> ReadU32(const std::span<const std::byte> bytes,
                                                   const std::size_t offset) noexcept
{
    if (offset > bytes.size() || bytes.size() - offset < 4U)
        return std::nullopt;
    std::uint32_t value = 0U;
    std::memcpy(&value, bytes.data() + offset, 4U);
    return value;
}

[[nodiscard]] std::optional<float> ReadF32(const std::span<const std::byte> bytes,
                                           const std::size_t offset) noexcept
{
    if (offset > bytes.size() || bytes.size() - offset < 4U)
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
[[nodiscard]] std::optional<std::size_t> FindTag(const std::span<const std::byte> bytes,
                                                 const std::string_view tag) noexcept
{
    if (bytes.size() < tag.size())
        return std::nullopt;
    for (std::size_t i = 0U; i <= bytes.size() - tag.size(); ++i)
    {
        if (std::memcmp(bytes.data() + i, tag.data(), tag.size()) == 0)
            return i;
    }
    return std::nullopt;
}

[[nodiscard]] std::uint32_t SectionCount(const std::span<const std::byte> bytes,
                                         const std::string_view tag) noexcept
{
    const auto offset = FindTag(bytes, tag);
    if (!offset)
        return 0U;
    return ReadU32(bytes, *offset + 4U).value_or(0U);
}

// Returns the content length of a NUL-terminated printable-ASCII model name at
// `offset`. The preflight retains only a byte span; no model string is
// allocated.
[[nodiscard]] std::optional<std::size_t> ReadModelNameLength(const std::span<const std::byte> bytes,
                                                             const std::size_t offset) noexcept
{
    for (std::size_t i = 0U; i < kMaximumNameBytes; ++i)
    {
        if (offset >= bytes.size() || i >= bytes.size() - offset)
            return std::nullopt;
        const auto character = static_cast<unsigned char>(bytes[offset + i]);
        if (character == 0U)
            return i >= 2U ? std::optional<std::size_t>(i) : std::nullopt;
        if (character < 0x20U || character > 0x7EU)
            return std::nullopt;
    }
    return std::nullopt;
}

[[nodiscard]] bool IsFiniteCoordinate(const float value) noexcept
{
    return std::isfinite(value) && std::fabs(value) < kMaximumCoordinate;
}

[[nodiscard]] bool IsNavCoordinate(const float value) noexcept
{
    return std::isfinite(value) && std::fabs(value) < kNavCoordinateBound;
}

[[nodiscard]] asset::DecodeResult<void> ChargeNpc(PopLayout& layout, const std::size_t model_length,
                                                  const asset::DecodeLimits& limits,
                                                  const std::uint64_t byte_offset)
{
    // maximum_items is one shared semantic budget: one NPC plus its model string.
    auto charged = Accumulate(layout.decoded_items, 2U, limits.maximum_items,
                              "POP decoded item count overflows",
                              "POP objects exceed the decoder item limit", byte_offset);
    if (!charged)
        return std::unexpected(charged.error());

    std::uint64_t object_bytes = 0U;
    if (!Add(sizeof(PopNpcSpawn), model_length, object_bytes))
        return std::unexpected(Error(asset::DecodeErrorCode::Overflow,
                                     "POP decoded output size overflows", byte_offset));
    charged = Accumulate(layout.logical_output_bytes, object_bytes, limits.maximum_output_bytes,
                         "POP decoded output size overflows",
                         "POP objects exceed the decoder output limit", byte_offset);
    if (!charged)
        return std::unexpected(charged.error());

    // Preflight descriptors are conservative semantic staging storage. Charging
    // the corresponding output object plus characters overbounds each descriptor
    // and makes the cumulative scratch contract independent of vector capacity.
    charged = Accumulate(layout.scratch_bytes, object_bytes, limits.maximum_scratch_bytes,
                         "POP decoder scratch size overflows",
                         "POP objects exceed the decoder scratch limit", byte_offset);
    if (!charged)
        return std::unexpected(charged.error());
    return {};
}

[[nodiscard]] asset::DecodeResult<void> ChargeNavNode(PopLayout& layout,
                                                      const std::uint32_t link_count,
                                                      const asset::DecodeLimits& limits,
                                                      const std::uint64_t byte_offset)
{
    std::uint64_t item_count = 0U;
    if (!Add(1U, link_count, item_count))
        return std::unexpected(Error(asset::DecodeErrorCode::Overflow,
                                     "POP decoded item count overflows", byte_offset));
    auto charged = Accumulate(layout.decoded_items, item_count, limits.maximum_items,
                              "POP decoded item count overflows",
                              "POP objects exceed the decoder item limit", byte_offset);
    if (!charged)
        return std::unexpected(charged.error());

    std::uint64_t link_bytes = 0U;
    std::uint64_t object_bytes = 0U;
    if (!Multiply(link_count, sizeof(PopNavLink), link_bytes) ||
        !Add(sizeof(PopNavNode), link_bytes, object_bytes))
        return std::unexpected(Error(asset::DecodeErrorCode::Overflow,
                                     "POP decoded output size overflows", byte_offset));
    charged = Accumulate(layout.logical_output_bytes, object_bytes, limits.maximum_output_bytes,
                         "POP decoded output size overflows",
                         "POP objects exceed the decoder output limit", byte_offset);
    if (!charged)
        return std::unexpected(charged.error());
    charged = Accumulate(layout.scratch_bytes, object_bytes, limits.maximum_scratch_bytes,
                         "POP decoder scratch size overflows",
                         "POP objects exceed the decoder scratch limit", byte_offset);
    if (!charged)
        return std::unexpected(charged.error());
    return {};
}

[[nodiscard]] asset::DecodeResult<void> PreflightNpcSection(
    const std::span<const std::byte> pop_bytes, PopLayout& layout,
    const asset::DecodeLimits& limits)
{
    const auto npc_offset = FindTag(pop_bytes, "NPC:");
    if (!npc_offset)
        return {};

    const std::size_t body_begin = *npc_offset + 8U;
    const auto wpn_offset = FindTag(pop_bytes, "WPN:");
    const std::size_t body_end =
        wpn_offset && *wpn_offset > body_begin ? *wpn_offset : pop_bytes.size();
    if (body_begin >= body_end)
        return {};
    const std::span<const std::byte> body = pop_bytes.subspan(body_begin, body_end - body_begin);

    // Walk records: split on the class delimiter, and retain only records that
    // carry a valid model name and finite position. Variant records remain
    // fail-soft, but a valid record that exceeds a caller budget fails closed.
    std::size_t i = 0U;
    while (i <= body.size() && body.size() - i >= 4U)
    {
        if (ReadU32(body, i).value_or(0U) != kRecordClass)
        {
            i += 4U;
            continue;
        }
        const std::uint32_t id = ReadU32(body, i + 4U).value_or(0U);
        const auto model_length = ReadModelNameLength(body, i + 8U);
        if (id == 0U || id >= 0x10000000U || !model_length)
        {
            i += 4U;
            continue;
        }
        const std::size_t position_offset =
            i + Align4(8U + *model_length + 1U) + kParamsBytesBeforePosition;
        const auto x = ReadF32(body, position_offset);
        const auto y = ReadF32(body, position_offset + 4U);
        const auto z = ReadF32(body, position_offset + 8U);
        if (!x || !y || !z || !IsFiniteCoordinate(*x) || !IsFiniteCoordinate(*y) ||
            !IsFiniteCoordinate(*z))
        {
            i += 4U;
            continue;
        }
        if (*model_length > limits.maximum_string_bytes)
            return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
                                         "POP model name exceeds the decoder string limit",
                                         body_begin + i + 8U));

        auto charged = ChargeNpc(layout, *model_length, limits, body_begin + i);
        if (!charged)
            return std::unexpected(charged.error());
        layout.npc_spawns.push_back(NpcLayout{
            .id = id,
            .model_offset = body_begin + i + 8U,
            .model_length = *model_length,
            .position = asset::Float3IR{.x = *x, .y = *y, .z = *z},
        });

        std::size_t next = position_offset + 12U;
        while (next <= body.size() && body.size() - next >= 4U &&
               ReadU32(body, next).value_or(0U) != kRecordClass)
        {
            next += 4U;
        }
        i = next;
    }
    return {};
}

[[nodiscard]] asset::DecodeResult<void> PreflightNavSection(
    const std::span<const std::byte> pop_bytes, PopLayout& layout,
    const asset::DecodeLimits& limits)
{
    const auto nod_offset = FindTag(pop_bytes, "NOD:");
    if (!nod_offset)
        return {};
    const std::size_t body_begin = *nod_offset + 8U;
    const auto gen_offset = FindTag(pop_bytes, "GEN:");
    const std::size_t body_end =
        gen_offset && *gen_offset > body_begin ? *gen_offset : pop_bytes.size();
    if (body_begin >= body_end)
        return {};
    const std::span<const std::byte> body = pop_bytes.subspan(body_begin, body_end - body_begin);

    std::size_t i = 0U;
    while (i <= body.size() && body.size() - i >= 4U)
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
        if (id == 0U || !x || !y || !z || !IsNavCoordinate(*x) || !IsNavCoordinate(*y) ||
            !IsNavCoordinate(*z) || !link_count || *link_count > kMaximumNavLinks)
        {
            i += 4U;
            continue;
        }

        std::size_t link_offset = i + kNavNodeLinkCountOffset + 4U;
        bool links_valid = true;
        for (std::uint32_t link = 0U; link < *link_count; ++link)
        {
            const auto neighbor = ReadU32(body, link_offset);
            const auto weight = ReadU32(body, link_offset + 4U);
            if (!neighbor || !weight ||
                (layout.nav_node_count != 0U && *neighbor >= layout.nav_node_count))
            {
                links_valid = false;
                break;
            }
            link_offset += 8U;
        }
        if (!links_valid)
        {
            i += 4U;
            continue;
        }

        auto charged = ChargeNavNode(layout, *link_count, limits, body_begin + i);
        if (!charged)
            return std::unexpected(charged.error());
        layout.nav_nodes.push_back(NavNodeLayout{
            .id = id,
            .links_offset = body_begin + i + kNavNodeLinkCountOffset + 4U,
            .link_count = *link_count,
            .position = asset::Float3IR{.x = *x, .y = *y, .z = *z},
        });

        std::size_t next = link_offset;
        while (next <= body.size() && body.size() - next >= 4U &&
               ReadU32(body, next).value_or(0U) != kRecordClass)
        {
            next += 4U;
        }
        i = next;
    }
    return {};
}

[[nodiscard]] asset::DecodeResult<PopLayout> Preflight(const std::span<const std::byte> pop_bytes,
                                                       const asset::DecodeLimits limits)
{
    if (pop_bytes.size() > limits.maximum_input_bytes)
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
                                     "POP input exceeds the decoder input limit"));

    PopLayout layout{
        .npc_section_count = SectionCount(pop_bytes, "NPC:"),
        .nav_node_count = SectionCount(pop_bytes, "NOD:"),
        .hotbox_count = SectionCount(pop_bytes, "BOX:"),
    };
    if (layout.logical_output_bytes > limits.maximum_output_bytes)
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
                                     "POP objects exceed the decoder output limit"));

    try
    {
        auto decoded = PreflightNpcSection(pop_bytes, layout, limits);
        if (!decoded)
            return std::unexpected(decoded.error());
        decoded = PreflightNavSection(pop_bytes, layout, limits);
        if (!decoded)
            return std::unexpected(decoded.error());
        return layout;
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected(
            Error(asset::DecodeErrorCode::LimitExceeded, "POP decoder allocation"));
    }
    catch (const std::length_error&)
    {
        return std::unexpected(
            Error(asset::DecodeErrorCode::Overflow, "POP decoder allocation length"));
    }
}
} // namespace

asset::DecodeResult<PopGameObjectsIR> DecodePopGameObjects(
    const std::span<const std::byte> pop_bytes, const asset::DecodeLimits limits)
{
    auto layout = Preflight(pop_bytes, limits);
    if (!layout)
        return std::unexpected(layout.error());

    try
    {
        PopGameObjectsIR result{
            .npc_section_count = layout->npc_section_count,
            .nav_node_count = layout->nav_node_count,
            .hotbox_count = layout->hotbox_count,
        };
        result.npc_spawns.reserve(layout->npc_spawns.size());
        for (const NpcLayout& npc : layout->npc_spawns)
        {
            result.npc_spawns.push_back(PopNpcSpawn{
                .id = npc.id,
                .model =
                    std::string(reinterpret_cast<const char*>(pop_bytes.data() + npc.model_offset),
                                npc.model_length),
                .position = npc.position,
            });
        }

        result.nav_nodes.reserve(layout->nav_nodes.size());
        for (const NavNodeLayout& node_layout : layout->nav_nodes)
        {
            PopNavNode node{
                .id = node_layout.id,
                .position = node_layout.position,
            };
            node.links.reserve(node_layout.link_count);
            std::size_t link_offset = node_layout.links_offset;
            for (std::uint32_t link = 0U; link < node_layout.link_count; ++link)
            {
                node.links.push_back(PopNavLink{
                    .neighbor = *ReadU32(pop_bytes, link_offset),
                    .weight = *ReadU32(pop_bytes, link_offset + 4U),
                });
                link_offset += 8U;
            }
            result.nav_nodes.push_back(std::move(node));
        }
        return result;
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected(
            Error(asset::DecodeErrorCode::LimitExceeded, "POP decoder allocation"));
    }
    catch (const std::length_error&)
    {
        return std::unexpected(
            Error(asset::DecodeErrorCode::Overflow, "POP decoder allocation length"));
    }
}
} // namespace omega::retail
