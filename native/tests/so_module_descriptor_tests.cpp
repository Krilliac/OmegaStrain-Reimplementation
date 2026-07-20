#include "omega/retail/so_module_descriptor.h"

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

// Size-based failure injection. Failing by allocation *ordinal* is unsound under MSVC's
// _ITERATOR_DEBUG_LEVEL=2: std::vector's default constructor is noexcept yet allocates a small
// container proxy, so failing that first allocation throws out of a noexcept function and terminates
// before any catch can run. The decoder's owned-output buffers are the only large allocations, so a
// size threshold above the tiny proxies deterministically fails a non-noexcept reserve inside the
// decoder's try - in both Debug and Release.
std::size_t fail_allocation_at_least_bytes = kAllocationInjectionDisabled;

void ArmAllocationFailureAtLeast(const std::size_t bytes) noexcept
{
    fail_allocation_at_least_bytes = bytes;
}

void DisarmAllocationFailure() noexcept
{
    fail_allocation_at_least_bytes = kAllocationInjectionDisabled;
}
} // namespace

void* operator new(const std::size_t size)
{
    if (fail_allocation_at_least_bytes != kAllocationInjectionDisabled &&
        size >= fail_allocation_at_least_bytes)
    {
        DisarmAllocationFailure();
        throw std::bad_alloc{};
    }
    if (void* allocation = std::malloc(size == 0 ? 1U : size))
        return allocation;
    throw std::bad_alloc{};
}

void operator delete(void* allocation) noexcept { std::free(allocation); }
void operator delete(void* allocation, std::size_t) noexcept { std::free(allocation); }

namespace
{
using omega::asset::DecodeErrorCode;
using omega::asset::DecodeLimits;
using omega::retail::InspectSoModule;
using omega::retail::SoModuleDescriptor;

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
void CheckError(
    const Result& result, const DecodeErrorCode code, const std::string_view message)
{
    Check(!result.has_value() && result.error().code == code, message);
}

void EmitU32(std::vector<std::byte>& out, const std::uint32_t value)
{
    for (unsigned shift = 0; shift < 32; shift += 8)
        out.push_back(static_cast<std::byte>((value >> shift) & 0xFFU));
}

// Length-prefixed, NUL-terminated, 4-byte zero-padded value exactly as the grammar requires.
void EmitLp(std::vector<std::byte>& out, const std::string_view content)
{
    EmitU32(out, static_cast<std::uint32_t>(content.size() + 1U));
    for (const char character : content)
        out.push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
    out.push_back(std::byte{0});
    while (out.size() % 4U != 0U)
        out.push_back(std::byte{0});
}

struct CallableSpec
{
    std::uint32_t flag = 0;
    std::uint32_t ordinal = 0;
    std::uint32_t label = 0;
    std::uint32_t entry = 0;
};

// A complete, canonical module exercising every section, a local+external callable, and a matching
// pre-entry 0x003B/label marker. `callables` and code-cell markers are configurable for the
// invariant-observation tests.
std::vector<std::byte> BuildValid(
    const std::vector<CallableSpec>& callables = {{.flag = 0, .ordinal = 0, .label = 5, .entry = 3},
        {.flag = 1, .ordinal = 1, .label = 0, .entry = 0}},
    const bool write_markers = true, const std::uint32_t extra_simple_types = 0)
{
    constexpr std::uint32_t kCodeCells = 8;
    std::array<std::uint32_t, kCodeCells> code{};
    if (write_markers)
    {
        for (const CallableSpec& spec : callables)
        {
            if (spec.flag == 0 && spec.entry > 0 && spec.entry <= kCodeCells)
                code[spec.entry - 1U] = (spec.label << 16U) | 0x003BU;
        }
    }

    std::vector<std::byte> out;
    EmitU32(out, 8);            // code_offset
    EmitU32(out, kCodeCells);   // code_cell_count
    for (const std::uint32_t cell : code)
        EmitU32(out, cell);

    EmitU32(out, 1);            // literal_count
    EmitLp(out, "lit");

    EmitU32(out, 1 + extra_simple_types); // type_count
    EmitLp(out, "T");
    EmitU32(out, 0);            // external_flag
    EmitLp(out, "B");           // base
    EmitU32(out, 1);            // member_count
    EmitLp(out, "m");
    EmitU32(out, 7);            // member value (opaque)
    for (std::uint32_t extra = 0; extra < extra_simple_types; ++extra)
    {
        EmitLp(out, "T");       // name
        EmitU32(out, 0);        // external_flag
        EmitLp(out, "B");       // base
        EmitU32(out, 0);        // member_count
    }

    EmitU32(out, 1);            // enum_count
    EmitLp(out, "E");
    EmitU32(out, 0);            // external_flag (local -> has values)
    EmitU32(out, 2);            // value_count
    EmitLp(out, "A");
    EmitU32(out, 0);
    EmitLp(out, "B");
    EmitU32(out, 1);

    EmitU32(out, 0);            // globals_reserved
    EmitU32(out, 2);            // global_count
    EmitLp(out, "g0");
    EmitU32(out, 0);            // fields[0]
    EmitU32(out, 0);            // fields[1] flag
    EmitU32(out, 0);            // fields[2] ordinal
    EmitU32(out, 0);            // fields[3] initializer cell count
    EmitLp(out, "g1");
    EmitU32(out, 0);
    EmitU32(out, 1);            // external
    EmitU32(out, 1);            // ordinal 1
    EmitU32(out, 0);

    EmitU32(out, static_cast<std::uint32_t>(callables.size()));
    for (const CallableSpec& spec : callables)
    {
        EmitLp(out, "c");
        EmitU32(out, 0);            // fields[0]
        EmitU32(out, spec.flag);    // fields[1] flag
        EmitU32(out, spec.ordinal); // fields[2] ordinal
        EmitU32(out, spec.label);   // fields[3] label id
        EmitU32(out, spec.entry);   // fields[4] entry cell
        for (int extra = 0; extra < 5; ++extra)
            EmitU32(out, 0);        // fields[5..9]
        EmitU32(out, 0);            // parameter_count
    }
    return out;
}

// A minimal module whose parsing reaches `count_field_writer` producing a hostile section count and
// then stops, so the ceiling check fires before any body bytes are needed.
std::vector<std::byte> HostilePrefix(const std::string_view section)
{
    std::vector<std::byte> out;
    EmitU32(out, 8);
    EmitU32(out, section == "code" ? 0x7FFFFFFFU : 0U); // code_cell_count
    if (section == "code")
        return out;
    EmitU32(out, section == "literal" ? 0x7FFFFFFFU : 0U); // literal_count
    if (section == "literal")
        return out;
    EmitU32(out, section == "type" ? 0x7FFFFFFFU : 0U); // type_count
    if (section == "type")
        return out;
    EmitU32(out, section == "enum" ? 0x7FFFFFFFU : 0U); // enum_count
    if (section == "enum")
        return out;
    EmitU32(out, 0);                                     // globals_reserved
    EmitU32(out, section == "global" ? 0x7FFFFFFFU : 0U); // global_count
    if (section == "global")
        return out;
    EmitU32(out, 0x7FFFFFFFU);                           // callable_count
    return out;
}

void RunValidModuleChecks()
{
    const auto bytes = BuildValid();
    const auto result = InspectSoModule(bytes);
    Check(result.has_value(), "canonical SO module is accepted");
    if (!result)
        return;
    const SoModuleDescriptor& descriptor = *result;

    Check(descriptor.code_cell_count == 8 && descriptor.code_offset_is_8,
        "SO code header is validated and the code region is measured");
    Check(descriptor.literal_count == 1, "SO literal count is recovered");
    Check(descriptor.types.size() == 1 && descriptor.types[0].external_flag == 0 &&
              descriptor.types[0].member_count == 1 && descriptor.type_member_total == 1,
        "SO type record and member total are recovered");
    Check(descriptor.enums.size() == 1 && descriptor.enums[0].value_count == 2 &&
              descriptor.local_enum_value_total == 2,
        "SO local enum values are recovered");
    Check(descriptor.globals.size() == 2 && descriptor.globals_reserved == 0 &&
              descriptor.global_ordinals_match_table_order,
        "SO globals and reserved word are recovered with ordinal observation");
    Check(descriptor.callables.size() == 2 && descriptor.local_callable_count == 1 &&
              descriptor.external_callable_count == 1,
        "SO callables split into local and external counts");
    Check(descriptor.callable_ordinals_match_table_order &&
              descriptor.local_entries_strictly_increasing,
        "SO ordinal and local-entry-order observations hold for the canonical module");
    Check(descriptor.local_entry_marker_matches == 1 && descriptor.local_entry_marker_expected == 1,
        "SO local entry marker matches the pre-entry 0x003B/label relationship");
    Check(descriptor.module_extent.observed_bytes == bytes.size() &&
              descriptor.module_extent.input_bytes == bytes.size() &&
              descriptor.module_extent.relation == omega::retail::ObservedExtentRelation::Exact,
        "SO module extent reaches exact EOF");

    const auto again = InspectSoModule(bytes);
    Check(again.has_value() && *again == descriptor, "SO inspection is deterministic");
}

void RunOwnershipCheck()
{
    const auto reference = InspectSoModule(BuildValid());
    Check(reference.has_value(), "SO reference descriptor decodes");
    const auto owned = [&]() {
        auto transient = BuildValid();
        auto decoded = InspectSoModule(transient);
        Check(decoded.has_value(), "transient SO source decodes before ownership check");
        auto descriptor = decoded ? std::move(*decoded) : SoModuleDescriptor{};
        transient.assign(transient.size(), std::byte{0xFF});
        transient.clear();
        transient.shrink_to_fit();
        return descriptor;
    }();
    Check(reference.has_value() && owned == *reference,
        "SO descriptor remains valid after its source is overwritten and destroyed");
}

void RunTruncationChecks()
{
    const auto bytes = BuildValid();
    bool all_short_prefixes_rejected = true;
    for (std::size_t size = 0; size < bytes.size(); ++size)
    {
        const auto result =
            InspectSoModule(std::span<const std::byte>(bytes.data(), size));
        all_short_prefixes_rejected = all_short_prefixes_rejected && !result.has_value();
    }
    Check(all_short_prefixes_rejected, "every truncated SO prefix is rejected");
    CheckError(InspectSoModule(std::span<const std::byte>(bytes.data(), bytes.size() - 1U)),
        DecodeErrorCode::Truncated, "SO one byte short of EOF is rejected");

    auto trailing = bytes;
    trailing.push_back(std::byte{0});
    CheckError(InspectSoModule(trailing), DecodeErrorCode::Malformed,
        "SO trailing bytes after the callable table are rejected");
}

void RunLpStringChecks()
{
    // Missing NUL terminator on the first literal.
    {
        std::vector<std::byte> out;
        EmitU32(out, 8);
        EmitU32(out, 0);          // no code cells
        EmitU32(out, 1);          // literal_count
        EmitU32(out, 2);          // length 2
        out.push_back(std::byte{'a'});
        out.push_back(std::byte{'b'}); // no NUL
        CheckError(InspectSoModule(out), DecodeErrorCode::Malformed,
            "SO length-prefixed value without a NUL terminator is rejected");
    }
    // Nonzero padding after a literal.
    {
        std::vector<std::byte> out;
        EmitU32(out, 8);
        EmitU32(out, 0);
        EmitU32(out, 1);
        EmitU32(out, 2);          // length 2 -> content "a\0", then 2 padding bytes to 4-align
        out.push_back(std::byte{'a'});
        out.push_back(std::byte{0});
        out.push_back(std::byte{1}); // nonzero padding
        out.push_back(std::byte{0});
        CheckError(InspectSoModule(out), DecodeErrorCode::Malformed,
            "SO length-prefixed value with nonzero padding is rejected");
    }
    // Length reaches past EOF.
    {
        std::vector<std::byte> out;
        EmitU32(out, 8);
        EmitU32(out, 0);
        EmitU32(out, 1);
        EmitU32(out, 0x0000FFFFU); // huge length, no content follows
        CheckError(InspectSoModule(out), DecodeErrorCode::Truncated,
            "SO length-prefixed value whose length reaches EOF is rejected");
    }
    // Non-ASCII byte inside a type name (ASCII-validated field).
    {
        std::vector<std::byte> out;
        EmitU32(out, 8);
        EmitU32(out, 0);
        EmitU32(out, 0);          // literal_count
        EmitU32(out, 1);          // type_count
        EmitU32(out, 2);          // name length 2
        out.push_back(std::byte{0x01}); // control character
        out.push_back(std::byte{0});
        out.push_back(std::byte{0});
        out.push_back(std::byte{0});
        CheckError(InspectSoModule(out), DecodeErrorCode::Malformed,
            "SO non-printable-ASCII metadata name is rejected");
    }
}

void RunFlagChecks()
{
    // type external flag 2
    {
        std::vector<std::byte> out;
        EmitU32(out, 8);
        EmitU32(out, 0);
        EmitU32(out, 0);
        EmitU32(out, 1);          // type_count
        EmitLp(out, "T");
        EmitU32(out, 2);          // invalid flag
        CheckError(InspectSoModule(out), DecodeErrorCode::Malformed,
            "SO type flag outside {0,1} is rejected");
    }
    // enum external flag 2
    {
        std::vector<std::byte> out;
        EmitU32(out, 8);
        EmitU32(out, 0);
        EmitU32(out, 0);
        EmitU32(out, 0);          // type_count
        EmitU32(out, 1);          // enum_count
        EmitLp(out, "E");
        EmitU32(out, 2);          // invalid flag
        CheckError(InspectSoModule(out), DecodeErrorCode::Malformed,
            "SO enum flag outside {0,1} is rejected");
    }
    // global flag 2
    {
        std::vector<std::byte> out;
        EmitU32(out, 8);
        EmitU32(out, 0);
        EmitU32(out, 0);
        EmitU32(out, 0);
        EmitU32(out, 0);          // enum_count
        EmitU32(out, 0);          // globals_reserved
        EmitU32(out, 1);          // global_count
        EmitLp(out, "g");
        EmitU32(out, 0);
        EmitU32(out, 2);          // fields[1] invalid flag
        EmitU32(out, 0);
        EmitU32(out, 0);
        CheckError(InspectSoModule(out), DecodeErrorCode::Malformed,
            "SO global flag outside {0,1} is rejected");
    }
    // callable flag 2
    {
        std::vector<std::byte> out;
        EmitU32(out, 8);
        EmitU32(out, 0);
        EmitU32(out, 0);
        EmitU32(out, 0);
        EmitU32(out, 0);
        EmitU32(out, 0);          // globals_reserved
        EmitU32(out, 0);          // global_count
        EmitU32(out, 1);          // callable_count
        EmitLp(out, "c");
        EmitU32(out, 0);
        EmitU32(out, 2);          // fields[1] invalid flag
        for (int extra = 0; extra < 8; ++extra)
            EmitU32(out, 0);
        EmitU32(out, 0);          // parameter_count
        CheckError(InspectSoModule(out), DecodeErrorCode::Malformed,
            "SO callable flag outside {0,1} is rejected");
    }
}

void RunCodeOffsetAndCountChecks()
{
    auto wrong_offset = BuildValid();
    wrong_offset[0] = std::byte{9}; // code_offset 9 != 8
    CheckError(InspectSoModule(wrong_offset), DecodeErrorCode::Malformed,
        "SO code offset that does not follow the header is rejected");

    for (const std::string_view section : {"code", "literal", "type", "enum", "global", "callable"})
        CheckError(InspectSoModule(HostilePrefix(section)), DecodeErrorCode::LimitExceeded,
            "SO hostile section count is rejected by the fixed ceiling");
}

void RunInvariantObservationChecks()
{
    // Ordinal mismatch is observed, not rejected.
    {
        const auto bytes = BuildValid({{.flag = 0, .ordinal = 3, .label = 5, .entry = 3},
            {.flag = 1, .ordinal = 1, .label = 0, .entry = 0}});
        const auto result = InspectSoModule(bytes);
        Check(result.has_value() && !result->callable_ordinals_match_table_order,
            "SO ordinal mismatch is retained as an observation, not a rejection");
    }
    // Non-increasing local entries are observed, not rejected.
    {
        const auto bytes = BuildValid({{.flag = 0, .ordinal = 0, .label = 5, .entry = 5},
            {.flag = 0, .ordinal = 1, .label = 6, .entry = 3}});
        const auto result = InspectSoModule(bytes);
        Check(result.has_value() && !result->local_entries_strictly_increasing &&
                  result->local_callable_count == 2,
            "SO non-increasing local entries are retained as an observation");
    }
    // A local entry whose code cell lacks the marker lowers the match count without rejection.
    {
        const auto bytes = BuildValid({{.flag = 0, .ordinal = 0, .label = 5, .entry = 3}},
            /*write_markers=*/false);
        const auto result = InspectSoModule(bytes);
        Check(result.has_value() && result->local_entry_marker_expected == 1 &&
                  result->local_entry_marker_matches == 0,
            "SO missing pre-entry marker is observed as a non-match, not a rejection");
    }
}

void RunLimitChecks()
{
    const auto bytes = BuildValid();

    DecodeLimits limits;
    limits.maximum_input_bytes = bytes.size();
    Check(InspectSoModule(bytes, limits).has_value(),
        "SO succeeds at the exact input-byte budget");
    limits.maximum_input_bytes = bytes.size() - 1U;
    CheckError(InspectSoModule(bytes, limits), DecodeErrorCode::LimitExceeded,
        "SO rejects one byte below the required input-byte budget");

    // The canonical module retains 1 type + 1 enum + 2 globals + 2 callables = 6 records.
    limits = DecodeLimits{};
    limits.maximum_items = 6;
    Check(InspectSoModule(bytes, limits).has_value(),
        "SO succeeds at the exact retained-item budget");
    limits.maximum_items = 5;
    CheckError(InspectSoModule(bytes, limits), DecodeErrorCode::LimitExceeded,
        "SO rejects one item below the retained-item budget");

    limits = DecodeLimits{};
    const std::uint64_t exact_output = sizeof(SoModuleDescriptor) +
        1U * sizeof(omega::retail::SoTypeSummary) + 1U * sizeof(omega::retail::SoEnumSummary) +
        2U * sizeof(omega::retail::SoGlobalSummary) + 2U * sizeof(omega::retail::SoCallableSummary);
    limits.maximum_output_bytes = exact_output;
    Check(InspectSoModule(bytes, limits).has_value(),
        "SO succeeds at the exact owned-output budget");
    limits.maximum_output_bytes = exact_output - 1U;
    CheckError(InspectSoModule(bytes, limits), DecodeErrorCode::LimitExceeded,
        "SO rejects one byte below the owned-output budget");

    limits = DecodeLimits{};
    limits.maximum_output_bytes = sizeof(SoModuleDescriptor) - 1U;
    CheckError(InspectSoModule(bytes, limits), DecodeErrorCode::LimitExceeded,
        "SO rejects an output budget below the fixed descriptor size before parsing");
}

void RunAllocationFailureChecks()
{
    // Fixtures must be built BEFORE arming: any allocation while armed that is not inside the
    // decoder's try (e.g. a fixture vector growing past the threshold) would escape uncaught.
    const auto large = BuildValid(
        {{.flag = 0, .ordinal = 0, .label = 5, .entry = 3}, {.flag = 1, .ordinal = 1}}, true, 64U);
    const auto small = BuildValid();
    constexpr std::size_t kThreshold = 256; // above container proxies, below the 65-record reserve

    Check(InspectSoModule(large).has_value(),
        "SO large fixture decodes cleanly without any injected failure");

    ArmAllocationFailureAtLeast(kThreshold);
    const auto failed = InspectSoModule(large);
    DisarmAllocationFailure();
    CheckError(failed, DecodeErrorCode::LimitExceeded,
        "SO maps an owned-output allocation failure to a typed error rather than crashing");

    ArmAllocationFailureAtLeast(kThreshold);
    const auto small_result = InspectSoModule(small);
    DisarmAllocationFailure();
    Check(small_result.has_value(),
        "SO succeeds under the allocation-size threshold when no large allocation is needed");
}
} // namespace

int main()
{
    RunValidModuleChecks();
    RunOwnershipCheck();
    RunTruncationChecks();
    RunLpStringChecks();
    RunFlagChecks();
    RunCodeOffsetAndCountChecks();
    RunInvariantObservationChecks();
    RunLimitChecks();
    RunAllocationFailureChecks();

    if (failures != 0)
        std::cerr << failures << " SO module descriptor check(s) failed\n";
    return failures == 0 ? 0 : 1;
}
