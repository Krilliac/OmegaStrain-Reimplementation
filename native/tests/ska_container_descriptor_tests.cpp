#include "omega/retail/ska_container_descriptor.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
struct SkaSpec
{
    std::uint32_t word_0x04 = 1;
    std::uint32_t word_0x08 = 88;
    std::uint32_t word_0x10 = 1;
    std::uint32_t version = 3;
};

void WriteU32(
    std::vector<std::byte>& bytes, const std::size_t offset, const std::uint32_t value)
{
    for (unsigned shift = 0; shift < 32; shift += 8)
        bytes[offset + shift / 8U] = static_cast<std::byte>((value >> shift) & 0xFFU);
}

[[nodiscard]] std::uint64_t LogicalBytes(const SkaSpec spec)
{
    return 112U + 4ULL * spec.word_0x08 *
        (static_cast<std::uint64_t>(spec.word_0x04) + (spec.word_0x10 == 0 ? 1U : 0U));
}

[[nodiscard]] std::vector<std::byte> MakeSka(
    const SkaSpec spec = {}, const std::size_t zero_tail_bytes = 0)
{
    const auto logical_bytes = static_cast<std::size_t>(LogicalBytes(spec));
    std::vector<std::byte> bytes(logical_bytes + zero_tail_bytes, std::byte{0});
    WriteU32(bytes, 0, spec.version);
    WriteU32(bytes, 4, spec.word_0x04);
    WriteU32(bytes, 8, spec.word_0x08);
    WriteU32(bytes, 16, spec.word_0x10);
    for (std::size_t offset = 112; offset < logical_bytes; ++offset)
        bytes[offset] = static_cast<std::byte>((offset * 29U + 7U) & 0xFFU);
    return bytes;
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

template <typename Result>
void CheckErrorAt(const Result& result, const omega::asset::DecodeErrorCode code,
    const std::uint64_t byte_offset, const std::string_view message)
{
    Check(!result && result.error().code == code &&
              result.error().byte_offset == byte_offset,
        message);
}

void CheckUnsupportedWord(
    const std::size_t offset, const std::uint32_t value, const std::string_view message)
{
    auto bytes = MakeSka();
    WriteU32(bytes, offset, value);
    CheckErrorAt(omega::retail::InspectSkaContainer(bytes),
        omega::asset::DecodeErrorCode::UnsupportedVariant, offset, message);
}
} // namespace

int SkaContainerDescriptorFailureCount()
{
    static_assert(std::is_trivially_copyable_v<omega::retail::SkaContainerDescriptor>);
    static_assert(std::is_standard_layout_v<omega::retail::SkaContainerDescriptor>);

    const auto exact_bytes = MakeSka();
    const auto exact = omega::retail::InspectSkaContainer(exact_bytes);
    const auto exact_repeat = omega::retail::InspectSkaContainer(exact_bytes);
    Check(exact.has_value(), "minimum-envelope SKA structure is accepted");
    Check(exact && exact_repeat && *exact == *exact_repeat,
        "SKA exact-extent classification is deterministic");
    if (exact)
    {
        Check(exact->format_version == 3 && exact->observed_word_0x04 == 1 &&
                  exact->observed_word_0x08 == 88 && exact->observed_word_0x10 == 1,
            "SKA publishes only the observed version and counted-word inputs");
        Check(exact->logical_extent.observed_bytes == 464U &&
                  exact->logical_extent.input_bytes == 464U &&
                  exact->logical_extent.relation ==
                      omega::retail::ObservedExtentRelation::Exact,
            "SKA publishes the exact computed logical extent");
    }

    auto zero_tail_bytes = MakeSka({}, 16U);
    const auto zero_tail = omega::retail::InspectSkaContainer(zero_tail_bytes);
    const auto zero_tail_repeat = omega::retail::InspectSkaContainer(zero_tail_bytes);
    Check(zero_tail && zero_tail->logical_extent.observed_bytes == exact_bytes.size() &&
              zero_tail->logical_extent.input_bytes == zero_tail_bytes.size() &&
              zero_tail->logical_extent.relation ==
                  omega::retail::ObservedExtentRelation::ZeroPaddedTail,
        "SKA accepts and classifies an aligned all-zero trailing region");
    Check(zero_tail && zero_tail_repeat && *zero_tail == *zero_tail_repeat,
        "SKA zero-padded-tail classification is deterministic");

    const auto maximum_zero_tail = omega::retail::InspectSkaContainer(MakeSka({}, 2000U));
    Check(maximum_zero_tail && maximum_zero_tail->logical_extent.input_bytes == 2464U &&
              maximum_zero_tail->logical_extent.relation ==
                  omega::retail::ObservedExtentRelation::ZeroPaddedTail,
        "SKA accepts the observed maximum 2000-byte all-zero tail");

    for (const std::size_t dirty_offset :
        {exact_bytes.size(), exact_bytes.size() + 8U, zero_tail_bytes.size() - 1U})
    {
        auto dirty_tail = zero_tail_bytes;
        dirty_tail[dirty_offset] = std::byte{1};
        const auto first = omega::retail::InspectSkaContainer(dirty_tail);
        const auto second = omega::retail::InspectSkaContainer(dirty_tail);
        Check(first && second && *first == *second &&
                  first->logical_extent.observed_bytes == exact_bytes.size() &&
                  first->logical_extent.input_bytes == dirty_tail.size() &&
                  first->logical_extent.relation ==
                      omega::retail::ObservedExtentRelation::NonzeroTail,
            "SKA deterministically classifies a nonzero trailing region without interpreting it");
    }

    auto misaligned_span = exact_bytes;
    misaligned_span.push_back(std::byte{0});
    CheckError(omega::retail::InspectSkaContainer(misaligned_span),
        omega::asset::DecodeErrorCode::Malformed,
        "SKA rejects a complete physical span that is not 16-byte aligned");

    bool all_fixed_header_prefixes_truncated = true;
    for (std::size_t size = 0; size < 112U; ++size)
    {
        const auto result = omega::retail::InspectSkaContainer(
            std::span<const std::byte>(exact_bytes.data(), size));
        all_fixed_header_prefixes_truncated = all_fixed_header_prefixes_truncated && !result &&
            result.error().code == omega::asset::DecodeErrorCode::Truncated;
    }
    Check(all_fixed_header_prefixes_truncated,
        "every truncated SKA fixed-header prefix is classified as truncated before alignment");

    for (const std::size_t input_bytes :
        std::array<std::size_t, 2>{112U, exact_bytes.size() - 16U})
    {
        const auto result = omega::retail::InspectSkaContainer(
            std::span<const std::byte>(exact_bytes.data(), input_bytes));
        const auto repeat = omega::retail::InspectSkaContainer(
            std::span<const std::byte>(exact_bytes.data(), input_bytes));
        Check(result && repeat && *result == *repeat &&
                  result->logical_extent.observed_bytes == exact_bytes.size() &&
                  result->logical_extent.input_bytes == input_bytes &&
                  result->logical_extent.relation ==
                      omega::retail::ObservedExtentRelation::ExceedsInput,
            "SKA deterministically classifies an aligned counted extent beyond the input");
    }

    CheckError(omega::retail::InspectSkaContainer(std::span<const std::byte>(
                   exact_bytes.data(), 113U)),
        omega::asset::DecodeErrorCode::Malformed,
        "SKA preserves physical-span alignment before reporting an extent relation");

    const std::array<SkaSpec, 5> observed_pairs{{
        {.word_0x04 = 1, .word_0x08 = 56, .word_0x10 = 0},
        {.word_0x04 = 1, .word_0x08 = 88, .word_0x10 = 0},
        {.word_0x04 = 1, .word_0x08 = 88, .word_0x10 = 1},
        {.word_0x04 = 1, .word_0x08 = 92, .word_0x10 = 0},
        {.word_0x04 = 1, .word_0x08 = 92, .word_0x10 = 1},
    }};
    bool all_observed_pairs_accepted = true;
    for (const SkaSpec spec : observed_pairs)
    {
        const auto bytes = MakeSka(spec);
        const auto descriptor = omega::retail::InspectSkaContainer(bytes);
        all_observed_pairs_accepted = all_observed_pairs_accepted && descriptor &&
            descriptor->logical_extent.observed_bytes == LogicalBytes(spec);
    }
    Check(all_observed_pairs_accepted,
        "SKA accepts every observed 0x08/0x10 pair and both extent branches");

    const SkaSpec maximum_spec{
        .word_0x04 = 357,
        .word_0x08 = 88,
        .word_0x10 = 1,
    };
    const auto maximum = omega::retail::InspectSkaContainer(MakeSka(maximum_spec));
    Check(maximum && maximum->logical_extent.observed_bytes == 125'776U,
        "SKA accepts the observed maximum computed logical extent");

    for (const std::uint32_t version : {2U, 4U})
        CheckUnsupportedWord(0, version,
            "SKA rejects versions outside the observed version-three family");
    for (const std::uint32_t word_0x04 : {0U, 358U, 0xFFFFFFFFU})
        CheckUnsupportedWord(4, word_0x04,
            "SKA rejects word 0x04 outside the observed range");
    for (const std::uint32_t word_0x08 : {0U, 55U, 57U, 87U, 89U, 91U, 93U})
        CheckUnsupportedWord(8, word_0x08,
            "SKA rejects word 0x08 outside the observed discrete family");
    for (const std::uint32_t word_0x10 : {2U, 0xFFFFFFFFU})
        CheckUnsupportedWord(16, word_0x10,
            "SKA rejects word 0x10 outside the observed zero-or-one family");

    CheckErrorAt(omega::retail::InspectSkaContainer(MakeSka(
                     {.word_0x04 = 1, .word_0x08 = 56, .word_0x10 = 1})),
        omega::asset::DecodeErrorCode::UnsupportedVariant, 8,
        "SKA rejects the unobserved 56/1 word pair");
    CheckErrorAt(omega::retail::InspectSkaContainer(MakeSka(
                     {.word_0x04 = 357, .word_0x08 = 92, .word_0x10 = 1})),
        omega::asset::DecodeErrorCode::UnsupportedVariant, 4,
        "SKA rejects a marginally valid combination beyond the observed logical range");

    auto invalid_version_short_input = exact_bytes;
    WriteU32(invalid_version_short_input, 0, 2);
    CheckErrorAt(omega::retail::InspectSkaContainer(std::span<const std::byte>(
                     invalid_version_short_input.data(), 112U)),
        omega::asset::DecodeErrorCode::UnsupportedVariant, 0,
        "SKA validates the version before classifying the counted extent relation");

    const auto owned = [&]() {
        auto transient_source = MakeSka();
        auto decoded = omega::retail::InspectSkaContainer(transient_source);
        Check(decoded.has_value(), "transient SKA source decodes before ownership check");
        auto descriptor =
            decoded ? std::move(*decoded) : omega::retail::SkaContainerDescriptor{};
        transient_source.assign(transient_source.size(), std::byte{0xFF});
        transient_source.clear();
        transient_source.shrink_to_fit();
        return descriptor;
    }();
    Check(exact && owned == *exact,
        "SKA fixed descriptor remains valid after source replacement and destruction");

    auto opaque_header = exact_bytes;
    std::fill(opaque_header.begin() + 12, opaque_header.begin() + 16, std::byte{0xA5});
    std::fill(opaque_header.begin() + 20, opaque_header.begin() + 112, std::byte{0x5A});
    const auto opaque_header_result = omega::retail::InspectSkaContainer(opaque_header);
    Check(exact && opaque_header_result && *opaque_header_result == *exact,
        "SKA ignores unproven words and bytes in the 112-byte prefix");

    auto opaque_payload = exact_bytes;
    std::fill(opaque_payload.begin() + 112, opaque_payload.end(), std::byte{0x3C});
    const auto opaque_payload_result = omega::retail::InspectSkaContainer(opaque_payload);
    Check(exact && opaque_payload_result && *opaque_payload_result == *exact,
        "SKA descriptor remains independent of counted-region payload bytes");

    std::vector<std::byte> unaligned_storage(exact_bytes.size() + 1U, std::byte{0});
    std::copy(exact_bytes.begin(), exact_bytes.end(), unaligned_storage.begin() + 1);
    const auto unaligned_backing = omega::retail::InspectSkaContainer(
        std::span<const std::byte>(unaligned_storage.data() + 1, exact_bytes.size()));
    Check(exact && unaligned_backing && *unaligned_backing == *exact,
        "SKA requires physical-span length alignment, not backing-address alignment");

    auto limits = omega::asset::DecodeLimits{};
    limits.maximum_input_bytes = exact_bytes.size();
    Check(omega::retail::InspectSkaContainer(exact_bytes, limits).has_value(),
        "SKA succeeds at the exact input-byte budget");
    limits.maximum_input_bytes = exact_bytes.size() - 1U;
    CheckError(omega::retail::InspectSkaContainer(exact_bytes, limits),
        omega::asset::DecodeErrorCode::LimitExceeded,
        "SKA rejects one byte below the required input-byte budget");

    limits = omega::asset::DecodeLimits{};
    limits.maximum_items = 1;
    Check(omega::retail::InspectSkaContainer(exact_bytes, limits).has_value(),
        "SKA succeeds at the exact one-descriptor item budget");
    limits.maximum_items = 0;
    CheckError(omega::retail::InspectSkaContainer(exact_bytes, limits),
        omega::asset::DecodeErrorCode::LimitExceeded,
        "SKA rejects one item below the fixed descriptor item budget");

    limits = omega::asset::DecodeLimits{};
    limits.maximum_output_bytes = sizeof(omega::retail::SkaContainerDescriptor);
    Check(omega::retail::InspectSkaContainer(exact_bytes, limits).has_value(),
        "SKA succeeds at the exact fixed-descriptor output budget");
    limits.maximum_output_bytes = sizeof(omega::retail::SkaContainerDescriptor) - 1U;
    CheckError(omega::retail::InspectSkaContainer(exact_bytes, limits),
        omega::asset::DecodeErrorCode::LimitExceeded,
        "SKA rejects one byte below the fixed-descriptor output budget");

    limits = omega::asset::DecodeLimits{};
    limits.maximum_scratch_bytes = 0;
    limits.maximum_nesting_depth = 0;
    Check(omega::retail::InspectSkaContainer(exact_bytes, limits).has_value(),
        "SKA uses zero scratch and treats its descriptor root as nesting depth zero");

    limits = omega::asset::DecodeLimits{};
    limits.maximum_items = 0;
    limits.maximum_output_bytes = 0;
    CheckError(omega::retail::InspectSkaContainer(std::span<const std::byte>{}, limits),
        omega::asset::DecodeErrorCode::LimitExceeded,
        "SKA enforces fixed descriptor limits before diagnosing truncation");

    return failures;
}
