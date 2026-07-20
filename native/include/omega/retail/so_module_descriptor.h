#pragma once

#include "omega/asset/decode.h"
#include "omega/retail/container_descriptors.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

namespace omega::retail
{
// Move-only exact-size storage for retained SO record summaries. Default construction and moves do
// not allocate. The explicit size constructor performs the buffer's sole allocation, allowing the
// inspector to account for and catch every owned-output allocation.
template <typename Summary>
class SoOwnedSummaryBuffer
{
    static_assert(std::is_nothrow_default_constructible_v<Summary>,
        "SO summary storage requires a non-throwing sentinel constructor");
    static_assert(std::is_nothrow_destructible_v<Summary>,
        "SO summary storage requires non-throwing destruction");

  public:
    SoOwnedSummaryBuffer() noexcept = default;

    explicit SoOwnedSummaryBuffer(const std::size_t size)
        : data_(size == 0 ? std::unique_ptr<Summary[]>{} : std::make_unique<Summary[]>(size)),
          size_(size)
    {
    }

    SoOwnedSummaryBuffer(const SoOwnedSummaryBuffer&) = delete;
    SoOwnedSummaryBuffer& operator=(const SoOwnedSummaryBuffer&) = delete;

    SoOwnedSummaryBuffer(SoOwnedSummaryBuffer&& other) noexcept
        : data_(std::move(other.data_)), size_(std::exchange(other.size_, 0))
    {
    }

    SoOwnedSummaryBuffer& operator=(SoOwnedSummaryBuffer&& other) noexcept
    {
        if (this != &other)
        {
            data_ = std::move(other.data_);
            size_ = std::exchange(other.size_, 0);
        }
        return *this;
    }

    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }

    [[nodiscard]] Summary& operator[](const std::size_t index) noexcept { return data_[index]; }
    [[nodiscard]] const Summary& operator[](const std::size_t index) const noexcept
    {
        return data_[index];
    }

    [[nodiscard]] Summary* begin() noexcept { return IteratorBase(); }
    [[nodiscard]] const Summary* begin() const noexcept { return IteratorBase(); }
    [[nodiscard]] const Summary* cbegin() const noexcept { return IteratorBase(); }
    [[nodiscard]] Summary* end() noexcept
    {
        return IteratorBase() + size_;
    }
    [[nodiscard]] const Summary* end() const noexcept
    {
        return IteratorBase() + size_;
    }
    [[nodiscard]] const Summary* cend() const noexcept
    {
        return IteratorBase() + size_;
    }

    [[nodiscard]] bool operator==(const SoOwnedSummaryBuffer& other) const
    {
        if (size_ != other.size_)
            return false;
        for (std::size_t index = 0; index < size_; ++index)
        {
            if (!(data_[index] == other.data_[index]))
                return false;
        }
        return true;
    }

  private:
    // A valid buffer can be empty initially or after a move. Returning a pointer to this per-object
    // sentinel keeps begin/end in one actual object, so pointer distance and random-access range
    // operations remain defined without allocating storage for an empty buffer.
    [[nodiscard]] Summary* IteratorBase() noexcept
    {
        return size_ == 0 ? std::addressof(empty_sentinel_) : data_.get();
    }
    [[nodiscard]] const Summary* IteratorBase() const noexcept
    {
        return size_ == 0 ? std::addressof(empty_sentinel_) : data_.get();
    }

    std::unique_ptr<Summary[]> data_;
    std::size_t size_ = 0;
    Summary empty_sentinel_{};
};

// Project-owned synthetic decoder safety ceiling. It is neither an owner-corpus maximum nor a
// limit encoded by the wire format.
inline constexpr std::uint64_t kSoMaximumModuleBytes = 1ULL << 19U;
// A complete module containing one literal still needs 37 bytes for the two-word code header, the
// literal/table counts, its length word and NUL, and the remaining empty table headers. The aligned
// exact-ceiling fixture therefore proves this is the largest LP-value content admitted by the
// synthetic kSoMaximumModuleBytes policy, not by the wire format's u32 length field.
inline constexpr std::uint32_t kSoMaximumLpValueContentBytes =
    static_cast<std::uint32_t>(kSoMaximumModuleBytes - 37U);

// Preserve the shared defaults but widen this decoder's default per-value budget to its fixed,
// policy-derived ceiling. Explicit caller limits may tighten it; InspectSoModule always clamps a
// larger caller value back to kSoMaximumLpValueContentBytes.
[[nodiscard]] constexpr asset::DecodeLimits DefaultSoDecodeLimits() noexcept
{
    auto limits = asset::DecodeLimits{};
    limits.maximum_string_bytes = kSoMaximumLpValueContentBytes;
    return limits;
}

// SO parsing uses static diagnostics so allocation failure while creating an owned summary can
// still return a typed error without constructing the shared DecodeError's owning std::string.
struct SoDecodeError
{
    asset::DecodeErrorCode code = asset::DecodeErrorCode::Malformed;
    std::optional<std::uint64_t> byte_offset;
    std::string_view static_message;
};

template <typename Value>
using SoDecodeResult = std::expected<Value, SoDecodeError>;

// One serialized type record, reduced to structural counts only. The recovered name, base name, and
// member names are validated as ASCII during inspection but never retained. observed_flag preserves
// the serialized zero-or-one field without assigning import, export, or ownership semantics.
struct SoTypeSummary
{
    std::uint32_t observed_flag = 0;
    std::uint32_t member_count = 0;

    [[nodiscard]] bool operator==(const SoTypeSummary&) const = default;
};

// One serialized enum record. Flag-one records carry no value list in the recovered grammar, so
// their value_count is 0. The flag's runtime meaning remains unassigned.
struct SoEnumSummary
{
    std::uint32_t observed_flag = 0;
    std::uint32_t value_count = 0;

    [[nodiscard]] bool operator==(const SoEnumSummary&) const = default;
};

// One serialized global record. fields[1] is the observed zero-or-one flag, fields[2] the observed
// table ordinal, and fields[3] the initializer cell count. No runtime meaning is assigned.
struct SoGlobalSummary
{
    std::array<std::uint32_t, 4> fields{};
    std::uint32_t initializer_cell_count = 0;

    [[nodiscard]] bool operator==(const SoGlobalSummary&) const = default;
};

// One serialized callable record. fields[1] is the observed zero-or-one flag, fields[2] the observed
// table ordinal, fields[3] the observed label-id candidate, and fields[4] the observed entry-cell
// candidate. fields[0] and fields[5..9] are retained verbatim without interpretation.
struct SoCallableSummary
{
    std::array<std::uint32_t, 10> fields{};
    std::uint32_t parameter_count = 0;

    [[nodiscard]] bool operator==(const SoCallableSummary&) const = default;
};

// Bounded, owned, passive structural summary of an Omega Strain .SO VM module. It retains section
// ranges, counts, per-record flags/ordinals/label and entry candidates, and the observed pre-entry
// high/low word-pattern relationship as counts. It NEVER retains code cell values, string contents,
// or payload bytes, and assigns NO opcode, stack, type, event, or gameplay semantics.
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

    SoOwnedSummaryBuffer<SoTypeSummary> types;
    std::uint32_t type_member_total = 0;
    SoOwnedSummaryBuffer<SoEnumSummary> enums;
    std::uint32_t flag_zero_enum_value_total = 0;
    SoOwnedSummaryBuffer<SoGlobalSummary> globals;
    std::uint32_t global_initializer_cell_total = 0;
    SoOwnedSummaryBuffer<SoCallableSummary> callables;
    std::uint32_t callable_parameter_total = 0;

    // Structural regularities observed but not enforced by the grammar. "Pre-entry word addressable"
    // means only that entry > 0 and entry <= code_cell_count, making entry - 1 a readable code-cell
    // index; it does not assert that entry itself identifies executable code. A pattern match only
    // compares the documented high/low 16-bit shape and assigns no opcode or dispatch meaning.
    std::uint32_t flag_zero_callable_count = 0;
    std::uint32_t flag_one_callable_count = 0;
    std::uint32_t flag_zero_pre_entry_pattern_matches = 0;
    std::uint32_t flag_zero_pre_entry_pattern_expected = 0;
    bool global_ordinals_match_table_order = false;
    bool callable_ordinals_match_table_order = false;
    bool flag_zero_entries_strictly_increasing = false;
    bool flag_zero_entries_are_nonzero = false;
    bool flag_zero_pre_entry_words_are_addressable = false;
    bool flag_one_entries_are_zero = false;
    bool flag_one_global_initializers_are_empty = false;

    ObservedExtent module_extent;

    [[nodiscard]] bool operator==(const SoModuleDescriptor& other) const
    {
        return code_region == other.code_region && code_cell_count == other.code_cell_count &&
               literals_region == other.literals_region && literal_count == other.literal_count &&
               types_region == other.types_region && enums_region == other.enums_region &&
               globals_region == other.globals_region && callables_region == other.callables_region &&
               globals_reserved == other.globals_reserved && types == other.types &&
               type_member_total == other.type_member_total && enums == other.enums &&
               flag_zero_enum_value_total == other.flag_zero_enum_value_total &&
               globals == other.globals &&
               global_initializer_cell_total == other.global_initializer_cell_total &&
               callables == other.callables &&
               callable_parameter_total == other.callable_parameter_total &&
               flag_zero_callable_count == other.flag_zero_callable_count &&
               flag_one_callable_count == other.flag_one_callable_count &&
               flag_zero_pre_entry_pattern_matches ==
                   other.flag_zero_pre_entry_pattern_matches &&
               flag_zero_pre_entry_pattern_expected ==
                   other.flag_zero_pre_entry_pattern_expected &&
               global_ordinals_match_table_order == other.global_ordinals_match_table_order &&
               callable_ordinals_match_table_order == other.callable_ordinals_match_table_order &&
               flag_zero_entries_strictly_increasing ==
                   other.flag_zero_entries_strictly_increasing &&
               flag_zero_entries_are_nonzero == other.flag_zero_entries_are_nonzero &&
               flag_zero_pre_entry_words_are_addressable ==
                   other.flag_zero_pre_entry_words_are_addressable &&
               flag_one_entries_are_zero == other.flag_one_entries_are_zero &&
               flag_one_global_initializers_are_empty ==
                   other.flag_one_global_initializers_are_empty &&
               module_extent.observed_bytes == other.module_extent.observed_bytes &&
               module_extent.input_bytes == other.module_extent.input_bytes &&
               module_extent.relation == other.module_extent.relation;
    }
};

// [any worker thread; reentrant] Passive, analysis-only structural inspection of an Omega Strain .SO
// VM module. It validates the recovered little-endian serialization grammar through exact EOF: the
// enforced code offset 8 and code array; length-prefixed, 4-byte-padded values that are either a
// zero-length absent-value sentinel or NUL-terminated content; types/members; enums/values; globals;
// and callables. Ordinal, entry, flag-correlation, and pre-entry word-pattern regularities are
// reported as observations and do not reject a well-framed module. Length limits count content
// bytes only, excluding the serialized NUL and alignment padding. maximum_items charges the module
// root, every code cell and literal, every top-level type/enum/global/callable record, and every
// nested member/value/initializer/parameter cell. The flat inspector uses no dynamic scratch and no
// nesting edges. Fixed decoder safety ceilings cannot be raised; caller limits can only tighten
// them. This is NOT a runtime, interpreter, recompiler, or dispatcher and never executes,
// translates, emulates, or assigns opcode meaning to a code cell.
[[nodiscard]] SoDecodeResult<SoModuleDescriptor> InspectSoModule(std::span<const std::byte> bytes,
    asset::DecodeLimits limits = DefaultSoDecodeLimits());
} // namespace omega::retail
