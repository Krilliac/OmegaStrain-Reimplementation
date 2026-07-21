#pragma once

#include "omega/asset/decode.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace omega::retail {
inline constexpr std::uint64_t kTblProbeStrideBytes = 128;
inline constexpr std::uint64_t kTblProbeBytes = 16;

// This is a project safety ceiling, not an observed retail size maximum.
inline constexpr std::uint64_t kTblMaximumInputBytes = 1ULL * 1024ULL * 1024ULL;
inline constexpr std::uint64_t kTblMaximumDecodedItems = 1;

// Passive description of the bounded TBL envelope scan. Starting at byte zero,
// the observed consumer examines one 16-byte window every 128 bytes and stops
// at the first all-zero window. Inter-probe bytes and any bytes after that
// sentinel remain opaque. No lane, integer, endianness, table-record, lookup,
// or front-end semantics are assigned, and no source bytes are retained.
struct TblEnvelopeDescriptor {
  std::uint64_t payload_size = 0;
  std::uint64_t sentinel_offset = 0;
  std::uint64_t nonzero_probe_count = 0;
  std::uint64_t opaque_trailing_bytes = 0;

  bool operator==(const TblEnvelopeDescriptor &) const = default;
};

inline constexpr std::uint64_t kTblMaximumLogicalOutputBytes =
    sizeof(TblEnvelopeDescriptor);

// [any worker thread; stateless/reentrant] Applies only the fixed-stride,
// zero-sentinel observation above. A nonzero probe requires another complete
// probe at the next stride until the sentinel is found. Caller limits may
// tighten but cannot raise the fixed input, item, or output ceilings. The flat
// inspection uses zero dynamic scratch, string budget, and nesting depth.
[[nodiscard]] asset::DecodeResult<TblEnvelopeDescriptor>
InspectTblEnvelope(std::span<const std::byte> bytes,
                   asset::DecodeLimits limits = {});
} // namespace omega::retail
