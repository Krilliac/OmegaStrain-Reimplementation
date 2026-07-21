#include "omega/runtime/tdx_indexed4_candidate_debug_image.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <iostream>
#include <limits>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace {
using omega::asset::TexturePaletteStorageIR;
using omega::asset::TextureSampleEncoding;
using omega::asset::TextureStorageBlockIR;
using omega::asset::TextureStorageIR;
using omega::asset::TextureStoragePlaneIR;
using omega::asset::TextureTransferElementEncoding;
using omega::runtime::DebugImage;
using omega::runtime::TdxIndexed4AlphaCandidate;
using omega::runtime::TdxIndexed4CandidateDebugImageError;
using omega::runtime::TdxIndexed4CandidateDebugImageErrorCode;
using omega::runtime::TdxIndexed4CandidateDebugImageLimits;
using omega::runtime::TdxIndexed4CandidatePolicy;
using omega::runtime::TdxIndexed4NibbleOrderCandidate;
using omega::runtime::TdxIndexed4PalettePermutationCandidate;
using omega::runtime::TdxIndexed4SourceChannelCandidate;
using omega::runtime::TdxIndexed4SourceLayoutCandidate;

using BuilderSignature =
    std::expected<DebugImage, TdxIndexed4CandidateDebugImageError> (*)(
        const TextureStorageIR &, const TdxIndexed4CandidatePolicy &,
        const TdxIndexed4CandidateDebugImageLimits &);
static_assert(std::is_same_v<
              decltype(&omega::runtime::BuildTdxIndexed4CandidateDebugImage),
              BuilderSignature>);
static_assert(!std::is_default_constructible_v<TdxIndexed4CandidatePolicy>);
static_assert(
    noexcept(omega::runtime::TdxIndexed4CandidateDebugImageErrorCodeName(
        TdxIndexed4CandidateDebugImageErrorCode::InvalidNibbleOrderCandidate)));
static_assert(
    noexcept(omega::runtime::TdxIndexed4CandidateDebugImageErrorMessage(
        TdxIndexed4CandidateDebugImageErrorCode::InvalidNibbleOrderCandidate)));
static_assert(static_cast<std::uint8_t>(
                  TdxIndexed4NibbleOrderCandidate::LowNibbleFirst) == 0U &&
              static_cast<std::uint8_t>(
                  TdxIndexed4NibbleOrderCandidate::HighNibbleFirst) == 1U);
static_assert(
    static_cast<std::uint8_t>(
        TdxIndexed4PalettePermutationCandidate::SourceOrderIdentity) == 0U);
static_assert(static_cast<std::uint8_t>(
                  TdxIndexed4SourceChannelCandidate::SourceSlots012) == 0U &&
              static_cast<std::uint8_t>(
                  TdxIndexed4SourceChannelCandidate::SourceSlots210) == 5U);
static_assert(static_cast<std::uint8_t>(TdxIndexed4AlphaCandidate::Opaque) ==
                  0U &&
              static_cast<std::uint8_t>(
                  TdxIndexed4AlphaCandidate::SourceSlot3TimesTwoClamped) == 2U);
static_assert(static_cast<std::uint8_t>(
                  TdxIndexed4SourceLayoutCandidate::LinearRowsTopToBottom) ==
                  0U &&
              static_cast<std::uint8_t>(
                  TdxIndexed4SourceLayoutCandidate::LinearRowsBottomToTop) ==
                  1U);

int failures = 0;

void Check(const bool condition, const std::string_view message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    ++failures;
  }
}

[[nodiscard]] constexpr TdxIndexed4CandidatePolicy IdentityPolicy() noexcept {
  return TdxIndexed4CandidatePolicy{
      TdxIndexed4NibbleOrderCandidate::LowNibbleFirst,
      TdxIndexed4PalettePermutationCandidate::SourceOrderIdentity,
      TdxIndexed4SourceChannelCandidate::SourceSlots012,
      TdxIndexed4AlphaCandidate::SourceSlot3,
      TdxIndexed4SourceLayoutCandidate::LinearRowsTopToBottom,
  };
}

[[nodiscard]] constexpr std::array<std::byte, 4>
PaletteEntry(const std::uint8_t index, const std::uint8_t seed = 0U) noexcept {
  return {
      static_cast<std::byte>(static_cast<std::uint8_t>(index + seed)),
      static_cast<std::byte>(static_cast<std::uint8_t>(0x40U + index + seed)),
      static_cast<std::byte>(static_cast<std::uint8_t>(0xe0U - index + seed)),
      static_cast<std::byte>(static_cast<std::uint8_t>(index * 17U + seed)),
  };
}

[[nodiscard]] std::vector<std::array<std::byte, 4>>
MakePalette(const std::uint8_t seed = 0U) {
  std::vector<std::array<std::byte, 4>> entries(16U);
  for (std::size_t index = 0U; index < entries.size(); ++index) {
    entries[index] = PaletteEntry(static_cast<std::uint8_t>(index), seed);
  }
  return entries;
}

[[nodiscard]] TextureStorageIR
MakeStorage(const std::uint8_t palette_seed = 0U) {
  constexpr std::uint32_t width = 8U;
  constexpr std::uint32_t height = 4U;
  std::vector<std::byte> packed_indices(16U);
  for (std::size_t index = 0U; index < packed_indices.size(); ++index) {
    const std::uint8_t low = static_cast<std::uint8_t>(index);
    const std::uint8_t high = static_cast<std::uint8_t>(15U - index);
    packed_indices[index] =
        static_cast<std::byte>(static_cast<std::uint8_t>((high << 4U) | low));
  }
  return TextureStorageIR{
      .width = width,
      .height = height,
      .sample_encoding = TextureSampleEncoding::Indexed4,
      .blocks =
          {
              TextureStorageBlockIR{
                  .planes =
                      {
                          TextureStoragePlaneIR{
                              .width = width,
                              .height = height,
                              .element_encoding =
                                  TextureTransferElementEncoding::Packed4,
                              .bytes = std::move(packed_indices),
                          },
                      },
                  .palette =
                      TexturePaletteStorageIR{
                          .width = 4U,
                          .height = 4U,
                          .entries = MakePalette(palette_seed),
                      },
              },
          },
  };
}

[[nodiscard]] TextureStorageIR
MakeOddStorage(std::vector<std::byte> packed_indices) {
  return TextureStorageIR{
      .width = 5U,
      .height = 1U,
      .sample_encoding = TextureSampleEncoding::Indexed4,
      .blocks =
          {
              TextureStorageBlockIR{
                  .planes =
                      {
                          TextureStoragePlaneIR{
                              .width = 5U,
                              .height = 1U,
                              .element_encoding =
                                  TextureTransferElementEncoding::Packed4,
                              .bytes = std::move(packed_indices),
                          },
                      },
                  .palette =
                      TexturePaletteStorageIR{
                          .width = 1U,
                          .height = 16U,
                          .entries = MakePalette(),
                      },
              },
          },
  };
}

[[nodiscard]] constexpr std::array<std::size_t, 3>
ChannelSlots(const TdxIndexed4SourceChannelCandidate candidate) noexcept {
  switch (candidate) {
  case TdxIndexed4SourceChannelCandidate::SourceSlots012:
    return {0U, 1U, 2U};
  case TdxIndexed4SourceChannelCandidate::SourceSlots021:
    return {0U, 2U, 1U};
  case TdxIndexed4SourceChannelCandidate::SourceSlots102:
    return {1U, 0U, 2U};
  case TdxIndexed4SourceChannelCandidate::SourceSlots120:
    return {1U, 2U, 0U};
  case TdxIndexed4SourceChannelCandidate::SourceSlots201:
    return {2U, 0U, 1U};
  case TdxIndexed4SourceChannelCandidate::SourceSlots210:
    return {2U, 1U, 0U};
  }
  return {0U, 1U, 2U};
}

[[nodiscard]] constexpr std::byte
ExpectedAlpha(const std::byte source,
              const TdxIndexed4AlphaCandidate candidate) noexcept {
  if (candidate == TdxIndexed4AlphaCandidate::Opaque)
    return std::byte{0xff};
  if (candidate == TdxIndexed4AlphaCandidate::SourceSlot3)
    return source;
  const std::uint16_t doubled =
      static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(source)) * 2U;
  return static_cast<std::byte>(std::min<std::uint16_t>(doubled, 0xffU));
}

[[nodiscard]] constexpr std::uint8_t
ExpectedIndex(const std::byte packed, const std::size_t nibble_index,
              const TdxIndexed4NibbleOrderCandidate candidate) noexcept {
  const std::uint8_t value = std::to_integer<std::uint8_t>(packed);
  const bool first = (nibble_index & 1U) == 0U;
  const bool low = candidate == TdxIndexed4NibbleOrderCandidate::LowNibbleFirst
                       ? first
                       : !first;
  return low ? static_cast<std::uint8_t>(value & 0x0fU)
             : static_cast<std::uint8_t>(value >> 4U);
}

[[nodiscard]] std::array<std::byte, 4> PixelAt(const DebugImage &image,
                                               const std::size_t pixel) {
  const std::size_t offset = pixel * 4U;
  return {
      image.rgba8_pixels[offset],
      image.rgba8_pixels[offset + 1U],
      image.rgba8_pixels[offset + 2U],
      image.rgba8_pixels[offset + 3U],
  };
}

[[nodiscard]] std::array<std::byte, 4>
ExpectedPixel(const TextureStorageIR &storage,
              const TdxIndexed4CandidatePolicy &policy,
              const std::size_t output_pixel) {
  const std::size_t width = storage.width;
  const std::size_t height = storage.height;
  const std::size_t output_y = output_pixel / width;
  const std::size_t x = output_pixel % width;
  const std::size_t source_y =
      policy.source_layout ==
              TdxIndexed4SourceLayoutCandidate::LinearRowsTopToBottom
          ? output_y
          : height - 1U - output_y;
  const std::size_t source_pixel = source_y * width + x;
  const auto &block = storage.blocks.front();
  const std::uint8_t palette_index =
      ExpectedIndex(block.planes.front().bytes[source_pixel / 2U], source_pixel,
                    policy.nibble_order);
  const auto &entry = block.palette->entries[palette_index];
  const std::array<std::size_t, 3> slots = ChannelSlots(policy.source_channels);
  return {
      entry[slots[0]],
      entry[slots[1]],
      entry[slots[2]],
      ExpectedAlpha(entry[3], policy.alpha_mapping),
  };
}

[[nodiscard]] bool EveryPixelMatches(const DebugImage &image,
                                     const TextureStorageIR &storage,
                                     const TdxIndexed4CandidatePolicy &policy) {
  if (image.width != storage.width || image.height != storage.height ||
      image.rgba8_pixels.size() !=
          static_cast<std::size_t>(storage.width) * storage.height * 4U) {
    return false;
  }
  const std::size_t pixels =
      static_cast<std::size_t>(storage.width) * storage.height;
  for (std::size_t pixel = 0U; pixel < pixels; ++pixel) {
    if (PixelAt(image, pixel) != ExpectedPixel(storage, policy, pixel))
      return false;
  }
  return true;
}

void CheckError(const TextureStorageIR &storage,
                const TdxIndexed4CandidatePolicy &policy,
                const TdxIndexed4CandidateDebugImageErrorCode code,
                const std::string_view message,
                const TdxIndexed4CandidateDebugImageLimits &limits = {}) {
  const std::expected<DebugImage, TdxIndexed4CandidateDebugImageError> result =
      omega::runtime::BuildTdxIndexed4CandidateDebugImage(storage, policy,
                                                          limits);
  Check(
      !result && result.error().code == code &&
          result.error().message ==
              omega::runtime::TdxIndexed4CandidateDebugImageErrorMessage(code),
      message);
}

void CheckErrorContract() {
  struct ErrorContract {
    TdxIndexed4CandidateDebugImageErrorCode code;
    std::string_view name;
    std::string_view message;
  };
  constexpr std::array contracts{
      ErrorContract{
          TdxIndexed4CandidateDebugImageErrorCode::InvalidNibbleOrderCandidate,
          "invalid-nibble-order-candidate",
          "indexed-4 TDX candidate nibble order is invalid"},
      ErrorContract{TdxIndexed4CandidateDebugImageErrorCode::
                        InvalidPalettePermutationCandidate,
                    "invalid-palette-permutation-candidate",
                    "indexed-4 TDX candidate palette permutation is invalid"},
      ErrorContract{
          TdxIndexed4CandidateDebugImageErrorCode::
              InvalidSourceChannelCandidate,
          "invalid-source-channel-candidate",
          "indexed-4 TDX candidate source-channel mapping is invalid"},
      ErrorContract{
          TdxIndexed4CandidateDebugImageErrorCode::InvalidAlphaCandidate,
          "invalid-alpha-candidate",
          "indexed-4 TDX candidate alpha mapping is invalid"},
      ErrorContract{
          TdxIndexed4CandidateDebugImageErrorCode::InvalidSourceLayoutCandidate,
          "invalid-source-layout-candidate",
          "indexed-4 TDX candidate source layout is invalid"},
      ErrorContract{
          TdxIndexed4CandidateDebugImageErrorCode::InvalidLimits,
          "invalid-limits",
          "indexed-4 TDX candidate limits exceed the project hard maxima"},
      ErrorContract{
          TdxIndexed4CandidateDebugImageErrorCode::InvalidTextureDimensions,
          "invalid-texture-dimensions",
          "indexed-4 TDX candidate requires nonzero texture dimensions"},
      ErrorContract{
          TdxIndexed4CandidateDebugImageErrorCode::InvalidSampleEncoding,
          "invalid-sample-encoding",
          "indexed-4 TDX candidate sample encoding is invalid"},
      ErrorContract{
          TdxIndexed4CandidateDebugImageErrorCode::UnsupportedSampleEncoding,
          "unsupported-sample-encoding",
          "indexed-4 TDX candidate requires indexed-4 sample encoding"},
      ErrorContract{TdxIndexed4CandidateDebugImageErrorCode::BlockCountMismatch,
                    "block-count-mismatch",
                    "indexed-4 TDX candidate requires exactly one block"},
      ErrorContract{TdxIndexed4CandidateDebugImageErrorCode::PlaneCountMismatch,
                    "plane-count-mismatch",
                    "indexed-4 TDX candidate requires exactly one plane"},
      ErrorContract{TdxIndexed4CandidateDebugImageErrorCode::MissingPalette,
                    "missing-palette",
                    "indexed-4 TDX candidate requires one palette"},
      ErrorContract{
          TdxIndexed4CandidateDebugImageErrorCode::InvalidPlaneDimensions,
          "invalid-plane-dimensions",
          "indexed-4 TDX candidate requires nonzero plane dimensions"},
      ErrorContract{TdxIndexed4CandidateDebugImageErrorCode::
                        InvalidTransferElementEncoding,
                    "invalid-transfer-element-encoding",
                    "indexed-4 TDX candidate transfer-element encoding is "
                    "invalid"},
      ErrorContract{TdxIndexed4CandidateDebugImageErrorCode::
                        UnsupportedTransferElementEncoding,
                    "unsupported-transfer-element-encoding",
                    "indexed-4 TDX candidate requires packed-4 "
                    "transfer-element encoding"},
      ErrorContract{TdxIndexed4CandidateDebugImageErrorCode::
                        TexturePlaneDimensionMismatch,
                    "texture-plane-dimension-mismatch",
                    "indexed-4 TDX candidate texture and plane dimensions do "
                    "not match"},
      ErrorContract{
          TdxIndexed4CandidateDebugImageErrorCode::SourceByteSizeOverflow,
          "source-byte-size-overflow",
          "indexed-4 TDX candidate source byte size overflows"},
      ErrorContract{
          TdxIndexed4CandidateDebugImageErrorCode::OutputByteSizeOverflow,
          "output-byte-size-overflow",
          "indexed-4 TDX candidate output byte size overflows"},
      ErrorContract{
          TdxIndexed4CandidateDebugImageErrorCode::IndexByteSizeMismatch,
          "index-byte-size-mismatch",
          "indexed-4 TDX candidate index byte size does not match the packed "
          "transfer rectangle"},
      ErrorContract{
          TdxIndexed4CandidateDebugImageErrorCode::InvalidPaletteDimensions,
          "invalid-palette-dimensions",
          "indexed-4 TDX candidate requires nonzero palette dimensions"},
      ErrorContract{
          TdxIndexed4CandidateDebugImageErrorCode::PaletteEntryCountMismatch,
          "palette-entry-count-mismatch",
          "indexed-4 TDX candidate palette dimensions do not match its entry "
          "count"},
      ErrorContract{
          TdxIndexed4CandidateDebugImageErrorCode::PaletteCardinalityMismatch,
          "palette-cardinality-mismatch",
          "indexed-4 TDX candidate requires exactly 16 palette entries"},
      ErrorContract{
          TdxIndexed4CandidateDebugImageErrorCode::SourceByteLimitExceeded,
          "source-byte-limit-exceeded",
          "indexed-4 TDX candidate exceeds the source-byte limit"},
      ErrorContract{
          TdxIndexed4CandidateDebugImageErrorCode::OutputByteLimitExceeded,
          "output-byte-limit-exceeded",
          "indexed-4 TDX candidate exceeds the output-byte limit"},
      ErrorContract{TdxIndexed4CandidateDebugImageErrorCode::AllocationFailed,
                    "allocation-failed",
                    "indexed-4 TDX candidate allocation failed"},
  };

  bool exact = true;
  for (std::size_t index = 0U; index < contracts.size(); ++index) {
    const ErrorContract &contract = contracts[index];
    exact = exact && static_cast<std::size_t>(contract.code) == index &&
            omega::runtime::TdxIndexed4CandidateDebugImageErrorCodeName(
                contract.code) == contract.name &&
            omega::runtime::TdxIndexed4CandidateDebugImageErrorMessage(
                contract.code) == contract.message;
  }
  Check(exact, "all 25 error ordinals, names, and fixed messages are frozen");

  constexpr auto unknown =
      static_cast<TdxIndexed4CandidateDebugImageErrorCode>(255U);
  Check(omega::runtime::TdxIndexed4CandidateDebugImageErrorCodeName(unknown) ==
                "unknown" &&
            omega::runtime::TdxIndexed4CandidateDebugImageErrorMessage(
                unknown) == "indexed-4 TDX candidate error is unknown",
        "unknown error values use fixed path/value-free fallbacks");

  constexpr TdxIndexed4CandidateDebugImageLimits defaults;
  Check(defaults.maximum_source_bytes ==
                omega::runtime::kTdxIndexed4CandidateHardMaximumSourceBytes &&
            defaults.maximum_output_bytes ==
                omega::runtime::kTdxIndexed4CandidateHardMaximumOutputBytes &&
            defaults.maximum_source_bytes == 8ULL * 1024ULL * 1024ULL + 64ULL &&
            defaults.maximum_output_bytes == 64ULL * 1024ULL * 1024ULL,
        "default budgets freeze the packed-source, palette, and output hard "
        "maxima");
}

void CheckAllCandidateMappings() {
  const TextureStorageIR storage = MakeStorage();
  const TextureStorageIR storage_before = storage;
  constexpr std::array nibble_candidates{
      TdxIndexed4NibbleOrderCandidate::LowNibbleFirst,
      TdxIndexed4NibbleOrderCandidate::HighNibbleFirst,
  };
  constexpr std::array palette_candidates{
      TdxIndexed4PalettePermutationCandidate::SourceOrderIdentity,
  };
  constexpr std::array channel_candidates{
      TdxIndexed4SourceChannelCandidate::SourceSlots012,
      TdxIndexed4SourceChannelCandidate::SourceSlots021,
      TdxIndexed4SourceChannelCandidate::SourceSlots102,
      TdxIndexed4SourceChannelCandidate::SourceSlots120,
      TdxIndexed4SourceChannelCandidate::SourceSlots201,
      TdxIndexed4SourceChannelCandidate::SourceSlots210,
  };
  constexpr std::array alpha_candidates{
      TdxIndexed4AlphaCandidate::Opaque,
      TdxIndexed4AlphaCandidate::SourceSlot3,
      TdxIndexed4AlphaCandidate::SourceSlot3TimesTwoClamped,
  };
  constexpr std::array layout_candidates{
      TdxIndexed4SourceLayoutCandidate::LinearRowsTopToBottom,
      TdxIndexed4SourceLayoutCandidate::LinearRowsBottomToTop,
  };

  std::size_t combinations = 0U;
  bool every_combination_matches = true;
  for (const auto nibble : nibble_candidates) {
    std::array<bool, 16> saw_index{};
    const auto &bytes = storage.blocks.front().planes.front().bytes;
    for (std::size_t texel = 0U; texel < 32U; ++texel) {
      saw_index[ExpectedIndex(bytes[texel / 2U], texel, nibble)] = true;
    }
    Check(
        std::ranges::all_of(saw_index, [](const bool value) { return value; }),
        "each nibble-order fixture traverses all 16 source-order indices");

    for (const auto palette : palette_candidates) {
      for (const auto channels : channel_candidates) {
        for (const auto alpha : alpha_candidates) {
          for (const auto layout : layout_candidates) {
            const TdxIndexed4CandidatePolicy policy{nibble, palette, channels,
                                                    alpha, layout};
            const auto image =
                omega::runtime::BuildTdxIndexed4CandidateDebugImage(storage,
                                                                    policy);
            every_combination_matches =
                every_combination_matches && image &&
                EveryPixelMatches(*image, storage, policy);
            ++combinations;
          }
        }
      }
    }
  }
  Check(combinations == 72U && every_combination_matches,
        "all 72 explicit hypothesis-policy combinations map every texel");

  const TdxIndexed4CandidatePolicy low = IdentityPolicy();
  const TdxIndexed4CandidatePolicy high{
      TdxIndexed4NibbleOrderCandidate::HighNibbleFirst,
      TdxIndexed4PalettePermutationCandidate::SourceOrderIdentity,
      TdxIndexed4SourceChannelCandidate::SourceSlots012,
      TdxIndexed4AlphaCandidate::SourceSlot3,
      TdxIndexed4SourceLayoutCandidate::LinearRowsTopToBottom};
  const auto low_image =
      omega::runtime::BuildTdxIndexed4CandidateDebugImage(storage, low);
  const auto high_image =
      omega::runtime::BuildTdxIndexed4CandidateDebugImage(storage, high);
  Check(low_image && high_image &&
            low_image->rgba8_pixels != high_image->rgba8_pixels &&
            PixelAt(*low_image, 0U) == PixelAt(*high_image, 1U),
        "low-first and high-first candidates visibly exchange packed nibbles");

  std::array<std::array<std::byte, 4>, 6> channel_pixels{};
  for (std::size_t index = 0U; index < channel_candidates.size(); ++index) {
    const TdxIndexed4CandidatePolicy policy{
        TdxIndexed4NibbleOrderCandidate::LowNibbleFirst,
        TdxIndexed4PalettePermutationCandidate::SourceOrderIdentity,
        channel_candidates[index], TdxIndexed4AlphaCandidate::SourceSlot3,
        TdxIndexed4SourceLayoutCandidate::LinearRowsTopToBottom};
    const auto image =
        omega::runtime::BuildTdxIndexed4CandidateDebugImage(storage, policy);
    if (image)
      channel_pixels[index] = PixelAt(*image, 0U);
  }
  bool channels_distinct = true;
  for (std::size_t left = 0U; left < channel_pixels.size(); ++left) {
    for (std::size_t right = left + 1U; right < channel_pixels.size();
         ++right) {
      channels_distinct =
          channels_distinct && channel_pixels[left] != channel_pixels[right];
    }
  }
  Check(channels_distinct,
        "the generated palette distinguishes all six source-slot mappings");

  std::array<std::vector<std::byte>, 3> alpha_outputs;
  for (std::size_t index = 0U; index < alpha_candidates.size(); ++index) {
    const TdxIndexed4CandidatePolicy policy{
        TdxIndexed4NibbleOrderCandidate::LowNibbleFirst,
        TdxIndexed4PalettePermutationCandidate::SourceOrderIdentity,
        TdxIndexed4SourceChannelCandidate::SourceSlots012,
        alpha_candidates[index],
        TdxIndexed4SourceLayoutCandidate::LinearRowsTopToBottom};
    const auto image =
        omega::runtime::BuildTdxIndexed4CandidateDebugImage(storage, policy);
    if (image)
      alpha_outputs[index] = image->rgba8_pixels;
  }
  Check(alpha_outputs[0] != alpha_outputs[1] &&
            alpha_outputs[1] != alpha_outputs[2],
        "opaque, source-slot-three, and doubled-clamped candidates differ");

  const TdxIndexed4CandidatePolicy top_down = IdentityPolicy();
  const TdxIndexed4CandidatePolicy bottom_up{
      TdxIndexed4NibbleOrderCandidate::LowNibbleFirst,
      TdxIndexed4PalettePermutationCandidate::SourceOrderIdentity,
      TdxIndexed4SourceChannelCandidate::SourceSlots012,
      TdxIndexed4AlphaCandidate::SourceSlot3,
      TdxIndexed4SourceLayoutCandidate::LinearRowsBottomToTop};
  const auto top_image =
      omega::runtime::BuildTdxIndexed4CandidateDebugImage(storage, top_down);
  const auto bottom_image =
      omega::runtime::BuildTdxIndexed4CandidateDebugImage(storage, bottom_up);
  Check(top_image && bottom_image &&
            top_image->rgba8_pixels != bottom_image->rgba8_pixels &&
            PixelAt(*top_image, 0U) == PixelAt(*bottom_image, 24U) &&
            PixelAt(*top_image, 7U) == PixelAt(*bottom_image, 31U),
        "whole-row reversal preserves intra-row order on distinguishing rows");
  Check(storage == storage_before,
        "candidate projection never mutates borrowed texture storage");
}

void CheckOddCardinality() {
  TextureStorageIR low_storage =
      MakeOddStorage({std::byte{0x21}, std::byte{0x43}, std::byte{0xa5}});
  const auto low = omega::runtime::BuildTdxIndexed4CandidateDebugImage(
      low_storage, IdentityPolicy());
  low_storage.blocks.front().planes.front().bytes.back() = std::byte{0xf5};
  const auto low_unused_changed =
      omega::runtime::BuildTdxIndexed4CandidateDebugImage(low_storage,
                                                          IdentityPolicy());
  bool low_exact = low && low_unused_changed &&
                   low->rgba8_pixels == low_unused_changed->rgba8_pixels;
  for (std::size_t pixel = 0U; low_exact && pixel < 5U; ++pixel) {
    low_exact = PixelAt(*low, pixel)[0] ==
                static_cast<std::byte>(static_cast<std::uint8_t>(pixel + 1U));
  }
  Check(low_exact,
        "odd low-first cardinality consumes five nibbles and ignores the final "
        "high nibble");

  const TdxIndexed4CandidatePolicy high_policy{
      TdxIndexed4NibbleOrderCandidate::HighNibbleFirst,
      TdxIndexed4PalettePermutationCandidate::SourceOrderIdentity,
      TdxIndexed4SourceChannelCandidate::SourceSlots012,
      TdxIndexed4AlphaCandidate::SourceSlot3,
      TdxIndexed4SourceLayoutCandidate::LinearRowsTopToBottom};
  TextureStorageIR high_storage =
      MakeOddStorage({std::byte{0x12}, std::byte{0x34}, std::byte{0x5a}});
  const auto high = omega::runtime::BuildTdxIndexed4CandidateDebugImage(
      high_storage, high_policy);
  high_storage.blocks.front().planes.front().bytes.back() = std::byte{0x5f};
  const auto high_unused_changed =
      omega::runtime::BuildTdxIndexed4CandidateDebugImage(high_storage,
                                                          high_policy);
  bool high_exact = high && high_unused_changed &&
                    high->rgba8_pixels == high_unused_changed->rgba8_pixels;
  for (std::size_t pixel = 0U; high_exact && pixel < 5U; ++pixel) {
    high_exact = PixelAt(*high, pixel)[0] ==
                 static_cast<std::byte>(static_cast<std::uint8_t>(pixel + 1U));
  }
  Check(
      high_exact,
      "odd high-first cardinality consumes five nibbles and ignores the final "
      "low nibble");
}

void CheckDeterminismAndOwnership() {
  TextureStorageIR first_storage = MakeStorage();
  const TextureStorageIR second_storage = MakeStorage(0x31U);
  auto first = omega::runtime::BuildTdxIndexed4CandidateDebugImage(
      first_storage, IdentityPolicy());
  auto repeated = omega::runtime::BuildTdxIndexed4CandidateDebugImage(
      first_storage, IdentityPolicy());
  const auto second = omega::runtime::BuildTdxIndexed4CandidateDebugImage(
      second_storage, IdentityPolicy());
  Check(first && repeated && second,
        "two generated palettes and a repeated call produce diagnostic images");
  if (!first || !repeated || !second)
    return;

  Check(first->width == 8U && first->height == 4U &&
            first->rgba8_pixels.size() == 128U,
        "the generated 8x4 fixture produces exactly 128 output bytes");
  Check(first->rgba8_pixels == repeated->rgba8_pixels &&
            first->rgba8_pixels.data() != repeated->rgba8_pixels.data(),
        "repeated projections are deterministic and independently allocated");
  Check(first->rgba8_pixels != second->rgba8_pixels &&
            PixelAt(*first, 0U) != PixelAt(*second, 0U) &&
            PixelAt(*first, 31U) != PixelAt(*second, 31U),
        "distinguishing generated palettes remain payload-sensitive");

  const std::vector<std::byte> owned_before_mutation = first->rgba8_pixels;
  std::ranges::fill(first_storage.blocks.front().planes.front().bytes,
                    std::byte{0xff});
  std::ranges::fill(first_storage.blocks.front().palette->entries,
                    std::array<std::byte, 4>{std::byte{0}, std::byte{0},
                                             std::byte{0}, std::byte{0}});
  first_storage.blocks.clear();
  Check(first->rgba8_pixels == owned_before_mutation,
        "returned output survives source mutation and destruction");
}

void CheckValidationPriorityAndLimits() {
  const TextureStorageIR storage = MakeStorage();
  const TdxIndexed4CandidatePolicy valid = IdentityPolicy();

  CheckError(
      storage,
      TdxIndexed4CandidatePolicy{
          static_cast<TdxIndexed4NibbleOrderCandidate>(255U),
          static_cast<TdxIndexed4PalettePermutationCandidate>(255U),
          static_cast<TdxIndexed4SourceChannelCandidate>(255U),
          static_cast<TdxIndexed4AlphaCandidate>(255U),
          static_cast<TdxIndexed4SourceLayoutCandidate>(255U)},
      TdxIndexed4CandidateDebugImageErrorCode::InvalidNibbleOrderCandidate,
      "nibble-order validation has first priority");
  CheckError(storage,
             TdxIndexed4CandidatePolicy{
                 TdxIndexed4NibbleOrderCandidate::LowNibbleFirst,
                 static_cast<TdxIndexed4PalettePermutationCandidate>(255U),
                 static_cast<TdxIndexed4SourceChannelCandidate>(255U),
                 static_cast<TdxIndexed4AlphaCandidate>(255U),
                 static_cast<TdxIndexed4SourceLayoutCandidate>(255U)},
             TdxIndexed4CandidateDebugImageErrorCode::
                 InvalidPalettePermutationCandidate,
             "palette-candidate validation follows nibble order");
  CheckError(
      storage,
      TdxIndexed4CandidatePolicy{
          TdxIndexed4NibbleOrderCandidate::LowNibbleFirst,
          TdxIndexed4PalettePermutationCandidate::SourceOrderIdentity,
          static_cast<TdxIndexed4SourceChannelCandidate>(255U),
          static_cast<TdxIndexed4AlphaCandidate>(255U),
          static_cast<TdxIndexed4SourceLayoutCandidate>(255U)},
      TdxIndexed4CandidateDebugImageErrorCode::InvalidSourceChannelCandidate,
      "source-channel validation follows palette policy");
  CheckError(storage,
             TdxIndexed4CandidatePolicy{
                 TdxIndexed4NibbleOrderCandidate::LowNibbleFirst,
                 TdxIndexed4PalettePermutationCandidate::SourceOrderIdentity,
                 TdxIndexed4SourceChannelCandidate::SourceSlots012,
                 static_cast<TdxIndexed4AlphaCandidate>(255U),
                 static_cast<TdxIndexed4SourceLayoutCandidate>(255U)},
             TdxIndexed4CandidateDebugImageErrorCode::InvalidAlphaCandidate,
             "alpha validation follows source channels");
  CheckError(
      storage,
      TdxIndexed4CandidatePolicy{
          TdxIndexed4NibbleOrderCandidate::LowNibbleFirst,
          TdxIndexed4PalettePermutationCandidate::SourceOrderIdentity,
          TdxIndexed4SourceChannelCandidate::SourceSlots012,
          TdxIndexed4AlphaCandidate::SourceSlot3,
          static_cast<TdxIndexed4SourceLayoutCandidate>(255U)},
      TdxIndexed4CandidateDebugImageErrorCode::InvalidSourceLayoutCandidate,
      "source-layout validation follows alpha");

  auto invalid_limits = TdxIndexed4CandidateDebugImageLimits{};
  ++invalid_limits.maximum_source_bytes;
  CheckError(
      storage, valid, TdxIndexed4CandidateDebugImageErrorCode::InvalidLimits,
      "caller source budget cannot raise the hard maximum", invalid_limits);
  invalid_limits = TdxIndexed4CandidateDebugImageLimits{};
  ++invalid_limits.maximum_output_bytes;
  CheckError(
      storage, valid, TdxIndexed4CandidateDebugImageErrorCode::InvalidLimits,
      "caller output budget cannot raise the hard maximum", invalid_limits);

  TextureStorageIR invalid = storage;
  invalid.width = 0U;
  invalid.sample_encoding = static_cast<TextureSampleEncoding>(255U);
  invalid.blocks.clear();
  CheckError(invalid, valid,
             TdxIndexed4CandidateDebugImageErrorCode::InvalidTextureDimensions,
             "texture dimensions lead storage validation");
  invalid = storage;
  invalid.sample_encoding = static_cast<TextureSampleEncoding>(255U);
  invalid.blocks.clear();
  CheckError(invalid, valid,
             TdxIndexed4CandidateDebugImageErrorCode::InvalidSampleEncoding,
             "unknown sample encoding precedes structure");
  for (const auto encoding :
       {TextureSampleEncoding::Indexed8, TextureSampleEncoding::Packed24,
        TextureSampleEncoding::Packed32}) {
    invalid = storage;
    invalid.sample_encoding = encoding;
    invalid.blocks.clear();
    CheckError(
        invalid, valid,
        TdxIndexed4CandidateDebugImageErrorCode::UnsupportedSampleEncoding,
        "known non-Indexed4 sample encoding precedes structure");
  }

  invalid = storage;
  invalid.blocks.clear();
  CheckError(invalid, valid,
             TdxIndexed4CandidateDebugImageErrorCode::BlockCountMismatch,
             "zero blocks fail exact cardinality");
  invalid = storage;
  invalid.blocks.push_back(TextureStorageBlockIR{});
  CheckError(invalid, valid,
             TdxIndexed4CandidateDebugImageErrorCode::BlockCountMismatch,
             "multiple blocks fail without selection");
  invalid = storage;
  invalid.blocks.front().planes.clear();
  invalid.blocks.front().palette.reset();
  CheckError(invalid, valid,
             TdxIndexed4CandidateDebugImageErrorCode::PlaneCountMismatch,
             "plane cardinality precedes palette presence");
  invalid = storage;
  invalid.blocks.front().planes.push_back(TextureStoragePlaneIR{});
  CheckError(invalid, valid,
             TdxIndexed4CandidateDebugImageErrorCode::PlaneCountMismatch,
             "multiple planes fail without selection");
  invalid = storage;
  invalid.blocks.front().palette.reset();
  invalid.blocks.front().planes.front().width = 0U;
  CheckError(invalid, valid,
             TdxIndexed4CandidateDebugImageErrorCode::MissingPalette,
             "palette presence precedes plane validation");

  invalid = storage;
  invalid.blocks.front().planes.front().width = 0U;
  invalid.blocks.front().planes.front().element_encoding =
      static_cast<TextureTransferElementEncoding>(255U);
  CheckError(invalid, valid,
             TdxIndexed4CandidateDebugImageErrorCode::InvalidPlaneDimensions,
             "plane dimensions precede transfer encoding");
  invalid = storage;
  invalid.blocks.front().planes.front().element_encoding =
      static_cast<TextureTransferElementEncoding>(255U);
  invalid.blocks.front().planes.front().width = 7U;
  CheckError(
      invalid, valid,
      TdxIndexed4CandidateDebugImageErrorCode::InvalidTransferElementEncoding,
      "unknown transfer encoding precedes rectangle matching");
  for (const auto encoding : {TextureTransferElementEncoding::Packed8,
                              TextureTransferElementEncoding::Packed24,
                              TextureTransferElementEncoding::Packed32}) {
    invalid = storage;
    invalid.blocks.front().planes.front().element_encoding = encoding;
    invalid.blocks.front().planes.front().width = 7U;
    CheckError(invalid, valid,
               TdxIndexed4CandidateDebugImageErrorCode::
                   UnsupportedTransferElementEncoding,
               "known non-Packed4 transfer encoding precedes rectangle match");
  }
  invalid = storage;
  invalid.blocks.front().planes.front().width = 7U;
  CheckError(
      invalid, valid,
      TdxIndexed4CandidateDebugImageErrorCode::TexturePlaneDimensionMismatch,
      "texture and plane rectangles must match exactly");

  invalid = storage;
  invalid.width = std::numeric_limits<std::uint32_t>::max();
  invalid.height = std::numeric_limits<std::uint32_t>::max();
  invalid.blocks.front().planes.front().width = invalid.width;
  invalid.blocks.front().planes.front().height = invalid.height;
  invalid.blocks.front().planes.front().bytes.clear();
  CheckError(invalid, valid,
             TdxIndexed4CandidateDebugImageErrorCode::OutputByteSizeOverflow,
             "output multiplication fails before cardinality or allocation");

  const TdxIndexed4CandidateDebugImageLimits zero_limits{
      .maximum_source_bytes = 0U,
      .maximum_output_bytes = 0U,
  };
  invalid = storage;
  invalid.blocks.front().planes.front().bytes.pop_back();
  invalid.blocks.front().palette->width = 0U;
  CheckError(invalid, valid,
             TdxIndexed4CandidateDebugImageErrorCode::IndexByteSizeMismatch,
             "short packed input precedes palette and caller budgets",
             zero_limits);
  invalid = storage;
  invalid.blocks.front().planes.front().bytes.push_back(std::byte{0});
  CheckError(invalid, valid,
             TdxIndexed4CandidateDebugImageErrorCode::IndexByteSizeMismatch,
             "extra packed input fails exact ceil-texels-over-two cardinality");

  invalid = storage;
  invalid.blocks.front().palette->width = 0U;
  invalid.blocks.front().palette->entries.clear();
  CheckError(invalid, valid,
             TdxIndexed4CandidateDebugImageErrorCode::InvalidPaletteDimensions,
             "present palettes require nonzero rectangles");
  invalid = storage;
  invalid.blocks.front().palette->entries.pop_back();
  CheckError(invalid, valid,
             TdxIndexed4CandidateDebugImageErrorCode::PaletteEntryCountMismatch,
             "palette rectangle and entry count must match exactly");
  invalid = storage;
  invalid.blocks.front().palette->width = 3U;
  invalid.blocks.front().palette->height = 5U;
  invalid.blocks.front().palette->entries.resize(15U);
  CheckError(
      invalid, valid,
      TdxIndexed4CandidateDebugImageErrorCode::PaletteCardinalityMismatch,
      "an internally exact 15-entry palette fails fixed cardinality");
  invalid = storage;
  invalid.blocks.front().palette->width = 2U;
  invalid.blocks.front().palette->height = 8U;
  Check(omega::runtime::BuildTdxIndexed4CandidateDebugImage(invalid, valid)
            .has_value(),
        "an alternate exact nonzero 2x8 palette rectangle is accepted");

  constexpr TdxIndexed4CandidateDebugImageLimits exact_limits{
      .maximum_source_bytes = 80U,
      .maximum_output_bytes = 128U,
  };
  const auto exact = omega::runtime::BuildTdxIndexed4CandidateDebugImage(
      storage, valid, exact_limits);
  Check(exact && EveryPixelMatches(*exact, storage, valid),
        "exact 16-byte packed plus 64-byte palette and output budgets succeed");
  auto tight_limits = exact_limits;
  --tight_limits.maximum_source_bytes;
  CheckError(storage, valid,
             TdxIndexed4CandidateDebugImageErrorCode::SourceByteLimitExceeded,
             "one below exact aggregate source bytes fails", tight_limits);
  tight_limits = exact_limits;
  --tight_limits.maximum_output_bytes;
  CheckError(storage, valid,
             TdxIndexed4CandidateDebugImageErrorCode::OutputByteLimitExceeded,
             "one below exact output bytes fails", tight_limits);

  invalid = storage;
  invalid.width = 4097U;
  invalid.height = 4096U;
  invalid.blocks.front().planes.front().width = invalid.width;
  invalid.blocks.front().planes.front().height = invalid.height;
  const std::size_t over_hard_packed_bytes =
      (static_cast<std::size_t>(invalid.width) * invalid.height + 1U) / 2U;
  invalid.blocks.front().planes.front().bytes.assign(over_hard_packed_bytes,
                                                     std::byte{0});
  CheckError(
      invalid, valid,
      TdxIndexed4CandidateDebugImageErrorCode::SourceByteLimitExceeded,
      "the first exact packed rectangle above 8 MiB fails the source hard cap");
}
} // namespace

int main() {
  CheckErrorContract();
  CheckAllCandidateMappings();
  CheckOddCardinality();
  CheckDeterminismAndOwnership();
  CheckValidationPriorityAndLimits();

  if (failures == 0) {
    std::cout << "omega_tdx_indexed4_candidate_debug_image_tests: all checks "
                 "passed\n";
  }
  return failures == 0 ? 0 : 1;
}
