#include "omega/runtime/tdx_indexed8_candidate_debug_image.h"

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
using omega::runtime::TdxIndexed8AlphaCandidate;
using omega::runtime::TdxIndexed8CandidateDebugImageError;
using omega::runtime::TdxIndexed8CandidateDebugImageErrorCode;
using omega::runtime::TdxIndexed8CandidateDebugImageLimits;
using omega::runtime::TdxIndexed8CandidatePolicy;
using omega::runtime::TdxIndexed8ClutPermutationCandidate;
using omega::runtime::TdxIndexed8SourceChannelCandidate;
using omega::runtime::TdxIndexed8SourceLayoutCandidate;

using BuilderSignature =
    std::expected<DebugImage, TdxIndexed8CandidateDebugImageError> (*)(
        const TextureStorageIR &, const TdxIndexed8CandidatePolicy &,
        const TdxIndexed8CandidateDebugImageLimits &);
static_assert(std::is_same_v<
              decltype(&omega::runtime::BuildTdxIndexed8CandidateDebugImage),
              BuilderSignature>);
static_assert(!std::is_default_constructible_v<TdxIndexed8CandidatePolicy>);
static_assert(
    noexcept(omega::runtime::TdxIndexed8CandidateDebugImageErrorCodeName(
        TdxIndexed8CandidateDebugImageErrorCode::
            InvalidClutPermutationCandidate)));
static_assert(
    noexcept(omega::runtime::TdxIndexed8CandidateDebugImageErrorMessage(
        TdxIndexed8CandidateDebugImageErrorCode::
            InvalidClutPermutationCandidate)));

int failures = 0;

void Check(const bool condition, const std::string_view message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    ++failures;
  }
}

[[nodiscard]] constexpr TdxIndexed8CandidatePolicy IdentityPolicy() noexcept {
  return TdxIndexed8CandidatePolicy{
      TdxIndexed8ClutPermutationCandidate::Identity,
      TdxIndexed8SourceChannelCandidate::SourceSlots012,
      TdxIndexed8AlphaCandidate::SourceSlot3,
      TdxIndexed8SourceLayoutCandidate::LinearRowsTopToBottom,
  };
}

[[nodiscard]] constexpr std::array<std::byte, 4>
PaletteEntry(const std::uint8_t index, const std::uint8_t seed = 0U) noexcept {
  return {
      static_cast<std::byte>(static_cast<std::uint8_t>(index + seed)),
      static_cast<std::byte>(static_cast<std::uint8_t>((index ^ 0x5aU) + seed)),
      static_cast<std::byte>(static_cast<std::uint8_t>((0xffU - index) + seed)),
      static_cast<std::byte>(static_cast<std::uint8_t>(index + seed)),
  };
}

[[nodiscard]] TextureStorageIR
MakeStorage(const std::uint8_t palette_seed = 0U) {
  constexpr std::uint32_t width = 16U;
  constexpr std::uint32_t height = 16U;
  std::vector<std::byte> indices(width * height);
  std::vector<std::array<std::byte, 4>> entries(256U);
  for (std::size_t index = 0U; index < indices.size(); ++index) {
    indices[index] = static_cast<std::byte>(static_cast<std::uint8_t>(index));
    entries[index] =
        PaletteEntry(static_cast<std::uint8_t>(index), palette_seed);
  }
  return TextureStorageIR{
      .width = width,
      .height = height,
      .sample_encoding = TextureSampleEncoding::Indexed8,
      .blocks =
          {
              TextureStorageBlockIR{
                  .planes =
                      {
                          TextureStoragePlaneIR{
                              .width = width,
                              .height = height,
                              .element_encoding =
                                  TextureTransferElementEncoding::Packed8,
                              .bytes = std::move(indices),
                          },
                      },
                  .palette =
                      TexturePaletteStorageIR{
                          .width = 16U,
                          .height = 16U,
                          .entries = std::move(entries),
                      },
              },
          },
  };
}

[[nodiscard]] constexpr std::uint8_t
PermuteIndex(const std::uint8_t index,
             const TdxIndexed8ClutPermutationCandidate candidate) noexcept {
  if (candidate == TdxIndexed8ClutPermutationCandidate::Identity)
    return index;
  return static_cast<std::uint8_t>((index & 0xe7U) | ((index & 0x08U) << 1U) |
                                   ((index & 0x10U) >> 1U));
}

[[nodiscard]] constexpr std::array<std::size_t, 3>
ChannelSlots(const TdxIndexed8SourceChannelCandidate candidate) noexcept {
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
ExpectedAlpha(const std::byte source,
              const TdxIndexed8AlphaCandidate candidate) noexcept {
  if (candidate == TdxIndexed8AlphaCandidate::Opaque)
    return std::byte{0xff};
  if (candidate == TdxIndexed8AlphaCandidate::SourceSlot3)
    return source;
  const std::uint16_t doubled =
      static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(source)) * 2U;
  return static_cast<std::byte>(std::min<std::uint16_t>(doubled, 0xffU));
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
              const TdxIndexed8CandidatePolicy &policy,
              const std::size_t output_pixel) {
  const std::size_t width = storage.width;
  const std::size_t height = storage.height;
  const std::size_t output_y = output_pixel / width;
  const std::size_t x = output_pixel % width;
  const std::size_t source_y =
      policy.source_layout ==
              TdxIndexed8SourceLayoutCandidate::LinearRowsTopToBottom
          ? output_y
          : height - 1U - output_y;
  const std::size_t source_pixel = source_y * width + x;
  const auto &block = storage.blocks.front();
  const std::uint8_t source_index =
      std::to_integer<std::uint8_t>(block.planes.front().bytes[source_pixel]);
  const auto &entry =
      block.palette
          ->entries[PermuteIndex(source_index, policy.clut_permutation)];
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
                                     const TdxIndexed8CandidatePolicy &policy) {
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
                const TdxIndexed8CandidatePolicy &policy,
                const TdxIndexed8CandidateDebugImageErrorCode code,
                const std::string_view message,
                const TdxIndexed8CandidateDebugImageLimits &limits = {}) {
  const std::expected<DebugImage, TdxIndexed8CandidateDebugImageError> result =
      omega::runtime::BuildTdxIndexed8CandidateDebugImage(storage, policy,
                                                          limits);
  Check(
      !result && result.error().code == code &&
          result.error().message ==
              omega::runtime::TdxIndexed8CandidateDebugImageErrorMessage(code),
      message);
}

void CheckErrorContract() {
  struct ErrorContract {
    TdxIndexed8CandidateDebugImageErrorCode code;
    std::string_view name;
    std::string_view message;
  };
  constexpr std::array contracts{
      ErrorContract{TdxIndexed8CandidateDebugImageErrorCode::
                        InvalidClutPermutationCandidate,
                    "invalid-clut-permutation-candidate",
                    "indexed-8 TDX candidate CLUT permutation is invalid"},
      ErrorContract{
          TdxIndexed8CandidateDebugImageErrorCode::
              InvalidSourceChannelCandidate,
          "invalid-source-channel-candidate",
          "indexed-8 TDX candidate source-channel mapping is invalid"},
      ErrorContract{
          TdxIndexed8CandidateDebugImageErrorCode::InvalidAlphaCandidate,
          "invalid-alpha-candidate",
          "indexed-8 TDX candidate alpha mapping is invalid"},
      ErrorContract{
          TdxIndexed8CandidateDebugImageErrorCode::InvalidSourceLayoutCandidate,
          "invalid-source-layout-candidate",
          "indexed-8 TDX candidate source layout is invalid"},
      ErrorContract{
          TdxIndexed8CandidateDebugImageErrorCode::InvalidLimits,
          "invalid-limits",
          "indexed-8 TDX candidate limits exceed the project hard maxima"},
      ErrorContract{
          TdxIndexed8CandidateDebugImageErrorCode::InvalidTextureDimensions,
          "invalid-texture-dimensions",
          "indexed-8 TDX candidate requires nonzero texture dimensions"},
      ErrorContract{
          TdxIndexed8CandidateDebugImageErrorCode::InvalidSampleEncoding,
          "invalid-sample-encoding",
          "indexed-8 TDX candidate sample encoding is invalid"},
      ErrorContract{
          TdxIndexed8CandidateDebugImageErrorCode::UnsupportedSampleEncoding,
          "unsupported-sample-encoding",
          "indexed-8 TDX candidate requires indexed-8 sample encoding"},
      ErrorContract{TdxIndexed8CandidateDebugImageErrorCode::BlockCountMismatch,
                    "block-count-mismatch",
                    "indexed-8 TDX candidate requires exactly one block"},
      ErrorContract{TdxIndexed8CandidateDebugImageErrorCode::PlaneCountMismatch,
                    "plane-count-mismatch",
                    "indexed-8 TDX candidate requires exactly one plane"},
      ErrorContract{TdxIndexed8CandidateDebugImageErrorCode::MissingPalette,
                    "missing-palette",
                    "indexed-8 TDX candidate requires one palette"},
      ErrorContract{
          TdxIndexed8CandidateDebugImageErrorCode::InvalidPlaneDimensions,
          "invalid-plane-dimensions",
          "indexed-8 TDX candidate requires nonzero plane dimensions"},
      ErrorContract{
          TdxIndexed8CandidateDebugImageErrorCode::
              InvalidTransferElementEncoding,
          "invalid-transfer-element-encoding",
          "indexed-8 TDX candidate transfer-element encoding is invalid"},
      ErrorContract{TdxIndexed8CandidateDebugImageErrorCode::
                        UnsupportedTransferElementEncoding,
                    "unsupported-transfer-element-encoding",
                    "indexed-8 TDX candidate requires packed-8 "
                    "transfer-element encoding"},
      ErrorContract{
          TdxIndexed8CandidateDebugImageErrorCode::
              TexturePlaneDimensionMismatch,
          "texture-plane-dimension-mismatch",
          "indexed-8 TDX candidate texture and plane dimensions do not match"},
      ErrorContract{
          TdxIndexed8CandidateDebugImageErrorCode::SourceByteSizeOverflow,
          "source-byte-size-overflow",
          "indexed-8 TDX candidate source byte size overflows"},
      ErrorContract{
          TdxIndexed8CandidateDebugImageErrorCode::OutputByteSizeOverflow,
          "output-byte-size-overflow",
          "indexed-8 TDX candidate output byte size overflows"},
      ErrorContract{
          TdxIndexed8CandidateDebugImageErrorCode::IndexByteSizeMismatch,
          "index-byte-size-mismatch",
          "indexed-8 TDX candidate index byte size does not match the transfer "
          "rectangle"},
      ErrorContract{
          TdxIndexed8CandidateDebugImageErrorCode::InvalidPaletteDimensions,
          "invalid-palette-dimensions",
          "indexed-8 TDX candidate requires nonzero palette dimensions"},
      ErrorContract{
          TdxIndexed8CandidateDebugImageErrorCode::PaletteEntryCountMismatch,
          "palette-entry-count-mismatch",
          "indexed-8 TDX candidate palette dimensions do not match its entry "
          "count"},
      ErrorContract{
          TdxIndexed8CandidateDebugImageErrorCode::PaletteCardinalityMismatch,
          "palette-cardinality-mismatch",
          "indexed-8 TDX candidate requires exactly 256 palette entries"},
      ErrorContract{
          TdxIndexed8CandidateDebugImageErrorCode::SourceByteLimitExceeded,
          "source-byte-limit-exceeded",
          "indexed-8 TDX candidate exceeds the source-byte limit"},
      ErrorContract{
          TdxIndexed8CandidateDebugImageErrorCode::OutputByteLimitExceeded,
          "output-byte-limit-exceeded",
          "indexed-8 TDX candidate exceeds the output-byte limit"},
      ErrorContract{TdxIndexed8CandidateDebugImageErrorCode::AllocationFailed,
                    "allocation-failed",
                    "indexed-8 TDX candidate allocation failed"},
  };

  bool exact = true;
  for (std::size_t index = 0U; index < contracts.size(); ++index) {
    const ErrorContract &contract = contracts[index];
    exact = exact && static_cast<std::size_t>(contract.code) == index &&
            omega::runtime::TdxIndexed8CandidateDebugImageErrorCodeName(
                contract.code) == contract.name &&
            omega::runtime::TdxIndexed8CandidateDebugImageErrorMessage(
                contract.code) == contract.message;
  }
  Check(exact, "all 24 error ordinals, names, and fixed messages are frozen");

  constexpr auto unknown =
      static_cast<TdxIndexed8CandidateDebugImageErrorCode>(255U);
  Check(omega::runtime::TdxIndexed8CandidateDebugImageErrorCodeName(unknown) ==
                "unknown" &&
            omega::runtime::TdxIndexed8CandidateDebugImageErrorMessage(
                unknown) == "indexed-8 TDX candidate error is unknown",
        "unknown error values use fixed identity-free fallbacks");

  constexpr TdxIndexed8CandidateDebugImageLimits defaults;
  Check(defaults.maximum_source_bytes ==
                omega::runtime::kTdxIndexed8CandidateHardMaximumSourceBytes &&
            defaults.maximum_output_bytes ==
                omega::runtime::kTdxIndexed8CandidateHardMaximumOutputBytes &&
            defaults.maximum_source_bytes ==
                16ULL * 1024ULL * 1024ULL + 1024ULL &&
            defaults.maximum_output_bytes == 64ULL * 1024ULL * 1024ULL,
        "default budgets equal the non-overridable synthetic hard maxima");
}

void CheckAllCandidateMappings() {
  const TextureStorageIR storage = MakeStorage();
  const TextureStorageIR storage_before = storage;

  constexpr std::array clut_candidates{
      TdxIndexed8ClutPermutationCandidate::Identity,
      TdxIndexed8ClutPermutationCandidate::SwapBitsThreeAndFour,
  };
  std::array<std::vector<std::byte>, 2> clut_outputs;
  for (std::size_t index = 0U; index < clut_candidates.size(); ++index) {
    const TdxIndexed8CandidatePolicy policy{
        clut_candidates[index],
        TdxIndexed8SourceChannelCandidate::SourceSlots012,
        TdxIndexed8AlphaCandidate::SourceSlot3,
        TdxIndexed8SourceLayoutCandidate::LinearRowsTopToBottom};
    const auto image =
        omega::runtime::BuildTdxIndexed8CandidateDebugImage(storage, policy);
    Check(image && EveryPixelMatches(*image, storage, policy),
          "each CLUT candidate maps all 256 source indices exactly");
    if (image)
      clut_outputs[index] = image->rgba8_pixels;
  }
  Check(clut_outputs[0] != clut_outputs[1] &&
            PixelAt(DebugImage{16U, 16U, clut_outputs[0]}, 8U) !=
                PixelAt(DebugImage{16U, 16U, clut_outputs[1]}, 8U) &&
            PixelAt(DebugImage{16U, 16U, clut_outputs[0]}, 16U) !=
                PixelAt(DebugImage{16U, 16U, clut_outputs[1]}, 16U),
        "the bit-three/four candidate visibly permutes distinguishing palette "
        "entries");

  constexpr std::array channel_candidates{
      TdxIndexed8SourceChannelCandidate::SourceSlots012,
      TdxIndexed8SourceChannelCandidate::SourceSlots021,
      TdxIndexed8SourceChannelCandidate::SourceSlots102,
      TdxIndexed8SourceChannelCandidate::SourceSlots120,
      TdxIndexed8SourceChannelCandidate::SourceSlots201,
      TdxIndexed8SourceChannelCandidate::SourceSlots210,
  };
  std::array<std::array<std::byte, 4>, 6> sample_pixels{};
  for (std::size_t index = 0U; index < channel_candidates.size(); ++index) {
    const TdxIndexed8CandidatePolicy policy{
        TdxIndexed8ClutPermutationCandidate::Identity,
        channel_candidates[index], TdxIndexed8AlphaCandidate::SourceSlot3,
        TdxIndexed8SourceLayoutCandidate::LinearRowsTopToBottom};
    const auto image =
        omega::runtime::BuildTdxIndexed8CandidateDebugImage(storage, policy);
    Check(image && EveryPixelMatches(*image, storage, policy),
          "each source-channel candidate maps every source slot exactly");
    if (image)
      sample_pixels[index] = PixelAt(*image, 37U);
  }
  bool all_channel_candidates_distinct = true;
  for (std::size_t left = 0U; left < sample_pixels.size(); ++left) {
    for (std::size_t right = left + 1U; right < sample_pixels.size(); ++right) {
      all_channel_candidates_distinct =
          all_channel_candidates_distinct &&
          sample_pixels[left] != sample_pixels[right];
    }
  }
  Check(all_channel_candidates_distinct,
        "all six source-slot permutations are distinguishable on the generated "
        "palette");

  constexpr std::array alpha_candidates{
      TdxIndexed8AlphaCandidate::Opaque,
      TdxIndexed8AlphaCandidate::SourceSlot3,
      TdxIndexed8AlphaCandidate::SourceSlot3TimesTwoClamped,
  };
  std::array<std::vector<std::byte>, 3> alpha_outputs;
  for (std::size_t index = 0U; index < alpha_candidates.size(); ++index) {
    const TdxIndexed8CandidatePolicy policy{
        TdxIndexed8ClutPermutationCandidate::Identity,
        TdxIndexed8SourceChannelCandidate::SourceSlots012,
        alpha_candidates[index],
        TdxIndexed8SourceLayoutCandidate::LinearRowsTopToBottom};
    const auto image =
        omega::runtime::BuildTdxIndexed8CandidateDebugImage(storage, policy);
    Check(image && EveryPixelMatches(*image, storage, policy),
          "each alpha candidate maps all 256 source-slot-three values exactly");
    if (image)
      alpha_outputs[index] = image->rgba8_pixels;
  }
  Check(alpha_outputs[0] != alpha_outputs[1] &&
            alpha_outputs[1] != alpha_outputs[2] &&
            PixelAt(DebugImage{16U, 16U, alpha_outputs[0]}, 64U)[3] ==
                std::byte{0xff} &&
            PixelAt(DebugImage{16U, 16U, alpha_outputs[1]}, 64U)[3] ==
                std::byte{64U} &&
            PixelAt(DebugImage{16U, 16U, alpha_outputs[2]}, 64U)[3] ==
                std::byte{128U} &&
            PixelAt(DebugImage{16U, 16U, alpha_outputs[2]}, 200U)[3] ==
                std::byte{0xff},
        "opaque, unchanged, doubled, and doubled-clamped alpha candidates are "
        "distinct");

  constexpr std::array layout_candidates{
      TdxIndexed8SourceLayoutCandidate::LinearRowsTopToBottom,
      TdxIndexed8SourceLayoutCandidate::LinearRowsBottomToTop,
  };
  std::array<std::vector<std::byte>, 2> layout_outputs;
  for (std::size_t index = 0U; index < layout_candidates.size(); ++index) {
    const TdxIndexed8CandidatePolicy policy{
        TdxIndexed8ClutPermutationCandidate::Identity,
        TdxIndexed8SourceChannelCandidate::SourceSlots012,
        TdxIndexed8AlphaCandidate::SourceSlot3, layout_candidates[index]};
    const auto image =
        omega::runtime::BuildTdxIndexed8CandidateDebugImage(storage, policy);
    Check(image && EveryPixelMatches(*image, storage, policy),
          "each linear row-orientation candidate maps all 256 texels exactly");
    if (image)
      layout_outputs[index] = image->rgba8_pixels;
  }
  const DebugImage top_down{16U, 16U, layout_outputs[0]};
  const DebugImage bottom_up{16U, 16U, layout_outputs[1]};
  Check(
      layout_outputs[0] != layout_outputs[1] &&
          PixelAt(top_down, 0U) == PixelAt(bottom_up, 240U) &&
          PixelAt(top_down, 15U) == PixelAt(bottom_up, 255U) &&
          PixelAt(top_down, 240U) == PixelAt(bottom_up, 0U),
      "bottom-up reverses whole linear rows without changing intra-row order");

  Check(storage == storage_before,
        "candidate projection never mutates its borrowed texture storage");
}

void CheckDistinctPalettesDeterminismAndOwnership() {
  TextureStorageIR first_storage = MakeStorage(0x00U);
  TextureStorageIR second_storage = MakeStorage(0x31U);
  const TdxIndexed8CandidatePolicy policy = IdentityPolicy();
  auto first = omega::runtime::BuildTdxIndexed8CandidateDebugImage(
      first_storage, policy);
  auto repeated = omega::runtime::BuildTdxIndexed8CandidateDebugImage(
      first_storage, policy);
  const auto second = omega::runtime::BuildTdxIndexed8CandidateDebugImage(
      second_storage, policy);
  Check(first && repeated && second, "two distinguishing palettes and a "
                                     "repeated call produce diagnostic images");
  if (!first || !repeated || !second)
    return;

  Check(first->width == 16U && first->height == 16U &&
            first->rgba8_pixels.size() == 1024U,
        "the 16x16 fixture produces exactly 1024 owned output bytes");
  Check(first->rgba8_pixels == repeated->rgba8_pixels &&
            first->rgba8_pixels.data() != repeated->rgba8_pixels.data(),
        "repeated projections are byte-deterministic and independently "
        "allocated");
  Check(first->rgba8_pixels != second->rgba8_pixels &&
            PixelAt(*first, 0U) != PixelAt(*second, 0U) &&
            PixelAt(*first, 255U) != PixelAt(*second, 255U),
        "different generated palettes remain payload-sensitive at both ends");

  const std::vector<std::byte> owned_before_mutation = first->rgba8_pixels;
  std::ranges::fill(first_storage.blocks.front().planes.front().bytes,
                    std::byte{0xff});
  std::ranges::fill(first_storage.blocks.front().palette->entries,
                    std::array<std::byte, 4>{std::byte{0}, std::byte{0},
                                             std::byte{0}, std::byte{0}});
  first_storage.blocks.clear();
  Check(first->rgba8_pixels == owned_before_mutation,
        "the returned image survives source payload mutation and block "
        "destruction");
}

void CheckPoliciesValidationAndLimits() {
  const TextureStorageIR storage = MakeStorage();
  const TdxIndexed8CandidatePolicy valid = IdentityPolicy();

  CheckError(
      storage,
      TdxIndexed8CandidatePolicy{
          static_cast<TdxIndexed8ClutPermutationCandidate>(255U),
          TdxIndexed8SourceChannelCandidate::SourceSlots012,
          TdxIndexed8AlphaCandidate::SourceSlot3,
          TdxIndexed8SourceLayoutCandidate::LinearRowsTopToBottom},
      TdxIndexed8CandidateDebugImageErrorCode::InvalidClutPermutationCandidate,
      "an unknown CLUT candidate fails before all other validation");
  CheckError(
      storage,
      TdxIndexed8CandidatePolicy{
          TdxIndexed8ClutPermutationCandidate::Identity,
          static_cast<TdxIndexed8SourceChannelCandidate>(255U),
          TdxIndexed8AlphaCandidate::SourceSlot3,
          TdxIndexed8SourceLayoutCandidate::LinearRowsTopToBottom},
      TdxIndexed8CandidateDebugImageErrorCode::InvalidSourceChannelCandidate,
      "an unknown source-channel candidate has a typed failure");
  CheckError(storage,
             TdxIndexed8CandidatePolicy{
                 TdxIndexed8ClutPermutationCandidate::Identity,
                 TdxIndexed8SourceChannelCandidate::SourceSlots012,
                 static_cast<TdxIndexed8AlphaCandidate>(255U),
                 TdxIndexed8SourceLayoutCandidate::LinearRowsTopToBottom},
             TdxIndexed8CandidateDebugImageErrorCode::InvalidAlphaCandidate,
             "an unknown alpha candidate has a typed failure");
  CheckError(
      storage,
      TdxIndexed8CandidatePolicy{
          TdxIndexed8ClutPermutationCandidate::Identity,
          TdxIndexed8SourceChannelCandidate::SourceSlots012,
          TdxIndexed8AlphaCandidate::SourceSlot3,
          static_cast<TdxIndexed8SourceLayoutCandidate>(255U)},
      TdxIndexed8CandidateDebugImageErrorCode::InvalidSourceLayoutCandidate,
      "an unknown source-layout candidate has a typed failure");

  auto invalid_limits = TdxIndexed8CandidateDebugImageLimits{};
  ++invalid_limits.maximum_source_bytes;
  CheckError(
      storage, valid, TdxIndexed8CandidateDebugImageErrorCode::InvalidLimits,
      "a source budget cannot raise the project hard maximum", invalid_limits);
  invalid_limits = TdxIndexed8CandidateDebugImageLimits{};
  ++invalid_limits.maximum_output_bytes;
  CheckError(
      storage, valid, TdxIndexed8CandidateDebugImageErrorCode::InvalidLimits,
      "an output budget cannot raise the project hard maximum", invalid_limits);

  TextureStorageIR invalid = storage;
  invalid.width = 0U;
  invalid.sample_encoding = static_cast<TextureSampleEncoding>(255U);
  invalid.blocks.clear();
  CheckError(invalid, valid,
             TdxIndexed8CandidateDebugImageErrorCode::InvalidTextureDimensions,
             "texture dimensions have first storage-validation priority");

  invalid = storage;
  invalid.sample_encoding = static_cast<TextureSampleEncoding>(255U);
  invalid.blocks.clear();
  CheckError(invalid, valid,
             TdxIndexed8CandidateDebugImageErrorCode::InvalidSampleEncoding,
             "an unknown sample enum fails before structure");
  for (const TextureSampleEncoding encoding :
       {TextureSampleEncoding::Indexed4, TextureSampleEncoding::Packed24,
        TextureSampleEncoding::Packed32}) {
    invalid = storage;
    invalid.sample_encoding = encoding;
    invalid.blocks.clear();
    CheckError(
        invalid, valid,
        TdxIndexed8CandidateDebugImageErrorCode::UnsupportedSampleEncoding,
        "known non-Indexed8 sample encodings fail before structure");
  }

  invalid = storage;
  invalid.blocks.clear();
  CheckError(invalid, valid,
             TdxIndexed8CandidateDebugImageErrorCode::BlockCountMismatch,
             "zero blocks fail exact cardinality");
  invalid = storage;
  invalid.blocks.push_back(TextureStorageBlockIR{});
  CheckError(invalid, valid,
             TdxIndexed8CandidateDebugImageErrorCode::BlockCountMismatch,
             "multiple blocks fail without selecting a first block");

  invalid = storage;
  invalid.blocks.front().planes.clear();
  invalid.blocks.front().palette.reset();
  CheckError(invalid, valid,
             TdxIndexed8CandidateDebugImageErrorCode::PlaneCountMismatch,
             "plane cardinality precedes palette presence");
  invalid = storage;
  invalid.blocks.front().planes.push_back(TextureStoragePlaneIR{});
  CheckError(invalid, valid,
             TdxIndexed8CandidateDebugImageErrorCode::PlaneCountMismatch,
             "multiple planes fail without selecting a first plane");
  invalid = storage;
  invalid.blocks.front().palette.reset();
  invalid.blocks.front().planes.front().width = 0U;
  CheckError(invalid, valid,
             TdxIndexed8CandidateDebugImageErrorCode::MissingPalette,
             "palette presence precedes plane validation");

  invalid = storage;
  invalid.blocks.front().planes.front().width = 0U;
  invalid.blocks.front().planes.front().element_encoding =
      static_cast<TextureTransferElementEncoding>(255U);
  CheckError(invalid, valid,
             TdxIndexed8CandidateDebugImageErrorCode::InvalidPlaneDimensions,
             "plane dimensions precede transfer-element validation");
  invalid = storage;
  invalid.blocks.front().planes.front().element_encoding =
      static_cast<TextureTransferElementEncoding>(255U);
  invalid.blocks.front().planes.front().width = 15U;
  CheckError(
      invalid, valid,
      TdxIndexed8CandidateDebugImageErrorCode::InvalidTransferElementEncoding,
      "an unknown transfer enum fails before dimension matching");
  for (const TextureTransferElementEncoding encoding :
       {TextureTransferElementEncoding::Packed4,
        TextureTransferElementEncoding::Packed24,
        TextureTransferElementEncoding::Packed32}) {
    invalid = storage;
    invalid.blocks.front().planes.front().element_encoding = encoding;
    invalid.blocks.front().planes.front().width = 15U;
    CheckError(
        invalid, valid,
        TdxIndexed8CandidateDebugImageErrorCode::
            UnsupportedTransferElementEncoding,
        "known non-Packed8 transfer encodings fail before dimension matching");
  }
  invalid = storage;
  invalid.blocks.front().planes.front().width = 15U;
  CheckError(
      invalid, valid,
      TdxIndexed8CandidateDebugImageErrorCode::TexturePlaneDimensionMismatch,
      "texture and plane rectangles must match exactly");

  invalid = storage;
  invalid.width = std::numeric_limits<std::uint32_t>::max();
  invalid.height = 0x40000001U;
  invalid.blocks.front().planes.front().width = invalid.width;
  invalid.blocks.front().planes.front().height = invalid.height;
  invalid.blocks.front().planes.front().bytes.clear();
  CheckError(invalid, valid,
             TdxIndexed8CandidateDebugImageErrorCode::OutputByteSizeOverflow,
             "output-byte multiplication has a no-allocation overflow oracle");

  invalid = storage;
  invalid.blocks.front().planes.front().bytes.pop_back();
  const TdxIndexed8CandidateDebugImageLimits zero_limits{
      .maximum_source_bytes = 0U,
      .maximum_output_bytes = 0U,
  };
  CheckError(invalid, valid,
             TdxIndexed8CandidateDebugImageErrorCode::IndexByteSizeMismatch,
             "a short index plane fails before both caller budgets",
             zero_limits);
  invalid = storage;
  invalid.blocks.front().planes.front().bytes.push_back(std::byte{0});
  CheckError(invalid, valid,
             TdxIndexed8CandidateDebugImageErrorCode::IndexByteSizeMismatch,
             "an extra index byte fails exact transfer-rectangle cardinality");

  invalid = storage;
  invalid.blocks.front().palette->width = 0U;
  invalid.blocks.front().palette->entries.clear();
  CheckError(invalid, valid,
             TdxIndexed8CandidateDebugImageErrorCode::InvalidPaletteDimensions,
             "present palettes require nonzero dimensions");
  invalid = storage;
  invalid.blocks.front().palette->entries.pop_back();
  CheckError(invalid, valid,
             TdxIndexed8CandidateDebugImageErrorCode::PaletteEntryCountMismatch,
             "palette rectangle and owned entry count must match exactly");
  invalid = storage;
  invalid.blocks.front().palette->width = 15U;
  invalid.blocks.front().palette->height = 17U;
  invalid.blocks.front().palette->entries.resize(255U);
  CheckError(
      invalid, valid,
      TdxIndexed8CandidateDebugImageErrorCode::PaletteCardinalityMismatch,
      "an internally exact 255-entry palette still fails the 256-entry "
      "contract");
  invalid = storage;
  invalid.blocks.front().palette->width = 8U;
  invalid.blocks.front().palette->height = 32U;
  Check(omega::runtime::BuildTdxIndexed8CandidateDebugImage(invalid, valid)
            .has_value(),
        "an alternate exact 8x32 palette rectangle is accepted without "
        "assigning layout meaning");

  const TdxIndexed8CandidateDebugImageLimits exact_limits{
      .maximum_source_bytes = 1280U,
      .maximum_output_bytes = 1024U,
  };
  const auto exact = omega::runtime::BuildTdxIndexed8CandidateDebugImage(
      storage, valid, exact_limits);
  Check(exact && EveryPixelMatches(*exact, storage, valid),
        "the exact 256 index plus 1024 palette source bytes and output budget "
        "succeed");
  auto tight_limits = exact_limits;
  --tight_limits.maximum_source_bytes;
  CheckError(storage, valid,
             TdxIndexed8CandidateDebugImageErrorCode::SourceByteLimitExceeded,
             "one below the exact aggregate source-byte budget is rejected",
             tight_limits);
  tight_limits = exact_limits;
  --tight_limits.maximum_output_bytes;
  CheckError(storage, valid,
             TdxIndexed8CandidateDebugImageErrorCode::OutputByteLimitExceeded,
             "one below the exact output-byte budget is rejected",
             tight_limits);
}
} // namespace

int main() {
  CheckErrorContract();
  CheckAllCandidateMappings();
  CheckDistinctPalettesDeterminismAndOwnership();
  CheckPoliciesValidationAndLimits();

  if (failures == 0)
    std::cout << "omega_tdx_indexed8_candidate_debug_image_tests: all checks "
                 "passed\n";
  return failures == 0 ? 0 : 1;
}
