#include "omega/retail/vpk_wrapper_envelope_decoder.h"

#include <array>
#include <optional>
#include <string>
#include <utility>

namespace omega::retail
{
namespace
{
constexpr std::uint64_t kMinimumPrefixBytes = 16;
constexpr std::array<std::byte, 4> kObservedRawSignature{
    std::byte{0x20},
    std::byte{0x4B},
    std::byte{0x50},
    std::byte{0x56},
};

[[nodiscard]] asset::DecodeError Error(const asset::DecodeErrorCode code, std::string message,
                                       const std::optional<std::uint64_t> byte_offset = std::nullopt)
{
    return asset::DecodeError{
        .code = code,
        .byte_offset = byte_offset,
        .message = std::move(message),
    };
}

[[nodiscard]] std::uint32_t ReadU32(const std::span<const std::byte> bytes, const std::size_t offset) noexcept
{
    return std::to_integer<std::uint32_t>(bytes[offset]) | (std::to_integer<std::uint32_t>(bytes[offset + 1U]) << 8U) |
           (std::to_integer<std::uint32_t>(bytes[offset + 2U]) << 16U) |
           (std::to_integer<std::uint32_t>(bytes[offset + 3U]) << 24U);
}

[[nodiscard]] std::array<std::byte, 4> ReadOpaqueBytes(const std::span<const std::byte> bytes,
                                                       const std::size_t offset) noexcept
{
    return {
        bytes[offset],
        bytes[offset + 1U],
        bytes[offset + 2U],
        bytes[offset + 3U],
    };
}
} // namespace

asset::DecodeResult<VpkWrapperEnvelopeDescriptor> DecodeVpkWrapperEnvelope(const std::span<const std::byte> bytes,
                                                                           const asset::DecodeLimits limits)
{
    if (bytes.size() > kVpkWrapperMaximumInputBytes)
    {
        return std::unexpected(
            Error(asset::DecodeErrorCode::LimitExceeded, "VPK input exceeds the fixed decoder byte limit"));
    }
    if (bytes.size() > limits.maximum_input_bytes)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded, "VPK input exceeds the caller byte limit"));
    }
    if (limits.maximum_items < kVpkWrapperMaximumDecodedItems)
    {
        return std::unexpected(
            Error(asset::DecodeErrorCode::LimitExceeded, "VPK descriptor exceeds the caller item limit"));
    }
    if (limits.maximum_output_bytes < kVpkWrapperMaximumLogicalOutputBytes)
    {
        return std::unexpected(
            Error(asset::DecodeErrorCode::LimitExceeded, "VPK descriptor exceeds the caller output limit"));
    }
    if (bytes.size() < kMinimumPrefixBytes)
    {
        return std::unexpected(
            Error(asset::DecodeErrorCode::Truncated, "VPK 16-byte prefix is truncated", bytes.size()));
    }
    if (bytes.size() < kVpkWrapperMinimumInputBytes)
    {
        return std::unexpected(
            Error(asset::DecodeErrorCode::UnsupportedVariant, "VPK input is below the observed physical-size range"));
    }
    if (bytes.size() % kVpkPhysicalAlignmentBytes != 0)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::Malformed, "VPK physical span is not 2048-byte aligned"));
    }

    for (std::size_t index = 0; index < kObservedRawSignature.size(); ++index)
    {
        if (bytes[index] != kObservedRawSignature[index])
        {
            return std::unexpected(
                Error(asset::DecodeErrorCode::UnsupportedVariant, "VPK raw signature at 0x00 is not supported", index));
        }
    }
    if (ReadU32(bytes, 8) != kVpkObservedWord0x08)
    {
        return std::unexpected(
            Error(asset::DecodeErrorCode::UnsupportedVariant, "VPK observed word at 0x08 is not supported", 8));
    }

    return VpkWrapperEnvelopeDescriptor{
        .opaque_prefix_bytes_0x04 = ReadOpaqueBytes(bytes, 4),
        .opaque_prefix_bytes_0x0c = ReadOpaqueBytes(bytes, 12),
        .physical_byte_count = bytes.size(),
        .aligned_block_count = bytes.size() / kVpkPhysicalAlignmentBytes,
    };
}
} // namespace omega::retail
