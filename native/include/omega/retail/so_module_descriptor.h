#pragma once

#include "omega/asset/decode.h"
#include "omega/retail/container_descriptors.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace omega::retail
{
// One serialized type record, reduced to structural counts only. The recovered name, base name, and
// member names are validated as ASCII during inspection but never retained.
struct SoTypeSummary
{
    std::uint32_t external_flag = 0;
    std::uint32_t member_count = 0;

    [[nodiscard]] bool operator==(const SoTypeSummary&) const = default;
};

// One serialized enum record. External (flag 1) records carry no value list in the grammar, so their
// value_count is 0.
struct SoEnumSummary
{
    std::uint32_t external_flag = 0;
    std::uint32_t value_count = 0;

    [[nodiscard]] bool operator==(const SoEnumSummary&) const = default;
};

// One serialized global record. fields[1] is the observed local/external flag, fields[2] the observed
// table ordinal, and fields[3] the initializer cell count. No field is assigned semantics.
struct SoGlobalSummary
{
    std::array<std::uint32_t, 4> fields{};
    std::uint32_t initializer_cell_count = 0;

    [[nodiscard]] bool operator==(const SoGlobalSummary&) const = default;
};

// One serialized callable record. fields[1] is the observed local/external flag, fields[2] the
// observed table ordinal, fields[3] the observed label id, and fields[4] the observed entry cell.
// fields[0] and fields[5..9] are retained verbatim without interpretation.
struct SoCallableSummary
{
    std::array<std::uint32_t, 10> fields{};
    std::uint32_t parameter_count = 0;

    [[nodiscard]] bool operator==(const SoCallableSummary&) const = default;
};

// Bounded, owned, passive structural summary of an Omega Strain .SO VM module. It retains section
// ranges, counts, per-record flags/ordinals/label-ids/entry-cells, and the observed pre-entry
// 0x003B/label marker relationship as counts. It NEVER retains code cell values, string contents, or
// payload bytes, and assigns NO opcode, stack, type, event, or gameplay semantics.
struct SoModuleDescriptor
{
    ObservedByteRange code_region;
    std::uint32_t code_cell_count = 0;
    ObservedByteRange literals_region;
    std::uint32_t literal_count = 0;
    ObservedByteRange types_region;
    ObservedByteRange enums_region;
    ObservedByteRange globals_region;
    ObservedByteRange callables_region;

    std::uint32_t globals_reserved = 0;

    std::vector<SoTypeSummary> types;
    std::uint32_t type_member_total = 0;
    std::vector<SoEnumSummary> enums;
    std::uint32_t local_enum_value_total = 0;
    std::vector<SoGlobalSummary> globals;
    std::uint32_t global_initializer_cell_total = 0;
    std::vector<SoCallableSummary> callables;
    std::uint32_t callable_parameter_total = 0;

    // Structural regularities observed but not enforced by the grammar; retained as data, never used
    // to reject a well-framed module. None of these is a semantic claim.
    std::uint32_t local_callable_count = 0;
    std::uint32_t external_callable_count = 0;
    std::uint32_t local_entry_marker_matches = 0;
    std::uint32_t local_entry_marker_expected = 0;
    bool code_offset_is_8 = false;
    bool global_ordinals_match_table_order = false;
    bool callable_ordinals_match_table_order = false;
    bool local_entries_strictly_increasing = false;

    ObservedExtent module_extent;

    [[nodiscard]] bool operator==(const SoModuleDescriptor& other) const
    {
        return code_region == other.code_region && code_cell_count == other.code_cell_count &&
               literals_region == other.literals_region && literal_count == other.literal_count &&
               types_region == other.types_region && enums_region == other.enums_region &&
               globals_region == other.globals_region && callables_region == other.callables_region &&
               globals_reserved == other.globals_reserved && types == other.types &&
               type_member_total == other.type_member_total && enums == other.enums &&
               local_enum_value_total == other.local_enum_value_total && globals == other.globals &&
               global_initializer_cell_total == other.global_initializer_cell_total &&
               callables == other.callables &&
               callable_parameter_total == other.callable_parameter_total &&
               local_callable_count == other.local_callable_count &&
               external_callable_count == other.external_callable_count &&
               local_entry_marker_matches == other.local_entry_marker_matches &&
               local_entry_marker_expected == other.local_entry_marker_expected &&
               code_offset_is_8 == other.code_offset_is_8 &&
               global_ordinals_match_table_order == other.global_ordinals_match_table_order &&
               callable_ordinals_match_table_order == other.callable_ordinals_match_table_order &&
               local_entries_strictly_increasing == other.local_entries_strictly_increasing &&
               module_extent.observed_bytes == other.module_extent.observed_bytes &&
               module_extent.input_bytes == other.module_extent.input_bytes &&
               module_extent.relation == other.module_extent.relation;
    }
};

// [any worker thread; reentrant] Passive, analysis-only structural inspection of an Omega Strain .SO
// VM module. It validates the recovered little-endian serialization grammar through exact EOF - code
// header/array, length-prefixed NUL-terminated 4-byte-padded strings, types/members, enums/values,
// globals, callables, ordinals, and entry bounds - using checked arithmetic, fixed hard ceilings, and
// caller-tightenable DecodeLimits. It is NOT a runtime, script engine, interpreter, recompiler, or
// dispatcher, and it never interprets, executes, translates, or emulates a single code cell.
[[nodiscard]] asset::DecodeResult<SoModuleDescriptor> InspectSoModule(
    std::span<const std::byte> bytes, asset::DecodeLimits limits = {});
} // namespace omega::retail
