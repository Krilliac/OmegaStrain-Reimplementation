#include "omega/retail/tbl_envelope_descriptor.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {
[[nodiscard]] std::vector<std::byte>
MakeTbl(const std::size_t nonzero_probe_count,
        const std::size_t trailing_bytes = 0U,
        const std::byte opaque_fill = std::byte{0xA5}) {
  const std::size_t sentinel_offset =
      nonzero_probe_count *
      static_cast<std::size_t>(omega::retail::kTblProbeStrideBytes);
  std::vector<std::byte> bytes(
      sentinel_offset +
          static_cast<std::size_t>(omega::retail::kTblProbeBytes) +
          trailing_bytes,
      opaque_fill);
  std::fill_n(bytes.begin() + static_cast<std::ptrdiff_t>(sentinel_offset),
              static_cast<std::size_t>(omega::retail::kTblProbeBytes),
              std::byte{0});
  return bytes;
}

int failures = 0;

void Check(const bool condition, const std::string_view message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    ++failures;
  }
}

template <typename Result>
void CheckError(const Result &result, const omega::asset::DecodeErrorCode code,
                const std::string_view message) {
  if (result) {
    Check(false, message);
    return;
  }
  Check(result.error().code == code, message);
  Check(!result.error().message.empty(),
        "TBL errors own a nonempty diagnostic");
  Check(result.error().message.find('/') == std::string::npos &&
            result.error().message.find('\\') == std::string::npos,
        "TBL errors contain no filesystem path");
}

template <typename Result>
void CheckErrorAt(const Result &result,
                  const omega::asset::DecodeErrorCode code,
                  const std::uint64_t offset, const std::string_view message) {
  CheckError(result, code, message);
  if (!result) {
    Check(result.error().byte_offset == offset,
          "TBL error reports the expected byte offset");
  }
}
} // namespace

int main() {
  static_assert(omega::retail::kTblProbeStrideBytes == 128U);
  static_assert(omega::retail::kTblProbeBytes == 16U);
  static_assert(omega::retail::kTblMaximumDecodedItems == 1U);
  static_assert(omega::retail::kTblMaximumInputBytes <=
                std::numeric_limits<std::uint64_t>::max() -
                    omega::retail::kTblProbeStrideBytes);
  static_assert(omega::retail::kTblMaximumLogicalOutputBytes ==
                sizeof(omega::retail::TblEnvelopeDescriptor));

  const auto exact_bytes = MakeTbl(3U);
  const auto exact = omega::retail::InspectTblEnvelope(exact_bytes);
  Check(exact.has_value(), "TBL accepts a generated sentinel-stride envelope");
  if (exact) {
    Check(exact->payload_size == 400U && exact->sentinel_offset == 384U &&
              exact->nonzero_probe_count == 3U &&
              exact->opaque_trailing_bytes == 0U,
          "TBL reports only payload size, sentinel offset, nonzero probe "
          "count, and opaque trailing size");
  }

  const auto empty_table = omega::retail::InspectTblEnvelope(MakeTbl(0U));
  Check(empty_table && empty_table->payload_size == 16U &&
            empty_table->sentinel_offset == 0U &&
            empty_table->nonzero_probe_count == 0U &&
            empty_table->opaque_trailing_bytes == 0U,
        "TBL accepts the smallest complete sentinel envelope");

  const auto trailing = omega::retail::InspectTblEnvelope(MakeTbl(1U, 37U));
  Check(trailing && trailing->payload_size == 181U &&
            trailing->sentinel_offset == 128U &&
            trailing->nonzero_probe_count == 1U &&
            trailing->opaque_trailing_bytes == 37U,
        "TBL leaves bytes after the first sentinel opaque");

  auto earlier_sentinel_bytes = MakeTbl(2U, 5U);
  std::fill_n(earlier_sentinel_bytes.begin() + 128, 16U, std::byte{0});
  const auto earlier_sentinel =
      omega::retail::InspectTblEnvelope(earlier_sentinel_bytes);
  Check(earlier_sentinel && earlier_sentinel->sentinel_offset == 128U &&
            earlier_sentinel->nonzero_probe_count == 1U &&
            earlier_sentinel->opaque_trailing_bytes == 133U,
        "TBL stops at the first all-zero fixed-stride probe");

  auto alternate_gaps = exact_bytes;
  std::fill(alternate_gaps.begin() + 16, alternate_gaps.begin() + 128,
            std::byte{0});
  std::fill(alternate_gaps.begin() + 144, alternate_gaps.begin() + 256,
            std::byte{0x7E});
  std::fill(alternate_gaps.begin() + 272, alternate_gaps.begin() + 384,
            std::byte{0x19});
  const auto alternate_gaps_result =
      omega::retail::InspectTblEnvelope(alternate_gaps);
  Check(exact && alternate_gaps_result && *alternate_gaps_result == *exact,
        "TBL does not inspect or retain the 112-byte inter-probe regions");

  for (std::size_t index = 0U; index < 16U; ++index) {
    auto partly_nonzero_sentinel = MakeTbl(2U);
    partly_nonzero_sentinel[256U + index] = std::byte{1};
    CheckErrorAt(omega::retail::InspectTblEnvelope(partly_nonzero_sentinel),
                 omega::asset::DecodeErrorCode::Malformed,
                 partly_nonzero_sentinel.size(),
                 "TBL requires every sentinel byte to be zero");
  }

  auto zeros_between_probes = MakeTbl(2U);
  std::fill_n(zeros_between_probes.begin() + 32, 16U, std::byte{0});
  const auto zeros_between_result =
      omega::retail::InspectTblEnvelope(zeros_between_probes);
  Check(zeros_between_result && zeros_between_result->sentinel_offset == 256U,
        "TBL ignores zero runs away from fixed-stride probe positions");

  const auto owned = [&]() {
    auto transient = MakeTbl(2U, 9U);
    auto inspected = omega::retail::InspectTblEnvelope(transient);
    Check(inspected.has_value(),
          "transient TBL source inspects before ownership check");
    auto descriptor =
        inspected ? *inspected : omega::retail::TblEnvelopeDescriptor{};
    transient.assign(transient.size(), std::byte{0xFF});
    transient.clear();
    transient.shrink_to_fit();
    return descriptor;
  }();
  Check(
      owned.payload_size == 281U && owned.sentinel_offset == 256U &&
          owned.nonzero_probe_count == 2U && owned.opaque_trailing_bytes == 9U,
      "TBL descriptor remains valid after source replacement and destruction");

  const auto repeated = omega::retail::InspectTblEnvelope(exact_bytes);
  Check(exact && repeated && *repeated == *exact,
        "TBL inspection is deterministic and stateless");

  std::vector<std::byte> unaligned_storage(exact_bytes.size() + 1U,
                                           std::byte{0xCC});
  std::copy(exact_bytes.begin(), exact_bytes.end(),
            unaligned_storage.begin() + 1);
  const auto unaligned =
      omega::retail::InspectTblEnvelope(std::span<const std::byte>(
          unaligned_storage.data() + 1, exact_bytes.size()));
  Check(exact && unaligned && *unaligned == *exact,
        "TBL accepts an unaligned backing slice because no address-alignment "
        "rule is claimed");

  const auto minimum = MakeTbl(0U);
  for (std::size_t size = 0;
       size < static_cast<std::size_t>(omega::retail::kTblProbeBytes); ++size) {
    CheckErrorAt(omega::retail::InspectTblEnvelope(
                     std::span<const std::byte>(minimum.data(), size)),
                 omega::asset::DecodeErrorCode::Truncated, size,
                 "TBL rejects and locates every truncated initial probe");
  }

  std::vector<std::byte> incomplete_probe(128U + 15U, std::byte{0x5A});
  for (std::size_t index = 1U; index < 16U; ++index) {
    const std::size_t size = 128U + index;
    CheckErrorAt(omega::retail::InspectTblEnvelope(
                     std::span<const std::byte>(incomplete_probe.data(), size)),
                 omega::asset::DecodeErrorCode::Truncated, 128U,
                 "TBL rejects and locates an incomplete fixed-stride probe");
  }

  std::vector<std::byte> missing_sentinel(128U, std::byte{0x41});
  CheckErrorAt(omega::retail::InspectTblEnvelope(missing_sentinel),
               omega::asset::DecodeErrorCode::Malformed, 128U,
               "TBL rejects a complete nonzero stride with no sentinel");

  auto limits = omega::asset::DecodeLimits{};
  limits.maximum_input_bytes = exact_bytes.size();
  Check(omega::retail::InspectTblEnvelope(exact_bytes, limits).has_value(),
        "TBL accepts the exact caller input-byte budget");
  limits.maximum_input_bytes = exact_bytes.size() - 1U;
  CheckError(omega::retail::InspectTblEnvelope(exact_bytes, limits),
             omega::asset::DecodeErrorCode::LimitExceeded,
             "TBL rejects one byte below the caller input-byte budget");

  limits = omega::asset::DecodeLimits{};
  limits.maximum_items = omega::retail::kTblMaximumDecodedItems;
  Check(omega::retail::InspectTblEnvelope(exact_bytes, limits).has_value(),
        "TBL accepts the exact caller item budget");
  limits.maximum_items = omega::retail::kTblMaximumDecodedItems - 1U;
  CheckError(omega::retail::InspectTblEnvelope(exact_bytes, limits),
             omega::asset::DecodeErrorCode::LimitExceeded,
             "TBL rejects one below the caller item budget");

  limits = omega::asset::DecodeLimits{};
  limits.maximum_output_bytes = omega::retail::kTblMaximumLogicalOutputBytes;
  Check(omega::retail::InspectTblEnvelope(exact_bytes, limits).has_value(),
        "TBL accepts the exact caller output-byte budget");
  limits.maximum_output_bytes =
      omega::retail::kTblMaximumLogicalOutputBytes - 1U;
  CheckError(omega::retail::InspectTblEnvelope(exact_bytes, limits),
             omega::asset::DecodeErrorCode::LimitExceeded,
             "TBL rejects one below the caller output-byte budget");

  limits = omega::asset::DecodeLimits{};
  limits.maximum_scratch_bytes = 0U;
  limits.maximum_nesting_depth = 0U;
  limits.maximum_string_bytes = 0U;
  Check(omega::retail::InspectTblEnvelope(exact_bytes, limits).has_value(),
        "TBL flat inspection requires no dynamic scratch, string, or nesting "
        "budget");

  std::vector<std::byte> maximum_input(
      static_cast<std::size_t>(omega::retail::kTblMaximumInputBytes),
      std::byte{0x5A});
  constexpr std::size_t maximum_sentinel_offset =
      static_cast<std::size_t>(omega::retail::kTblMaximumInputBytes -
                               omega::retail::kTblProbeStrideBytes);
  std::fill_n(maximum_input.begin() +
                  static_cast<std::ptrdiff_t>(maximum_sentinel_offset),
              static_cast<std::size_t>(omega::retail::kTblProbeBytes),
              std::byte{0});
  const auto maximum = omega::retail::InspectTblEnvelope(maximum_input);
  Check(maximum && maximum->sentinel_offset == maximum_sentinel_offset &&
            maximum->opaque_trailing_bytes == 112U,
        "TBL accepts the fixed project ceiling without offset overflow");
  maximum_input.push_back(std::byte{0});
  auto permissive_limits = omega::asset::DecodeLimits{};
  permissive_limits.maximum_input_bytes =
      std::numeric_limits<std::uint64_t>::max();
  permissive_limits.maximum_items = std::numeric_limits<std::uint64_t>::max();
  permissive_limits.maximum_output_bytes =
      std::numeric_limits<std::uint64_t>::max();
  CheckError(
      omega::retail::InspectTblEnvelope(maximum_input, permissive_limits),
      omega::asset::DecodeErrorCode::LimitExceeded,
      "TBL caller limits cannot raise the fixed project input ceiling");

  if (failures != 0) {
    std::cerr << failures << " TBL envelope descriptor test(s) failed\n";
  }
  return failures == 0 ? 0 : 1;
}
