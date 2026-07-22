#pragma once

#include "omega/asset/decode.h"
#include "omega/asset/frontend_ir.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace omega::retail {
// Fixed decoder safety ceilings. Caller limits may tighten but cannot raise
// these bounds.
inline constexpr std::uint64_t kFrontendMaximumDecodedItems = 1ULL << 20U;
inline constexpr std::uint64_t kFrontendMaximumLogicalOutputBytes =
    64ULL * 1024ULL * 1024ULL;
inline constexpr std::uint32_t kFrontendMaximumStringBytes = 4096;
inline constexpr std::uint32_t kFrontendMaximumNestingDepth = 16;

struct DecodedGuiFrontend {
  asset::FrontendWidgetDocumentIR document;
  std::uint64_t decoded_items = 0;
  std::uint64_t logical_output_bytes = 0;
  std::uint8_t trailing_zero_bytes = 0;
};

struct DecodedIeFrontend {
  asset::FrontendVisualDocumentIR document;
  std::uint64_t decoded_items = 0;
  std::uint64_t logical_output_bytes = 0;
  std::uint8_t trailing_zero_bytes = 0;
};

// [any worker thread; reentrant] Converts the bounded retail GUI hierarchy used
// by the title, create-agent, and open-agent bundles into canonical owned
// widget data. The decoder supports only the four factory families and one
// decorator grammar proven for those bundles.
[[nodiscard]] asset::DecodeResult<asset::FrontendWidgetDocumentIR>
DecodeGuiFrontend(std::span<const std::byte> bytes,
                  asset::DecodeLimits limits = {});

[[nodiscard]] asset::DecodeResult<DecodedGuiFrontend>
DecodeGuiFrontendMeasured(std::span<const std::byte> bytes,
                          asset::DecodeLimits limits = {});

// [any worker thread; reentrant] Converts the paired retail IE hierarchy into
// canonical owned visual-resource data. Fixed streams and the observed
// animation-track families are structurally validated and discarded rather than
// assigned unproven canonical meanings.
[[nodiscard]] asset::DecodeResult<asset::FrontendVisualDocumentIR>
DecodeIeFrontend(std::span<const std::byte> bytes,
                 asset::DecodeLimits limits = {});

[[nodiscard]] asset::DecodeResult<DecodedIeFrontend>
DecodeIeFrontendMeasured(std::span<const std::byte> bytes,
                         asset::DecodeLimits limits = {});
} // namespace omega::retail
