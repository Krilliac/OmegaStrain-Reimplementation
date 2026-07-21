#include "omega/retail/ie_envelope_descriptor.h"

#include <optional>
#include <string>
#include <utility>

namespace omega::retail {
namespace {
[[nodiscard]] asset::DecodeError
Error(const asset::DecodeErrorCode code, std::string message,
      const std::optional<std::uint64_t> byte_offset = std::nullopt) {
  return asset::DecodeError{
      .code = code,
      .byte_offset = byte_offset,
      .message = std::move(message),
  };
}

[[nodiscard]] std::uint8_t ReadU8(const std::span<const std::byte> bytes,
                                  const std::size_t offset) noexcept {
  return std::to_integer<std::uint8_t>(bytes[offset]);
}

[[nodiscard]] std::uint16_t ReadU16(const std::span<const std::byte> bytes,
                                    const std::size_t offset) noexcept {
  return static_cast<std::uint16_t>(ReadU8(bytes, offset)) |
         static_cast<std::uint16_t>(
             static_cast<std::uint16_t>(ReadU8(bytes, offset + 1U)) << 8U);
}
} // namespace

asset::DecodeResult<IeEnvelopeDescriptor>
InspectIeEnvelope(const std::span<const std::byte> bytes,
                  const asset::DecodeLimits limits) {
  if (bytes.size() > kIeMaximumInputBytes) {
    return std::unexpected(
        Error(asset::DecodeErrorCode::LimitExceeded,
              "IE input exceeds the fixed inspector byte limit"));
  }
  if (bytes.size() > limits.maximum_input_bytes) {
    return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
                                 "IE input exceeds the caller byte limit"));
  }
  if (limits.maximum_items < kIeMaximumDecodedItems) {
    return std::unexpected(
        Error(asset::DecodeErrorCode::LimitExceeded,
              "IE descriptor exceeds the caller item limit"));
  }
  if (limits.maximum_output_bytes < kIeMaximumLogicalOutputBytes) {
    return std::unexpected(
        Error(asset::DecodeErrorCode::LimitExceeded,
              "IE descriptor exceeds the caller output limit"));
  }

  if (bytes.size() < kIeOpaquePrefixBytes) {
    return std::unexpected(Error(asset::DecodeErrorCode::Truncated,
                                 "IE opaque skipped prefix is truncated",
                                 bytes.size()));
  }
  if (bytes.size() < kIeRootBoundaryOffset) {
    return std::unexpected(Error(asset::DecodeErrorCode::Truncated,
                                 "IE observed word at 0x02 is truncated",
                                 bytes.size()));
  }
  const std::uint16_t observed_word =
      ReadU16(bytes, static_cast<std::size_t>(kIeObservedWordOffset));

  if (bytes.size() == kIeRootBoundaryOffset) {
    return std::unexpected(Error(asset::DecodeErrorCode::Truncated,
                                 "IE opaque root is missing",
                                 kIeRootBoundaryOffset));
  }

  return IeEnvelopeDescriptor{
      .observed_word_0x02 = observed_word,
      .opaque_prefix_region = {.offset = kIeOpaquePrefixOffset,
                               .size = kIeOpaquePrefixBytes},
      .observed_word_region = {.offset = kIeObservedWordOffset,
                               .size = kIeObservedWordBytes},
      .opaque_root_region =
          {
              .offset = kIeRootBoundaryOffset,
              .size = static_cast<std::uint64_t>(bytes.size()) -
                      kIeRootBoundaryOffset,
          },
  };
}
} // namespace omega::retail
