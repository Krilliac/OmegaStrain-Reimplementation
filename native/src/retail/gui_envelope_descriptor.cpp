#include "omega/retail/gui_envelope_descriptor.h"

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

asset::DecodeResult<GuiEnvelopeDescriptor>
InspectGuiEnvelope(const std::span<const std::byte> bytes,
                   const asset::DecodeLimits limits) {
  if (bytes.size() > kGuiMaximumInputBytes) {
    return std::unexpected(
        Error(asset::DecodeErrorCode::LimitExceeded,
              "GUI input exceeds the fixed inspector byte limit"));
  }
  if (bytes.size() > limits.maximum_input_bytes) {
    return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
                                 "GUI input exceeds the caller byte limit"));
  }
  if (limits.maximum_items < kGuiMaximumDecodedItems) {
    return std::unexpected(
        Error(asset::DecodeErrorCode::LimitExceeded,
              "GUI descriptor exceeds the caller item limit"));
  }
  if (limits.maximum_output_bytes < kGuiMaximumLogicalOutputBytes) {
    return std::unexpected(
        Error(asset::DecodeErrorCode::LimitExceeded,
              "GUI descriptor exceeds the caller output limit"));
  }

  if (bytes.size() < kGuiTagBytes) {
    return std::unexpected(Error(asset::DecodeErrorCode::Truncated,
                                 "GUI observed tag is truncated",
                                 bytes.size()));
  }
  for (std::size_t index = 0; index < kGuiObservedTag.size(); ++index) {
    if (ReadU8(bytes, index) != kGuiObservedTag[index]) {
      return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
                                   "GUI tag is outside the observed family",
                                   index));
    }
  }

  if (bytes.size() <= kGuiOpaqueGapOffset) {
    return std::unexpected(Error(asset::DecodeErrorCode::Truncated,
                                 "GUI fixed prefix is truncated",
                                 bytes.size()));
  }
  if (bytes.size() < kGuiRootBoundaryOffset) {
    return std::unexpected(Error(asset::DecodeErrorCode::Truncated,
                                 "GUI observed word at 0x04 is truncated",
                                 bytes.size()));
  }
  const std::uint16_t observed_word =
      ReadU16(bytes, static_cast<std::size_t>(kGuiObservedWordOffset));

  if (bytes.size() == kGuiRootBoundaryOffset) {
    return std::unexpected(Error(asset::DecodeErrorCode::Truncated,
                                 "GUI opaque root is missing",
                                 kGuiRootBoundaryOffset));
  }

  return GuiEnvelopeDescriptor{
      .observed_word_0x04 = observed_word,
      .tag_region = {.offset = kGuiTagOffset, .size = kGuiTagBytes},
      .opaque_gap_region = {.offset = kGuiOpaqueGapOffset,
                            .size = kGuiOpaqueGapBytes},
      .observed_word_region = {.offset = kGuiObservedWordOffset,
                               .size = kGuiObservedWordBytes},
      .opaque_root_region =
          {
              .offset = kGuiRootBoundaryOffset,
              .size = static_cast<std::uint64_t>(bytes.size()) -
                      kGuiRootBoundaryOffset,
          },
  };
}
} // namespace omega::retail
