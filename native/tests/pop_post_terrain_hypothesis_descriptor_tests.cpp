#include "omega/retail/pop_post_terrain_hypothesis_descriptor.h"

#include "omega/asset/pop_terrain_index.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
constexpr std::array<std::string_view, 19> kLiteralOrder{
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

struct CandidateSpec
{
    omega::retail::PopPostTerrainCandidate candidate;
    std::size_t marker_ordinal;
    std::uint32_t stride;
};

constexpr std::array<CandidateSpec, 5> kCandidateSpecs{{
    {omega::retail::PopPostTerrainCandidate::Inl, 3, 36},
    {omega::retail::PopPostTerrainCandidate::Pnt, 8, 88},
    {omega::retail::PopPostTerrainCandidate::Dir, 9, 44},
    {omega::retail::PopPostTerrainCandidate::Env, 10, 76},
    {omega::retail::PopPostTerrainCandidate::Inv, 17, 84},
}};

constexpr std::array<std::uint32_t, 5> kCandidateWords{2, 0, 1, 3, 0};
constexpr std::string_view kFirstTerrainName = "CELL_A.HOG";
constexpr std::string_view kSecondTerrainName = "B";

struct PopFixture
{
    std::vector<std::byte> bytes;
    std::array<std::size_t, kLiteralOrder.size()> marker_offsets{};
};

void AppendU32(std::vector<std::byte>& bytes, const std::uint32_t value)
{
    for (unsigned shift = 0; shift < 32; shift += 8)
        bytes.push_back(static_cast<std::byte>((value >> shift) & 0xFFU));
}

void WriteU32(
    std::vector<std::byte>& bytes, const std::size_t offset, const std::uint32_t value)
{
    for (unsigned shift = 0; shift < 32; shift += 8)
        bytes[offset + shift / 8U] = static_cast<std::byte>((value >> shift) & 0xFFU);
}

void AppendText(std::vector<std::byte>& bytes, const std::string_view value)
{
    for (const char character : value)
        bytes.push_back(static_cast<std::byte>(character));
}

void WriteText(
    std::vector<std::byte>& bytes, const std::size_t offset, const std::string_view value)
{
    for (std::size_t index = 0; index < value.size(); ++index)
        bytes[offset + index] = static_cast<std::byte>(value[index]);
}

void AppendTerrainRecord(std::vector<std::byte>& bytes, const std::uint32_t kind,
    const std::uint32_t index, const std::string_view name)
{
    AppendU32(bytes, kind);
    AppendU32(bytes, index);
    AppendText(bytes, name);
    bytes.push_back(std::byte{0});
    while (bytes.size() % 4U != 0)
        bytes.push_back(std::byte{0x5A});
}

[[nodiscard]] PopFixture MakePop()
{
    PopFixture fixture;
    AppendU32(fixture.bytes, 70);
    AppendText(fixture.bytes, "TER:");
    AppendU32(fixture.bytes, 2);
    AppendTerrainRecord(fixture.bytes, 4, 10, kFirstTerrainName);
    AppendTerrainRecord(fixture.bytes, 9, 22, kSecondTerrainName);

    for (std::size_t ordinal = 0; ordinal < kLiteralOrder.size(); ++ordinal)
    {
        fixture.marker_offsets[ordinal] = fixture.bytes.size();
        AppendText(fixture.bytes, kLiteralOrder[ordinal]);
        for (std::size_t candidate_index = 0;
             candidate_index < kCandidateSpecs.size(); ++candidate_index)
        {
            if (kCandidateSpecs[candidate_index].marker_ordinal != ordinal)
                continue;
            AppendU32(fixture.bytes, kCandidateWords[candidate_index]);
            const std::size_t opaque_bytes =
                static_cast<std::size_t>(kCandidateWords[candidate_index]) *
                kCandidateSpecs[candidate_index].stride;
            fixture.bytes.insert(
                fixture.bytes.end(), opaque_bytes,
                static_cast<std::byte>(0x80U + candidate_index));
            break;
        }
    }
    fixture.bytes.insert(fixture.bytes.end(), 8, std::byte{0xCC});
    return fixture;
}

[[nodiscard]] omega::retail::PopPostTerrainHypothesisDescriptor ExpectedDescriptor(
    const PopFixture& fixture)
{
    omega::retail::PopPostTerrainHypothesisDescriptor expected{
        .observed_aligned_literal_count = 19,
    };
    for (std::size_t index = 0; index < kCandidateSpecs.size(); ++index)
    {
        const CandidateSpec spec = kCandidateSpecs[index];
        expected.guarded_extents[index] = omega::retail::PopPostTerrainCandidateExtent{
            .candidate = spec.candidate,
            .observed_word_at_plus_4 = kCandidateWords[index],
            .arithmetic_stride_bytes = spec.stride,
            .opaque_region = omega::retail::ObservedByteRange{
                .offset = fixture.marker_offsets[spec.marker_ordinal] + 8U,
                .size = static_cast<std::uint64_t>(kCandidateWords[index]) * spec.stride,
            },
        };
    }
    return expected;
}

int failures = 0;

void Check(const bool condition, const std::string_view message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

template <typename Result>
void CheckError(
    const Result& result, const omega::asset::DecodeErrorCode code,
    const std::string_view message)
{
    Check(!result && result.error().code == code, message);
}
} // namespace

int PopPostTerrainHypothesisDescriptorFailureCount()
{
    using omega::retail::PopPostTerrainCandidateExtent;
    using omega::retail::PopPostTerrainHypothesisDescriptor;

    static_assert(std::is_trivially_copyable_v<PopPostTerrainCandidateExtent>);
    static_assert(std::is_standard_layout_v<PopPostTerrainCandidateExtent>);
    static_assert(std::is_trivially_copyable_v<PopPostTerrainHypothesisDescriptor>);
    static_assert(std::is_standard_layout_v<PopPostTerrainHypothesisDescriptor>);
    static_assert(std::is_same_v<decltype(PopPostTerrainHypothesisDescriptor{}.guarded_extents),
        std::array<PopPostTerrainCandidateExtent, 5>>);

    const auto fixture = MakePop();
    const auto expected = ExpectedDescriptor(fixture);
    const auto inspected =
        omega::retail::InspectPopPostTerrainHypotheses(fixture.bytes);
    Check(inspected.has_value(),
        "exact ordered POP literal and arithmetic hypothesis envelope is accepted");
    Check(inspected && *inspected == expected,
        "descriptor publishes only the five guarded opaque arithmetic extents");
    if (inspected)
    {
        Check(inspected->guarded_extents[1].observed_word_at_plus_4 == 0 &&
                  inspected->guarded_extents[1].opaque_region.size == 0 &&
                  inspected->guarded_extents[4].observed_word_at_plus_4 == 0 &&
                  inspected->guarded_extents[4].opaque_region.size == 0,
            "zero words are retained only with their exactly empty opaque extents");
    }

    auto mutated_opaque = fixture.bytes;
    for (const auto& extent : expected.guarded_extents)
    {
        const auto begin = static_cast<std::size_t>(extent.opaque_region.offset);
        const auto end = begin + static_cast<std::size_t>(extent.opaque_region.size);
        std::fill(mutated_opaque.begin() + begin, mutated_opaque.begin() + end,
            std::byte{0x3C});
    }
    std::fill(mutated_opaque.end() - 8, mutated_opaque.end(), std::byte{0x69});
    const auto opaque_result =
        omega::retail::InspectPopPostTerrainHypotheses(mutated_opaque);
    Check(opaque_result && *opaque_result == expected,
        "opaque and trailing byte mutations do not change fixed hypothesis metadata");

    const auto owned = [&]() {
        auto transient = MakePop();
        auto result = omega::retail::InspectPopPostTerrainHypotheses(transient.bytes);
        Check(result.has_value(), "transient POP input inspects before ownership check");
        auto descriptor = result ? std::move(*result) : PopPostTerrainHypothesisDescriptor{};
        transient.bytes.assign(transient.bytes.size(), std::byte{0xFF});
        transient.bytes.clear();
        transient.bytes.shrink_to_fit();
        return descriptor;
    }();
    Check(owned == expected,
        "fixed descriptor remains valid after replacement and destruction of source bytes");

    auto nonzero_mismatch = MakePop();
    WriteU32(nonzero_mismatch.bytes, nonzero_mismatch.marker_offsets[3] + 4U, 1);
    CheckError(omega::retail::InspectPopPostTerrainHypotheses(nonzero_mismatch.bytes),
        omega::asset::DecodeErrorCode::UnsupportedVariant,
        "nonzero word whose arithmetic extent misses the next literal is unsupported");

    auto zero_nonempty = MakePop();
    zero_nonempty.bytes.insert(
        zero_nonempty.bytes.begin() + zero_nonempty.marker_offsets[9], 4,
        std::byte{0x42});
    CheckError(omega::retail::InspectPopPostTerrainHypotheses(zero_nonempty.bytes),
        omega::asset::DecodeErrorCode::UnsupportedVariant,
        "zero word with a nonempty opaque extent is unsupported");

    auto missing = MakePop();
    missing.bytes[missing.marker_offsets[1]] = std::byte{'X'};
    CheckError(omega::retail::InspectPopPostTerrainHypotheses(missing.bytes),
        omega::asset::DecodeErrorCode::UnsupportedVariant,
        "missing aligned published literal is unsupported");

    auto duplicate = MakePop();
    WriteText(duplicate.bytes, duplicate.marker_offsets[2], "SND:");
    CheckError(omega::retail::InspectPopPostTerrainHypotheses(duplicate.bytes),
        omega::asset::DecodeErrorCode::UnsupportedVariant,
        "duplicate aligned published literal is unsupported");

    auto reordered = MakePop();
    for (std::size_t index = 0; index < 4; ++index)
    {
        std::swap(reordered.bytes[reordered.marker_offsets[1] + index],
            reordered.bytes[reordered.marker_offsets[2] + index]);
    }
    CheckError(omega::retail::InspectPopPostTerrainHypotheses(reordered.bytes),
        omega::asset::DecodeErrorCode::UnsupportedVariant,
        "reordered aligned published literals are unsupported");

    auto extra_published = MakePop();
    AppendText(extra_published.bytes, "ANPC:");
    CheckError(omega::retail::InspectPopPostTerrainHypotheses(extra_published.bytes),
        omega::asset::DecodeErrorCode::UnsupportedVariant,
        "extra aligned five-byte published literal is unsupported");

    auto unaligned_published = MakePop();
    unaligned_published.bytes.push_back(std::byte{0x11});
    AppendText(unaligned_published.bytes, "ANPC:");
    const auto unaligned_result =
        omega::retail::InspectPopPostTerrainHypotheses(unaligned_published.bytes);
    Check(unaligned_result && *unaligned_result == expected,
        "unaligned marker-shaped bytes remain outside the literal envelope");

    auto bad_header = MakePop();
    bad_header.bytes[0] = std::byte{69};
    const auto bad_header_result =
        omega::retail::InspectPopPostTerrainHypotheses(bad_header.bytes);
    CheckError(bad_header_result,
        omega::asset::DecodeErrorCode::Malformed,
        "typed terrain-prefix malformed errors cross the passive-inspector boundary");
    Check(!bad_header_result && bad_header_result.error().byte_offset &&
              *bad_header_result.error().byte_offset == 0U,
        "mapped terrain-parser errors preserve their source byte offset");

    auto excessive_records = MakePop();
    WriteU32(excessive_records.bytes, 8,
        omega::asset::PopTerrainParseLimits{}.maximum_records + 1U);
    const auto excessive_records_result =
        omega::retail::InspectPopPostTerrainHypotheses(excessive_records.bytes);
    Check(!excessive_records_result &&
              excessive_records_result.error().code ==
                  omega::asset::DecodeErrorCode::LimitExceeded &&
              excessive_records_result.error().byte_offset &&
              *excessive_records_result.error().byte_offset == 8U,
        "declared terrain-record limit preserves the typed error and count offset");

    CheckError(omega::retail::InspectPopPostTerrainHypotheses(
                   std::span<const std::byte>(fixture.bytes).first(8)),
        omega::asset::DecodeErrorCode::Truncated,
        "typed terrain-prefix truncation errors cross the passive-inspector boundary");

    auto bad_gob = MakePop();
    bad_gob.bytes[bad_gob.marker_offsets[0]] = std::byte{'X'};
    CheckError(omega::retail::InspectPopPostTerrainHypotheses(bad_gob.bytes),
        omega::asset::DecodeErrorCode::Malformed,
        "the existing typed parser remains authoritative for the TER-to-GOB boundary");

    constexpr std::uint64_t terrain_record_count = 2;
    constexpr std::uint64_t terrain_name_bytes =
        kFirstTerrainName.size() + kSecondTerrainName.size();
    constexpr std::uint64_t terrain_name_maximum = kFirstTerrainName.size();
    constexpr std::uint64_t parser_record_overhead =
        sizeof(omega::asset::PopTerrainRecord) + 2U * sizeof(void*);
    constexpr std::uint64_t exact_scratch_bytes =
        terrain_record_count * parser_record_overhead + terrain_name_bytes;

    auto exact_limits = omega::asset::DecodeLimits{};
    exact_limits.maximum_input_bytes = fixture.bytes.size();
    exact_limits.maximum_items = 1;
    exact_limits.maximum_output_bytes = sizeof(PopPostTerrainHypothesisDescriptor);
    exact_limits.maximum_string_bytes = static_cast<std::uint32_t>(terrain_name_maximum);
    exact_limits.maximum_scratch_bytes = exact_scratch_bytes;
    exact_limits.maximum_nesting_depth = 0;
    Check(omega::retail::InspectPopPostTerrainHypotheses(fixture.bytes, exact_limits)
              .has_value(),
        "exact input, item, output, string, and conservative scratch budgets succeed");

    auto one_below = exact_limits;
    --one_below.maximum_input_bytes;
    CheckError(omega::retail::InspectPopPostTerrainHypotheses(fixture.bytes, one_below),
        omega::asset::DecodeErrorCode::LimitExceeded,
        "one byte below the complete input budget fails closed");

    one_below = exact_limits;
    --one_below.maximum_items;
    CheckError(omega::retail::InspectPopPostTerrainHypotheses(fixture.bytes, one_below),
        omega::asset::DecodeErrorCode::LimitExceeded,
        "one item below the fixed descriptor budget fails closed");

    one_below = exact_limits;
    --one_below.maximum_output_bytes;
    CheckError(omega::retail::InspectPopPostTerrainHypotheses(fixture.bytes, one_below),
        omega::asset::DecodeErrorCode::LimitExceeded,
        "one byte below the fixed owned-output budget fails closed");

    one_below = exact_limits;
    --one_below.maximum_string_bytes;
    CheckError(omega::retail::InspectPopPostTerrainHypotheses(fixture.bytes, one_below),
        omega::asset::DecodeErrorCode::LimitExceeded,
        "one byte below the longest terrain-name budget fails closed");

    one_below = exact_limits;
    --one_below.maximum_scratch_bytes;
    CheckError(omega::retail::InspectPopPostTerrainHypotheses(fixture.bytes, one_below),
        omega::asset::DecodeErrorCode::LimitExceeded,
        "one byte below the conservative parser-scratch budget fails closed");

    auto private_name_failure = MakePop();
    WriteU32(private_name_failure.bytes, private_name_failure.marker_offsets[3] + 4U, 1);
    const auto private_name_result =
        omega::retail::InspectPopPostTerrainHypotheses(private_name_failure.bytes);
    Check(!private_name_result &&
              private_name_result.error().message.find(kFirstTerrainName) == std::string::npos,
        "post-terrain hypothesis failures disclose no terrain name");

    return failures;
}
