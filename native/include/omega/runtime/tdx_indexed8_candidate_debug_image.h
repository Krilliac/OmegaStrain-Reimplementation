#pragma once

#include "omega/asset/texture_storage_ir.h"
#include "omega/runtime/debug_image.h"

#include <cstdint>
#include <expected>
#include <string_view>

namespace omega::runtime {
// Candidate policies are project diagnostics only. Their names describe
// byte-slot operations, not retail color, alpha, row-origin, swizzle, or
// display semantics.
enum class TdxIndexed8ClutPermutationCandidate : std::uint8_t {
  Identity,
  SwapBitsThreeAndFour,
};

enum class TdxIndexed8SourceChannelCandidate : std::uint8_t {
  SourceSlots012,
  SourceSlots021,
  SourceSlots102,
  SourceSlots120,
  SourceSlots201,
  SourceSlots210,
};

enum class TdxIndexed8AlphaCandidate : std::uint8_t {
  Opaque,
  SourceSlot3,
  SourceSlot3TimesTwoClamped,
};

enum class TdxIndexed8SourceLayoutCandidate : std::uint8_t {
  LinearRowsTopToBottom,
  LinearRowsBottomToTop,
};

struct TdxIndexed8CandidatePolicy {
  constexpr TdxIndexed8CandidatePolicy(
      const TdxIndexed8ClutPermutationCandidate clut,
      const TdxIndexed8SourceChannelCandidate channels,
      const TdxIndexed8AlphaCandidate alpha,
      const TdxIndexed8SourceLayoutCandidate layout) noexcept
      : clut_permutation(clut), source_channels(channels), alpha_mapping(alpha),
        source_layout(layout) {}

  TdxIndexed8ClutPermutationCandidate clut_permutation;
  TdxIndexed8SourceChannelCandidate source_channels;
  TdxIndexed8AlphaCandidate alpha_mapping;
  TdxIndexed8SourceLayoutCandidate source_layout;
};

inline constexpr std::uint64_t kTdxIndexed8CandidateHardMaximumSourceBytes =
    16ULL * 1024ULL * 1024ULL + 256ULL * 4ULL;
inline constexpr std::uint64_t kTdxIndexed8CandidateHardMaximumOutputBytes =
    64ULL * 1024ULL * 1024ULL;

struct TdxIndexed8CandidateDebugImageLimits {
  // Caller budgets may tighten but never exceed these synthetic project hard
  // maxima.
  std::uint64_t maximum_source_bytes =
      kTdxIndexed8CandidateHardMaximumSourceBytes;
  std::uint64_t maximum_output_bytes =
      kTdxIndexed8CandidateHardMaximumOutputBytes;
};

enum class TdxIndexed8CandidateDebugImageErrorCode {
  InvalidClutPermutationCandidate,
  InvalidSourceChannelCandidate,
  InvalidAlphaCandidate,
  InvalidSourceLayoutCandidate,
  InvalidLimits,
  InvalidTextureDimensions,
  InvalidSampleEncoding,
  UnsupportedSampleEncoding,
  BlockCountMismatch,
  PlaneCountMismatch,
  MissingPalette,
  InvalidPlaneDimensions,
  InvalidTransferElementEncoding,
  UnsupportedTransferElementEncoding,
  TexturePlaneDimensionMismatch,
  SourceByteSizeOverflow,
  OutputByteSizeOverflow,
  IndexByteSizeMismatch,
  InvalidPaletteDimensions,
  PaletteEntryCountMismatch,
  PaletteCardinalityMismatch,
  SourceByteLimitExceeded,
  OutputByteLimitExceeded,
  AllocationFailed,
};

[[nodiscard]] constexpr std::string_view
TdxIndexed8CandidateDebugImageErrorCodeName(
    const TdxIndexed8CandidateDebugImageErrorCode code) noexcept {
  switch (code) {
  case TdxIndexed8CandidateDebugImageErrorCode::InvalidClutPermutationCandidate:
    return "invalid-clut-permutation-candidate";
  case TdxIndexed8CandidateDebugImageErrorCode::InvalidSourceChannelCandidate:
    return "invalid-source-channel-candidate";
  case TdxIndexed8CandidateDebugImageErrorCode::InvalidAlphaCandidate:
    return "invalid-alpha-candidate";
  case TdxIndexed8CandidateDebugImageErrorCode::InvalidSourceLayoutCandidate:
    return "invalid-source-layout-candidate";
  case TdxIndexed8CandidateDebugImageErrorCode::InvalidLimits:
    return "invalid-limits";
  case TdxIndexed8CandidateDebugImageErrorCode::InvalidTextureDimensions:
    return "invalid-texture-dimensions";
  case TdxIndexed8CandidateDebugImageErrorCode::InvalidSampleEncoding:
    return "invalid-sample-encoding";
  case TdxIndexed8CandidateDebugImageErrorCode::UnsupportedSampleEncoding:
    return "unsupported-sample-encoding";
  case TdxIndexed8CandidateDebugImageErrorCode::BlockCountMismatch:
    return "block-count-mismatch";
  case TdxIndexed8CandidateDebugImageErrorCode::PlaneCountMismatch:
    return "plane-count-mismatch";
  case TdxIndexed8CandidateDebugImageErrorCode::MissingPalette:
    return "missing-palette";
  case TdxIndexed8CandidateDebugImageErrorCode::InvalidPlaneDimensions:
    return "invalid-plane-dimensions";
  case TdxIndexed8CandidateDebugImageErrorCode::InvalidTransferElementEncoding:
    return "invalid-transfer-element-encoding";
  case TdxIndexed8CandidateDebugImageErrorCode::
      UnsupportedTransferElementEncoding:
    return "unsupported-transfer-element-encoding";
  case TdxIndexed8CandidateDebugImageErrorCode::TexturePlaneDimensionMismatch:
    return "texture-plane-dimension-mismatch";
  case TdxIndexed8CandidateDebugImageErrorCode::SourceByteSizeOverflow:
    return "source-byte-size-overflow";
  case TdxIndexed8CandidateDebugImageErrorCode::OutputByteSizeOverflow:
    return "output-byte-size-overflow";
  case TdxIndexed8CandidateDebugImageErrorCode::IndexByteSizeMismatch:
    return "index-byte-size-mismatch";
  case TdxIndexed8CandidateDebugImageErrorCode::InvalidPaletteDimensions:
    return "invalid-palette-dimensions";
  case TdxIndexed8CandidateDebugImageErrorCode::PaletteEntryCountMismatch:
    return "palette-entry-count-mismatch";
  case TdxIndexed8CandidateDebugImageErrorCode::PaletteCardinalityMismatch:
    return "palette-cardinality-mismatch";
  case TdxIndexed8CandidateDebugImageErrorCode::SourceByteLimitExceeded:
    return "source-byte-limit-exceeded";
  case TdxIndexed8CandidateDebugImageErrorCode::OutputByteLimitExceeded:
    return "output-byte-limit-exceeded";
  case TdxIndexed8CandidateDebugImageErrorCode::AllocationFailed:
    return "allocation-failed";
  }
  return "unknown";
}

[[nodiscard]] constexpr std::string_view
TdxIndexed8CandidateDebugImageErrorMessage(
    const TdxIndexed8CandidateDebugImageErrorCode code) noexcept {
  switch (code) {
  case TdxIndexed8CandidateDebugImageErrorCode::InvalidClutPermutationCandidate:
    return "indexed-8 TDX candidate CLUT permutation is invalid";
  case TdxIndexed8CandidateDebugImageErrorCode::InvalidSourceChannelCandidate:
    return "indexed-8 TDX candidate source-channel mapping is invalid";
  case TdxIndexed8CandidateDebugImageErrorCode::InvalidAlphaCandidate:
    return "indexed-8 TDX candidate alpha mapping is invalid";
  case TdxIndexed8CandidateDebugImageErrorCode::InvalidSourceLayoutCandidate:
    return "indexed-8 TDX candidate source layout is invalid";
  case TdxIndexed8CandidateDebugImageErrorCode::InvalidLimits:
    return "indexed-8 TDX candidate limits exceed the project hard maxima";
  case TdxIndexed8CandidateDebugImageErrorCode::InvalidTextureDimensions:
    return "indexed-8 TDX candidate requires nonzero texture dimensions";
  case TdxIndexed8CandidateDebugImageErrorCode::InvalidSampleEncoding:
    return "indexed-8 TDX candidate sample encoding is invalid";
  case TdxIndexed8CandidateDebugImageErrorCode::UnsupportedSampleEncoding:
    return "indexed-8 TDX candidate requires indexed-8 sample encoding";
  case TdxIndexed8CandidateDebugImageErrorCode::BlockCountMismatch:
    return "indexed-8 TDX candidate requires exactly one block";
  case TdxIndexed8CandidateDebugImageErrorCode::PlaneCountMismatch:
    return "indexed-8 TDX candidate requires exactly one plane";
  case TdxIndexed8CandidateDebugImageErrorCode::MissingPalette:
    return "indexed-8 TDX candidate requires one palette";
  case TdxIndexed8CandidateDebugImageErrorCode::InvalidPlaneDimensions:
    return "indexed-8 TDX candidate requires nonzero plane dimensions";
  case TdxIndexed8CandidateDebugImageErrorCode::InvalidTransferElementEncoding:
    return "indexed-8 TDX candidate transfer-element encoding is invalid";
  case TdxIndexed8CandidateDebugImageErrorCode::
      UnsupportedTransferElementEncoding:
    return "indexed-8 TDX candidate requires packed-8 transfer-element "
           "encoding";
  case TdxIndexed8CandidateDebugImageErrorCode::TexturePlaneDimensionMismatch:
    return "indexed-8 TDX candidate texture and plane dimensions do not match";
  case TdxIndexed8CandidateDebugImageErrorCode::SourceByteSizeOverflow:
    return "indexed-8 TDX candidate source byte size overflows";
  case TdxIndexed8CandidateDebugImageErrorCode::OutputByteSizeOverflow:
    return "indexed-8 TDX candidate output byte size overflows";
  case TdxIndexed8CandidateDebugImageErrorCode::IndexByteSizeMismatch:
    return "indexed-8 TDX candidate index byte size does not match the "
           "transfer rectangle";
  case TdxIndexed8CandidateDebugImageErrorCode::InvalidPaletteDimensions:
    return "indexed-8 TDX candidate requires nonzero palette dimensions";
  case TdxIndexed8CandidateDebugImageErrorCode::PaletteEntryCountMismatch:
    return "indexed-8 TDX candidate palette dimensions do not match its entry "
           "count";
  case TdxIndexed8CandidateDebugImageErrorCode::PaletteCardinalityMismatch:
    return "indexed-8 TDX candidate requires exactly 256 palette entries";
  case TdxIndexed8CandidateDebugImageErrorCode::SourceByteLimitExceeded:
    return "indexed-8 TDX candidate exceeds the source-byte limit";
  case TdxIndexed8CandidateDebugImageErrorCode::OutputByteLimitExceeded:
    return "indexed-8 TDX candidate exceeds the output-byte limit";
  case TdxIndexed8CandidateDebugImageErrorCode::AllocationFailed:
    return "indexed-8 TDX candidate allocation failed";
  }
  return "indexed-8 TDX candidate error is unknown";
}

struct TdxIndexed8CandidateDebugImageError {
  TdxIndexed8CandidateDebugImageErrorCode code;
  // Fixed category-only text; it contains no dimensions, payload, offset, or
  // source identity.
  std::string_view message;
};

// [any worker thread; reentrant] Statelessly borrows one strict direct
// Indexed8/Packed8 shape and returns independently owned four-slot diagnostic
// pixels. Every unresolved display choice is an explicit caller-selected
// candidate. No I/O, platform, GPU, service, or shared-state work occurs.
[[nodiscard]] std::expected<DebugImage, TdxIndexed8CandidateDebugImageError>
BuildTdxIndexed8CandidateDebugImage(
    const asset::TextureStorageIR &storage,
    const TdxIndexed8CandidatePolicy &policy,
    const TdxIndexed8CandidateDebugImageLimits &limits = {});
} // namespace omega::runtime
