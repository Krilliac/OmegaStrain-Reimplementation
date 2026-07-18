#include "omega/retail/pop_post_terrain_hypothesis_descriptor.h"

#include "omega/asset/pop_terrain_index.h"

#include <algorithm>
#include <array>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace omega::retail
{
namespace
{
constexpr std::array<std::string_view, 21> kPublishedLiteralTags{
    "TER:",
    "GOB:",
    "SND:",
    "ACL:",
    "INL:",
    "NPC:",
    "ANPC:",
    "WPN:",
    "PLR:",
    "SKY:",
    "PNT:",
    "DIR:",
    "ENV:",
    "NOD:",
    "GEN:",
    "GRP:",
    "BOX:",
    "FIR:",
    "CAM:",
    "INV:",
    "BUG:",
};

constexpr std::array<std::string_view, 19> kExpectedLiteralOrder{
    "GOB:",
    "SND:",
    "ACL:",
    "INL:",
    "NPC:",
    "WPN:",
    "PLR:",
    "SKY:",
    "PNT:",
    "DIR:",
    "ENV:",
    "NOD:",
    "GEN:",
    "GRP:",
    "BOX:",
    "FIR:",
    "CAM:",
    "INV:",
    "BUG:",
};

struct CandidateFormula
{
    PopPostTerrainCandidate candidate = PopPostTerrainCandidate::Inl;
    std::size_t marker_ordinal = 0;
    std::size_t next_marker_ordinal = 0;
    std::uint32_t arithmetic_stride_bytes = 0;
};

constexpr std::array<CandidateFormula, 5> kCandidateFormulas{{
    {PopPostTerrainCandidate::Inl, 3, 4, 36},
    {PopPostTerrainCandidate::Pnt, 8, 9, 88},
    {PopPostTerrainCandidate::Dir, 9, 10, 44},
    {PopPostTerrainCandidate::Env, 10, 11, 76},
    {PopPostTerrainCandidate::Inv, 17, 18, 84},
}};

struct LiteralHit
{
    std::uint64_t offset = 0;
    std::string_view literal;
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

[[nodiscard]] asset::DecodeError MapParseError(const asset::PopTerrainParseError& error)
{
    asset::DecodeErrorCode code = asset::DecodeErrorCode::Malformed;
    switch (error.code)
    {
    case asset::PopTerrainParseErrorCode::Truncated:
        code = asset::DecodeErrorCode::Truncated;
        break;
    case asset::PopTerrainParseErrorCode::Malformed:
        code = asset::DecodeErrorCode::Malformed;
        break;
    case asset::PopTerrainParseErrorCode::Overflow:
        code = asset::DecodeErrorCode::Overflow;
        break;
    case asset::PopTerrainParseErrorCode::LimitExceeded:
        code = asset::DecodeErrorCode::LimitExceeded;
        break;
    }
    return Error(code, error.message, error.byte_offset);
}

[[nodiscard]] std::uint32_t ReadU32(
    const std::span<const std::byte> bytes, const std::size_t offset) noexcept
{
    return std::to_integer<std::uint32_t>(bytes[offset]) |
           (std::to_integer<std::uint32_t>(bytes[offset + 1]) << 8U) |
           (std::to_integer<std::uint32_t>(bytes[offset + 2]) << 16U) |
           (std::to_integer<std::uint32_t>(bytes[offset + 3]) << 24U);
}

[[nodiscard]] bool HasLiteral(const std::span<const std::byte> bytes,
    const std::size_t offset, const std::string_view literal) noexcept
{
    if (offset > bytes.size() || literal.size() > bytes.size() - offset)
        return false;
    for (std::size_t index = 0; index < literal.size(); ++index)
    {
        if (bytes[offset + index] != static_cast<std::byte>(literal[index]))
            return false;
    }
    return true;
}

[[nodiscard]] bool HasTerrainPrefix(const std::span<const std::byte> bytes) noexcept
{
    return bytes.size() >= 16U && ReadU32(bytes, 0) == 70U &&
           HasLiteral(bytes, 4, "TER:");
}

[[nodiscard]] bool Multiply(
    const std::uint64_t left, const std::uint64_t right, std::uint64_t& result) noexcept
{
    if (left != 0 && right > std::numeric_limits<std::uint64_t>::max() / left)
        return false;
    result = left * right;
    return true;
}

[[nodiscard]] bool Add(
    const std::uint64_t left, const std::uint64_t right, std::uint64_t& result) noexcept
{
    if (right > std::numeric_limits<std::uint64_t>::max() - left)
        return false;
    result = left + right;
    return true;
}

[[nodiscard]] asset::DecodeResult<std::uint64_t> PreflightTerrainScratch(
    const std::span<const std::byte> bytes, const asset::DecodeLimits limits)
{
    if (!HasTerrainPrefix(bytes))
        return limits.maximum_scratch_bytes;

    const std::uint64_t declared_records = ReadU32(bytes, 8);
    const auto parser_defaults = asset::PopTerrainParseLimits{};
    if (declared_records > parser_defaults.maximum_records)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
            "POP terrain record count exceeds passive-inspector limit", 8));
    }

    constexpr std::uint64_t record_overhead =
        sizeof(asset::PopTerrainRecord) + 2U * sizeof(void*);
    std::uint64_t fixed_scratch_bytes = 0;
    if (!Multiply(declared_records, record_overhead, fixed_scratch_bytes))
    {
        return std::unexpected(Error(
            asset::DecodeErrorCode::Overflow, "POP parser scratch size overflows", 8));
    }
    if (fixed_scratch_bytes > limits.maximum_scratch_bytes)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
            "POP parser records exceed passive-inspector scratch limit", 8));
    }
    return limits.maximum_scratch_bytes - fixed_scratch_bytes;
}

[[nodiscard]] asset::DecodeResult<std::array<LiteralHit, 19>> ScanLiteralEnvelope(
    const std::span<const std::byte> bytes, const std::uint64_t gob_offset)
{
    std::array<LiteralHit, kExpectedLiteralOrder.size()> hits{};
    std::size_t hit_count = 1;
    hits[0] = LiteralHit{.offset = gob_offset, .literal = kExpectedLiteralOrder[0]};

    const auto gob_offset_size = static_cast<std::size_t>(gob_offset);
    std::size_t cursor = gob_offset_size + 4U;
    while (cursor < bytes.size())
    {
        for (const std::string_view literal : kPublishedLiteralTags)
        {
            if (!HasLiteral(bytes, cursor, literal))
                continue;
            if (hit_count == hits.size())
            {
                return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
                    "POP contains an extra aligned published literal", cursor));
            }
            hits[hit_count++] = LiteralHit{
                .offset = cursor,
                .literal = literal,
            };
            break;
        }

        if (cursor > std::numeric_limits<std::size_t>::max() - 4U)
            break;
        cursor += 4U;
    }

    if (hit_count != kExpectedLiteralOrder.size())
    {
        return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
            "POP aligned published-literal envelope is incomplete", gob_offset));
    }
    for (std::size_t ordinal = 0; ordinal < hits.size(); ++ordinal)
    {
        if (hits[ordinal].literal != kExpectedLiteralOrder[ordinal])
        {
            return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
                "POP aligned published-literal order is outside the observed family",
                hits[ordinal].offset));
        }
    }
    return hits;
}
} // namespace

asset::DecodeResult<PopPostTerrainHypothesisDescriptor> InspectPopPostTerrainHypotheses(
    const std::span<const std::byte> bytes, const asset::DecodeLimits limits)
{
    if (bytes.size() > limits.maximum_input_bytes)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
            "POP input exceeds passive-inspector byte limit"));
    }
    if (limits.maximum_items < 1U)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
            "POP hypothesis descriptor exceeds passive-inspector item limit"));
    }
    if (limits.maximum_output_bytes < sizeof(PopPostTerrainHypothesisDescriptor))
    {
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
            "POP hypothesis descriptor exceeds passive-inspector output limit"));
    }

    auto parser_name_scratch_bytes = PreflightTerrainScratch(bytes, limits);
    if (!parser_name_scratch_bytes)
        return std::unexpected(parser_name_scratch_bytes.error());

    std::uint64_t gob_offset = 0;
    {
        const auto parser_defaults = asset::PopTerrainParseLimits{};
        auto terrain = asset::PopTerrainIndex::Parse(bytes, asset::PopTerrainParseLimits{
            .maximum_records = parser_defaults.maximum_records,
            .maximum_name_bytes = limits.maximum_string_bytes,
            .maximum_owned_name_bytes = static_cast<std::size_t>(
                std::min<std::uint64_t>(*parser_name_scratch_bytes,
                    std::numeric_limits<std::size_t>::max())),
        });
        if (!terrain)
            return std::unexpected(MapParseError(terrain.error()));
        gob_offset = terrain->next_section_offset();
    }

    auto hits = ScanLiteralEnvelope(bytes, gob_offset);
    if (!hits)
        return std::unexpected(hits.error());

    PopPostTerrainHypothesisDescriptor descriptor{
        .observed_aligned_literal_count =
            static_cast<std::uint32_t>(kExpectedLiteralOrder.size()),
    };
    for (std::size_t index = 0; index < kCandidateFormulas.size(); ++index)
    {
        const CandidateFormula formula = kCandidateFormulas[index];
        const LiteralHit marker = (*hits)[formula.marker_ordinal];
        const LiteralHit next_marker = (*hits)[formula.next_marker_ordinal];

        std::uint64_t word_offset = 0;
        std::uint64_t opaque_offset = 0;
        if (!Add(marker.offset, 4U, word_offset) || !Add(marker.offset, 8U, opaque_offset) ||
            word_offset > bytes.size() || bytes.size() - word_offset < 4U)
        {
            return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
                "POP arithmetic candidate header is outside the input", marker.offset));
        }

        const std::uint32_t observed_word =
            ReadU32(bytes, static_cast<std::size_t>(word_offset));
        std::uint64_t opaque_size = 0;
        std::uint64_t arithmetic_end = 0;
        if (!Multiply(observed_word, formula.arithmetic_stride_bytes, opaque_size) ||
            !Add(opaque_offset, opaque_size, arithmetic_end))
        {
            return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
                "POP arithmetic candidate extent overflows", word_offset));
        }
        if (arithmetic_end != next_marker.offset)
        {
            return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
                "POP arithmetic candidate extent does not reach its next literal",
                word_offset));
        }

        descriptor.guarded_extents[index] = PopPostTerrainCandidateExtent{
            .candidate = formula.candidate,
            .observed_word_at_plus_4 = observed_word,
            .arithmetic_stride_bytes = formula.arithmetic_stride_bytes,
            .opaque_region = ObservedByteRange{
                .offset = opaque_offset,
                .size = opaque_size,
            },
        };
    }
    return descriptor;
}
} // namespace omega::retail
