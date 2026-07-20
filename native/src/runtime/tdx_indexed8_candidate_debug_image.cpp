#include "omega/runtime/tdx_indexed8_candidate_debug_image.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <stdexcept>
#include <vector>

namespace omega::runtime {
namespace {
constexpr std::uint64_t kPaletteEntries = 256U;
constexpr std::uint64_t kPaletteEntryBytes = 4U;
constexpr std::uint64_t kPaletteBytes = kPaletteEntries * kPaletteEntryBytes;

struct ImagePlan {
  std::uint64_t index_bytes = 0U;
  std::uint64_t source_bytes = 0U;
  std::uint64_t output_bytes = 0U;
};

[[nodiscard]] constexpr TdxIndexed8CandidateDebugImageError
Error(const TdxIndexed8CandidateDebugImageErrorCode code) noexcept {
  return TdxIndexed8CandidateDebugImageError{
      .code = code,
      .message = TdxIndexed8CandidateDebugImageErrorMessage(code),
  };
}

[[nodiscard]] constexpr bool Add(const std::uint64_t left,
                                 const std::uint64_t right,
                                 std::uint64_t &output) noexcept {
  if (right > std::numeric_limits<std::uint64_t>::max() - left)
    return false;
  output = left + right;
  return true;
}

[[nodiscard]] constexpr bool Multiply(const std::uint64_t left,
                                      const std::uint64_t right,
                                      std::uint64_t &output) noexcept {
  if (left != 0U && right > std::numeric_limits<std::uint64_t>::max() / left)
    return false;
  output = left * right;
  return true;
}

[[nodiscard]] constexpr bool IsValidClutPermutation(
    const TdxIndexed8ClutPermutationCandidate candidate) noexcept {
  switch (candidate) {
  case TdxIndexed8ClutPermutationCandidate::Identity:
  case TdxIndexed8ClutPermutationCandidate::SwapBitsThreeAndFour:
    return true;
  }
  return false;
}

[[nodiscard]] constexpr bool IsValidSourceChannels(
    const TdxIndexed8SourceChannelCandidate candidate) noexcept {
  switch (candidate) {
  case TdxIndexed8SourceChannelCandidate::SourceSlots012:
  case TdxIndexed8SourceChannelCandidate::SourceSlots021:
  case TdxIndexed8SourceChannelCandidate::SourceSlots102:
  case TdxIndexed8SourceChannelCandidate::SourceSlots120:
  case TdxIndexed8SourceChannelCandidate::SourceSlots201:
  case TdxIndexed8SourceChannelCandidate::SourceSlots210:
    return true;
  }
  return false;
}

[[nodiscard]] constexpr bool
IsValidAlpha(const TdxIndexed8AlphaCandidate candidate) noexcept {
  switch (candidate) {
  case TdxIndexed8AlphaCandidate::Opaque:
  case TdxIndexed8AlphaCandidate::SourceSlot3:
  case TdxIndexed8AlphaCandidate::SourceSlot3TimesTwoClamped:
    return true;
  }
  return false;
}

[[nodiscard]] constexpr bool
IsValidSourceLayout(const TdxIndexed8SourceLayoutCandidate candidate) noexcept {
  switch (candidate) {
  case TdxIndexed8SourceLayoutCandidate::LinearRowsTopToBottom:
  case TdxIndexed8SourceLayoutCandidate::LinearRowsBottomToTop:
    return true;
  }
  return false;
}

[[nodiscard]] constexpr bool
IsValidSampleEncoding(const asset::TextureSampleEncoding encoding) noexcept {
  switch (encoding) {
  case asset::TextureSampleEncoding::Indexed4:
  case asset::TextureSampleEncoding::Indexed8:
  case asset::TextureSampleEncoding::Packed24:
  case asset::TextureSampleEncoding::Packed32:
    return true;
  }
  return false;
}

[[nodiscard]] constexpr bool IsValidTransferElementEncoding(
    const asset::TextureTransferElementEncoding encoding) noexcept {
  switch (encoding) {
  case asset::TextureTransferElementEncoding::Packed4:
  case asset::TextureTransferElementEncoding::Packed8:
  case asset::TextureTransferElementEncoding::Packed24:
  case asset::TextureTransferElementEncoding::Packed32:
    return true;
  }
  return false;
}

[[nodiscard]] std::expected<ImagePlan, TdxIndexed8CandidateDebugImageError>
Preflight(const asset::TextureStorageIR &storage,
          const TdxIndexed8CandidatePolicy &policy,
          const TdxIndexed8CandidateDebugImageLimits &limits) noexcept {
  if (!IsValidClutPermutation(policy.clut_permutation)) {
    return std::unexpected(Error(TdxIndexed8CandidateDebugImageErrorCode::
                                     InvalidClutPermutationCandidate));
  }
  if (!IsValidSourceChannels(policy.source_channels)) {
    return std::unexpected(Error(TdxIndexed8CandidateDebugImageErrorCode::
                                     InvalidSourceChannelCandidate));
  }
  if (!IsValidAlpha(policy.alpha_mapping)) {
    return std::unexpected(
        Error(TdxIndexed8CandidateDebugImageErrorCode::InvalidAlphaCandidate));
  }
  if (!IsValidSourceLayout(policy.source_layout)) {
    return std::unexpected(Error(
        TdxIndexed8CandidateDebugImageErrorCode::InvalidSourceLayoutCandidate));
  }
  if (limits.maximum_source_bytes >
          kTdxIndexed8CandidateHardMaximumSourceBytes ||
      limits.maximum_output_bytes >
          kTdxIndexed8CandidateHardMaximumOutputBytes) {
    return std::unexpected(
        Error(TdxIndexed8CandidateDebugImageErrorCode::InvalidLimits));
  }

  if (storage.width == 0U || storage.height == 0U) {
    return std::unexpected(Error(
        TdxIndexed8CandidateDebugImageErrorCode::InvalidTextureDimensions));
  }
  if (!IsValidSampleEncoding(storage.sample_encoding)) {
    return std::unexpected(
        Error(TdxIndexed8CandidateDebugImageErrorCode::InvalidSampleEncoding));
  }
  if (storage.sample_encoding != asset::TextureSampleEncoding::Indexed8) {
    return std::unexpected(Error(
        TdxIndexed8CandidateDebugImageErrorCode::UnsupportedSampleEncoding));
  }
  if (storage.blocks.size() != 1U) {
    return std::unexpected(
        Error(TdxIndexed8CandidateDebugImageErrorCode::BlockCountMismatch));
  }

  const asset::TextureStorageBlockIR &block = storage.blocks.front();
  if (block.planes.size() != 1U) {
    return std::unexpected(
        Error(TdxIndexed8CandidateDebugImageErrorCode::PlaneCountMismatch));
  }
  if (!block.palette) {
    return std::unexpected(
        Error(TdxIndexed8CandidateDebugImageErrorCode::MissingPalette));
  }

  const asset::TextureStoragePlaneIR &plane = block.planes.front();
  if (plane.width == 0U || plane.height == 0U) {
    return std::unexpected(
        Error(TdxIndexed8CandidateDebugImageErrorCode::InvalidPlaneDimensions));
  }
  if (!IsValidTransferElementEncoding(plane.element_encoding)) {
    return std::unexpected(Error(TdxIndexed8CandidateDebugImageErrorCode::
                                     InvalidTransferElementEncoding));
  }
  if (plane.element_encoding !=
      asset::TextureTransferElementEncoding::Packed8) {
    return std::unexpected(Error(TdxIndexed8CandidateDebugImageErrorCode::
                                     UnsupportedTransferElementEncoding));
  }
  if (plane.width != storage.width || plane.height != storage.height) {
    return std::unexpected(Error(TdxIndexed8CandidateDebugImageErrorCode::
                                     TexturePlaneDimensionMismatch));
  }

  std::uint64_t index_bytes = 0U;
  std::uint64_t source_bytes = 0U;
  std::uint64_t output_bytes = 0U;
  if (!Multiply(storage.width, storage.height, index_bytes) ||
      !Add(index_bytes, kPaletteBytes, source_bytes)) {
    return std::unexpected(
        Error(TdxIndexed8CandidateDebugImageErrorCode::SourceByteSizeOverflow));
  }
  if (!Multiply(index_bytes, 4U, output_bytes)) {
    return std::unexpected(
        Error(TdxIndexed8CandidateDebugImageErrorCode::OutputByteSizeOverflow));
  }
  if (index_bytes != static_cast<std::uint64_t>(plane.bytes.size())) {
    return std::unexpected(
        Error(TdxIndexed8CandidateDebugImageErrorCode::IndexByteSizeMismatch));
  }

  const asset::TexturePaletteStorageIR &palette = *block.palette;
  if (palette.width == 0U || palette.height == 0U) {
    return std::unexpected(Error(
        TdxIndexed8CandidateDebugImageErrorCode::InvalidPaletteDimensions));
  }
  std::uint64_t palette_rectangle_entries = 0U;
  if (!Multiply(palette.width, palette.height, palette_rectangle_entries) ||
      palette_rectangle_entries !=
          static_cast<std::uint64_t>(palette.entries.size())) {
    return std::unexpected(Error(
        TdxIndexed8CandidateDebugImageErrorCode::PaletteEntryCountMismatch));
  }
  if (palette.entries.size() != kPaletteEntries) {
    return std::unexpected(Error(
        TdxIndexed8CandidateDebugImageErrorCode::PaletteCardinalityMismatch));
  }
  if (source_bytes > limits.maximum_source_bytes) {
    return std::unexpected(Error(
        TdxIndexed8CandidateDebugImageErrorCode::SourceByteLimitExceeded));
  }
  if (output_bytes > limits.maximum_output_bytes ||
      output_bytes > std::numeric_limits<std::size_t>::max()) {
    return std::unexpected(Error(
        TdxIndexed8CandidateDebugImageErrorCode::OutputByteLimitExceeded));
  }

  return ImagePlan{
      .index_bytes = index_bytes,
      .source_bytes = source_bytes,
      .output_bytes = output_bytes,
  };
}

[[nodiscard]] constexpr std::uint8_t PermutePaletteIndex(
    const std::uint8_t index,
    const TdxIndexed8ClutPermutationCandidate candidate) noexcept {
  if (candidate == TdxIndexed8ClutPermutationCandidate::Identity)
    return index;
  return static_cast<std::uint8_t>((index & 0xe7U) | ((index & 0x08U) << 1U) |
                                   ((index & 0x10U) >> 1U));
}

[[nodiscard]] constexpr std::array<std::size_t, 3>
SourceChannelSlots(const TdxIndexed8SourceChannelCandidate candidate) noexcept {
  switch (candidate) {
  case TdxIndexed8SourceChannelCandidate::SourceSlots012:
    return {0U, 1U, 2U};
  case TdxIndexed8SourceChannelCandidate::SourceSlots021:
    return {0U, 2U, 1U};
  case TdxIndexed8SourceChannelCandidate::SourceSlots102:
    return {1U, 0U, 2U};
  case TdxIndexed8SourceChannelCandidate::SourceSlots120:
    return {1U, 2U, 0U};
  case TdxIndexed8SourceChannelCandidate::SourceSlots201:
    return {2U, 0U, 1U};
  case TdxIndexed8SourceChannelCandidate::SourceSlots210:
    return {2U, 1U, 0U};
  }
  return {0U, 1U, 2U};
}

[[nodiscard]] constexpr std::byte
MapAlpha(const std::byte source,
         const TdxIndexed8AlphaCandidate candidate) noexcept {
  if (candidate == TdxIndexed8AlphaCandidate::Opaque)
    return std::byte{0xff};
  if (candidate == TdxIndexed8AlphaCandidate::SourceSlot3)
    return source;
  const std::uint16_t doubled =
      static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(source)) * 2U;
  return static_cast<std::byte>(std::min<std::uint16_t>(doubled, 0xffU));
}
} // namespace

std::expected<DebugImage, TdxIndexed8CandidateDebugImageError>
BuildTdxIndexed8CandidateDebugImage(
    const asset::TextureStorageIR &storage,
    const TdxIndexed8CandidatePolicy &policy,
    const TdxIndexed8CandidateDebugImageLimits &limits) {
  const auto planned = Preflight(storage, policy, limits);
  if (!planned)
    return std::unexpected(planned.error());

  try {
    DebugImage image{
        .width = storage.width,
        .height = storage.height,
        .rgba8_pixels = std::vector<std::byte>(
            static_cast<std::size_t>(planned->output_bytes)),
    };
    const asset::TextureStorageBlockIR &block = storage.blocks.front();
    const asset::TextureStoragePlaneIR &plane = block.planes.front();
    const asset::TexturePaletteStorageIR &palette = *block.palette;
    const std::array<std::size_t, 3> source_slots =
        SourceChannelSlots(policy.source_channels);

    const std::size_t width = storage.width;
    const std::size_t height = storage.height;
    for (std::size_t output_y = 0U; output_y < height; ++output_y) {
      const std::size_t source_y =
          policy.source_layout ==
                  TdxIndexed8SourceLayoutCandidate::LinearRowsTopToBottom
              ? output_y
              : height - 1U - output_y;
      for (std::size_t x = 0U; x < width; ++x) {
        const std::size_t source_index = source_y * width + x;
        const std::uint8_t palette_index = PermutePaletteIndex(
            std::to_integer<std::uint8_t>(plane.bytes[source_index]),
            policy.clut_permutation);
        const std::array<std::byte, 4> &entry = palette.entries[palette_index];
        const std::size_t output = (output_y * width + x) * 4U;
        image.rgba8_pixels[output] = entry[source_slots[0]];
        image.rgba8_pixels[output + 1U] = entry[source_slots[1]];
        image.rgba8_pixels[output + 2U] = entry[source_slots[2]];
        image.rgba8_pixels[output + 3U] =
            MapAlpha(entry[3], policy.alpha_mapping);
      }
    }
    return image;
  } catch (const std::bad_alloc &) {
    return std::unexpected(
        Error(TdxIndexed8CandidateDebugImageErrorCode::AllocationFailed));
  } catch (const std::length_error &) {
    return std::unexpected(
        Error(TdxIndexed8CandidateDebugImageErrorCode::AllocationFailed));
  }
}
} // namespace omega::runtime
