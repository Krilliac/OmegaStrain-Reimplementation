#include "omega/retail/lpd_envelope_decoder.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
constexpr std::size_t kAllocationInjectionDisabled = std::numeric_limits<std::size_t>::max();
std::size_t allocation_size_to_fail = kAllocationInjectionDisabled;
std::size_t matching_allocations_before_failure = 0;

void ArmAllocationFailure(const std::size_t allocation_size,
                          const std::size_t successful_matching_allocations = 0) noexcept
{
    allocation_size_to_fail = allocation_size;
    matching_allocations_before_failure = successful_matching_allocations;
}

void DisarmAllocationFailure() noexcept
{
    allocation_size_to_fail = kAllocationInjectionDisabled;
    matching_allocations_before_failure = 0;
}
} // namespace

void* operator new(const std::size_t size)
{
    if (size == allocation_size_to_fail)
    {
        if (matching_allocations_before_failure == 0)
        {
            DisarmAllocationFailure();
            throw std::bad_alloc{};
        }
        --matching_allocations_before_failure;
    }
    if (void* allocation = std::malloc(size == 0 ? 1U : size))
        return allocation;
    throw std::bad_alloc{};
}

void operator delete(void* allocation) noexcept
{
    std::free(allocation);
}

void operator delete(void* allocation, std::size_t) noexcept
{
    std::free(allocation);
}

namespace
{
using Counts = std::array<std::uint32_t, omega::asset::kLpdSourceTrackCount>;

void WriteU32(std::vector<std::byte>& bytes, const std::size_t offset, const std::uint32_t value)
{
    for (unsigned shift = 0; shift < 32; shift += 8)
        bytes[offset + shift / 8U] = static_cast<std::byte>((value >> shift) & 0xFFU);
}

[[nodiscard]] std::vector<std::byte> MakeLpd(const Counts& counts,
                                             const std::size_t zero_tail_bytes = 0)
{
    std::size_t entry_count = 0;
    for (const std::uint32_t count : counts)
        entry_count += count;

    constexpr std::size_t header_bytes = omega::retail::kLpdHeaderWordCount * 4U;
    std::vector<std::byte> bytes(header_bytes + entry_count * 4U + zero_tail_bytes, std::byte{0});
    WriteU32(bytes, 0, omega::retail::kLpdHeaderWordCount);
    for (std::size_t track_index = 0; track_index < counts.size(); ++track_index)
        WriteU32(bytes, 4U * (track_index + 1U), counts[track_index]);

    std::size_t source_offset = header_bytes;
    for (std::size_t track_index = 0; track_index < counts.size(); ++track_index)
    {
        for (std::uint32_t entry_index = 0; entry_index < counts[track_index]; ++entry_index)
        {
            for (std::size_t byte_index = 0; byte_index < 4U; ++byte_index)
            {
                const auto value =
                    static_cast<unsigned>(1U + track_index * 17U + entry_index * 5U + byte_index);
                bytes[source_offset++] = static_cast<std::byte>(value & 0xFFU);
            }
        }
    }
    return bytes;
}

int failures = 0;

void Check(const bool condition, const std::string_view message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

template <typename Result>
void CheckError(const Result& result, const omega::asset::DecodeErrorCode code,
                const std::string_view message)
{
    if (result)
    {
        Check(false, message);
        return;
    }
    Check(result.error().code == code, message);
    Check(!result.error().message.empty(), "LPD errors own a fixed nonempty diagnostic");
    Check(result.error().message.find('/') == std::string::npos &&
              result.error().message.find('\\') == std::string::npos,
          "LPD errors contain no filesystem path");
}

template <typename Result>
void CheckErrorAt(const Result& result, const omega::asset::DecodeErrorCode code,
                  const std::uint64_t byte_offset, const std::string_view message)
{
    CheckError(result, code, message);
    if (!result)
        Check(result.error().byte_offset == byte_offset,
              "LPD error reports the expected byte offset");
}
} // namespace

int main()
{
    static_assert(omega::retail::kLpdHeaderBytes == 88U);
    static_assert(omega::retail::kLpdMaximumEntryCount == 1002U);
    static_assert(omega::retail::kLpdMaximumDecodedItems == 1024U);
    static_assert(omega::retail::kLpdMaximumLogicalOutputBytes ==
                  sizeof(omega::asset::LpdEnvelopeIR) + 4008U);

    Counts counts{};
    counts[0] = 2;
    counts[1] = 1;
    counts[20] = 1;
    const auto exact_bytes = MakeLpd(counts);
    const auto exact = omega::retail::DecodeLpdEnvelope(exact_bytes);
    Check(exact && exact->tracks.size() == omega::asset::kLpdSourceTrackCount,
          "LPD exact counted envelope decodes all fixed source-order tracks");
    if (exact)
    {
        Check(exact->tracks[0].entries.size() == 2 && exact->tracks[1].entries.size() == 1 &&
                  exact->tracks[2].entries.empty() && exact->tracks[20].entries.size() == 1,
              "LPD count words group entries under the corresponding source-order "
              "track");
        Check(exact->tracks[0].entries[0] == std::array<std::byte, 4>{std::byte{1}, std::byte{2},
                                                                      std::byte{3}, std::byte{4}} &&
                  exact->tracks[0].entries[1] ==
                      std::array<std::byte, 4>{std::byte{6}, std::byte{7}, std::byte{8},
                                               std::byte{9}} &&
                  exact->tracks[1].entries[0] ==
                      std::array<std::byte, 4>{std::byte{18}, std::byte{19}, std::byte{20},
                                               std::byte{21}} &&
                  exact->tracks[20].entries[0] ==
                      std::array<std::byte, 4>{std::byte{85}, std::byte{86}, std::byte{87},
                                               std::byte{88}},
              "LPD preserves each opaque four-byte entry without numeric "
              "interpretation");
    }

    const Counts empty_counts{};
    const auto empty = omega::retail::DecodeLpdEnvelope(MakeLpd(empty_counts));
    Check(empty && std::all_of(empty->tracks.begin(), empty->tracks.end(),
                               [](const omega::asset::LpdTrackIR& track)
                               { return track.entries.empty(); }),
          "LPD accepts the exact fixed header with all source-track counts zero");

    const auto owned = [&]()
    {
        auto transient = MakeLpd(counts);
        auto result = omega::retail::DecodeLpdEnvelope(transient);
        Check(result.has_value(), "LPD transient source decodes before ownership check");
        auto envelope = result ? std::move(*result) : omega::asset::LpdEnvelopeIR{};
        transient.assign(transient.size(), std::byte{0xFF});
        transient.clear();
        transient.shrink_to_fit();
        return envelope;
    }();
    Check(exact && owned == *exact, "LPD decoded tracks remain owned after "
                                    "source replacement and destruction");
    const auto repeated = omega::retail::DecodeLpdEnvelope(exact_bytes);
    Check(exact && repeated && *repeated == *exact,
          "LPD stateless repeated decode returns identical canonical data");

    std::vector<std::byte> unaligned_storage(exact_bytes.size() + 1U, std::byte{0xA5});
    std::copy(exact_bytes.begin(), exact_bytes.end(), unaligned_storage.begin() + 1);
    const auto unaligned = omega::retail::DecodeLpdEnvelope(
        std::span<const std::byte>(unaligned_storage.data() + 1, exact_bytes.size()));
    Check(exact && unaligned && *unaligned == *exact,
          "LPD accepts an unaligned backing slice because the format has no "
          "address-alignment rule");

    bool accepted_bounded_zero_tails = true;
    const std::array<std::size_t, 6> zero_tail_cases{
        1U, 3U, 7U, 8U, 17U, static_cast<std::size_t>(omega::retail::kLpdMaximumZeroTailBytes),
    };
    for (const std::size_t tail_bytes : zero_tail_cases)
    {
        const auto result = omega::retail::DecodeLpdEnvelope(MakeLpd(counts, tail_bytes));
        accepted_bounded_zero_tails =
            accepted_bounded_zero_tails && result && exact && *result == *exact;
    }
    Check(accepted_bounded_zero_tails, "LPD omits any all-zero physical tail through the fixed "
                                       "evidence-backed ceiling");

    constexpr std::uint64_t logical_bytes = omega::retail::kLpdHeaderWordCount * 4U + 4U * 4U;
    auto excessive_tail =
        MakeLpd(counts, static_cast<std::size_t>(omega::retail::kLpdMaximumZeroTailBytes + 1U));
    CheckErrorAt(omega::retail::DecodeLpdEnvelope(excessive_tail),
                 omega::asset::DecodeErrorCode::LimitExceeded,
                 logical_bytes + omega::retail::kLpdMaximumZeroTailBytes,
                 "LPD rejects the first byte beyond the fixed zero-tail ceiling");

    for (const std::size_t dirty_tail_offset : {0U, 9U, 31U})
    {
        auto dirty_tail = MakeLpd(counts, 32U);
        dirty_tail[static_cast<std::size_t>(logical_bytes) + dirty_tail_offset] = std::byte{1};
        CheckErrorAt(omega::retail::DecodeLpdEnvelope(dirty_tail),
                     omega::asset::DecodeErrorCode::Malformed, logical_bytes + dirty_tail_offset,
                     "LPD rejects and locates the first nonzero byte in its physical tail");
    }
    auto earliest_dirty_tail = MakeLpd(counts, 32U);
    earliest_dirty_tail[static_cast<std::size_t>(logical_bytes) + 23U] = std::byte{2};
    earliest_dirty_tail[static_cast<std::size_t>(logical_bytes) + 4U] = std::byte{1};
    CheckErrorAt(omega::retail::DecodeLpdEnvelope(earliest_dirty_tail),
                 omega::asset::DecodeErrorCode::Malformed, logical_bytes + 4U,
                 "LPD reports the earliest nonzero physical-tail byte deterministically");

    auto dirty_before_excess = excessive_tail;
    dirty_before_excess[static_cast<std::size_t>(logical_bytes) + 5U] = std::byte{1};
    CheckErrorAt(omega::retail::DecodeLpdEnvelope(dirty_before_excess),
                 omega::asset::DecodeErrorCode::Malformed, logical_bytes + 5U,
                 "LPD reports an earlier dirty tail byte before a later "
                 "fixed-tail limit violation");

    auto bad_word_count = exact_bytes;
    WriteU32(bad_word_count, 0, omega::retail::kLpdHeaderWordCount - 1U);
    CheckErrorAt(omega::retail::DecodeLpdEnvelope(bad_word_count),
                 omega::asset::DecodeErrorCode::UnsupportedVariant, 0,
                 "LPD rejects a first word outside the fixed 22-word header family");
    CheckErrorAt(
        omega::retail::DecodeLpdEnvelope(std::span<const std::byte>(bad_word_count.data(), 4U)),
        omega::asset::DecodeErrorCode::UnsupportedVariant, 0,
        "LPD validates an available wrong first word before a later "
        "header truncation");

    bool all_header_prefixes_truncated = true;
    for (std::size_t size = 0; size < omega::retail::kLpdHeaderWordCount * 4U; ++size)
    {
        const auto result =
            omega::retail::DecodeLpdEnvelope(std::span<const std::byte>(exact_bytes.data(), size));
        all_header_prefixes_truncated =
            all_header_prefixes_truncated && !result &&
            result.error().code == omega::asset::DecodeErrorCode::Truncated &&
            result.error().byte_offset == size;
    }
    Check(all_header_prefixes_truncated,
          "every LPD header prefix is rejected at its earliest missing byte");

    bool all_payload_prefixes_truncated = true;
    constexpr std::size_t header_bytes = omega::retail::kLpdHeaderWordCount * 4U;
    for (std::size_t size = header_bytes; size < exact_bytes.size(); ++size)
    {
        const auto result =
            omega::retail::DecodeLpdEnvelope(std::span<const std::byte>(exact_bytes.data(), size));
        all_payload_prefixes_truncated =
            all_payload_prefixes_truncated && !result &&
            result.error().code == omega::asset::DecodeErrorCode::Truncated &&
            result.error().byte_offset == size;
    }
    Check(all_payload_prefixes_truncated,
          "every LPD counted-payload prefix is rejected at its earliest missing "
          "byte");

    auto hostile_count = MakeLpd(empty_counts);
    WriteU32(hostile_count, 4U, std::numeric_limits<std::uint32_t>::max());
    CheckErrorAt(omega::retail::DecodeLpdEnvelope(hostile_count),
                 omega::asset::DecodeErrorCode::LimitExceeded, 4U,
                 "LPD rejects a hostile first-track count through the bounded "
                 "item budget");

    Counts maximum_counts{};
    maximum_counts[0] = static_cast<std::uint32_t>(omega::retail::kLpdMaximumEntryCount);
    const auto maximum_input = MakeLpd(maximum_counts);
    Check(maximum_input.size() == omega::retail::kLpdMaximumInputBytes &&
              omega::retail::DecodeLpdEnvelope(maximum_input).has_value(),
          "LPD accepts the derived maximum entry, item, output, and physical-input envelope");

    auto over_fixed_input = maximum_input;
    over_fixed_input.push_back(std::byte{0});
    auto permissive_limits = omega::asset::DecodeLimits{};
    permissive_limits.maximum_input_bytes = std::numeric_limits<std::uint64_t>::max();
    permissive_limits.maximum_items = std::numeric_limits<std::uint64_t>::max();
    permissive_limits.maximum_output_bytes = std::numeric_limits<std::uint64_t>::max();
    CheckError(omega::retail::DecodeLpdEnvelope(over_fixed_input, permissive_limits),
               omega::asset::DecodeErrorCode::LimitExceeded,
               "LPD caller limits cannot raise the fixed 4096-byte physical-input ceiling");

    auto over_fixed_entries = MakeLpd(empty_counts);
    WriteU32(over_fixed_entries, 4U,
             static_cast<std::uint32_t>(omega::retail::kLpdMaximumEntryCount + 1U));
    CheckErrorAt(omega::retail::DecodeLpdEnvelope(over_fixed_entries, permissive_limits),
                 omega::asset::DecodeErrorCode::LimitExceeded, 4U,
                 "LPD caller limits cannot raise the derived fixed entry, item, or output ceiling");

    auto limits = omega::asset::DecodeLimits{};
    limits.maximum_input_bytes = exact_bytes.size();
    Check(omega::retail::DecodeLpdEnvelope(exact_bytes, limits).has_value(),
          "LPD succeeds at the exact physical-input budget");
    limits.maximum_input_bytes = exact_bytes.size() - 1U;
    CheckError(omega::retail::DecodeLpdEnvelope(exact_bytes, limits),
               omega::asset::DecodeErrorCode::LimitExceeded,
               "LPD rejects one byte below the physical-input budget");

    constexpr std::uint64_t decoded_items = 1U + omega::asset::kLpdSourceTrackCount + 4U;
    limits = omega::asset::DecodeLimits{};
    limits.maximum_items = decoded_items;
    Check(omega::retail::DecodeLpdEnvelope(exact_bytes, limits).has_value(),
          "LPD succeeds at the exact root-track-entry item budget");
    limits.maximum_items = decoded_items - 1U;
    CheckErrorAt(omega::retail::DecodeLpdEnvelope(exact_bytes, limits),
                 omega::asset::DecodeErrorCode::LimitExceeded, 84U,
                 "LPD reports the first count word that exceeds the item budget");

    constexpr std::uint64_t output_bytes = sizeof(omega::asset::LpdEnvelopeIR) + 4U * 4U;
    limits = omega::asset::DecodeLimits{};
    limits.maximum_output_bytes = output_bytes;
    Check(omega::retail::DecodeLpdEnvelope(exact_bytes, limits).has_value(),
          "LPD succeeds at the exact owned-output budget");
    limits.maximum_output_bytes = output_bytes - 1U;
    CheckErrorAt(omega::retail::DecodeLpdEnvelope(exact_bytes, limits),
                 omega::asset::DecodeErrorCode::LimitExceeded, 84U,
                 "LPD reports the first count word that exceeds the output budget");

    limits = omega::asset::DecodeLimits{};
    limits.maximum_scratch_bytes = 0;
    limits.maximum_nesting_depth = 0;
    Check(omega::retail::DecodeLpdEnvelope(exact_bytes, limits).has_value(),
          "LPD flat two-pass decode uses no dynamic scratch or nesting edges");

    limits = omega::asset::DecodeLimits{};
    limits.maximum_items = 1U + omega::asset::kLpdSourceTrackCount - 1U;
    CheckError(omega::retail::DecodeLpdEnvelope(std::span<const std::byte>{}, limits),
               omega::asset::DecodeErrorCode::LimitExceeded,
               "LPD enforces its fixed root-track item budget before reading input");

    Counts allocation_counts{};
    allocation_counts[0] = 17U;
    allocation_counts[1] = 19U;
    allocation_counts[20] = 23U;
    const auto allocation_bytes = MakeLpd(allocation_counts);
    constexpr std::array<std::size_t, 3> owning_allocation_sizes{
        17U * sizeof(std::array<std::byte, 4>),
        19U * sizeof(std::array<std::byte, 4>),
        23U * sizeof(std::array<std::byte, 4>),
    };
    for (const std::size_t allocation_size : owning_allocation_sizes)
    {
        ArmAllocationFailure(allocation_size);
        const auto allocation_attempt = omega::retail::DecodeLpdEnvelope(allocation_bytes);
        DisarmAllocationFailure();
        CheckError(allocation_attempt, omega::asset::DecodeErrorCode::LimitExceeded,
                   "LPD maps each explicit owning-storage allocation failure to a typed error");
    }
    Check(omega::retail::DecodeLpdEnvelope(allocation_bytes).has_value(),
          "LPD decode succeeds unchanged after every owning-storage allocation failure");

    return failures;
}
