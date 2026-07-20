#include "omega/retail/so_module_descriptor.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <optional>
#include <span>
#include <stdexcept>
#include <string_view>

namespace omega::retail
{
namespace
{
using asset::DecodeErrorCode;
using asset::DecodeLimits;

constexpr std::uint64_t kWordBytes = 4;
constexpr std::uint64_t kCodeHeaderOffset = 8;
constexpr std::uint32_t kObservedPatternLow16 = 0x003BU;

// Fixed, unraiseable per-module ceilings. Each is the smallest power of two strictly greater than
// the maximum observed in any one of the 118 tracked NTSC-U modules. Nested quantities are aggregate
// module totals, not per-record allowances. DecodeLimits can only tighten these ceilings.
constexpr std::uint64_t kMaxCodeCells = 1ULL << 16;           // observed maximum 37'314
constexpr std::uint64_t kMaxLiterals = 1ULL << 10;            // observed maximum 643
constexpr std::uint64_t kMaxTypes = 1ULL << 7;                // observed maximum 80
constexpr std::uint64_t kMaxTypeMembers = 1ULL << 6;          // observed aggregate maximum 55
constexpr std::uint64_t kMaxEnums = 1ULL << 6;                // observed maximum 40
constexpr std::uint64_t kMaxEnumValues = 1ULL << 4;           // observed aggregate maximum 11
constexpr std::uint64_t kMaxGlobals = 1ULL << 10;             // observed maximum 547
constexpr std::uint64_t kMaxGlobalInitCells = 1ULL << 13;     // observed aggregate maximum 5'706
constexpr std::uint64_t kMaxCallables = 1ULL << 11;           // observed maximum 1'043
constexpr std::uint64_t kMaxCallableParameters = 1ULL << 11;  // observed aggregate maximum 1'832
constexpr std::size_t kGlobalFieldCount = 4;
constexpr std::size_t kCallableFieldCount = 10;

struct SoParseError
{
    SoDecodeError error;
};

[[nodiscard]] constexpr SoDecodeError MakeError(const DecodeErrorCode code,
    const std::string_view static_message,
    const std::optional<std::uint64_t> byte_offset = std::nullopt) noexcept
{
    return SoDecodeError{
        .code = code, .byte_offset = byte_offset, .static_message = static_message};
}

[[noreturn]] void Fail(const DecodeErrorCode code, const std::string_view static_message,
    const std::optional<std::uint64_t> byte_offset = std::nullopt)
{
    throw SoParseError{MakeError(code, static_message, byte_offset)};
}

[[nodiscard]] bool Add(
    const std::uint64_t left, const std::uint64_t right, std::uint64_t& out) noexcept
{
    if (right > std::numeric_limits<std::uint64_t>::max() - left)
        return false;
    out = left + right;
    return true;
}

[[nodiscard]] bool Multiply(
    const std::uint64_t left, const std::uint64_t right, std::uint64_t& out) noexcept
{
    if (left != 0 && right > std::numeric_limits<std::uint64_t>::max() / left)
        return false;
    out = left * right;
    return true;
}

class Reader
{
  public:
    explicit Reader(const std::span<const std::byte> data,
        const std::uint32_t maximum_string_bytes) noexcept
        : data_(data), maximum_string_bytes_(maximum_string_bytes)
    {
    }

    [[nodiscard]] std::uint64_t offset() const noexcept { return offset_; }
    [[nodiscard]] std::uint64_t size() const noexcept { return data_.size(); }

    std::uint32_t U32()
    {
        std::uint64_t end = 0;
        if (!Add(offset_, kWordBytes, end) || end > data_.size())
            Fail(DecodeErrorCode::Truncated, "u32 reaches end of module", offset_);
        const std::uint32_t value = ReadU32At(offset_);
        offset_ = end;
        return value;
    }

    // Advance past `count` 32-bit cells whose values are not retained (code array, initializer cells,
    // parameter type cells). Fully bounds- and overflow-checked.
    void SkipCells(const std::uint64_t count)
    {
        std::uint64_t bytes = 0;
        std::uint64_t end = 0;
        if (!Multiply(count, kWordBytes, bytes) || !Add(offset_, bytes, end) || end > data_.size())
            Fail(DecodeErrorCode::Truncated, "cell array reaches end of module", offset_);
        offset_ = end;
    }

    // Read one code cell value by index within the already-validated code region. The caller
    // guarantees index < code_cell_count, so the read is in bounds.
    [[nodiscard]] std::uint32_t CodeCellAt(
        const std::uint64_t code_region_offset, const std::uint64_t index) const noexcept
    {
        return ReadU32At(code_region_offset + index * kWordBytes);
    }

    // Validate a 32-bit-length-prefixed, 4-byte zero-padded value and advance past it. Length zero
    // is the recovered absent-value sentinel; every nonzero length includes a trailing NUL. When
    // `ascii` is set the content (excluding the terminator) must be printable ASCII. The content
    // bytes are validated but never retained.
    void ValidateLpValue(const bool ascii)
    {
        const std::uint64_t length_offset = offset_;
        const std::uint32_t length = U32();
        if (length > data_.size() - offset_)
            Fail(DecodeErrorCode::Truncated, "length-prefixed value reaches end of module",
                length_offset);
        const std::uint64_t content_end = offset_ + length;
        if (length != 0)
        {
            if (data_[static_cast<std::size_t>(content_end - 1)] != std::byte{0})
                Fail(DecodeErrorCode::Malformed, "length-prefixed value lacks a NUL terminator",
                    length_offset);
            const std::uint64_t content_bytes = static_cast<std::uint64_t>(length) - 1U;
            if (content_bytes > maximum_string_bytes_)
                Fail(DecodeErrorCode::LimitExceeded,
                    "length-prefixed value content exceeds the decoder string limit",
                    length_offset);
            if (ascii)
            {
                for (std::uint64_t index = offset_; index + 1 < content_end; ++index)
                {
                    const auto value =
                        std::to_integer<unsigned>(data_[static_cast<std::size_t>(index)]);
                    if (value < 0x20U || value > 0x7EU)
                        Fail(DecodeErrorCode::Malformed, "metadata name is not printable ASCII",
                            index);
                }
            }
        }
        offset_ = content_end;
        std::uint64_t padded = 0;
        if (!Add(offset_, 3U, padded))
            Fail(DecodeErrorCode::Overflow, "length-prefixed padding overflows", length_offset);
        padded &= ~static_cast<std::uint64_t>(3U);
        if (padded > data_.size())
            Fail(DecodeErrorCode::Truncated, "length-prefixed padding reaches end of module",
                length_offset);
        for (std::uint64_t index = offset_; index < padded; ++index)
        {
            if (data_[static_cast<std::size_t>(index)] != std::byte{0})
                Fail(DecodeErrorCode::Malformed, "length-prefixed value has nonzero padding", index);
        }
        offset_ = padded;
    }

  private:
    [[nodiscard]] std::uint32_t ReadU32At(const std::uint64_t at) const noexcept
    {
        const auto index = static_cast<std::size_t>(at);
        return std::to_integer<std::uint32_t>(data_[index]) |
               (std::to_integer<std::uint32_t>(data_[index + 1]) << 8U) |
               (std::to_integer<std::uint32_t>(data_[index + 2]) << 16U) |
               (std::to_integer<std::uint32_t>(data_[index + 3]) << 24U);
    }

    std::span<const std::byte> data_;
    std::uint32_t maximum_string_bytes_ = 0;
    std::uint64_t offset_ = 0;
};

[[nodiscard]] ObservedByteRange Range(const std::uint64_t start, const std::uint64_t end) noexcept
{
    return ObservedByteRange{.offset = start, .size = end - start};
}

[[nodiscard]] std::uint32_t AddBoundedU32Total(const std::uint32_t running,
    const std::uint32_t added, const std::uint64_t hard_ceiling,
    const std::string_view overflow_message, const std::string_view limit_message,
    const std::optional<std::uint64_t> byte_offset)
{
    std::uint64_t next = 0;
    if (!Add(running, added, next) || next > std::numeric_limits<std::uint32_t>::max())
        Fail(DecodeErrorCode::Overflow, overflow_message, byte_offset);
    if (next > hard_ceiling)
        Fail(DecodeErrorCode::LimitExceeded, limit_message, byte_offset);
    return static_cast<std::uint32_t>(next);
}

SoModuleDescriptor ParseModule(const std::span<const std::byte> bytes, const DecodeLimits limits)
{
    const std::uint32_t maximum_string_bytes =
        limits.maximum_string_bytes < kSoMaximumLpValueContentBytes
        ? limits.maximum_string_bytes
        : kSoMaximumLpValueContentBytes;
    Reader reader(bytes, maximum_string_bytes);
    std::uint64_t projected_items = 1;
    std::uint64_t projected_output = sizeof(SoModuleDescriptor);
    if (projected_items > limits.maximum_items)
        Fail(DecodeErrorCode::LimitExceeded, "SO descriptor exceeds the item limit");
    SoModuleDescriptor descriptor;
    const auto AccountItems = [&](const std::uint64_t added) {
        if (!Add(projected_items, added, projected_items) ||
            projected_items > limits.maximum_items)
            Fail(DecodeErrorCode::LimitExceeded, "SO decoded item count exceeds the item limit");
    };
    const auto AccountOutput = [&](const std::uint64_t element_size, const std::uint64_t count) {
        std::uint64_t added = 0;
        if (!Multiply(element_size, count, added) ||
            !Add(projected_output, added, projected_output) ||
            projected_output > limits.maximum_output_bytes)
            Fail(DecodeErrorCode::LimitExceeded, "SO owned output exceeds the output-byte limit");
    };

    // Header + code array. The recovered grammar requires the code array to begin immediately after
    // the two-word header (offset 8).
    const std::uint32_t code_offset = reader.U32();
    const std::uint32_t code_cell_count = reader.U32();
    if (code_offset != kCodeHeaderOffset)
        Fail(DecodeErrorCode::Malformed, "SO code offset does not follow the two-word header", 0);
    if (code_cell_count > kMaxCodeCells)
        Fail(DecodeErrorCode::LimitExceeded, "SO code cell count exceeds the fixed ceiling", 4);
    AccountItems(code_cell_count);
    const std::uint64_t code_start = reader.offset();
    reader.SkipCells(code_cell_count);
    descriptor.code_region = Range(code_start, reader.offset());
    descriptor.code_cell_count = code_cell_count;

    // Literals (length-prefixed byte strings, content not retained).
    const std::uint64_t literals_start = reader.offset();
    const std::uint32_t literal_count = reader.U32();
    if (literal_count > kMaxLiterals)
        Fail(DecodeErrorCode::LimitExceeded, "SO literal count exceeds the fixed ceiling",
            literals_start);
    AccountItems(literal_count);
    for (std::uint32_t index = 0; index < literal_count; ++index)
        reader.ValidateLpValue(/*ascii=*/false);
    descriptor.literals_region = Range(literals_start, reader.offset());
    descriptor.literal_count = literal_count;

    // Types.
    const std::uint64_t types_start = reader.offset();
    const std::uint32_t type_count = reader.U32();
    if (type_count > kMaxTypes)
        Fail(DecodeErrorCode::LimitExceeded, "SO type count exceeds the fixed ceiling", types_start);
    AccountItems(type_count);
    AccountOutput(sizeof(SoTypeSummary), type_count);
    descriptor.types = SoOwnedSummaryBuffer<SoTypeSummary>(type_count);
    for (std::uint32_t index = 0; index < type_count; ++index)
    {
        reader.ValidateLpValue(true);
        const std::uint64_t flag_offset = reader.offset();
        const std::uint32_t observed_flag = reader.U32();
        if (observed_flag > 1)
            Fail(DecodeErrorCode::Malformed, "SO type flag is not 0 or 1", flag_offset);
        reader.ValidateLpValue(true);
        const std::uint64_t member_count_offset = reader.offset();
        const std::uint32_t member_count = reader.U32();
        descriptor.type_member_total = AddBoundedU32Total(descriptor.type_member_total,
            member_count, kMaxTypeMembers, "SO type member total overflows",
            "SO aggregate type member count exceeds the fixed ceiling", member_count_offset);
        AccountItems(member_count);
        for (std::uint32_t member = 0; member < member_count; ++member)
        {
            reader.ValidateLpValue(true);
            (void)reader.U32();
        }
        descriptor.types[index] =
            SoTypeSummary{.observed_flag = observed_flag, .member_count = member_count};
    }
    descriptor.types_region = Range(types_start, reader.offset());

    // Enums (flag-one records carry no value list in the recovered grammar).
    const std::uint64_t enums_start = reader.offset();
    const std::uint32_t enum_count = reader.U32();
    if (enum_count > kMaxEnums)
        Fail(DecodeErrorCode::LimitExceeded, "SO enum count exceeds the fixed ceiling", enums_start);
    AccountItems(enum_count);
    AccountOutput(sizeof(SoEnumSummary), enum_count);
    descriptor.enums = SoOwnedSummaryBuffer<SoEnumSummary>(enum_count);
    for (std::uint32_t index = 0; index < enum_count; ++index)
    {
        reader.ValidateLpValue(true);
        const std::uint64_t flag_offset = reader.offset();
        const std::uint32_t observed_flag = reader.U32();
        if (observed_flag > 1)
            Fail(DecodeErrorCode::Malformed, "SO enum flag is not 0 or 1", flag_offset);
        std::uint32_t value_count = 0;
        if (observed_flag == 0)
        {
            const std::uint64_t value_count_offset = reader.offset();
            value_count = reader.U32();
            descriptor.flag_zero_enum_value_total = AddBoundedU32Total(
                descriptor.flag_zero_enum_value_total, value_count, kMaxEnumValues,
                "SO enum value total overflows",
                "SO aggregate enum value count exceeds the fixed ceiling", value_count_offset);
            AccountItems(value_count);
            for (std::uint32_t value = 0; value < value_count; ++value)
            {
                reader.ValidateLpValue(true);
                (void)reader.U32();
            }
        }
        descriptor.enums[index] =
            SoEnumSummary{.observed_flag = observed_flag, .value_count = value_count};
    }
    descriptor.enums_region = Range(enums_start, reader.offset());

    // Globals.
    const std::uint64_t globals_start = reader.offset();
    descriptor.globals_reserved = reader.U32();
    const std::uint32_t global_count = reader.U32();
    if (global_count > kMaxGlobals)
        Fail(DecodeErrorCode::LimitExceeded, "SO global count exceeds the fixed ceiling",
            globals_start);
    AccountItems(global_count);
    AccountOutput(sizeof(SoGlobalSummary), global_count);
    descriptor.globals = SoOwnedSummaryBuffer<SoGlobalSummary>(global_count);
    bool global_ordinals_match = true;
    bool flag_one_initializers_empty = true;
    for (std::uint32_t index = 0; index < global_count; ++index)
    {
        reader.ValidateLpValue(true);
        std::array<std::uint32_t, kGlobalFieldCount> fields{};
        for (std::size_t field = 0; field < kGlobalFieldCount; ++field)
            fields[field] = reader.U32();
        if (fields[1] > 1)
            Fail(DecodeErrorCode::Malformed, "SO global flag is not 0 or 1",
                reader.offset() - 3U * kWordBytes);
        const std::uint32_t initializer_cell_count = fields[3];
        descriptor.global_initializer_cell_total = AddBoundedU32Total(
            descriptor.global_initializer_cell_total, initializer_cell_count,
            kMaxGlobalInitCells, "SO global initializer cell total overflows",
            "SO aggregate global initializer cell count exceeds the fixed ceiling",
            reader.offset() - kWordBytes);
        AccountItems(initializer_cell_count);
        if (fields[1] == 1 && initializer_cell_count != 0)
            flag_one_initializers_empty = false;
        reader.SkipCells(initializer_cell_count);
        if (fields[2] != index)
            global_ordinals_match = false;
        descriptor.globals[index] =
            SoGlobalSummary{.fields = fields, .initializer_cell_count = initializer_cell_count};
    }
    descriptor.globals_region = Range(globals_start, reader.offset());
    descriptor.global_ordinals_match_table_order = global_ordinals_match;
    descriptor.flag_one_global_initializers_are_empty = flag_one_initializers_empty;

    // Callables.
    const std::uint64_t callables_start = reader.offset();
    const std::uint32_t callable_count = reader.U32();
    if (callable_count > kMaxCallables)
        Fail(DecodeErrorCode::LimitExceeded, "SO callable count exceeds the fixed ceiling",
            callables_start);
    AccountItems(callable_count);
    AccountOutput(sizeof(SoCallableSummary), callable_count);
    descriptor.callables = SoOwnedSummaryBuffer<SoCallableSummary>(callable_count);
    bool callable_ordinals_match = true;
    bool flag_zero_entries_increasing = true;
    bool flag_zero_entries_nonzero = true;
    bool flag_zero_pre_entry_words_addressable = true;
    bool flag_one_entries_zero = true;
    bool seen_flag_zero_entry = false;
    std::uint32_t previous_flag_zero_entry = 0;
    std::uint32_t flag_zero_callables = 0;
    std::uint32_t flag_one_callables = 0;
    std::uint32_t pre_entry_pattern_matches = 0;
    for (std::uint32_t index = 0; index < callable_count; ++index)
    {
        reader.ValidateLpValue(true);
        std::array<std::uint32_t, kCallableFieldCount> fields{};
        for (std::size_t field = 0; field < kCallableFieldCount; ++field)
            fields[field] = reader.U32();
        if (fields[1] > 1)
            Fail(DecodeErrorCode::Malformed, "SO callable flag is not 0 or 1",
                reader.offset() - 9U * kWordBytes);
        const std::uint64_t parameter_count_offset = reader.offset();
        const std::uint32_t parameter_count = reader.U32();
        descriptor.callable_parameter_total = AddBoundedU32Total(
            descriptor.callable_parameter_total, parameter_count, kMaxCallableParameters,
            "SO callable parameter total overflows",
            "SO aggregate callable parameter count exceeds the fixed ceiling",
            parameter_count_offset);
        AccountItems(parameter_count);
        reader.SkipCells(parameter_count);
        if (fields[2] != index)
            callable_ordinals_match = false;
        if (fields[1] == 0)
        {
            ++flag_zero_callables;
            const std::uint32_t entry = fields[4];
            const std::uint32_t label_candidate = fields[3];
            if (seen_flag_zero_entry && entry <= previous_flag_zero_entry)
                flag_zero_entries_increasing = false;
            seen_flag_zero_entry = true;
            previous_flag_zero_entry = entry;
            if (entry == 0)
                flag_zero_entries_nonzero = false;
            const bool pre_entry_word_addressable = entry > 0 && entry <= code_cell_count;
            if (!pre_entry_word_addressable)
                flag_zero_pre_entry_words_addressable = false;
            if (pre_entry_word_addressable)
            {
                const std::uint32_t pre_entry_word =
                    reader.CodeCellAt(descriptor.code_region.offset, entry - 1U);
                if ((pre_entry_word & 0xFFFFU) == kObservedPatternLow16 &&
                    (pre_entry_word >> 16U) == label_candidate)
                    ++pre_entry_pattern_matches;
            }
        }
        else
        {
            ++flag_one_callables;
            if (fields[4] != 0)
                flag_one_entries_zero = false;
        }
        descriptor.callables[index] =
            SoCallableSummary{.fields = fields, .parameter_count = parameter_count};
    }
    descriptor.callables_region = Range(callables_start, reader.offset());
    descriptor.callable_ordinals_match_table_order = callable_ordinals_match;
    descriptor.flag_zero_entries_strictly_increasing = flag_zero_entries_increasing;
    descriptor.flag_zero_entries_are_nonzero = flag_zero_entries_nonzero;
    descriptor.flag_zero_pre_entry_words_are_addressable =
        flag_zero_pre_entry_words_addressable;
    descriptor.flag_one_entries_are_zero = flag_one_entries_zero;
    descriptor.flag_zero_callable_count = flag_zero_callables;
    descriptor.flag_one_callable_count = flag_one_callables;
    descriptor.flag_zero_pre_entry_pattern_matches = pre_entry_pattern_matches;
    descriptor.flag_zero_pre_entry_pattern_expected = flag_zero_callables;

    if (reader.offset() != reader.size())
        Fail(DecodeErrorCode::Malformed, "SO module has trailing bytes after the callable table",
            reader.offset());

    descriptor.module_extent = ObservedExtent{
        .observed_bytes = reader.size(),
        .input_bytes = reader.size(),
        .relation = ObservedExtentRelation::Exact,
    };
    return descriptor;
}
} // namespace

SoDecodeResult<SoModuleDescriptor> InspectSoModule(
    const std::span<const std::byte> bytes, const DecodeLimits limits)
{
    try
    {
        if (bytes.size() > kSoMaximumModuleBytes)
            return std::unexpected(MakeError(
                DecodeErrorCode::LimitExceeded, "SO input exceeds the fixed module byte ceiling"));
        if (bytes.size() > limits.maximum_input_bytes)
            return std::unexpected(MakeError(
                DecodeErrorCode::LimitExceeded, "SO input exceeds the decoder byte limit"));
        if (limits.maximum_output_bytes < sizeof(SoModuleDescriptor))
            return std::unexpected(MakeError(
                DecodeErrorCode::LimitExceeded, "SO descriptor exceeds the decoder output limit"));
        return ParseModule(bytes, limits);
    }
    catch (const SoParseError& failure)
    {
        return std::unexpected(failure.error);
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected(MakeError(
            DecodeErrorCode::LimitExceeded, "SO owned-summary allocation failed"));
    }
    catch (const std::length_error&)
    {
        return std::unexpected(MakeError(
            DecodeErrorCode::LimitExceeded, "SO owned-summary length is invalid"));
    }
}
} // namespace omega::retail
