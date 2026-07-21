#pragma once

#include "omega/asset/texture_storage_ir.h"
#include "omega/runtime/debug_image.h"

#include <cstdint>
#include <expected>
#include <string_view>

namespace omega::runtime {
// Candidate policies are project diagnostics only. Their names describe
// source-slot operations, not retail color, alpha, row-origin, swizzle, or
// display semantics.
enum class TdxIndexed4NibbleOrderCandidate : std::uint8_t {
  LowNibbleFirst,
  HighNibbleFirst,
};

enum class TdxIndexed4PalettePermutationCandidate : std::uint8_t {
  SourceOrderIdentity,
};

enum class TdxIndexed4SourceChannelCandidate : std::uint8_t {
  SourceSlots012,
  SourceSlots021,
  SourceSlots102,
  SourceSlots120,
  SourceSlots201,
  SourceSlots210,
};

enum class TdxIndexed4AlphaCandidate : std::uint8_t {
  Opaque,
  SourceSlot3,
  SourceSlot3TimesTwoClamped,
};

enum class TdxIndexed4SourceLayoutCandidate : std::uint8_t {
  LinearRowsTopToBottom,
  LinearRowsBottomToTop,
};

struct TdxIndexed4CandidatePolicy {
  constexpr TdxIndexed4CandidatePolicy(
      const TdxIndexed4NibbleOrderCandidate nibble,
      const TdxIndexed4PalettePermutationCandidate palette,
      const TdxIndexed4SourceChannelCandidate channels,
      const TdxIndexed4AlphaCandidate alpha,
      const TdxIndexed4SourceLayoutCandidate layout) noexcept
      : nibble_order(nibble), palette_permutation(palette),
        source_channels(channels), alpha_mapping(alpha), source_layout(layout) {
  }

  TdxIndexed4NibbleOrderCandidate nibble_order;
  TdxIndexed4PalettePermutationCandidate palette_permutation;
  TdxIndexed4SourceChannelCandidate source_channels;
  TdxIndexed4AlphaCandidate alpha_mapping;
  TdxIndexed4SourceLayoutCandidate source_layout;
};

inline constexpr std::uint64_t kTdxIndexed4CandidateHardMaximumSourceBytes =
    8ULL * 1024ULL * 1024ULL + 16ULL * 4ULL;
inline constexpr std::uint64_t kTdxIndexed4CandidateHardMaximumOutputBytes =
    64ULL * 1024ULL * 1024ULL;

struct TdxIndexed4CandidateDebugImageLimits {
  // Caller budgets may tighten but never exceed the synthetic 8 MiB packed
  // source plus exact 64-byte palette and 64 MiB output project maxima.
  std::uint64_t maximum_source_bytes =
      kTdxIndexed4CandidateHardMaximumSourceBytes;
  std::uint64_t maximum_output_bytes =
      kTdxIndexed4CandidateHardMaximumOutputBytes;
};

enum class TdxIndexed4CandidateDebugImageErrorCode {
  InvalidNibbleOrderCandidate,
  InvalidPalettePermutationCandidate,
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
TdxIndexed4CandidateDebugImageErrorCodeName(
    const TdxIndexed4CandidateDebugImageErrorCode code) noexcept {
  switch (code) {
  case TdxIndexed4CandidateDebugImageErrorCode::InvalidNibbleOrderCandidate:
    return "invalid-nibble-order-candidate";
  case TdxIndexed4CandidateDebugImageErrorCode::
      InvalidPalettePermutationCandidate:
    return "invalid-palette-permutation-candidate";
  case TdxIndexed4CandidateDebugImageErrorCode::InvalidSourceChannelCandidate:
    return "invalid-source-channel-candidate";
  case TdxIndexed4CandidateDebugImageErrorCode::InvalidAlphaCandidate:
    return "invalid-alpha-candidate";
  case TdxIndexed4CandidateDebugImageErrorCode::InvalidSourceLayoutCandidate:
    return "invalid-source-layout-candidate";
  case TdxIndexed4CandidateDebugImageErrorCode::InvalidLimits:
    return "invalid-limits";
  case TdxIndexed4CandidateDebugImageErrorCode::InvalidTextureDimensions:
    return "invalid-texture-dimensions";
  case TdxIndexed4CandidateDebugImageErrorCode::InvalidSampleEncoding:
    return "invalid-sample-encoding";
  case TdxIndexed4CandidateDebugImageErrorCode::UnsupportedSampleEncoding:
    return "unsupported-sample-encoding";
  case TdxIndexed4CandidateDebugImageErrorCode::BlockCountMismatch:
    return "block-count-mismatch";
  case TdxIndexed4CandidateDebugImageErrorCode::PlaneCountMismatch:
    return "plane-count-mismatch";
  case TdxIndexed4CandidateDebugImageErrorCode::MissingPalette:
    return "missing-palette";
  case TdxIndexed4CandidateDebugImageErrorCode::InvalidPlaneDimensions:
    return "invalid-plane-dimensions";
  case TdxIndexed4CandidateDebugImageErrorCode::InvalidTransferElementEncoding:
    return "invalid-transfer-element-encoding";
  case TdxIndexed4CandidateDebugImageErrorCode::
      UnsupportedTransferElementEncoding:
    return "unsupported-transfer-element-encoding";
  case TdxIndexed4CandidateDebugImageErrorCode::TexturePlaneDimensionMismatch:
    return "texture-plane-dimension-mismatch";
  case TdxIndexed4CandidateDebugImageErrorCode::SourceByteSizeOverflow:
    return "source-byte-size-overflow";
  case TdxIndexed4CandidateDebugImageErrorCode::OutputByteSizeOverflow:
    return "output-byte-size-overflow";
  case TdxIndexed4CandidateDebugImageErrorCode::IndexByteSizeMismatch:
    return "index-byte-size-mismatch";
  case TdxIndexed4CandidateDebugImageErrorCode::InvalidPaletteDimensions:
    return "invalid-palette-dimensions";
  case TdxIndexed4CandidateDebugImageErrorCode::PaletteEntryCountMismatch:
    return "palette-entry-count-mismatch";
  case TdxIndexed4CandidateDebugImageErrorCode::PaletteCardinalityMismatch:
    return "palette-cardinality-mismatch";
  case TdxIndexed4CandidateDebugImageErrorCode::SourceByteLimitExceeded:
    return "source-byte-limit-exceeded";
  case TdxIndexed4CandidateDebugImageErrorCode::OutputByteLimitExceeded:
    return "output-byte-limit-exceeded";
  case TdxIndexed4CandidateDebugImageErrorCode::AllocationFailed:
    return "allocation-failed";
  }
  return "unknown";
}

[[nodiscard]] constexpr std::string_view
TdxIndexed4CandidateDebugImageErrorMessage(
    const TdxIndexed4CandidateDebugImageErrorCode code) noexcept {
  switch (code) {
  case TdxIndexed4CandidateDebugImageErrorCode::InvalidNibbleOrderCandidate:
    return "indexed-4 TDX candidate nibble order is invalid";
  case TdxIndexed4CandidateDebugImageErrorCode::
      InvalidPalettePermutationCandidate:
    return "indexed-4 TDX candidate palette permutation is invalid";
  case TdxIndexed4CandidateDebugImageErrorCode::InvalidSourceChannelCandidate:
    return "indexed-4 TDX candidate source-channel mapping is invalid";
  case TdxIndexed4CandidateDebugImageErrorCode::InvalidAlphaCandidate:
    return "indexed-4 TDX candidate alpha mapping is invalid";
  case TdxIndexed4CandidateDebugImageErrorCode::InvalidSourceLayoutCandidate:
    return "indexed-4 TDX candidate source layout is invalid";
  case TdxIndexed4CandidateDebugImageErrorCode::InvalidLimits:
    return "indexed-4 TDX candidate limits exceed the project hard maxima";
  case TdxIndexed4CandidateDebugImageErrorCode::InvalidTextureDimensions:
    return "indexed-4 TDX candidate requires nonzero texture dimensions";
  case TdxIndexed4CandidateDebugImageErrorCode::InvalidSampleEncoding:
    return "indexed-4 TDX candidate sample encoding is invalid";
  case TdxIndexed4CandidateDebugImageErrorCode::UnsupportedSampleEncoding:
    return "indexed-4 TDX candidate requires indexed-4 sample encoding";
  case TdxIndexed4CandidateDebugImageErrorCode::BlockCountMismatch:
    return "indexed-4 TDX candidate requires exactly one block";
  case TdxIndexed4CandidateDebugImageErrorCode::PlaneCountMismatch:
    return "indexed-4 TDX candidate requires exactly one plane";
  case TdxIndexed4CandidateDebugImageErrorCode::MissingPalette:
    return "indexed-4 TDX candidate requires one palette";
  case TdxIndexed4CandidateDebugImageErrorCode::InvalidPlaneDimensions:
    return "indexed-4 TDX candidate requires nonzero plane dimensions";
  case TdxIndexed4CandidateDebugImageErrorCode::InvalidTransferElementEncoding:
    return "indexed-4 TDX candidate transfer-element encoding is invalid";
  case TdxIndexed4CandidateDebugImageErrorCode::
      UnsupportedTransferElementEncoding:
    return "indexed-4 TDX candidate requires packed-4 transfer-element "
           "encoding";
  case TdxIndexed4CandidateDebugImageErrorCode::TexturePlaneDimensionMismatch:
    return "indexed-4 TDX candidate texture and plane dimensions do not match";
  case TdxIndexed4CandidateDebugImageErrorCode::SourceByteSizeOverflow:
    return "indexed-4 TDX candidate source byte size overflows";
  case TdxIndexed4CandidateDebugImageErrorCode::OutputByteSizeOverflow:
    return "indexed-4 TDX candidate output byte size overflows";
  case TdxIndexed4CandidateDebugImageErrorCode::IndexByteSizeMismatch:
    return "indexed-4 TDX candidate index byte size does not match the packed "
           "transfer rectangle";
  case TdxIndexed4CandidateDebugImageErrorCode::InvalidPaletteDimensions:
    return "indexed-4 TDX candidate requires nonzero palette dimensions";
  case TdxIndexed4CandidateDebugImageErrorCode::PaletteEntryCountMismatch:
    return "indexed-4 TDX candidate palette dimensions do not match its entry "
           "count";
  case TdxIndexed4CandidateDebugImageErrorCode::PaletteCardinalityMismatch:
    return "indexed-4 TDX candidate requires exactly 16 palette entries";
  case TdxIndexed4CandidateDebugImageErrorCode::SourceByteLimitExceeded:
    return "indexed-4 TDX candidate exceeds the source-byte limit";
  case TdxIndexed4CandidateDebugImageErrorCode::OutputByteLimitExceeded:
    return "indexed-4 TDX candidate exceeds the output-byte limit";
  case TdxIndexed4CandidateDebugImageErrorCode::AllocationFailed:
    return "indexed-4 TDX candidate allocation failed";
  }
  return "indexed-4 TDX candidate error is unknown";
}

struct TdxIndexed4CandidateDebugImageError {
  TdxIndexed4CandidateDebugImageErrorCode code;
  // Fixed category-only text; it contains no dimensions, payload, offset,
  // source identity, path, value, or exception text.
  std::string_view message;
};

// [any worker thread; reentrant] Statelessly borrows one strict direct
// Indexed4/Packed4 shape and returns independently owned four-slot diagnostic
// pixels. Every unresolved display choice is an explicit caller-selected
// hypothesis. No I/O, platform, GPU, service, or shared-state work occurs.
[[nodiscard]] std::expected<DebugImage, TdxIndexed4CandidateDebugImageError>
BuildTdxIndexed4CandidateDebugImage(
    const asset::TextureStorageIR &storage,
    const TdxIndexed4CandidatePolicy &policy,
    const TdxIndexed4CandidateDebugImageLimits &limits = {});
} // namespace omega::runtime
