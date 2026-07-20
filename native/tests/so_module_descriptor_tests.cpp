#include "omega/retail/so_module_descriptor.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <iterator>
#include <limits>
#include <new>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
struct AllocationProbeResult
{
    std::size_t allocation_count = 0;
    bool failure_triggered = false;
};

bool allocation_probe_armed = false;
std::size_t fail_allocation_ordinal = 0;
std::size_t allocation_count = 0;
bool allocation_failure_triggered = false;

void ArmAllocationProbe(const std::size_t failure_ordinal) noexcept
{
    allocation_probe_armed = true;
    fail_allocation_ordinal = failure_ordinal;
    allocation_count = 0;
    allocation_failure_triggered = false;
}

[[nodiscard]] AllocationProbeResult DisarmAllocationProbe() noexcept
{
    const AllocationProbeResult result{
        .allocation_count = allocation_count,
        .failure_triggered = allocation_failure_triggered,
    };
    allocation_probe_armed = false;
    fail_allocation_ordinal = 0;
    allocation_count = 0;
    allocation_failure_triggered = false;
    return result;
}

[[nodiscard]] bool ShouldFailAllocation() noexcept
{
    if (!allocation_probe_armed)
        return false;
    ++allocation_count;
    if (fail_allocation_ordinal != 0 && allocation_count >= fail_allocation_ordinal)
    {
        allocation_failure_triggered = true;
        return true;
    }
    return false;
}

[[nodiscard]] void* Allocate(const std::size_t size)
{
    if (ShouldFailAllocation())
        throw std::bad_alloc{};
    if (void* allocation = std::malloc(size == 0 ? 1U : size))
        return allocation;
    throw std::bad_alloc{};
}
} // namespace

void* operator new(const std::size_t size)
{
    return Allocate(size);
}

void* operator new[](const std::size_t size) { return Allocate(size); }

void operator delete(void* allocation) noexcept { std::free(allocation); }
void operator delete(void* allocation, std::size_t) noexcept { std::free(allocation); }
void operator delete[](void* allocation) noexcept { std::free(allocation); }
void operator delete[](void* allocation, std::size_t) noexcept { std::free(allocation); }

namespace
{
using omega::asset::DecodeErrorCode;
using omega::asset::DecodeLimits;
using omega::retail::DefaultSoDecodeLimits;
using omega::retail::InspectSoModule;
using omega::retail::kSoMaximumLpValueContentBytes;
using omega::retail::SoModuleDescriptor;
using omega::retail::SoDecodeError;
using omega::retail::SoOwnedSummaryBuffer;
using omega::retail::SoTypeSummary;

static_assert(!std::is_copy_constructible_v<SoModuleDescriptor>);
static_assert(std::is_nothrow_default_constructible_v<SoModuleDescriptor>);
static_assert(std::is_nothrow_move_constructible_v<SoModuleDescriptor>);
static_assert(std::is_nothrow_copy_constructible_v<SoDecodeError>);
static_assert(std::is_same_v<decltype(SoDecodeError{}.static_message), std::string_view>);

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

template <typename Result>
void CheckPathFreeError(const Result& result, const std::string_view secret,
    const std::string_view message)
{
    const bool path_free = !result.has_value() &&
        result.error().static_message.find(secret) == std::string_view::npos &&
        result.error().static_message.find('\\') == std::string_view::npos &&
        result.error().static_message.find('/') == std::string_view::npos;
    Check(path_free, message);
}

void EmitU32(std::vector<std::byte>& out, const std::uint32_t value)
{
    for (unsigned shift = 0; shift < 32; shift += 8)
        out.push_back(static_cast<std::byte>((value >> shift) & 0xFFU));
}

// Present (non-sentinel) LP value: length-prefixed, NUL-terminated, and 4-byte zero-padded.
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

// A complete, canonical module exercising every section, both flag branches, nonempty initializer
// and parameter arrays, and a matching pre-entry 0x003B/label word pattern. `callables` and pattern
// words are configurable for the invariant-observation tests.
std::vector<std::byte> BuildValid(
    const std::vector<CallableSpec>& callables = {{.flag = 0, .ordinal = 0, .label = 5, .entry = 3},
        {.flag = 1, .ordinal = 1, .label = 0, .entry = 0}},
    const bool write_patterns = true)
{
    constexpr std::uint32_t kCodeCells = 8;
    std::array<std::uint32_t, kCodeCells> code{};
    if (write_patterns)
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

    EmitU32(out, 1);            // type_count
    EmitLp(out, "T");
    EmitU32(out, 0);            // observed flag
    EmitLp(out, "B");           // base
    EmitU32(out, 1);            // member_count
    EmitLp(out, "m");
    EmitU32(out, 7);            // member value (opaque)
    EmitU32(out, 2);            // enum_count
    EmitLp(out, "E");
    EmitU32(out, 0);            // observed flag zero -> has values
    EmitU32(out, 2);            // value_count
    EmitLp(out, "A");
    EmitU32(out, 0);
    EmitLp(out, "B");
    EmitU32(out, 1);
    EmitLp(out, "X");
    EmitU32(out, 1);            // observed flag one -> no value-count field

    EmitU32(out, 0);            // globals_reserved
    EmitU32(out, 2);            // global_count
    EmitLp(out, "g0");
    EmitU32(out, 0);            // fields[0]
    EmitU32(out, 0);            // fields[1] flag
    EmitU32(out, 0);            // fields[2] ordinal
    EmitU32(out, 2);            // fields[3] initializer cell count
    EmitU32(out, 0x11111111U);
    EmitU32(out, 0x22222222U);
    EmitLp(out, "g1");
    EmitU32(out, 0);
    EmitU32(out, 1);            // observed flag one
    EmitU32(out, 1);            // ordinal 1
    EmitU32(out, 0);

    EmitU32(out, static_cast<std::uint32_t>(callables.size()));
    for (const CallableSpec& spec : callables)
    {
        EmitLp(out, "c");
        EmitU32(out, 0);            // fields[0]
        EmitU32(out, spec.flag);    // fields[1] flag
        EmitU32(out, spec.ordinal); // fields[2] ordinal
        EmitU32(out, spec.label);   // fields[3] label candidate
        EmitU32(out, spec.entry);   // fields[4] entry candidate
        for (int extra = 0; extra < 5; ++extra)
            EmitU32(out, 0);        // fields[5..9]
        const std::uint32_t parameter_count = spec.flag == 0 ? 2U : 0U;
        EmitU32(out, parameter_count);
        for (std::uint32_t parameter = 0; parameter < parameter_count; ++parameter)
            EmitU32(out, parameter + 1U);
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

std::vector<std::byte> BuildFixedCeilingLiteralModule()
{
    constexpr std::size_t kFixedModuleBytes = 524'288;
    constexpr std::uint32_t kLiteralContentBytes = 524'251;
    constexpr std::uint32_t kSerializedLiteralBytes = kLiteralContentBytes + 1U;

    std::vector<std::byte> out;
    out.reserve(kFixedModuleBytes);
    EmitU32(out, 8);
    EmitU32(out, 0); // code cells
    EmitU32(out, 1); // literals
    EmitU32(out, kSerializedLiteralBytes);
    out.insert(out.end(), kLiteralContentBytes, std::byte{'x'});
    out.push_back(std::byte{0});
    EmitU32(out, 0); // types
    EmitU32(out, 0); // enums
    EmitU32(out, 0); // globals reserved
    EmitU32(out, 0); // globals
    EmitU32(out, 0); // callables
    return out;
}

// Two records each remain below their nested ceiling, but their combined count exceeds the fixed
// per-module aggregate. The second body is deliberately omitted: rejection must precede traversal.
std::vector<std::byte> HostileNestedAggregatePrefix(const std::string_view section)
{
    std::vector<std::byte> out;
    EmitU32(out, 8);
    EmitU32(out, 0); // code cells
    EmitU32(out, 0); // literals

    if (section == "member")
    {
        EmitU32(out, 2); // types
        EmitLp(out, "T");
        EmitU32(out, 0);
        EmitLp(out, "B");
        EmitU32(out, 32);
        for (std::uint32_t index = 0; index < 32; ++index)
        {
            EmitLp(out, "m");
            EmitU32(out, 0);
        }
        EmitLp(out, "T");
        EmitU32(out, 0);
        EmitLp(out, "B");
        EmitU32(out, 33); // aggregate 65 > 64
        return out;
    }

    EmitU32(out, 0); // types
    if (section == "enum-value")
    {
        EmitU32(out, 2); // enums
        EmitLp(out, "E");
        EmitU32(out, 0);
        EmitU32(out, 8);
        for (std::uint32_t index = 0; index < 8; ++index)
        {
            EmitLp(out, "v");
            EmitU32(out, index);
        }
        EmitLp(out, "E");
        EmitU32(out, 0);
        EmitU32(out, 9); // aggregate 17 > 16
        return out;
    }

    EmitU32(out, 0); // enums
    EmitU32(out, 0); // globals reserved
    if (section == "initializer")
    {
        EmitU32(out, 2); // globals
        EmitLp(out, "g");
        EmitU32(out, 0);
        EmitU32(out, 0);
        EmitU32(out, 0);
        EmitU32(out, 4096);
        for (std::uint32_t index = 0; index < 4096; ++index)
            EmitU32(out, 0);
        EmitLp(out, "g");
        EmitU32(out, 0);
        EmitU32(out, 0);
        EmitU32(out, 1);
        EmitU32(out, 4097); // aggregate 8193 > 8192
        return out;
    }

    EmitU32(out, 0); // globals
    EmitU32(out, 2); // callables
    EmitLp(out, "c");
    EmitU32(out, 0);
    EmitU32(out, 0);
    EmitU32(out, 0);
    for (int field = 3; field < 10; ++field)
        EmitU32(out, 0);
    EmitU32(out, 1024);
    for (std::uint32_t index = 0; index < 1024; ++index)
        EmitU32(out, 0);
    EmitLp(out, "c");
    EmitU32(out, 0);
    EmitU32(out, 0);
    EmitU32(out, 1);
    for (int field = 3; field < 10; ++field)
        EmitU32(out, 0);
    EmitU32(out, 1025); // aggregate 2049 > 2048
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

    Check(descriptor.code_cell_count == 8 && descriptor.code_region.offset == 8,
        "SO enforced code offset and code region are measured");
    Check(descriptor.literal_count == 1, "SO literal count is recovered");
    Check(descriptor.types.size() == 1 && descriptor.types[0].observed_flag == 0 &&
              descriptor.types[0].member_count == 1 && descriptor.type_member_total == 1,
        "SO type record and member total are recovered");
    std::uint32_t iterated_type_members = 0;
    for (const omega::retail::SoTypeSummary& type : descriptor.types)
        iterated_type_members += type.member_count;
    Check(iterated_type_members == descriptor.type_member_total,
        "SO exact summary buffers expose stable const iteration");
    Check(descriptor.enums.size() == 2 && descriptor.enums[0].observed_flag == 0 &&
              descriptor.enums[0].value_count == 2 && descriptor.enums[1].observed_flag == 1 &&
              descriptor.enums[1].value_count == 0 &&
              descriptor.flag_zero_enum_value_total == 2,
        "SO enum flag branches and flag-zero values are recovered");
    Check(descriptor.globals.size() == 2 && descriptor.globals_reserved == 0 &&
              descriptor.global_initializer_cell_total == 2 &&
              descriptor.global_ordinals_match_table_order &&
              descriptor.flag_one_global_initializers_are_empty,
        "SO globals, initializer cells, and flag-one correlation are observed");
    Check(descriptor.callables.size() == 2 && descriptor.flag_zero_callable_count == 1 &&
              descriptor.flag_one_callable_count == 1 && descriptor.callable_parameter_total == 2,
        "SO callable flag branches and parameters are recovered");
    Check(descriptor.callable_ordinals_match_table_order &&
              descriptor.flag_zero_entries_strictly_increasing &&
              descriptor.flag_zero_entries_are_nonzero &&
              descriptor.flag_zero_pre_entry_words_are_addressable &&
              descriptor.flag_one_entries_are_zero,
        "SO ordinal and neutral entry observations hold for the canonical module");
    Check(descriptor.flag_zero_pre_entry_pattern_matches == 1 &&
              descriptor.flag_zero_pre_entry_pattern_expected == 1,
        "SO flag-zero pre-entry word matches the observed pattern relationship");
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

void RunSummaryBufferChecks()
{
    using Buffer = SoOwnedSummaryBuffer<SoTypeSummary>;

    Buffer empty;
    const Buffer& const_empty = empty;
    Check(empty.empty() && empty.begin() != nullptr && empty.begin() == empty.end() &&
              std::distance(empty.begin(), empty.end()) == 0 &&
              std::distance(const_empty.cbegin(), const_empty.cend()) == 0,
        "SO empty summary buffer exposes a valid same-object zero-distance range");

    Buffer source(1);
    source[0] = SoTypeSummary{.observed_flag = 1, .member_count = 7};
    Buffer moved(std::move(source));
    Check(source.empty() && source.begin() != nullptr && source.begin() == source.end() &&
              std::distance(source.begin(), source.end()) == 0 && moved.size() == 1 &&
              moved[0].observed_flag == 1 && moved[0].member_count == 7,
        "SO summary-buffer move construction preserves ownership and resets a safe source range");

    Buffer destination(2);
    destination[0] = SoTypeSummary{.observed_flag = 0, .member_count = 1};
    destination[1] = SoTypeSummary{.observed_flag = 0, .member_count = 2};
    destination = std::move(moved);
    Buffer expected(1);
    expected[0] = SoTypeSummary{.observed_flag = 1, .member_count = 7};
    Check(moved.empty() && moved.begin() != nullptr && moved.begin() == moved.end() &&
              std::distance(moved.begin(), moved.end()) == 0 && destination == expected,
        "SO summary-buffer move assignment releases old storage and preserves equality");

    destination = std::move(destination);
    Check(destination == expected && empty == Buffer{},
        "SO summary-buffer self move and empty equality remain safe");
}

void RunUnalignedAndDiagnosticChecks()
{
    const auto bytes = BuildValid();
    const auto aligned = InspectSoModule(bytes);
    std::vector<std::byte> storage;
    storage.reserve(bytes.size() + 1U);
    storage.push_back(std::byte{0xFF});
    storage.insert(storage.end(), bytes.begin(), bytes.end());
    const auto unaligned = InspectSoModule(
        std::span<const std::byte>(storage.data() + 1, bytes.size()));
    Check(aligned.has_value() && unaligned.has_value() && *unaligned == *aligned,
        "SO accepts an unaligned backing span without changing the owned descriptor");

    constexpr std::string_view kSyntheticSecret = "SECRET.SO";
    std::vector<std::byte> malformed;
    EmitU32(malformed, 8);
    EmitU32(malformed, 0); // code cells
    EmitU32(malformed, 0); // literals
    EmitU32(malformed, 1); // types
    EmitLp(malformed, "C:\\owner\\SECRET.SO");
    EmitU32(malformed, 2); // invalid observed flag; the parsed name must not enter the error
    const auto error = InspectSoModule(malformed);
    CheckError(error, DecodeErrorCode::Malformed,
        "SO malformed metadata returns the expected typed error");
    CheckPathFreeError(error, kSyntheticSecret,
        "SO errors do not echo input-derived names or path separators");
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
    // The recovered grammar uses length zero as an absent-value sentinel. Unlike a nonempty LP
    // value, it has no serialized NUL and no padding bytes.
    {
        std::vector<std::byte> out;
        EmitU32(out, 8);
        EmitU32(out, 0); // code cells
        EmitU32(out, 0); // literals
        EmitU32(out, 1); // types
        EmitLp(out, "T");
        EmitU32(out, 0); // observed flag
        EmitU32(out, 0); // absent base value
        EmitU32(out, 0); // member count
        EmitU32(out, 0); // enums
        EmitU32(out, 0); // globals reserved
        EmitU32(out, 0); // globals
        EmitU32(out, 0); // callables
        DecodeLimits limits = DefaultSoDecodeLimits();
        limits.maximum_string_bytes = 1;
        const auto result = InspectSoModule(out, limits);
        Check(result.has_value() && result->types.size() == 1,
            "SO zero-length absent-value sentinel is accepted without a NUL");
    }

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
    // type observed flag 2
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
    // enum observed flag 2
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

    for (const std::string_view section : {"member", "enum-value", "initializer", "parameter"})
        CheckError(InspectSoModule(HostileNestedAggregatePrefix(section)),
            DecodeErrorCode::LimitExceeded,
            "SO cumulative nested count is rejected before the second record body");
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
    // Non-increasing flag-zero entries are observed, not rejected.
    {
        const auto bytes = BuildValid({{.flag = 0, .ordinal = 0, .label = 5, .entry = 5},
            {.flag = 0, .ordinal = 1, .label = 6, .entry = 3}});
        const auto result = InspectSoModule(bytes);
        Check(result.has_value() && !result->flag_zero_entries_strictly_increasing &&
                  result->flag_zero_callable_count == 2,
            "SO non-increasing flag-zero entries are retained as an observation");
    }
    // A flag-zero entry whose preceding code cell lacks the pattern lowers the match count without
    // rejection.
    {
        const auto bytes = BuildValid({{.flag = 0, .ordinal = 0, .label = 5, .entry = 3}},
            /*write_patterns=*/false);
        const auto result = InspectSoModule(bytes);
        Check(result.has_value() && result->flag_zero_pre_entry_pattern_expected == 1 &&
                  result->flag_zero_pre_entry_pattern_matches == 0,
            "SO missing pre-entry word pattern is observed without rejection");
    }
    // Zero and out-of-range flag-zero entries remain grammar-valid but make the separate neutral
    // addressability observations false.
    {
        const auto bytes = BuildValid({{.flag = 0, .ordinal = 0, .label = 5, .entry = 0},
            {.flag = 0, .ordinal = 1, .label = 6, .entry = 9}});
        const auto result = InspectSoModule(bytes);
        Check(result.has_value() && !result->flag_zero_entries_are_nonzero &&
                  !result->flag_zero_pre_entry_words_are_addressable &&
                  result->flag_zero_pre_entry_pattern_expected == 2 &&
                  result->flag_zero_pre_entry_pattern_matches == 0,
            "SO unaddressable flag-zero pre-entry words are observed without rejection");
    }
    // A nonzero flag-one entry is likewise an observation, not a grammar rejection.
    {
        const auto bytes = BuildValid({{.flag = 1, .ordinal = 0, .label = 0, .entry = 2}});
        const auto result = InspectSoModule(bytes);
        Check(result.has_value() && !result->flag_one_entries_are_zero,
            "SO nonzero flag-one entry is retained as an observation");
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

    // The canonical module charges one root + 8 code cells + 1 literal + 1 type + 1 member +
    // 2 enums + 2 values + 2 globals + 2 initializer cells + 2 callables + 2 parameters = 24.
    limits = DefaultSoDecodeLimits();
    limits.maximum_items = 24;
    Check(InspectSoModule(bytes, limits).has_value(),
        "SO succeeds at the exact comprehensive item budget");
    limits.maximum_items = 23;
    CheckError(InspectSoModule(bytes, limits), DecodeErrorCode::LimitExceeded,
        "SO rejects one item below the comprehensive item budget");
    limits.maximum_items = 0;
    CheckError(InspectSoModule(bytes, limits), DecodeErrorCode::LimitExceeded,
        "SO rejects a zero item budget before section traversal");

    // maximum_string_bytes measures content only: the serialized NUL and alignment padding are not
    // charged. "lit" is the longest value in the canonical fixture.
    limits = DecodeLimits{};
    limits.maximum_string_bytes = 3;
    Check(InspectSoModule(bytes, limits).has_value(),
        "SO succeeds at the exact LP-value content-byte budget");
    limits.maximum_string_bytes = 2;
    CheckError(InspectSoModule(bytes, limits), DecodeErrorCode::LimitExceeded,
        "SO rejects one byte below the LP-value content-byte budget");
    limits.maximum_string_bytes = 0;
    CheckError(InspectSoModule(bytes, limits), DecodeErrorCode::LimitExceeded,
        "SO rejects nonempty LP-value content under a zero string budget");

    limits = DecodeLimits{};
    const std::uint64_t exact_output = sizeof(SoModuleDescriptor) +
        1U * sizeof(omega::retail::SoTypeSummary) + 2U * sizeof(omega::retail::SoEnumSummary) +
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

    auto fixed_ceiling = BuildFixedCeilingLiteralModule();
    Check(fixed_ceiling.size() == 524'288U,
        "SO fixed-ceiling fixture has the intended serialized size");
    Check(kSoMaximumLpValueContentBytes == 524'251U &&
              DefaultSoDecodeLimits().maximum_string_bytes == kSoMaximumLpValueContentBytes &&
              InspectSoModule(fixed_ceiling).has_value(),
        "SO default limits accept the format-derived maximum LP content at the input ceiling");
    limits = DefaultSoDecodeLimits();
    limits.maximum_string_bytes = kSoMaximumLpValueContentBytes - 1U;
    CheckError(InspectSoModule(fixed_ceiling, limits), DecodeErrorCode::LimitExceeded,
        "SO caller can tighten the fixed per-value content ceiling by one byte");
    limits.maximum_string_bytes = std::numeric_limits<std::uint32_t>::max();
    Check(InspectSoModule(fixed_ceiling, limits).has_value(),
        "SO clamps a wider caller string budget to the unraiseable format ceiling");
    fixed_ceiling.push_back(std::byte{0});
    CheckError(InspectSoModule(fixed_ceiling, limits), DecodeErrorCode::LimitExceeded,
        "SO rejects one byte above the fixed input ceiling before parsing");
}

void RunAllocationFailureChecks()
{
    const auto bytes = BuildValid();
    constexpr std::size_t kOwnedAllocationCount = 4;

    // Count the complete valid path first. The four exact-size summary arrays are the decoder's only
    // owned-output allocations; default construction and moves of their buffers allocate nothing.
    ArmAllocationProbe(/*failure_ordinal=*/0);
    const auto baseline = InspectSoModule(bytes);
    const AllocationProbeResult baseline_probe = DisarmAllocationProbe();
    Check(baseline.has_value() &&
              baseline_probe.allocation_count == kOwnedAllocationCount &&
              !baseline_probe.failure_triggered,
        "SO canonical decode performs exactly one owned allocation per summary array");

    for (std::size_t ordinal = 1; ordinal <= kOwnedAllocationCount; ++ordinal)
    {
        ArmAllocationProbe(ordinal);
        const auto failed = InspectSoModule(bytes);
        const AllocationProbeResult probe = DisarmAllocationProbe();
        CheckError(failed, DecodeErrorCode::LimitExceeded,
            "SO maps every owned-output allocation ordinal to a typed error");
        Check(probe.failure_triggered && probe.allocation_count == ordinal,
            "SO persistent allocation failure reaches each array with no allocation in recovery");
    }

    ArmAllocationProbe(kOwnedAllocationCount + 1U);
    const auto after_last = InspectSoModule(bytes);
    const AllocationProbeResult after_last_probe = DisarmAllocationProbe();
    Check(after_last.has_value() && !after_last_probe.failure_triggered &&
              after_last_probe.allocation_count == kOwnedAllocationCount,
        "SO has no hidden owned allocation after the four exact summary arrays");
}
} // namespace

int main()
{
    RunValidModuleChecks();
    RunOwnershipCheck();
    RunSummaryBufferChecks();
    RunUnalignedAndDiagnosticChecks();
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
