#include "omega/retail/fnt_envelope_descriptor.h"

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
constexpr std::string_view kSyntheticReference = "SAMPLE00.REF";

void WriteU16(std::vector<std::byte> &bytes, const std::size_t offset,
              const std::uint16_t value) {
  bytes[offset] = static_cast<std::byte>(value & 0xFFU);
  bytes[offset + 1U] = static_cast<std::byte>((value >> 8U) & 0xFFU);
}

[[nodiscard]] std::vector<std::byte>
MakeFnt(const std::size_t payload_bytes = 32U,
        const std::byte fill = std::byte{0xA5}) {
  std::vector<std::byte> bytes(
      static_cast<std::size_t>(omega::retail::kFntObservedPayloadOffset) +
          payload_bytes,
      fill);
  WriteU16(bytes, 0, omega::retail::kFntObservedWord0x00);
  bytes[2] = static_cast<std::byte>(omega::retail::kFntObservedByte0x02);
  for (std::size_t index = 0; index < kSyntheticReference.size(); ++index) {
    bytes[static_cast<std::size_t>(omega::retail::kFntObservedAsciiOffset) +
          index] =
        static_cast<std::byte>(
            static_cast<unsigned char>(kSyntheticReference[index]));
  }
  bytes[static_cast<std::size_t>(omega::retail::kFntObservedTerminatorOffset)] =
      std::byte{0};
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
        "FNT errors own a nonempty diagnostic");
  Check(result.error().message.find('/') == std::string::npos &&
            result.error().message.find('\\') == std::string::npos,
        "FNT errors contain no filesystem path");
}

template <typename Result>
void CheckErrorAt(const Result &result,
                  const omega::asset::DecodeErrorCode code,
                  const std::uint64_t offset, const std::string_view message) {
  CheckError(result, code, message);
  if (!result)
    Check(result.error().byte_offset == offset,
          "FNT error reports the expected byte offset");
}
} // namespace

int main() {
  static_assert(kSyntheticReference.size() ==
                omega::retail::kFntObservedAsciiBytes);
  static_assert(omega::retail::kFntObservedPayloadOffset == 16U);
  static_assert(omega::retail::kFntMaximumDecodedItems == 1U);
  static_assert(omega::retail::kFntMaximumLogicalOutputBytes ==
                sizeof(omega::retail::FntEnvelopeDescriptor));

  const auto exact_bytes = MakeFnt();
  const auto exact = omega::retail::InspectFntEnvelope(exact_bytes);
  Check(exact.has_value(), "FNT accepts the generated observed prefix family");
  if (exact) {
    Check(exact->observed_word_0x00 == omega::retail::kFntObservedWord0x00 &&
              exact->observed_byte_0x02 == omega::retail::kFntObservedByte0x02,
          "FNT reports the observed numeric prefix values without assigning "
          "semantics");
    Check(exact->ascii_region == omega::retail::ObservedByteRange{3, 12} &&
              exact->terminator_region ==
                  omega::retail::ObservedByteRange{15, 1} &&
              exact->opaque_payload_region ==
                  omega::retail::ObservedByteRange{16, 32},
          "FNT reports only the observed ASCII, terminator, and opaque payload "
          "ranges");
  }

  auto alternate = MakeFnt(7U, std::byte{0x3C});
  constexpr std::string_view alternate_reference = "ALTREF99.BIN";
  static_assert(alternate_reference.size() ==
                omega::retail::kFntObservedAsciiBytes);
  for (std::size_t index = 0; index < alternate_reference.size(); ++index) {
    alternate[static_cast<std::size_t>(omega::retail::kFntObservedAsciiOffset) +
              index] =
        static_cast<std::byte>(
            static_cast<unsigned char>(alternate_reference[index]));
  }
  const auto alternate_result = omega::retail::InspectFntEnvelope(alternate);
  Check(exact && alternate_result &&
            alternate_result->ascii_region == exact->ascii_region &&
            alternate_result->terminator_region == exact->terminator_region &&
            alternate_result->opaque_payload_region ==
                omega::retail::ObservedByteRange{16, 7},
        "FNT descriptor does not retain or interpret valid ASCII and opaque "
        "payload bytes");

  const auto repeated = omega::retail::InspectFntEnvelope(exact_bytes);
  Check(exact && repeated && *repeated == *exact,
        "FNT inspection is deterministic and stateless");

  std::vector<std::byte> unaligned_storage(exact_bytes.size() + 1U,
                                           std::byte{0xCC});
  std::copy(exact_bytes.begin(), exact_bytes.end(),
            unaligned_storage.begin() + 1);
  const auto unaligned =
      omega::retail::InspectFntEnvelope(std::span<const std::byte>(
          unaligned_storage.data() + 1, exact_bytes.size()));
  Check(exact && unaligned && *unaligned == *exact,
        "FNT accepts an unaligned backing slice because no address-alignment "
        "rule is claimed");

  bool prefixes_are_truncated = true;
  for (std::size_t size = 0; size < omega::retail::kFntObservedPayloadOffset;
       ++size) {
    const auto prefix = omega::retail::InspectFntEnvelope(
        std::span<const std::byte>(exact_bytes.data(), size));
    prefixes_are_truncated =
        prefixes_are_truncated && !prefix &&
        prefix.error().code == omega::asset::DecodeErrorCode::Truncated;
  }
  Check(prefixes_are_truncated, "every valid FNT observed-prefix truncation is "
                                "rejected without reading past the slice");

  auto wrong_word = exact_bytes;
  WriteU16(wrong_word, 0, omega::retail::kFntObservedWord0x00 + 1U);
  CheckErrorAt(omega::retail::InspectFntEnvelope(wrong_word),
               omega::asset::DecodeErrorCode::UnsupportedVariant, 0,
               "FNT rejects a word at 0x00 outside the observed family");

  auto wrong_byte = exact_bytes;
  wrong_byte[2] =
      static_cast<std::byte>(omega::retail::kFntObservedByte0x02 - 1U);
  CheckErrorAt(omega::retail::InspectFntEnvelope(wrong_byte),
               omega::asset::DecodeErrorCode::UnsupportedVariant, 2,
               "FNT rejects a byte at 0x02 outside the observed family");

  for (const std::size_t bad_offset : {3U, 8U, 14U}) {
    auto non_printable = exact_bytes;
    non_printable[bad_offset] =
        bad_offset == 8U ? std::byte{0} : std::byte{0x7F};
    CheckErrorAt(omega::retail::InspectFntEnvelope(non_printable),
                 omega::asset::DecodeErrorCode::Malformed, bad_offset,
                 "FNT rejects and locates a non-printable byte in the observed "
                 "ASCII region");
  }

  auto missing_terminator = exact_bytes;
  missing_terminator[static_cast<std::size_t>(
      omega::retail::kFntObservedTerminatorOffset)] =
      static_cast<std::byte>(static_cast<unsigned char>('X'));
  CheckErrorAt(omega::retail::InspectFntEnvelope(missing_terminator),
               omega::asset::DecodeErrorCode::Malformed,
               omega::retail::kFntObservedTerminatorOffset,
               "FNT rejects a non-NUL observed terminator");

  CheckErrorAt(
      omega::retail::InspectFntEnvelope(std::span<const std::byte>(
          exact_bytes.data(),
          static_cast<std::size_t>(omega::retail::kFntObservedPayloadOffset))),
      omega::asset::DecodeErrorCode::Truncated,
      omega::retail::kFntObservedPayloadOffset,
      "FNT rejects a prefix with no opaque payload byte");

  auto limits = omega::asset::DecodeLimits{};
  limits.maximum_input_bytes = exact_bytes.size();
  Check(omega::retail::InspectFntEnvelope(exact_bytes, limits).has_value(),
        "FNT accepts the exact caller input-byte budget");
  limits.maximum_input_bytes = exact_bytes.size() - 1U;
  CheckError(omega::retail::InspectFntEnvelope(exact_bytes, limits),
             omega::asset::DecodeErrorCode::LimitExceeded,
             "FNT rejects one byte below the caller input-byte budget");

  limits = omega::asset::DecodeLimits{};
  limits.maximum_items = omega::retail::kFntMaximumDecodedItems;
  Check(omega::retail::InspectFntEnvelope(exact_bytes, limits).has_value(),
        "FNT accepts the exact caller item budget");
  limits.maximum_items = omega::retail::kFntMaximumDecodedItems - 1U;
  CheckError(omega::retail::InspectFntEnvelope(exact_bytes, limits),
             omega::asset::DecodeErrorCode::LimitExceeded,
             "FNT rejects one below the caller item budget");

  limits = omega::asset::DecodeLimits{};
  limits.maximum_output_bytes = omega::retail::kFntMaximumLogicalOutputBytes;
  Check(omega::retail::InspectFntEnvelope(exact_bytes, limits).has_value(),
        "FNT accepts the exact caller output-byte budget");
  limits.maximum_output_bytes =
      omega::retail::kFntMaximumLogicalOutputBytes - 1U;
  CheckError(omega::retail::InspectFntEnvelope(exact_bytes, limits),
             omega::asset::DecodeErrorCode::LimitExceeded,
             "FNT rejects one below the caller output-byte budget");

  limits = omega::asset::DecodeLimits{};
  limits.maximum_scratch_bytes = 0;
  limits.maximum_nesting_depth = 0;
  Check(omega::retail::InspectFntEnvelope(exact_bytes, limits).has_value(),
        "FNT flat inspection requires no dynamic scratch or nesting edges");

  std::vector<std::byte> maximum_input(
      static_cast<std::size_t>(omega::retail::kFntMaximumInputBytes),
      std::byte{0x5A});
  const auto prefix = MakeFnt(1U);
  std::copy_n(
      prefix.begin(),
      static_cast<std::size_t>(omega::retail::kFntObservedPayloadOffset),
      maximum_input.begin());
  Check(omega::retail::InspectFntEnvelope(maximum_input).has_value(),
        "FNT accepts the fixed project input ceiling");
  maximum_input.push_back(std::byte{0});
  auto permissive_limits = omega::asset::DecodeLimits{};
  permissive_limits.maximum_input_bytes =
      std::numeric_limits<std::uint64_t>::max();
  permissive_limits.maximum_items = std::numeric_limits<std::uint64_t>::max();
  permissive_limits.maximum_output_bytes =
      std::numeric_limits<std::uint64_t>::max();
  CheckError(
      omega::retail::InspectFntEnvelope(maximum_input, permissive_limits),
      omega::asset::DecodeErrorCode::LimitExceeded,
      "FNT caller limits cannot raise the fixed project input ceiling");

  if (failures != 0)
    std::cerr << failures << " FNT envelope descriptor test(s) failed\n";
  return failures == 0 ? 0 : 1;
}
