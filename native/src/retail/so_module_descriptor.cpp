#include "omega/retail/so_module_descriptor.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace omega::retail
{
namespace
{
using asset::DecodeError;
using asset::DecodeErrorCode;
using asset::DecodeLimits;

constexpr std::uint64_t kWordBytes = 4;
constexpr std::uint64_t kCodeHeaderOffset = 8;
constexpr std::uint32_t kFunctionMarkerLow16 = 0x003BU;

// Fixed unraiseable per-collection hard ceilings. Each is the smallest power of two strictly greater
// than the whole-corpus total observed in analysis/formats/so-validation.json for that quantity; a
// single module cannot legitimately exceed the corpus total. Callers may only tighten effective
// limits through DecodeLimits (aggregate item and output budgets); they can never raise these.
constexpr std::uint64_t kMaxCodeCells = 1ULL << 19;          // corpus total 402'694
constexpr std::uint64_t kMaxLiterals = 1ULL << 13;           // corpus total 6'909
constexpr std::uint64_t kMaxTypes = 1ULL << 14;              // corpus total 9'250
constexpr std::uint64_t kMaxTypeMembers = 1ULL << 12;        // corpus total 3'681
constexpr std::uint64_t kMaxEnums = 1ULL << 13;              // corpus total 4'359
constexpr std::uint64_t kMaxEnumValues = 1ULL << 8;          // corpus total 174
constexpr std::uint64_t kMaxGlobals = 1ULL << 15;            // corpus total 24'928
constexpr std::uint64_t kMaxGlobalInitCells = 1ULL << 16;    // corpus total 42'804
constexpr std::uint64_t kMaxCallables = 1ULL << 17;          // corpus total 79'845
constexpr std::uint64_t kMaxCallableParameters = 1ULL << 18; // corpus total 140'322
constexpr std::size_t kGlobalFieldCount = 4;
constexpr std::size_t kCallableFieldCount = 10;

struct SoParseError
{
    DecodeError error;
};

[[nodiscard]] DecodeError MakeError(const DecodeErrorCode code, std::string message,
    const std::optional<std::uint64_t> byte_offset = std::nullopt)
{
    return DecodeError{.code = code, .byte_offset = byte_offset, .message = std::move(message)};
}

[[noreturn]] void Fail(const DecodeErrorCode code, std::string message,
    const std::optional<std::uint64_t> byte_offset = std::nullopt)
{
    throw SoParseError{MakeError(code, std::move(message), byte_offset)};
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
    explicit Reader(const std::span<const std::byte> data) noexcept : data_(data) {}

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

    // Validate a 32-bit-length-prefixed, NUL-terminated, 4-byte zero-padded value and advance past
    // it. When `ascii` is set the content (excluding the terminator) must be printable ASCII. The
    // content bytes are validated but never retained.
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
    std::uint64_t offset_ = 0;
};

[[nodiscard]] ObservedByteRange Range(const std::uint64_t start, const std::uint64_t end) noexcept
{
    return ObservedByteRange{.offset = start, .size = end - start};
}

[[nodiscard]] std::uint32_t AddU32Total(
    const std::uint32_t running, const std::uint32_t added, const char* what)
{
    std::uint64_t next = 0;
    if (!Add(running, added, next) || next > std::numeric_limits<std::uint32_t>::max())
        Fail(DecodeErrorCode::Overflow, std::string("SO ") + what + " total overflows");
    return static_cast<std::uint32_t>(next);
}

SoModuleDescriptor ParseModule(const std::span<const std::byte> bytes, const DecodeLimits limits)
{
    Reader reader(bytes);
    SoModuleDescriptor descriptor;

    std::uint64_t projected_items = 0;
    std::uint64_t projected_output = sizeof(SoModuleDescriptor);
    const auto AccountItems = [&](const std::uint64_t added) {
        if (!Add(projected_items, added, projected_items) ||
            projected_items > limits.maximum_items)
            Fail(DecodeErrorCode::LimitExceeded, "SO retained record count exceeds the item limit");
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
    if (code_offset != reader.offset())
        Fail(DecodeErrorCode::Malformed, "SO code offset does not follow the two-word header", 0);
    descriptor.code_offset_is_8 = (code_offset == kCodeHeaderOffset);
    if (code_cell_count > kMaxCodeCells)
        Fail(DecodeErrorCode::LimitExceeded, "SO code cell count exceeds the fixed ceiling", 4);
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
    descriptor.types.reserve(static_cast<std::size_t>(type_count));
    for (std::uint32_t index = 0; index < type_count; ++index)
    {
        reader.ValidateLpValue(true);
        const std::uint64_t flag_offset = reader.offset();
        const std::uint32_t external_flag = reader.U32();
        if (external_flag > 1)
            Fail(DecodeErrorCode::Malformed, "SO type external flag is not 0 or 1", flag_offset);
        reader.ValidateLpValue(true);
        const std::uint64_t member_count_offset = reader.offset();
        const std::uint32_t member_count = reader.U32();
        if (member_count > kMaxTypeMembers)
            Fail(DecodeErrorCode::LimitExceeded, "SO type member count exceeds the fixed ceiling",
                member_count_offset);
        for (std::uint32_t member = 0; member < member_count; ++member)
        {
            reader.ValidateLpValue(true);
            (void)reader.U32();
        }
        descriptor.type_member_total =
            AddU32Total(descriptor.type_member_total, member_count, "type member");
        descriptor.types.push_back(
            SoTypeSummary{.external_flag = external_flag, .member_count = member_count});
    }
    descriptor.types_region = Range(types_start, reader.offset());

    // Enums (external records carry no value list).
    const std::uint64_t enums_start = reader.offset();
    const std::uint32_t enum_count = reader.U32();
    if (enum_count > kMaxEnums)
        Fail(DecodeErrorCode::LimitExceeded, "SO enum count exceeds the fixed ceiling", enums_start);
    AccountItems(enum_count);
    AccountOutput(sizeof(SoEnumSummary), enum_count);
    descriptor.enums.reserve(static_cast<std::size_t>(enum_count));
    for (std::uint32_t index = 0; index < enum_count; ++index)
    {
        reader.ValidateLpValue(true);
        const std::uint64_t flag_offset = reader.offset();
        const std::uint32_t external_flag = reader.U32();
        if (external_flag > 1)
            Fail(DecodeErrorCode::Malformed, "SO enum external flag is not 0 or 1", flag_offset);
        std::uint32_t value_count = 0;
        if (external_flag == 0)
        {
            const std::uint64_t value_count_offset = reader.offset();
            value_count = reader.U32();
            if (value_count > kMaxEnumValues)
                Fail(DecodeErrorCode::LimitExceeded, "SO enum value count exceeds the fixed ceiling",
                    value_count_offset);
            for (std::uint32_t value = 0; value < value_count; ++value)
            {
                reader.ValidateLpValue(true);
                (void)reader.U32();
            }
            descriptor.local_enum_value_total =
                AddU32Total(descriptor.local_enum_value_total, value_count, "enum value");
        }
        descriptor.enums.push_back(
            SoEnumSummary{.external_flag = external_flag, .value_count = value_count});
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
    descriptor.globals.reserve(static_cast<std::size_t>(global_count));
    bool global_ordinals_match = true;
    for (std::uint32_t index = 0; index < global_count; ++index)
    {
        reader.ValidateLpValue(true);
        std::array<std::uint32_t, kGlobalFieldCount> fields{};
        for (std::size_t field = 0; field < kGlobalFieldCount; ++field)
            fields[field] = reader.U32();
        if (fields[1] > 1)
            Fail(DecodeErrorCode::Malformed, "SO global flag field is not 0 or 1",
                reader.offset() - 3U * kWordBytes);
        const std::uint32_t initializer_cell_count = fields[3];
        if (initializer_cell_count > kMaxGlobalInitCells)
            Fail(DecodeErrorCode::LimitExceeded,
                "SO global initializer cell count exceeds the fixed ceiling", reader.offset());
        reader.SkipCells(initializer_cell_count);
        descriptor.global_initializer_cell_total = AddU32Total(
            descriptor.global_initializer_cell_total, initializer_cell_count, "global initializer");
        if (fields[2] != index)
            global_ordinals_match = false;
        descriptor.globals.push_back(
            SoGlobalSummary{.fields = fields, .initializer_cell_count = initializer_cell_count});
    }
    descriptor.globals_region = Range(globals_start, reader.offset());
    descriptor.global_ordinals_match_table_order = global_ordinals_match;

    // Callables.
    const std::uint64_t callables_start = reader.offset();
    const std::uint32_t callable_count = reader.U32();
    if (callable_count > kMaxCallables)
        Fail(DecodeErrorCode::LimitExceeded, "SO callable count exceeds the fixed ceiling",
            callables_start);
    AccountItems(callable_count);
    AccountOutput(sizeof(SoCallableSummary), callable_count);
    descriptor.callables.reserve(static_cast<std::size_t>(callable_count));
    bool callable_ordinals_match = true;
    bool local_entries_increasing = true;
    bool seen_local_entry = false;
    std::uint32_t previous_local_entry = 0;
    std::uint32_t local_callables = 0;
    std::uint32_t external_callables = 0;
    std::uint32_t marker_matches = 0;
    for (std::uint32_t index = 0; index < callable_count; ++index)
    {
        reader.ValidateLpValue(true);
        std::array<std::uint32_t, kCallableFieldCount> fields{};
        for (std::size_t field = 0; field < kCallableFieldCount; ++field)
            fields[field] = reader.U32();
        if (fields[1] > 1)
            Fail(DecodeErrorCode::Malformed, "SO callable flag field is not 0 or 1",
                reader.offset() - 9U * kWordBytes);
        const std::uint64_t parameter_count_offset = reader.offset();
        const std::uint32_t parameter_count = reader.U32();
        if (parameter_count > kMaxCallableParameters)
            Fail(DecodeErrorCode::LimitExceeded,
                "SO callable parameter count exceeds the fixed ceiling", parameter_count_offset);
        reader.SkipCells(parameter_count);
        descriptor.callable_parameter_total =
            AddU32Total(descriptor.callable_parameter_total, parameter_count, "callable parameter");
        if (fields[2] != index)
            callable_ordinals_match = false;
        if (fields[1] == 0)
        {
            ++local_callables;
            const std::uint32_t entry = fields[4];
            const std::uint32_t label_id = fields[3];
            if (seen_local_entry && entry <= previous_local_entry)
                local_entries_increasing = false;
            seen_local_entry = true;
            previous_local_entry = entry;
            if (entry > 0 && entry <= code_cell_count)
            {
                const std::uint32_t marker =
                    reader.CodeCellAt(descriptor.code_region.offset, entry - 1U);
                if ((marker & 0xFFFFU) == kFunctionMarkerLow16 && (marker >> 16U) == label_id)
                    ++marker_matches;
            }
        }
        else
        {
            ++external_callables;
        }
        descriptor.callables.push_back(
            SoCallableSummary{.fields = fields, .parameter_count = parameter_count});
    }
    descriptor.callables_region = Range(callables_start, reader.offset());
    descriptor.callable_ordinals_match_table_order = callable_ordinals_match;
    descriptor.local_entries_strictly_increasing = local_entries_increasing;
    descriptor.local_callable_count = local_callables;
    descriptor.external_callable_count = external_callables;
    descriptor.local_entry_marker_matches = marker_matches;
    descriptor.local_entry_marker_expected = local_callables;

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

asset::DecodeResult<SoModuleDescriptor> InspectSoModule(
    const std::span<const std::byte> bytes, const DecodeLimits limits)
{
    if (bytes.size() > limits.maximum_input_bytes)
        return std::unexpected(
            MakeError(DecodeErrorCode::LimitExceeded, "SO input exceeds the decoder byte limit"));
    if (limits.maximum_output_bytes < sizeof(SoModuleDescriptor))
        return std::unexpected(MakeError(
            DecodeErrorCode::LimitExceeded, "SO descriptor exceeds the decoder output limit"));

    try
    {
        return ParseModule(bytes, limits);
    }
    catch (const SoParseError& failure)
    {
        return std::unexpected(failure.error);
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected(
            MakeError(DecodeErrorCode::LimitExceeded, "SO descriptor allocation failed"));
    }
    catch (const std::length_error&)
    {
        return std::unexpected(MakeError(
            DecodeErrorCode::LimitExceeded, "SO descriptor allocation exceeds capacity"));
    }
    catch (...)
    {
        // Defensive: any other allocation-related throw becomes a typed error, never a crash.
        return std::unexpected(
            MakeError(DecodeErrorCode::LimitExceeded, "SO descriptor allocation failed"));
    }
}
} // namespace omega::retail
