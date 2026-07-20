#include "omega/retail/vpk_wrapper_envelope_decoder.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
constexpr std::array<std::byte, 4> kMagic{
    std::byte{0x20},
    std::byte{0x4B},
    std::byte{0x50},
    std::byte{0x56},
};
constexpr std::array<std::byte, 4> kOpaque04{
    std::byte{0x11},
    std::byte{0x82},
    std::byte{0xF3},
    std::byte{0x64},
};
constexpr std::array<std::byte, 4> kOpaque0c{
    std::byte{0xA5},
    std::byte{0x16},
    std::byte{0xC7},
    std::byte{0x38},
};

void WriteU32(std::vector<std::byte> &bytes, const std::size_t offset, const std::uint32_t value)
{
    for (unsigned shift = 0; shift < 32; shift += 8)
        bytes[offset + shift / 8U] = static_cast<std::byte>((value >> shift) & 0xFFU);
}

[[nodiscard]] std::vector<std::byte> MakeVpk(const std::size_t physical_bytes)
{
    std::vector<std::byte> bytes(physical_bytes, std::byte{0});
    if (physical_bytes < 16U)
        return bytes;

    for (std::size_t index = 0; index < kMagic.size(); ++index)
    {
        bytes[index] = kMagic[index];
        bytes[4U + index] = kOpaque04[index];
        bytes[12U + index] = kOpaque0c[index];
    }
    WriteU32(bytes, 8, omega::retail::kVpkObservedWord0x08);

    // Project-generated sentinels exercise ignored wrapper and payload positions without importing
    // or deriving any owner bytes.
    bytes[16] = std::byte{0xD1};
    bytes[2047] = std::byte{0x42};
    bytes[2048] = std::byte{0xB3};
    bytes.back() = std::byte{0x24};
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
void CheckPathFreeError(const Result &result, const omega::asset::DecodeErrorCode code, const std::string_view message)
{
    if (result)
    {
        Check(false, message);
        return;
    }
    Check(result.error().code == code, message);
    Check(!result.error().message.empty(), "VPK errors own a fixed nonempty diagnostic");
    Check(result.error().message.find('/') == std::string::npos &&
              result.error().message.find('\\') == std::string::npos,
          "VPK errors contain no filesystem path");
}

template <typename Result>
void CheckPathFreeErrorAt(const Result &result, const omega::asset::DecodeErrorCode code,
                          const std::uint64_t byte_offset, const std::string_view message)
{
    CheckPathFreeError(result, code, message);
    if (!result)
    {
        Check(result.error().byte_offset && *result.error().byte_offset == byte_offset,
              "VPK error reports the expected byte offset");
    }
}
} // namespace

int main()
{
    using omega::asset::DecodeErrorCode;
    using omega::retail::VpkWrapperEnvelopeDescriptor;

    static_assert(std::is_trivially_copyable_v<VpkWrapperEnvelopeDescriptor>);
    static_assert(std::is_standard_layout_v<VpkWrapperEnvelopeDescriptor>);
    static_assert(omega::retail::kVpkObservedWord0x08 == 2048U);
    static_assert(omega::retail::kVpkPhysicalAlignmentBytes == 2048U);
    static_assert(omega::retail::kVpkWrapperMinimumInputBytes / 2048U == 645U);
    static_assert(omega::retail::kVpkWrapperMaximumInputBytes / 2048U == 4397U);
    static_assert(omega::retail::kVpkWrapperMaximumDecodedItems == 1U);
    static_assert(omega::retail::kVpkWrapperMaximumLogicalOutputBytes == sizeof(VpkWrapperEnvelopeDescriptor));

    auto minimum_bytes = MakeVpk(static_cast<std::size_t>(omega::retail::kVpkWrapperMinimumInputBytes));
    const auto minimum = omega::retail::DecodeVpkWrapperEnvelope(minimum_bytes);
    Check(minimum.has_value(), "VPK accepts the observed minimum physical span");
    if (minimum)
    {
        Check(minimum->opaque_prefix_bytes_0x04 == kOpaque04 && minimum->opaque_prefix_bytes_0x0c == kOpaque0c,
              "VPK preserves both unassigned prefix fields in source order");
        Check(minimum->physical_byte_count == omega::retail::kVpkWrapperMinimumInputBytes &&
                  minimum->aligned_block_count == 645U,
              "VPK publishes only physical bytes and derived aligned-block count");
    }

    auto maximum_bytes = MakeVpk(static_cast<std::size_t>(omega::retail::kVpkWrapperMaximumInputBytes));
    const auto maximum = omega::retail::DecodeVpkWrapperEnvelope(maximum_bytes);
    Check(maximum && maximum->physical_byte_count == omega::retail::kVpkWrapperMaximumInputBytes &&
              maximum->aligned_block_count == 4397U,
          "VPK accepts the observed maximum physical span and derives its block count");

    const auto interior = omega::retail::DecodeVpkWrapperEnvelope(MakeVpk(static_cast<std::size_t>(
        omega::retail::kVpkWrapperMinimumInputBytes + omega::retail::kVpkPhysicalAlignmentBytes)));
    Check(interior && interior->aligned_block_count == 646U,
          "VPK accepts an aligned physical span inside the bounded range");

    std::vector<std::byte> unaligned_storage(minimum_bytes.size() + 1U, std::byte{0xA5});
    std::copy(minimum_bytes.begin(), minimum_bytes.end(), unaligned_storage.begin() + 1);
    const auto unaligned = omega::retail::DecodeVpkWrapperEnvelope(
        std::span<const std::byte>(unaligned_storage.data() + 1, minimum_bytes.size()));
    Check(minimum && unaligned && *unaligned == *minimum,
          "VPK accepts an unaligned backing slice; only physical length has a 2048-byte rule");

    bool every_short_prefix_is_truncated = true;
    for (std::size_t size = 0; size < 16U; ++size)
    {
        const auto result =
            omega::retail::DecodeVpkWrapperEnvelope(std::span<const std::byte>(minimum_bytes.data(), size));
        every_short_prefix_is_truncated = every_short_prefix_is_truncated && !result &&
                                          result.error().code == DecodeErrorCode::Truncated &&
                                          result.error().byte_offset && *result.error().byte_offset == size;
    }
    Check(every_short_prefix_is_truncated, "VPK classifies and locates every truncated 16-byte prefix");

    CheckPathFreeError(omega::retail::DecodeVpkWrapperEnvelope(std::span<const std::byte>(
                           minimum_bytes.data(),
                           minimum_bytes.size() - static_cast<std::size_t>(omega::retail::kVpkPhysicalAlignmentBytes))),
                       DecodeErrorCode::UnsupportedVariant,
                       "VPK rejects an aligned physical span below the observed range");

    CheckPathFreeError(
        omega::retail::DecodeVpkWrapperEnvelope(
            std::span<const std::byte>(minimum_bytes.data(), minimum_bytes.size() - 1U)),
        DecodeErrorCode::UnsupportedVariant,
        "VPK rejects the physical span exactly one byte below the observed minimum");

    auto misaligned = MakeVpk(static_cast<std::size_t>(omega::retail::kVpkWrapperMinimumInputBytes + 1U));
    CheckPathFreeError(omega::retail::DecodeVpkWrapperEnvelope(misaligned), DecodeErrorCode::Malformed,
                       "VPK rejects a physical span not divisible by 2048");

    auto above_fixed_maximum = MakeVpk(static_cast<std::size_t>(omega::retail::kVpkWrapperMaximumInputBytes +
                                                                omega::retail::kVpkPhysicalAlignmentBytes));
    CheckPathFreeError(omega::retail::DecodeVpkWrapperEnvelope(above_fixed_maximum), DecodeErrorCode::LimitExceeded,
                       "VPK rejects the first aligned block above the fixed physical ceiling");

    const auto one_above_fixed_maximum = std::span<const std::byte>(
        above_fixed_maximum.data(),
        static_cast<std::size_t>(omega::retail::kVpkWrapperMaximumInputBytes + 1U));
    CheckPathFreeError(omega::retail::DecodeVpkWrapperEnvelope(one_above_fixed_maximum),
                       DecodeErrorCode::LimitExceeded,
                       "VPK rejects the physical span exactly one byte above the fixed ceiling");

    for (std::size_t index = 0; index < kMagic.size(); ++index)
    {
        const std::byte original = minimum_bytes[index];
        minimum_bytes[index] ^= std::byte{0xFF};
        CheckPathFreeErrorAt(omega::retail::DecodeVpkWrapperEnvelope(minimum_bytes),
                             DecodeErrorCode::UnsupportedVariant, index,
                             "VPK rejects and locates every invalid observed raw signature byte");
        minimum_bytes[index] = original;
    }

    const std::array<std::byte, 4> forward_spelling{
        std::byte{0x56},
        std::byte{0x50},
        std::byte{0x4B},
        std::byte{0x20},
    };
    for (std::size_t index = 0; index < forward_spelling.size(); ++index)
        minimum_bytes[index] = forward_spelling[index];
    CheckPathFreeErrorAt(omega::retail::DecodeVpkWrapperEnvelope(minimum_bytes), DecodeErrorCode::UnsupportedVariant, 0,
                         "VPK rejects the nonmatching forward ASCII byte spelling");
    for (std::size_t index = 0; index < kMagic.size(); ++index)
        minimum_bytes[index] = kMagic[index];

    for (const std::uint32_t observed_word : {0U, 16U, 2047U, 2049U, 0x00080000U})
    {
        WriteU32(minimum_bytes, 8, observed_word);
        CheckPathFreeErrorAt(omega::retail::DecodeVpkWrapperEnvelope(minimum_bytes),
                             DecodeErrorCode::UnsupportedVariant, 8,
                             "VPK rejects an observed word other than little-endian 2048 at 0x08");
    }
    WriteU32(minimum_bytes, 8, omega::retail::kVpkObservedWord0x08);

    auto different_opaque_bytes = minimum_bytes;
    const std::array<std::byte, 4> other_opaque04{
        std::byte{0xFE},
        std::byte{0x03},
        std::byte{0xDC},
        std::byte{0x29},
    };
    const std::array<std::byte, 4> other_opaque0c{
        std::byte{0x57},
        std::byte{0x68},
        std::byte{0x79},
        std::byte{0x8A},
    };
    for (std::size_t index = 0; index < other_opaque04.size(); ++index)
    {
        different_opaque_bytes[4U + index] = other_opaque04[index];
        different_opaque_bytes[12U + index] = other_opaque0c[index];
    }
    const auto different_opaque = omega::retail::DecodeVpkWrapperEnvelope(different_opaque_bytes);
    Check(different_opaque && different_opaque->opaque_prefix_bytes_0x04 == other_opaque04 &&
              different_opaque->opaque_prefix_bytes_0x0c == other_opaque0c,
          "VPK retains arbitrary unassigned prefix bytes without numeric interpretation");

    auto different_payload = minimum_bytes;
    const std::array<std::size_t, 4> opaque_positions{
        16U,
        2047U,
        2048U,
        different_payload.size() - 1U,
    };
    for (const std::size_t offset : opaque_positions)
        different_payload[offset] ^= std::byte{0xFF};
    const auto ignored_payload = omega::retail::DecodeVpkWrapperEnvelope(different_payload);
    Check(minimum && ignored_payload && *minimum == *ignored_payload,
          "VPK treats every byte after the proven prefix as opaque and retains none of it");

    const auto owned = [&]() {
        auto transient_source = MakeVpk(static_cast<std::size_t>(omega::retail::kVpkWrapperMinimumInputBytes));
        auto decoded = omega::retail::DecodeVpkWrapperEnvelope(transient_source);
        Check(decoded.has_value(), "transient VPK source decodes before ownership check");
        auto descriptor = decoded ? *decoded : VpkWrapperEnvelopeDescriptor{};
        transient_source.assign(transient_source.size(), std::byte{0xFF});
        transient_source.clear();
        transient_source.shrink_to_fit();
        return descriptor;
    }();
    Check(minimum && owned == *minimum, "VPK descriptor remains valid after source replacement and destruction");

    bool deterministic = minimum.has_value();
    for (std::size_t iteration = 0; iteration < 64U; ++iteration)
    {
        const auto repeated = omega::retail::DecodeVpkWrapperEnvelope(minimum_bytes);
        deterministic = deterministic && repeated && *repeated == *minimum;
    }
    Check(deterministic, "VPK decoding is deterministic across repeated independent calls");

    auto limits = omega::asset::DecodeLimits{};
    limits.maximum_input_bytes = minimum_bytes.size();
    Check(omega::retail::DecodeVpkWrapperEnvelope(minimum_bytes, limits).has_value(),
          "VPK succeeds at the exact caller input budget");
    limits.maximum_input_bytes = minimum_bytes.size() - 1U;
    CheckPathFreeError(omega::retail::DecodeVpkWrapperEnvelope(minimum_bytes, limits), DecodeErrorCode::LimitExceeded,
                       "VPK rejects one byte below the caller input budget");

    limits = omega::asset::DecodeLimits{};
    limits.maximum_items = omega::retail::kVpkWrapperMaximumDecodedItems;
    Check(omega::retail::DecodeVpkWrapperEnvelope(minimum_bytes, limits).has_value(),
          "VPK succeeds at the exact fixed item budget");
    limits.maximum_items = 0;
    CheckPathFreeError(omega::retail::DecodeVpkWrapperEnvelope(minimum_bytes, limits), DecodeErrorCode::LimitExceeded,
                       "VPK rejects one below the fixed item budget");

    limits = omega::asset::DecodeLimits{};
    limits.maximum_output_bytes = omega::retail::kVpkWrapperMaximumLogicalOutputBytes;
    Check(omega::retail::DecodeVpkWrapperEnvelope(minimum_bytes, limits).has_value(),
          "VPK succeeds at the exact fixed output-byte budget");
    limits.maximum_output_bytes = omega::retail::kVpkWrapperMaximumLogicalOutputBytes - 1U;
    CheckPathFreeError(omega::retail::DecodeVpkWrapperEnvelope(minimum_bytes, limits), DecodeErrorCode::LimitExceeded,
                       "VPK rejects one byte below the fixed output budget");

    limits = omega::asset::DecodeLimits{};
    limits.maximum_scratch_bytes = 0;
    limits.maximum_string_bytes = 0;
    limits.maximum_nesting_depth = 0;
    Check(omega::retail::DecodeVpkWrapperEnvelope(minimum_bytes, limits).has_value(),
          "VPK uses zero scratch, zero strings, and nesting depth zero");

    limits = omega::asset::DecodeLimits{};
    limits.maximum_input_bytes = std::numeric_limits<std::uint64_t>::max();
    CheckPathFreeError(omega::retail::DecodeVpkWrapperEnvelope(above_fixed_maximum, limits),
                       DecodeErrorCode::LimitExceeded,
                       "VPK caller limits cannot raise the fixed physical-input ceiling");

    limits = omega::asset::DecodeLimits{};
    limits.maximum_items = 0;
    limits.maximum_output_bytes = 0;
    CheckPathFreeError(omega::retail::DecodeVpkWrapperEnvelope(std::span<const std::byte>{}, limits),
                       DecodeErrorCode::LimitExceeded,
                       "VPK enforces fixed descriptor budgets before diagnosing truncation");

    return failures;
}
