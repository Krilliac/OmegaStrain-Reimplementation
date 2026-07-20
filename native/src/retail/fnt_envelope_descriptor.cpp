#include "omega/retail/fnt_envelope_descriptor.h"

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

[[nodiscard]] bool IsPrintableAscii(const std::uint8_t value) noexcept {
  return value >= 0x20U && value <= 0x7EU;
}
} // namespace

asset::DecodeResult<FntEnvelopeDescriptor>
InspectFntEnvelope(const std::span<const std::byte> bytes,
                   const asset::DecodeLimits limits) {
  if (bytes.size() > kFntMaximumInputBytes) {
    return std::unexpected(
        Error(asset::DecodeErrorCode::LimitExceeded,
              "FNT input exceeds the fixed inspector byte limit"));
  }
  if (bytes.size() > limits.maximum_input_bytes) {
    return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
                                 "FNT input exceeds the caller byte limit"));
  }
  if (limits.maximum_items < kFntMaximumDecodedItems) {
    return std::unexpected(
        Error(asset::DecodeErrorCode::LimitExceeded,
              "FNT descriptor exceeds the caller item limit"));
  }
  if (limits.maximum_output_bytes < kFntMaximumLogicalOutputBytes) {
    return std::unexpected(
        Error(asset::DecodeErrorCode::LimitExceeded,
              "FNT descriptor exceeds the caller output limit"));
  }

  if (bytes.size() < 2U) {
    return std::unexpected(Error(asset::DecodeErrorCode::Truncated,
                                 "FNT observed word at 0x00 is truncated",
                                 bytes.size()));
  }
  const std::uint16_t observed_word = ReadU16(bytes, 0);
  if (observed_word != kFntObservedWord0x00) {
    return std::unexpected(
        Error(asset::DecodeErrorCode::UnsupportedVariant,
              "FNT word at 0x00 is outside the observed family", 0));
  }

  if (bytes.size() < kFntObservedAsciiOffset) {
    return std::unexpected(Error(asset::DecodeErrorCode::Truncated,
                                 "FNT observed byte at 0x02 is truncated",
                                 bytes.size()));
  }
  const std::uint8_t observed_byte = ReadU8(bytes, 2);
  if (observed_byte != kFntObservedByte0x02) {
    return std::unexpected(
        Error(asset::DecodeErrorCode::UnsupportedVariant,
              "FNT byte at 0x02 is outside the observed family", 2));
  }

  if (bytes.size() < kFntObservedPayloadOffset) {
    return std::unexpected(Error(asset::DecodeErrorCode::Truncated,
                                 "FNT observed prefix is truncated",
                                 bytes.size()));
  }
  for (std::uint64_t index = 0; index < kFntObservedAsciiBytes; ++index) {
    const std::uint64_t offset = kFntObservedAsciiOffset + index;
    if (!IsPrintableAscii(ReadU8(bytes, static_cast<std::size_t>(offset)))) {
      return std::unexpected(Error(
          asset::DecodeErrorCode::Malformed,
          "FNT observed ASCII region contains a non-printable byte", offset));
    }
  }
  if (ReadU8(bytes, static_cast<std::size_t>(kFntObservedTerminatorOffset)) !=
      0) {
    return std::unexpected(
        Error(asset::DecodeErrorCode::Malformed,
              "FNT observed ASCII region is not NUL terminated",
              kFntObservedTerminatorOffset));
  }
  if (bytes.size() == kFntObservedPayloadOffset) {
    return std::unexpected(Error(asset::DecodeErrorCode::Truncated,
                                 "FNT opaque payload is missing",
                                 kFntObservedPayloadOffset));
  }

  return FntEnvelopeDescriptor{
      .observed_word_0x00 = observed_word,
      .observed_byte_0x02 = observed_byte,
      .ascii_region = {.offset = kFntObservedAsciiOffset,
                       .size = kFntObservedAsciiBytes},
      .terminator_region = {.offset = kFntObservedTerminatorOffset, .size = 1},
      .opaque_payload_region =
          {
              .offset = kFntObservedPayloadOffset,
              .size = static_cast<std::uint64_t>(bytes.size()) -
                      kFntObservedPayloadOffset,
          },
  };
}
} // namespace omega::retail
