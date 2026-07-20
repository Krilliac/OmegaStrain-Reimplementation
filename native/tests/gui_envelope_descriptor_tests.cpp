#include "omega/retail/gui_envelope_descriptor.h"

#include <algorithm>
#include <array>
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
MakeGui(const std::size_t root_bytes = 32U,
        const std::uint16_t observed_word = 0x1234U,
        const std::byte opaque_gap = std::byte{0xA5},
        const std::byte root_fill = std::byte{0x3C}) {
  std::vector<std::byte> bytes(
      static_cast<std::size_t>(omega::retail::kGuiRootBoundaryOffset) +
          root_bytes,
      root_fill);
  for (std::size_t index = 0; index < omega::retail::kGuiObservedTag.size();
       ++index) {
    bytes[index] =
        static_cast<std::byte>(omega::retail::kGuiObservedTag[index]);
  }
  bytes[static_cast<std::size_t>(omega::retail::kGuiOpaqueGapOffset)] =
      opaque_gap;
  WriteU16(bytes,
           static_cast<std::size_t>(omega::retail::kGuiObservedWordOffset),
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
  Check(!result.error().message.empty(),
        "GUI errors own a nonempty diagnostic");
  Check(result.error().message.find('/') == std::string::npos &&
            result.error().message.find('\\') == std::string::npos,
        "GUI errors contain no filesystem path");
}

template <typename Result>
void CheckErrorAt(const Result &result,
                  const omega::asset::DecodeErrorCode code,
                  const std::uint64_t offset, const std::string_view message) {
  CheckError(result, code, message);
  if (!result) {
    Check(result.error().byte_offset == offset,
          "GUI error reports the expected byte offset");
  }
}
} // namespace

int main() {
  static_assert(omega::retail::kGuiTagOffset == 0U);
  static_assert(omega::retail::kGuiTagBytes == 3U);
  static_assert(omega::retail::kGuiOpaqueGapOffset == 3U);
  static_assert(omega::retail::kGuiObservedWordOffset == 4U);
  static_assert(omega::retail::kGuiRootBoundaryOffset == 6U);
  static_assert(omega::retail::kGuiMaximumDecodedItems == 1U);
  static_assert(omega::retail::kGuiMaximumLogicalOutputBytes ==
                sizeof(omega::retail::GuiEnvelopeDescriptor));

  const auto exact_bytes = MakeGui();
  const auto exact = omega::retail::InspectGuiEnvelope(exact_bytes);
  Check(exact.has_value(), "GUI accepts a generated bounded envelope");
  if (exact) {
    Check(exact->observed_word_0x04 == 0x1234U,
          "GUI reports the little-endian word at 0x04 without assigning "
          "semantics");
    Check(exact->tag_region == omega::retail::ObservedByteRange{0, 3} &&
              exact->opaque_gap_region ==
                  omega::retail::ObservedByteRange{3, 1} &&
              exact->observed_word_region ==
                  omega::retail::ObservedByteRange{4, 2} &&
              exact->opaque_root_region ==
                  omega::retail::ObservedByteRange{6, 32},
          "GUI reports only fixed prefix and opaque root ranges");
  }

  const auto alternate_word =
      omega::retail::InspectGuiEnvelope(MakeGui(32U, 0xFEDCU));
  Check(alternate_word && alternate_word->observed_word_0x04 == 0xFEDCU,
        "GUI keeps the word at 0x04 observational instead of inventing a "
        "version constraint");

  const auto alternate_gap =
      omega::retail::InspectGuiEnvelope(MakeGui(32U, 0x1234U, std::byte{0x7E}));
  Check(exact && alternate_gap && *alternate_gap == *exact,
        "GUI does not interpret or retain the skipped byte at 0x03");

  auto alternate_root_bytes = exact_bytes;
  std::fill(
      alternate_root_bytes.begin() +
          static_cast<std::ptrdiff_t>(omega::retail::kGuiRootBoundaryOffset),
      alternate_root_bytes.end(), std::byte{0xE1});
  const auto alternate_root =
      omega::retail::InspectGuiEnvelope(alternate_root_bytes);
  Check(exact && alternate_root && *alternate_root == *exact,
        "GUI does not retain or interpret opaque root bytes");

  const auto one_root_byte = omega::retail::InspectGuiEnvelope(MakeGui(1U));
  Check(one_root_byte && one_root_byte->opaque_root_region ==
                             omega::retail::ObservedByteRange{6, 1},
        "GUI accepts the smallest nonempty opaque root boundary");

  const auto owned = [&]() {
    auto transient = MakeGui(9U, 0xBEEFU);
    auto inspected = omega::retail::InspectGuiEnvelope(transient);
    Check(inspected.has_value(),
          "transient GUI source inspects before ownership check");
    auto descriptor =
        inspected ? *inspected : omega::retail::GuiEnvelopeDescriptor{};
    transient.assign(transient.size(), std::byte{0xFF});
    transient.clear();
    transient.shrink_to_fit();
    return descriptor;
  }();
  Check(
      owned.observed_word_0x04 == 0xBEEFU &&
          owned.opaque_root_region == omega::retail::ObservedByteRange{6, 9},
      "GUI descriptor remains valid after source replacement and destruction");

  const auto repeated = omega::retail::InspectGuiEnvelope(exact_bytes);
  Check(exact && repeated && *repeated == *exact,
        "GUI inspection is deterministic and stateless");

  std::vector<std::byte> unaligned_storage(exact_bytes.size() + 1U,
                                           std::byte{0xCC});
  std::copy(exact_bytes.begin(), exact_bytes.end(),
            unaligned_storage.begin() + 1);
  const auto unaligned =
      omega::retail::InspectGuiEnvelope(std::span<const std::byte>(
          unaligned_storage.data() + 1, exact_bytes.size()));
  Check(exact && unaligned && *unaligned == *exact,
        "GUI accepts an unaligned backing slice because no address alignment "
        "rule is claimed");

  bool tag_prefixes_are_truncated = true;
  for (std::size_t size = 0; size < omega::retail::kGuiTagBytes; ++size) {
    const auto prefix = omega::retail::InspectGuiEnvelope(
        std::span<const std::byte>(exact_bytes.data(), size));
    tag_prefixes_are_truncated =
        tag_prefixes_are_truncated && !prefix &&
        prefix.error().code == omega::asset::DecodeErrorCode::Truncated &&
        prefix.error().byte_offset == size;
  }
  Check(tag_prefixes_are_truncated,
        "every truncated GUI tag prefix is rejected at the input end");

  for (std::size_t index = 0; index < omega::retail::kGuiObservedTag.size();
       ++index) {
    auto wrong_tag = exact_bytes;
    wrong_tag[index] = static_cast<std::byte>(
        omega::retail::kGuiObservedTag[index] ^ std::uint8_t{0x20});
    CheckErrorAt(omega::retail::InspectGuiEnvelope(wrong_tag),
                 omega::asset::DecodeErrorCode::UnsupportedVariant, index,
                 "GUI rejects and locates a mismatched tag byte");
  }

  CheckErrorAt(omega::retail::InspectGuiEnvelope(std::span<const std::byte>(
                   exact_bytes.data(),
                   static_cast<std::size_t>(omega::retail::kGuiTagBytes))),
               omega::asset::DecodeErrorCode::Truncated,
               omega::retail::kGuiTagBytes,
               "GUI rejects a tag-only slice missing the fixed prefix gap");
  for (std::size_t size = 4; size < omega::retail::kGuiRootBoundaryOffset;
       ++size) {
    CheckErrorAt(omega::retail::InspectGuiEnvelope(
                     std::span<const std::byte>(exact_bytes.data(), size)),
                 omega::asset::DecodeErrorCode::Truncated, size,
                 "GUI rejects and locates a truncated word at 0x04");
  }
  CheckErrorAt(
      omega::retail::InspectGuiEnvelope(std::span<const std::byte>(
          exact_bytes.data(),
          static_cast<std::size_t>(omega::retail::kGuiRootBoundaryOffset))),
      omega::asset::DecodeErrorCode::Truncated,
      omega::retail::kGuiRootBoundaryOffset,
      "GUI rejects a complete prefix with no opaque root byte");

  auto limits = omega::asset::DecodeLimits{};
  limits.maximum_input_bytes = exact_bytes.size();
  Check(omega::retail::InspectGuiEnvelope(exact_bytes, limits).has_value(),
        "GUI accepts the exact caller input-byte budget");
  limits.maximum_input_bytes = exact_bytes.size() - 1U;
  CheckError(omega::retail::InspectGuiEnvelope(exact_bytes, limits),
             omega::asset::DecodeErrorCode::LimitExceeded,
             "GUI rejects one byte below the caller input-byte budget");

  limits = omega::asset::DecodeLimits{};
  limits.maximum_items = omega::retail::kGuiMaximumDecodedItems;
  Check(omega::retail::InspectGuiEnvelope(exact_bytes, limits).has_value(),
        "GUI accepts the exact caller item budget");
  limits.maximum_items = omega::retail::kGuiMaximumDecodedItems - 1U;
  CheckError(omega::retail::InspectGuiEnvelope(exact_bytes, limits),
             omega::asset::DecodeErrorCode::LimitExceeded,
             "GUI rejects one below the caller item budget");

  limits = omega::asset::DecodeLimits{};
  limits.maximum_output_bytes = omega::retail::kGuiMaximumLogicalOutputBytes;
  Check(omega::retail::InspectGuiEnvelope(exact_bytes, limits).has_value(),
        "GUI accepts the exact caller output-byte budget");
  limits.maximum_output_bytes =
      omega::retail::kGuiMaximumLogicalOutputBytes - 1U;
  CheckError(omega::retail::InspectGuiEnvelope(exact_bytes, limits),
             omega::asset::DecodeErrorCode::LimitExceeded,
             "GUI rejects one below the caller output-byte budget");

  limits = omega::asset::DecodeLimits{};
  limits.maximum_scratch_bytes = 0;
  limits.maximum_nesting_depth = 0;
  limits.maximum_string_bytes = 0;
  Check(omega::retail::InspectGuiEnvelope(exact_bytes, limits).has_value(),
        "GUI flat inspection requires no dynamic scratch, string, or nesting "
        "budget");

  std::vector<std::byte> maximum_input(
      static_cast<std::size_t>(omega::retail::kGuiMaximumInputBytes),
      std::byte{0x5A});
  const auto prefix = MakeGui(1U);
  std::copy_n(
      prefix.begin(),
      static_cast<std::size_t>(omega::retail::kGuiRootBoundaryOffset + 1U),
      maximum_input.begin());
  Check(omega::retail::InspectGuiEnvelope(maximum_input).has_value(),
        "GUI accepts the fixed project input ceiling");
  maximum_input.push_back(std::byte{0});
  auto permissive_limits = omega::asset::DecodeLimits{};
  permissive_limits.maximum_input_bytes =
      std::numeric_limits<std::uint64_t>::max();
  permissive_limits.maximum_items = std::numeric_limits<std::uint64_t>::max();
  permissive_limits.maximum_output_bytes =
      std::numeric_limits<std::uint64_t>::max();
  CheckError(
      omega::retail::InspectGuiEnvelope(maximum_input, permissive_limits),
      omega::asset::DecodeErrorCode::LimitExceeded,
      "GUI caller limits cannot raise the fixed project input ceiling");

  if (failures != 0) {
    std::cerr << failures << " GUI envelope descriptor test(s) failed\n";
  }
  return failures == 0 ? 0 : 1;
}
