#include "omega/retail/tbl_envelope_descriptor.h"

#include <limits>
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

[[nodiscard]] bool IsZeroProbe(const std::span<const std::byte> bytes,
                               const std::size_t offset) noexcept {
  for (std::size_t index = 0; index < static_cast<std::size_t>(kTblProbeBytes);
       ++index) {
    if (bytes[offset + index] != std::byte{0})
      return false;
  }
  return true;
}
} // namespace

asset::DecodeResult<TblEnvelopeDescriptor>
InspectTblEnvelope(const std::span<const std::byte> bytes,
                   const asset::DecodeLimits limits) {
  if (bytes.size() > kTblMaximumInputBytes) {
    return std::unexpected(
        Error(asset::DecodeErrorCode::LimitExceeded,
              "TBL input exceeds the fixed inspector byte limit"));
  }
  if (bytes.size() > limits.maximum_input_bytes) {
    return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
                                 "TBL input exceeds the caller byte limit"));
  }
  if (limits.maximum_items < kTblMaximumDecodedItems) {
    return std::unexpected(
        Error(asset::DecodeErrorCode::LimitExceeded,
              "TBL descriptor exceeds the caller item limit"));
  }
  if (limits.maximum_output_bytes < kTblMaximumLogicalOutputBytes) {
    return std::unexpected(
        Error(asset::DecodeErrorCode::LimitExceeded,
              "TBL descriptor exceeds the caller output limit"));
  }

  const std::uint64_t payload_size = static_cast<std::uint64_t>(bytes.size());
  if (payload_size < kTblProbeBytes) {
    return std::unexpected(Error(asset::DecodeErrorCode::Truncated,
                                 "TBL initial 16-byte probe is truncated",
                                 payload_size));
  }

  std::uint64_t probe_offset = 0;
  std::uint64_t nonzero_probe_count = 0;
  for (;;) {
    if (probe_offset > payload_size ||
        kTblProbeBytes > payload_size - probe_offset) {
      return std::unexpected(Error(asset::DecodeErrorCode::Truncated,
                                   "TBL fixed-stride probe is truncated",
                                   probe_offset));
    }
    if (probe_offset > std::numeric_limits<std::size_t>::max() ||
        kTblProbeBytes >
            std::numeric_limits<std::size_t>::max() - probe_offset) {
      return std::unexpected(
          Error(asset::DecodeErrorCode::Overflow,
                "TBL probe offset exceeds the addressable input range",
                probe_offset));
    }

    if (IsZeroProbe(bytes, static_cast<std::size_t>(probe_offset))) {
      return TblEnvelopeDescriptor{
          .payload_size = payload_size,
          .sentinel_offset = probe_offset,
          .nonzero_probe_count = nonzero_probe_count,
          .opaque_trailing_bytes = payload_size - probe_offset - kTblProbeBytes,
      };
    }

    if (nonzero_probe_count == std::numeric_limits<std::uint64_t>::max()) {
      return std::unexpected(Error(asset::DecodeErrorCode::Overflow,
                                   "TBL nonzero probe count overflows",
                                   probe_offset));
    }
    ++nonzero_probe_count;
    if (probe_offset >
        std::numeric_limits<std::uint64_t>::max() - kTblProbeStrideBytes) {
      return std::unexpected(Error(asset::DecodeErrorCode::Overflow,
                                   "TBL next probe offset overflows",
                                   probe_offset));
    }
    const std::uint64_t next_probe = probe_offset + kTblProbeStrideBytes;
    if (next_probe >= payload_size) {
      return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
                                   "TBL all-zero sentinel probe is missing",
                                   payload_size));
    }
    if (kTblProbeBytes > payload_size - next_probe) {
      return std::unexpected(Error(asset::DecodeErrorCode::Truncated,
                                   "TBL fixed-stride probe is truncated",
                                   next_probe));
    }
    probe_offset = next_probe;
  }
}
} // namespace omega::retail
