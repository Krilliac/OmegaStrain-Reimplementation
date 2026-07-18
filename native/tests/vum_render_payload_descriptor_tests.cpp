#include "omega/retail/vum_render_payload_descriptor.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <vector>

namespace
{
constexpr std::uint32_t kNamesEnd = 116;
constexpr std::uint32_t kMaterialsEnd = 208;
constexpr std::uint32_t kPairCount = 2;

void WriteU32(
    std::vector<std::byte>& bytes, const std::size_t offset, const std::uint32_t value)
{
    for (unsigned shift = 0; shift < 32; shift += 8)
        bytes[offset + shift / 8U] = static_cast<std::byte>((value >> shift) & 0xFFU);
}

std::uint32_t ReadU32(const std::vector<std::byte>& bytes, const std::size_t offset)
{
    return std::to_integer<std::uint32_t>(bytes[offset]) |
           (std::to_integer<std::uint32_t>(bytes[offset + 1U]) << 8U) |
           (std::to_integer<std::uint32_t>(bytes[offset + 2U]) << 16U) |
           (std::to_integer<std::uint32_t>(bytes[offset + 3U]) << 24U);
}

void WriteText(
    std::vector<std::byte>& bytes, const std::size_t offset, const std::string_view value)
{
    for (std::size_t index = 0; index < value.size(); ++index)
        bytes[offset + index] = static_cast<std::byte>(value[index]);
}

void WriteMaterial(std::vector<std::byte>& bytes)
{
    WriteText(bytes, kNamesEnd, "MTRL");
    WriteU32(bytes, kNamesEnd + 56U, 0);
    WriteU32(bytes, kNamesEnd + 60U, 0xFFFFFFFFU);
    WriteU32(bytes, kNamesEnd + 64U, 0xFFFFFFFFU);
    WriteU32(bytes, kNamesEnd + 68U, 0xFFFFFFFFU);
    WriteU32(bytes, kNamesEnd + 72U, 2);
    WriteU32(bytes, kNamesEnd + 76U, 0xFFFFFFFFU);
    WriteU32(bytes, kNamesEnd + 80U, 0xFFFFFFFFU);
    WriteU32(bytes, kNamesEnd + 84U, 0xFFFFFFFFU);
    WriteU32(bytes, kNamesEnd + 88U, 1);
}

std::vector<std::byte> MakeVum(const std::vector<std::uint32_t>& target_pairs = {1},
    const std::uint32_t second_middle_span_bytes = 256U)
{
    const std::uint32_t target_count = static_cast<std::uint32_t>(target_pairs.size());
    const std::uint32_t qp_begin = kMaterialsEnd + target_count * 16U;
    const std::uint32_t metadata_end = qp_begin + kPairCount * 32U;
    const std::uint32_t middle_begin = (metadata_end + 15U) & ~15U;
    const std::uint32_t second_middle_begin = middle_begin + 16U;
    const std::uint32_t final_begin = second_middle_begin + second_middle_span_bytes;
    const std::uint32_t first_final_boundary = final_begin + 256U;
    const std::uint32_t primary_end = first_final_boundary + 7U * 64U + 16U;
    std::vector<std::byte> bytes(primary_end + 16U, std::byte{0});

    WriteText(bytes, 0, "VUMS");
    WriteU32(bytes, 12, kPairCount * 2U + 1U);
    WriteU32(bytes, 16, target_count);
    WriteU32(bytes, 20, 1);
    WriteU32(bytes, 24, 1);
    WriteU32(bytes, 80, kNamesEnd);
    WriteU32(bytes, 84, kMaterialsEnd);
    WriteU32(bytes, 88, primary_end);
    WriteU32(bytes, 92, middle_begin);
    WriteU32(bytes, 96, final_begin);
    WriteText(bytes, 112, "A");
    WriteMaterial(bytes);

    for (std::size_t index = 0; index < target_pairs.size(); ++index)
    {
        const std::uint32_t target =
            qp_begin + target_pairs[index] * 32U;
        WriteU32(bytes, kMaterialsEnd + index * 16U + 8U, target);
    }
    for (std::uint32_t pair = 0; pair < kPairCount; ++pair)
    {
        const std::uint32_t q = qp_begin + pair * 32U;
        const std::uint32_t p = q + 16U;
        WriteU32(bytes, q + 4U, pair == 0 ? middle_begin : second_middle_begin);
        WriteU32(bytes, q + 12U, first_final_boundary + pair * 4U * 64U);
        WriteU32(bytes, p, first_final_boundary + (pair * 4U + 1U) * 64U);
        WriteU32(bytes, p + 8U, first_final_boundary + (pair * 4U + 2U) * 64U);
        WriteU32(bytes, p + 12U, first_final_boundary + (pair * 4U + 3U) * 64U);
    }
    std::fill(bytes.begin() + middle_begin, bytes.begin() + primary_end, std::byte{0xA5});
    WriteU32(bytes, middle_begin + 4U, final_begin + 16U);
    WriteU32(bytes, second_middle_begin + 0x74U, final_begin + 464U);
    WriteU32(bytes, second_middle_begin + 0xF4U, final_begin + 608U);
    std::fill(bytes.begin() + primary_end, bytes.end(), std::byte{0xC3});
    return bytes;
}

std::vector<std::byte> MakeEmptyVum()
{
    constexpr std::uint32_t payload_begin = kMaterialsEnd;
    constexpr std::uint32_t primary_end = payload_begin + 16U;
    std::vector<std::byte> bytes(primary_end + 16U, std::byte{0});
    WriteText(bytes, 0, "VUMS");
    WriteU32(bytes, 12, 1);
    WriteU32(bytes, 20, 1);
    WriteU32(bytes, 24, 1);
    WriteU32(bytes, 80, kNamesEnd);
    WriteU32(bytes, 84, kMaterialsEnd);
    WriteU32(bytes, 88, primary_end);
    WriteU32(bytes, 92, payload_begin);
    WriteU32(bytes, 96, payload_begin);
    WriteText(bytes, 112, "A");
    WriteMaterial(bytes);
    std::fill(bytes.begin() + primary_end, bytes.end(), std::byte{0x69});
    return bytes;
}

std::vector<std::byte> MakeMidstreamTargetVum()
{
    auto bytes = MakeVum();
    std::array<std::byte, 16> target{};
    std::array<std::byte, 16> first_q{};
    std::array<std::byte, 16> first_p{};
    std::ranges::copy_n(bytes.begin() + kMaterialsEnd, 16, target.begin());
    std::ranges::copy_n(bytes.begin() + kMaterialsEnd + 16U, 16, first_q.begin());
    std::ranges::copy_n(bytes.begin() + kMaterialsEnd + 32U, 16, first_p.begin());
    std::ranges::copy(first_q, bytes.begin() + kMaterialsEnd);
    std::ranges::copy(first_p, bytes.begin() + kMaterialsEnd + 16U);
    std::ranges::copy(target, bytes.begin() + kMaterialsEnd + 32U);
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

template <typename Value>
void CheckError(const omega::asset::DecodeResult<Value>& result,
    const omega::asset::DecodeErrorCode code, const std::string_view message)
{
    Check(!result && result.error().code == code, message);
}
} // namespace

int VumRenderPayloadDescriptorFailureCount()
{
    auto bytes = MakeVum();
    const auto descriptor = omega::retail::InspectVumRenderPayload(bytes);
    Check(descriptor.has_value(), "VUM passive render-payload structure decodes");
    if (descriptor)
    {
        Check(descriptor->metadata_records_region == omega::retail::ObservedByteRange{208, 80} &&
                  descriptor->middle_payload_region ==
                      omega::retail::ObservedByteRange{288, 272} &&
                  descriptor->final_payload_region ==
                      omega::retail::ObservedByteRange{560, 720},
            "VUM passive descriptor owns only its three proven bounded regions");
        Check(descriptor->pairs ==
                  std::vector<omega::retail::VumRenderPayloadPairDescriptor>{
                      {.middle_payload_bytes = 16,
                          .middle_payload_structural_group_count = 0,
                          .middle_payload_final_reference_offsets = {16, 0},
                          .final_payload_reference_offsets = {256, 320, 384, 448}},
                      {.middle_payload_bytes = 256,
                          .middle_payload_structural_group_count = 1,
                          .middle_payload_final_reference_offsets = {464, 608},
                          .final_payload_reference_offsets = {512, 576, 640, 704}}},
            "VUM pairs normalize span classes and all opaque final-region references");
        Check(descriptor->targeted_pair_indices == std::vector<std::uint32_t>{1},
            "VUM T relationship becomes an owned pair ordinal without retaining a byte offset");
    }

    auto ownership_bytes = bytes;
    auto owned = omega::retail::InspectVumRenderPayload(ownership_bytes);
    std::fill(ownership_bytes.begin(), ownership_bytes.end(), std::byte{0});
    Check(owned && owned->pairs[1].final_payload_reference_offsets[3] == 704 &&
              owned->targeted_pair_indices[0] == 1,
        "VUM passive descriptor remains valid after source storage is replaced");

    auto opaque = bytes;
    const std::uint32_t middle_begin = ReadU32(opaque, 92);
    const std::uint32_t final_begin = ReadU32(opaque, 96);
    const std::uint32_t primary_end = ReadU32(opaque, 88);
    opaque[middle_begin + 8U] = std::byte{0x11};
    opaque[final_begin + 4U] = std::byte{0x22};
    opaque[primary_end] = std::byte{0x33};
    WriteU32(opaque, kMaterialsEnd, 0x12345678U);
    WriteU32(opaque, kMaterialsEnd + 4U, 0x87654321U);
    WriteU32(opaque, kMaterialsEnd + 12U, 0xA5A55A5AU);
    WriteU32(opaque, kMaterialsEnd + 16U, 0xCAFEBABEU);
    WriteU32(opaque, kMaterialsEnd + 16U + 8U, 0xDEADBEEFU);
    WriteU32(opaque, kMaterialsEnd + 32U + 4U, 0x0BADF00DU);
    const auto opaque_descriptor = omega::retail::InspectVumRenderPayload(opaque);
    Check(descriptor && opaque_descriptor && *descriptor == *opaque_descriptor,
        "VUM payload and optional-tail bytes cannot enter the passive descriptor");

    const auto midstream = omega::retail::InspectVumRenderPayload(MakeMidstreamTargetVum());
    Check(descriptor && midstream && *descriptor == *midstream,
        "VUM target-block normalization is stable when the contiguous block is midstream");

    constexpr std::array<std::uint32_t, 2> additional_grouped_spans{480U, 704U};
    for (const std::uint32_t span_bytes : additional_grouped_spans)
    {
        const std::uint32_t structural_group_count = (span_bytes - 32U) / 224U;
        const auto grouped = omega::retail::InspectVumRenderPayload(
            MakeVum({1}, span_bytes));
        Check(grouped && grouped->pairs[1].middle_payload_bytes == span_bytes &&
                  grouped->pairs[1].middle_payload_structural_group_count ==
                      structural_group_count,
            "VUM grouped middle spans normalize to their structural group values");
    }

    const auto empty = omega::retail::InspectVumRenderPayload(MakeEmptyVum());
    Check(empty && empty->pairs.empty() && empty->targeted_pair_indices.empty() &&
              empty->middle_payload_region.size == 0 && empty->final_payload_region.size == 16,
        "VUM empty family normalizes to zero pairs and its bounded final sentinel region");

    auto bad = MakeVum({1, 1});
    CheckError(omega::retail::InspectVumRenderPayload(bad),
        omega::asset::DecodeErrorCode::InvalidReference,
        "VUM rejects duplicate T-to-Q targets");
    bad = MakeVum({1, 0});
    CheckError(omega::retail::InspectVumRenderPayload(bad),
        omega::asset::DecodeErrorCode::InvalidReference,
        "VUM rejects decreasing T-to-Q targets");

    bad = bytes;
    const std::uint32_t qp_begin = kMaterialsEnd + 16U;
    WriteU32(bad, ReadU32(bad, 92) + 4U, ReadU32(bad, 96) + 32U);
    CheckError(omega::retail::InspectVumRenderPayload(bad),
        omega::asset::DecodeErrorCode::InvalidReference,
        "VUM rejects a compact span whose first combined reference is not final-begin plus 16");

    bad = bytes;
    const std::uint32_t second_middle = ReadU32(bad, qp_begin + 32U + 4U);
    WriteU32(bad, second_middle + 0xF4U, ReadU32(bad, 96) + 576U);
    CheckError(omega::retail::InspectVumRenderPayload(bad),
        omega::asset::DecodeErrorCode::InvalidReference,
        "VUM rejects an out-of-order grouped second middle-to-final reference");

    bad = bytes;
    WriteU32(bad, second_middle + 0x74U, ReadU32(bad, qp_begin + 16U + 12U));
    CheckError(omega::retail::InspectVumRenderPayload(bad),
        omega::asset::DecodeErrorCode::InvalidReference,
        "VUM rejects a combined reference that does not advance across pairs");

    bad = bytes;
    const std::uint32_t first_middle = ReadU32(bad, qp_begin + 4U);
    WriteU32(bad, qp_begin + 32U + 4U, first_middle + 32U);
    CheckError(omega::retail::InspectVumRenderPayload(bad),
        omega::asset::DecodeErrorCode::UnsupportedVariant,
        "VUM rejects a Q middle span outside the four observed sizes");

    bad = bytes;
    const std::uint32_t first_q_final = ReadU32(bad, qp_begin + 12U);
    WriteU32(bad, qp_begin + 16U, first_q_final);
    CheckError(omega::retail::InspectVumRenderPayload(bad),
        omega::asset::DecodeErrorCode::InvalidReference,
        "VUM rejects duplicate final-region boundaries within a pair");

    bad = bytes;
    const std::uint32_t first_pair_last = ReadU32(bad, qp_begin + 16U + 12U);
    WriteU32(bad, qp_begin + 32U + 12U, first_pair_last);
    CheckError(omega::retail::InspectVumRenderPayload(bad),
        omega::asset::DecodeErrorCode::InvalidReference,
        "VUM rejects non-increasing final-region boundaries across pairs");

    bad = bytes;
    WriteU32(bad, qp_begin + 48U + 12U, ReadU32(bad, 88) - 20U);
    CheckError(omega::retail::InspectVumRenderPayload(bad),
        omega::asset::DecodeErrorCode::UnsupportedVariant,
        "VUM rejects a final boundary with an unobserved trailing word count");

    bad = MakeEmptyVum();
    WriteU32(bad, 88, ReadU32(bad, 96) + 32U);
    CheckError(omega::retail::InspectVumRenderPayload(bad),
        omega::asset::DecodeErrorCode::UnsupportedVariant,
        "VUM empty family rejects an unobserved final sentinel extent");

    bad = bytes;
    bad[0] = std::byte{'X'};
    CheckError(omega::retail::InspectVumRenderPayload(bad),
        omega::asset::DecodeErrorCode::Malformed,
        "VUM passive descriptor rejects the wrong prefix");
    bad = bytes;
    WriteU32(bad, 12, 0xFFFFFFFFU);
    CheckError(omega::retail::InspectVumRenderPayload(bad),
        omega::asset::DecodeErrorCode::LimitExceeded,
        "VUM huge hostile metadata counts fail before allocation");

    auto limits = omega::asset::DecodeLimits{};
    limits.maximum_input_bytes = bytes.size();
    Check(omega::retail::InspectVumRenderPayload(bytes, limits).has_value(),
        "VUM passive descriptor accepts the exact input-byte limit");
    limits.maximum_input_bytes = bytes.size() - 1U;
    CheckError(omega::retail::InspectVumRenderPayload(bytes, limits),
        omega::asset::DecodeErrorCode::LimitExceeded,
        "VUM passive descriptor rejects one byte below its exact input limit");

    limits = omega::asset::DecodeLimits{};
    limits.maximum_items = 20;
    Check(omega::retail::InspectVumRenderPayload(bytes, limits).has_value(),
        "VUM passive descriptor accepts its exact source-plus-output item limit");
    limits.maximum_items = 19;
    CheckError(omega::retail::InspectVumRenderPayload(bytes, limits),
        omega::asset::DecodeErrorCode::LimitExceeded,
        "VUM passive descriptor rejects one below its exact item limit");

    constexpr std::uint64_t output_bytes =
        sizeof(omega::retail::VumRenderPayloadDescriptor) +
        2U * sizeof(omega::retail::VumRenderPayloadPairDescriptor) + sizeof(std::uint32_t);
    limits = omega::asset::DecodeLimits{};
    limits.maximum_output_bytes = output_bytes;
    limits.maximum_scratch_bytes = 0;
    Check(omega::retail::InspectVumRenderPayload(bytes, limits).has_value(),
        "VUM passive descriptor accepts exact output and zero-scratch limits");
    limits.maximum_output_bytes = output_bytes - 1U;
    CheckError(omega::retail::InspectVumRenderPayload(bytes, limits),
        omega::asset::DecodeErrorCode::LimitExceeded,
        "VUM passive descriptor rejects one byte below its exact output limit");

    return failures;
}
