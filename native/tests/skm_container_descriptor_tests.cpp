#include "omega/retail/skm_container_descriptor.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
struct ChunkSpec
{
    std::uint8_t qword_count = 0;
    std::uint8_t secondary_count = 0;
};

[[nodiscard]] std::size_t Align16(const std::size_t value)
{
    return (value + 15U) & ~std::size_t{15U};
}

[[nodiscard]] std::vector<std::byte> MakeSkm(
    const std::vector<ChunkSpec>& chunks, const std::uint8_t version = 3)
{
    const std::size_t header_bytes = Align16(2U + 2U * chunks.size());
    std::size_t logical_bytes = header_bytes;
    for (const auto& [qword_count, secondary_count] : chunks)
    {
        (void)secondary_count;
        logical_bytes += 16U * qword_count;
    }

    std::vector<std::byte> bytes(logical_bytes, std::byte{0});
    bytes[0] = static_cast<std::byte>(chunks.size());
    bytes[1] = static_cast<std::byte>(version);
    for (std::size_t index = 0; index < chunks.size(); ++index)
    {
        bytes[2U + 2U * index] = static_cast<std::byte>(chunks[index].qword_count);
        bytes[3U + 2U * index] = static_cast<std::byte>(chunks[index].secondary_count);
    }
    for (std::size_t offset = header_bytes; offset < bytes.size(); ++offset)
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
} // namespace

int SkmContainerDescriptorFailureCount()
{
    const std::vector<ChunkSpec> two_chunks{{4, 1}, {5, 2}};
    const auto exact_bytes = MakeSkm(two_chunks);
    const auto exact = omega::retail::InspectSkmContainer(exact_bytes);
    Check(exact.has_value(), "two-chunk SKM structure is accepted");
    if (exact)
    {
        Check(exact->format_version == 3 &&
                  exact->chunk_table_region == omega::retail::ObservedByteRange{2, 4} &&
                  exact->aligned_header_region == omega::retail::ObservedByteRange{0, 16},
            "SKM publishes the observed version, chunk table, and aligned header");
        Check(exact->chunks.size() == 2 && exact->chunks[0].qword_count == 4 &&
                  exact->chunks[0].observed_secondary_count == 1 &&
                  exact->chunks[0].payload_region ==
                      omega::retail::ObservedByteRange{16, 64} &&
                  exact->chunks[1].qword_count == 5 &&
                  exact->chunks[1].observed_secondary_count == 2 &&
                  exact->chunks[1].payload_region ==
                      omega::retail::ObservedByteRange{80, 80},
            "SKM chunk descriptors preserve source order and exact payload ranges");
        Check(exact->logical_extent.observed_bytes == 160 &&
                  exact->logical_extent.input_bytes == 160 &&
                  exact->logical_extent.relation ==
                      omega::retail::ObservedExtentRelation::Exact,
            "SKM exact logical extent includes its aligned header and counted payloads");
    }

    auto zero_tail_bytes = exact_bytes;
    zero_tail_bytes.resize(zero_tail_bytes.size() + 16U, std::byte{0});
    const auto zero_tail = omega::retail::InspectSkmContainer(zero_tail_bytes);
    Check(zero_tail && zero_tail->logical_extent.observed_bytes == exact_bytes.size() &&
              zero_tail->logical_extent.input_bytes == zero_tail_bytes.size() &&
              zero_tail->logical_extent.relation ==
                  omega::retail::ObservedExtentRelation::ZeroPaddedTail,
        "SKM accepts and reports an all-zero aligned trailing region");

    auto misaligned_span = exact_bytes;
    misaligned_span.push_back(std::byte{0});
    CheckError(omega::retail::InspectSkmContainer(misaligned_span),
        omega::asset::DecodeErrorCode::Malformed,
        "SKM rejects a complete physical span that is not 16-byte aligned");

    auto dirty_tail = zero_tail_bytes;
    dirty_tail.back() = std::byte{1};
    CheckError(omega::retail::InspectSkmContainer(dirty_tail),
        omega::asset::DecodeErrorCode::Malformed,
        "SKM rejects a nonzero trailing region");

    const auto owned = [&]() {
        auto transient_source = MakeSkm(two_chunks);
        auto decoded = omega::retail::InspectSkmContainer(transient_source);
        Check(decoded.has_value(), "transient SKM source decodes before ownership check");
        auto descriptor = decoded ? std::move(*decoded) : omega::retail::SkmContainerDescriptor{};
        transient_source.assign(transient_source.size(), std::byte{0xFF});
        transient_source.clear();
        transient_source.shrink_to_fit();
        return descriptor;
    }();
    Check(exact && owned == *exact,
        "SKM descriptor owns its chunk metadata after source replacement and destruction");

    auto opaque_header = exact_bytes;
    std::fill(opaque_header.begin() + 6, opaque_header.begin() + 16, std::byte{0xA5});
    const auto opaque_header_result = omega::retail::InspectSkmContainer(opaque_header);
    Check(exact && opaque_header_result && *opaque_header_result == *exact,
        "SKM descriptor ignores opaque bytes in the aligned header after the chunk table");

    auto opaque_payload = exact_bytes;
    std::fill(opaque_payload.begin() + 16, opaque_payload.end(), std::byte{0x3C});
    const auto opaque_payload_result = omega::retail::InspectSkmContainer(opaque_payload);
    Check(exact && opaque_payload_result && *opaque_payload_result == *exact,
        "SKM descriptor remains independent of opaque payload bytes");

    bool all_fixed_prefixes_truncated = true;
    for (std::size_t size = 0; size < 2U; ++size)
    {
        const auto result = omega::retail::InspectSkmContainer(
            std::span<const std::byte>(exact_bytes.data(), size));
        all_fixed_prefixes_truncated = all_fixed_prefixes_truncated &&
                                       !result && result.error().code ==
                                                      omega::asset::DecodeErrorCode::Truncated;
    }
    Check(all_fixed_prefixes_truncated,
        "every truncated SKM fixed-header prefix is classified as truncated");

    bool all_table_prefixes_truncated = true;
    for (std::size_t size = 2U; size < 6U; ++size)
    {
        const auto result = omega::retail::InspectSkmContainer(
            std::span<const std::byte>(exact_bytes.data(), size));
        all_table_prefixes_truncated = all_table_prefixes_truncated &&
                                       !result && result.error().code ==
                                                      omega::asset::DecodeErrorCode::Truncated;
    }
    Check(all_table_prefixes_truncated,
        "every truncated SKM chunk-table prefix is classified as truncated");

    bool all_aligned_header_prefixes_truncated = true;
    for (std::size_t size = 6U; size < 16U; ++size)
    {
        const auto result = omega::retail::InspectSkmContainer(
            std::span<const std::byte>(exact_bytes.data(), size));
        all_aligned_header_prefixes_truncated = all_aligned_header_prefixes_truncated &&
                                                !result && result.error().code ==
                                                               omega::asset::DecodeErrorCode::Truncated;
    }
    Check(all_aligned_header_prefixes_truncated,
        "every truncated SKM aligned-header prefix is classified as truncated");

    bool all_payload_prefixes_truncated = true;
    for (std::size_t size = 16U; size < exact_bytes.size(); ++size)
    {
        const auto result = omega::retail::InspectSkmContainer(
            std::span<const std::byte>(exact_bytes.data(), size));
        all_payload_prefixes_truncated = all_payload_prefixes_truncated &&
                                         !result && result.error().code ==
                                                        omega::asset::DecodeErrorCode::Truncated;
    }
    Check(all_payload_prefixes_truncated,
        "every truncated SKM payload prefix is classified as truncated");

    const auto envelope_edges =
        omega::retail::InspectSkmContainer(MakeSkm({{4, 1}, {55, 30}}));
    Check(envelope_edges && envelope_edges->chunks.front().qword_count == 4 &&
              envelope_edges->chunks.back().qword_count == 55 &&
              envelope_edges->chunks.front().observed_secondary_count == 1 &&
              envelope_edges->chunks.back().observed_secondary_count == 30,
        "SKM accepts the observed qword and secondary-count envelope edges");

    const auto minimum_chunks = omega::retail::InspectSkmContainer(MakeSkm({{4, 1}}));
    Check(minimum_chunks && minimum_chunks->chunks.size() == 1U &&
              minimum_chunks->chunk_table_region ==
                  omega::retail::ObservedByteRange{2, 2} &&
              minimum_chunks->logical_extent.observed_bytes == 80,
        "SKM accepts the observed minimum one-chunk envelope edge");

    for (const std::uint8_t version : {std::uint8_t{2}, std::uint8_t{4}})
    {
        CheckError(omega::retail::InspectSkmContainer(MakeSkm({{4, 1}}, version)),
            omega::asset::DecodeErrorCode::UnsupportedVariant,
            "SKM rejects versions outside the observed version-three family");
    }
    for (const std::size_t chunk_count : {std::size_t{0}, std::size_t{62}})
    {
        CheckError(omega::retail::InspectSkmContainer(
                       MakeSkm(std::vector<ChunkSpec>(chunk_count, ChunkSpec{4, 1}))),
            omega::asset::DecodeErrorCode::UnsupportedVariant,
            "SKM rejects chunk counts outside the observed envelope");
    }
    for (const std::uint8_t qword_count : {std::uint8_t{3}, std::uint8_t{56}})
    {
        CheckError(omega::retail::InspectSkmContainer(MakeSkm({{qword_count, 1}})),
            omega::asset::DecodeErrorCode::UnsupportedVariant,
            "SKM rejects qword counts outside the observed envelope");
    }
    for (const std::uint8_t secondary_count : {std::uint8_t{0}, std::uint8_t{31}})
    {
        CheckError(omega::retail::InspectSkmContainer(MakeSkm({{4, secondary_count}})),
            omega::asset::DecodeErrorCode::UnsupportedVariant,
            "SKM rejects secondary counts outside the observed envelope");
    }

    const std::vector<ChunkSpec> maximum_chunks(61U, ChunkSpec{4, 1});
    const auto maximum = omega::retail::InspectSkmContainer(MakeSkm(maximum_chunks));
    Check(maximum && maximum->chunks.size() == 61U &&
              maximum->chunk_table_region == omega::retail::ObservedByteRange{2, 122} &&
              maximum->aligned_header_region == omega::retail::ObservedByteRange{0, 128} &&
              maximum->chunks.back().payload_region ==
                  omega::retail::ObservedByteRange{3968, 64} &&
              maximum->logical_extent.observed_bytes == 4032,
        "SKM accepts the observed maximum 61 chunks with bounded source-order ranges");

    auto limits = omega::asset::DecodeLimits{};
    limits.maximum_input_bytes = exact_bytes.size();
    Check(omega::retail::InspectSkmContainer(exact_bytes, limits).has_value(),
        "SKM succeeds at the exact input-byte budget");
    limits.maximum_input_bytes = exact_bytes.size() - 1U;
    CheckError(omega::retail::InspectSkmContainer(exact_bytes, limits),
        omega::asset::DecodeErrorCode::LimitExceeded,
        "SKM rejects one byte below the required input-byte budget");

    limits = omega::asset::DecodeLimits{};
    limits.maximum_items = 1U + two_chunks.size();
    Check(omega::retail::InspectSkmContainer(exact_bytes, limits).has_value(),
        "SKM succeeds at the exact root-plus-chunks item budget");
    limits.maximum_items = two_chunks.size();
    CheckError(omega::retail::InspectSkmContainer(exact_bytes, limits),
        omega::asset::DecodeErrorCode::LimitExceeded,
        "SKM rejects one item below the root-plus-chunks item budget");

    const std::uint64_t exact_output_bytes = sizeof(omega::retail::SkmContainerDescriptor) +
                                             two_chunks.size() *
                                                 sizeof(omega::retail::SkmChunkDescriptor);
    limits = omega::asset::DecodeLimits{};
    limits.maximum_output_bytes = exact_output_bytes;
    Check(omega::retail::InspectSkmContainer(exact_bytes, limits).has_value(),
        "SKM succeeds at the exact owned-descriptor output budget");
    limits.maximum_output_bytes = exact_output_bytes - 1U;
    CheckError(omega::retail::InspectSkmContainer(exact_bytes, limits),
        omega::asset::DecodeErrorCode::LimitExceeded,
        "SKM rejects one byte below the owned-descriptor output budget");

    limits = omega::asset::DecodeLimits{};
    limits.maximum_scratch_bytes = 0;
    limits.maximum_nesting_depth = 0;
    Check(omega::retail::InspectSkmContainer(exact_bytes, limits).has_value(),
        "SKM uses zero scratch and treats its descriptor root as nesting depth zero");

    return failures;
}
