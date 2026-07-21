#include "omega/retail/ie_envelope_descriptor.h"

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
void WriteU16(std::vector<std::byte> &bytes, const std::size_t offset,
              const std::uint16_t value) {
  bytes[offset] = static_cast<std::byte>(value & 0xFFU);
  bytes[offset + 1U] = static_cast<std::byte>((value >> 8U) & 0xFFU);
}

[[nodiscard]] std::vector<std::byte>
MakeIe(const std::size_t root_bytes = 32U,
       const std::uint16_t observed_word = 0x1234U,
       const std::byte prefix_0 = std::byte{0xA5},
       const std::byte prefix_1 = std::byte{0x5A},
       const std::byte root_fill = std::byte{0x3C}) {
  std::vector<std::byte> bytes(
      static_cast<std::size_t>(omega::retail::kIeRootBoundaryOffset) +
          root_bytes,
      root_fill);
  bytes[0] = prefix_0;
  bytes[1] = prefix_1;
  WriteU16(bytes,
           static_cast<std::size_t>(omega::retail::kIeObservedWordOffset),
           observed_word);
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
  Check(!result.error().message.empty(), "IE errors own a nonempty diagnostic");
  Check(result.error().message.find('/') == std::string::npos &&
            result.error().message.find('\\') == std::string::npos,
        "IE errors contain no filesystem path");
}

template <typename Result>
void CheckErrorAt(const Result &result,
                  const omega::asset::DecodeErrorCode code,
                  const std::uint64_t offset, const std::string_view message) {
  CheckError(result, code, message);
  if (!result) {
    Check(result.error().byte_offset == offset,
          "IE error reports the expected byte offset");
  }
}
} // namespace

int main() {
  static_assert(omega::retail::kIeOpaquePrefixOffset == 0U);
  static_assert(omega::retail::kIeOpaquePrefixBytes == 2U);
  static_assert(omega::retail::kIeObservedWordOffset == 2U);
  static_assert(omega::retail::kIeObservedWordBytes == 2U);
  static_assert(omega::retail::kIeRootBoundaryOffset == 4U);
  static_assert(omega::retail::kIeMaximumDecodedItems == 1U);
  static_assert(omega::retail::kIeMaximumLogicalOutputBytes ==
                sizeof(omega::retail::IeEnvelopeDescriptor));

  const auto exact_bytes = MakeIe();
  const auto exact = omega::retail::InspectIeEnvelope(exact_bytes);
  Check(exact.has_value(), "IE accepts a generated bounded envelope");
  if (exact) {
    Check(exact->observed_word_0x02 == 0x1234U,
          "IE reports the little-endian word at 0x02 without assigning "
          "semantics");
    Check(exact->opaque_prefix_region ==
                  omega::retail::ObservedByteRange{0, 2} &&
              exact->observed_word_region ==
                  omega::retail::ObservedByteRange{2, 2} &&
              exact->opaque_root_region ==
                  omega::retail::ObservedByteRange{4, 32},
          "IE reports only the skipped prefix, observed word, and opaque root "
          "ranges");
  }

  const auto zero_word = omega::retail::InspectIeEnvelope(MakeIe(32U, 0U));
  Check(zero_word && zero_word->observed_word_0x02 == 0U,
        "IE accepts zero in the observational word without inventing a value "
        "constraint");
  const auto maximum_word =
      omega::retail::InspectIeEnvelope(MakeIe(32U, 0xFFFFU));
  Check(maximum_word && maximum_word->observed_word_0x02 == 0xFFFFU,
        "IE accepts the full observational word range");

  const auto alternate_prefix = omega::retail::InspectIeEnvelope(
      MakeIe(32U, 0x1234U, std::byte{0x00}, std::byte{0xFF}));
  Check(exact && alternate_prefix && *alternate_prefix == *exact,
        "IE does not validate or retain the skipped two-byte prefix");

  auto alternate_root_bytes = exact_bytes;
  std::fill(
      alternate_root_bytes.begin() +
          static_cast<std::ptrdiff_t>(omega::retail::kIeRootBoundaryOffset),
      alternate_root_bytes.end(), std::byte{0xE1});
  const auto alternate_root =
      omega::retail::InspectIeEnvelope(alternate_root_bytes);
  Check(exact && alternate_root && *alternate_root == *exact,
        "IE does not retain or interpret opaque root bytes");

  const auto one_root_byte = omega::retail::InspectIeEnvelope(MakeIe(1U));
  Check(one_root_byte && one_root_byte->opaque_root_region ==
                             omega::retail::ObservedByteRange{4, 1},
        "IE accepts the smallest nonempty root without an alignment claim");

  const auto owned = [&]() {
    auto transient = MakeIe(9U, 0xBEEFU);
    auto inspected = omega::retail::InspectIeEnvelope(transient);
    Check(inspected.has_value(),
          "transient IE source inspects before ownership check");
    auto descriptor =
        inspected ? *inspected : omega::retail::IeEnvelopeDescriptor{};
    transient.assign(transient.size(), std::byte{0xFF});
    transient.clear();
    transient.shrink_to_fit();
    return descriptor;
  }();
  Check(owned.observed_word_0x02 == 0xBEEFU &&
            owned.opaque_root_region == omega::retail::ObservedByteRange{4, 9},
        "IE descriptor remains valid after source replacement and destruction");

  const auto repeated = omega::retail::InspectIeEnvelope(exact_bytes);
  Check(exact && repeated && *repeated == *exact,
        "IE inspection is deterministic and stateless");

  std::vector<std::byte> unaligned_storage(exact_bytes.size() + 1U,
                                           std::byte{0xCC});
  std::copy(exact_bytes.begin(), exact_bytes.end(),
            unaligned_storage.begin() + 1);
  const auto unaligned =
      omega::retail::InspectIeEnvelope(std::span<const std::byte>(
          unaligned_storage.data() + 1, exact_bytes.size()));
  Check(exact && unaligned && *unaligned == *exact,
        "IE accepts an unaligned backing slice because no address alignment "
        "rule is claimed");

  for (std::size_t size = 0; size < omega::retail::kIeOpaquePrefixBytes;
       ++size) {
    CheckErrorAt(omega::retail::InspectIeEnvelope(
                     std::span<const std::byte>(exact_bytes.data(), size)),
                 omega::asset::DecodeErrorCode::Truncated, size,
                 "IE rejects and locates a truncated skipped prefix");
  }
  for (std::size_t size = omega::retail::kIeOpaquePrefixBytes;
       size < omega::retail::kIeRootBoundaryOffset; ++size) {
    CheckErrorAt(omega::retail::InspectIeEnvelope(
                     std::span<const std::byte>(exact_bytes.data(), size)),
                 omega::asset::DecodeErrorCode::Truncated, size,
                 "IE rejects and locates a truncated word at 0x02");
  }
  CheckErrorAt(
      omega::retail::InspectIeEnvelope(std::span<const std::byte>(
          exact_bytes.data(),
          static_cast<std::size_t>(omega::retail::kIeRootBoundaryOffset))),
      omega::asset::DecodeErrorCode::Truncated,
      omega::retail::kIeRootBoundaryOffset,
      "IE rejects a complete prefix with no opaque root byte");

  auto limits = omega::asset::DecodeLimits{};
  limits.maximum_input_bytes = exact_bytes.size();
  Check(omega::retail::InspectIeEnvelope(exact_bytes, limits).has_value(),
        "IE accepts the exact caller input-byte budget");
  limits.maximum_input_bytes = exact_bytes.size() - 1U;
  CheckError(omega::retail::InspectIeEnvelope(exact_bytes, limits),
             omega::asset::DecodeErrorCode::LimitExceeded,
             "IE rejects one byte below the caller input-byte budget");

  limits = omega::asset::DecodeLimits{};
  limits.maximum_items = omega::retail::kIeMaximumDecodedItems;
  Check(omega::retail::InspectIeEnvelope(exact_bytes, limits).has_value(),
        "IE accepts the exact caller item budget");
  limits.maximum_items = omega::retail::kIeMaximumDecodedItems - 1U;
  CheckError(omega::retail::InspectIeEnvelope(exact_bytes, limits),
             omega::asset::DecodeErrorCode::LimitExceeded,
             "IE rejects one below the caller item budget");

  limits = omega::asset::DecodeLimits{};
  limits.maximum_output_bytes = omega::retail::kIeMaximumLogicalOutputBytes;
  Check(omega::retail::InspectIeEnvelope(exact_bytes, limits).has_value(),
        "IE accepts the exact caller output-byte budget");
  limits.maximum_output_bytes =
      omega::retail::kIeMaximumLogicalOutputBytes - 1U;
  CheckError(omega::retail::InspectIeEnvelope(exact_bytes, limits),
             omega::asset::DecodeErrorCode::LimitExceeded,
             "IE rejects one below the caller output-byte budget");

  limits = omega::asset::DecodeLimits{};
  limits.maximum_scratch_bytes = 0;
  limits.maximum_nesting_depth = 0;
  limits.maximum_string_bytes = 0;
  Check(omega::retail::InspectIeEnvelope(exact_bytes, limits).has_value(),
        "IE flat inspection requires no dynamic scratch, string, or nesting "
        "budget");

  std::vector<std::byte> maximum_input(
      static_cast<std::size_t>(omega::retail::kIeMaximumInputBytes),
      std::byte{0x5A});
  const auto prefix = MakeIe(1U);
  std::copy_n(
      prefix.begin(),
      static_cast<std::size_t>(omega::retail::kIeRootBoundaryOffset + 1U),
      maximum_input.begin());
  Check(omega::retail::InspectIeEnvelope(maximum_input).has_value(),
        "IE accepts the fixed project input ceiling");
  maximum_input.push_back(std::byte{0});
  auto permissive_limits = omega::asset::DecodeLimits{};
  permissive_limits.maximum_input_bytes =
      std::numeric_limits<std::uint64_t>::max();
  permissive_limits.maximum_items = std::numeric_limits<std::uint64_t>::max();
  permissive_limits.maximum_output_bytes =
      std::numeric_limits<std::uint64_t>::max();
  CheckError(omega::retail::InspectIeEnvelope(maximum_input, permissive_limits),
             omega::asset::DecodeErrorCode::LimitExceeded,
             "IE caller limits cannot raise the fixed project input ceiling");

  if (failures != 0) {
    std::cerr << failures << " IE envelope descriptor test(s) failed\n";
  }
  return failures == 0 ? 0 : 1;
}
