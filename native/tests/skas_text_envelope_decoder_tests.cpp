#include "omega/retail/skas_text_envelope_decoder.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace skas_test_allocation
{
inline constexpr std::size_t kDisabled = std::numeric_limits<std::size_t>::max();
std::size_t allocations_before_failure = kDisabled;

void Arm(const std::size_t allocations_to_allow) noexcept
{
    allocations_before_failure = allocations_to_allow;
}

void Disarm() noexcept
{
    allocations_before_failure = kDisabled;
}
} // namespace skas_test_allocation

void *operator new(const std::size_t size)
{
    if (skas_test_allocation::allocations_before_failure != skas_test_allocation::kDisabled)
    {
        if (skas_test_allocation::allocations_before_failure == 0)
        {
            skas_test_allocation::Disarm();
            throw std::bad_alloc{};
        }
        --skas_test_allocation::allocations_before_failure;
    }
    if (void *allocation = std::malloc(size == 0 ? 1U : size))
        return allocation;
    throw std::bad_alloc{};
}

void operator delete(void *allocation) noexcept
{
    std::free(allocation);
}

void operator delete(void *allocation, const std::size_t) noexcept
{
    std::free(allocation);
}

namespace
{
using DecodeResult = omega::asset::DecodeResult<omega::asset::SkasTextEnvelopeIR>;

int failures = 0;

void Check(const bool condition, const std::string_view message)
{
    if (condition)
        return;
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

void CheckError(const DecodeResult &result, const omega::asset::DecodeErrorCode code, const std::string_view message)
{
    if (result)
    {
        Check(false, message);
        return;
    }
    Check(result.error().code == code && result.error().message.starts_with("SKAS"), message);
    Check(result.error().message.find('/') == std::string::npos &&
              result.error().message.find('\\') == std::string::npos,
          "SKAS errors contain no filesystem path");
}

void CheckErrorAt(const DecodeResult &result, const omega::asset::DecodeErrorCode code, const std::uint64_t byte_offset,
                  const std::string_view message)
{
    CheckError(result, code, message);
    if (!result)
        Check(result.error().byte_offset == byte_offset,
              "SKAS error reports the expected byte offset");
}

[[nodiscard]] std::string MakeLogical(const std::size_t logical_bytes, const std::size_t nonblank_lines = 67,
                                      const std::size_t blank_lines = 5)
{
    std::string logical;
    if (nonblank_lines == 0 || nonblank_lines + blank_lines != 72)
        return logical;

    const std::size_t base_bytes = nonblank_lines * 5U + blank_lines * 2U;
    if (logical_bytes < base_bytes)
        return logical;

    logical.reserve(logical_bytes);
    logical.append("K:V");
    logical.append(logical_bytes - base_bytes, 'X');
    logical.append("\r\n");
    for (std::size_t line = 1; line < nonblank_lines; ++line)
        logical.append("K:V\r\n");
    for (std::size_t line = 0; line < blank_lines; ++line)
        logical.append("\r\n");
    return logical;
}

[[nodiscard]] std::vector<std::byte> MakePhysical(const std::string_view logical, const std::size_t padding_bytes)
{
    std::vector<std::byte> physical(logical.size() + padding_bytes, std::byte{0});
    for (std::size_t index = 0; index < logical.size(); ++index)
    {
        physical[index] = static_cast<std::byte>(static_cast<unsigned char>(logical[index]));
    }
    return physical;
}

[[nodiscard]] std::vector<std::size_t> LineStarts(const std::string_view logical)
{
    std::vector<std::size_t> starts{0};
    for (std::size_t offset = 0; offset + 1 < logical.size(); ++offset)
    {
        if (logical[offset] == '\r' && logical[offset + 1] == '\n' && offset + 2 < logical.size())
        {
            starts.push_back(offset + 2);
            ++offset;
        }
    }
    return starts;
}

[[nodiscard]] std::string_view LineText(const omega::asset::SkasTextEnvelopeIR &envelope,
                                        const omega::asset::SkasOpaqueTextLineIR line)
{
    return std::string_view(envelope.logical_text).substr(line.text_offset, line.text_bytes);
}

void CheckAllocationFailure(const std::span<const std::byte> bytes, const std::size_t allocations_to_allow)
{
    std::optional<DecodeResult> decoded;
    bool threw = false;
    skas_test_allocation::Arm(allocations_to_allow);
    try
    {
        decoded.emplace(omega::retail::DecodeSkasTextEnvelope(bytes));
    }
    catch (...)
    {
        threw = true;
    }
    skas_test_allocation::Disarm();
    Check(!threw, "SKAS decoder catches an injected owning-allocation failure");
    Check(decoded.has_value(), "SKAS allocation-failure result remains observable");
    if (decoded)
    {
        CheckError(*decoded, omega::asset::DecodeErrorCode::LimitExceeded,
                   "SKAS allocation failure maps to a typed path-free error");
    }
}
} // namespace

int main()
{
    static_assert(omega::retail::kSkasMaximumDecodedItems == 73);
    static_assert(omega::retail::kSkasMaximumLogicalOutputBytes ==
                  sizeof(omega::asset::SkasTextEnvelopeIR) + 5155U + 72U * sizeof(omega::asset::SkasOpaqueTextLineIR));

    const std::string logical = MakeLogical(5130);
    auto physical = MakePhysical(logical, 2);
    const auto decoded = omega::retail::DecodeSkasTextEnvelope(physical);
    Check(decoded.has_value(), "SKAS aggregate-proven synthetic envelope decodes");
    if (decoded)
    {
        Check(decoded->logical_text == logical && decoded->padding_bytes == 2,
              "SKAS owns exact logical text and represents only the omitted "
              "zero-tail count");
        Check(decoded->lines.size() == 72 && decoded->blank_line_count == 5 && decoded->single_colon_line_count == 67,
              "SKAS publishes only the fixed structural counts");
        Check(decoded->lines.front().text_offset == 0 && decoded->lines.front().text_bytes == logical.find("\r\n") &&
                  decoded->lines.front().terminator_bytes == 2 &&
                  LineText(*decoded, decoded->lines.front()).starts_with("K:V") && decoded->lines[66].text_bytes == 3 &&
                  LineText(*decoded, decoded->lines[66]) == "K:V" && decoded->lines[67].text_bytes == 0 &&
                  decoded->lines.back().text_offset == logical.size() - 2U &&
                  decoded->lines.back().terminator_bytes == 2,
              "SKAS preserves exact source-order opaque line ranges and terminators");
        const bool every_terminator_is_crlf = std::ranges::all_of(decoded->lines, [&](const auto line) {
            const std::size_t terminator = line.text_offset + line.text_bytes;
            return line.terminator_bytes == 2 && decoded->logical_text[terminator] == '\r' &&
                   decoded->logical_text[terminator + 1U] == '\n';
        });
        Check(every_terminator_is_crlf, "every SKAS opaque range addresses its exact owned CRLF terminator");
    }

    const auto repeated = omega::retail::DecodeSkasTextEnvelope(physical);
    Check(decoded && repeated && *decoded == *repeated, "SKAS stateless repeated decode is deterministic");

    std::vector<std::byte> unaligned_storage(physical.size() + 1U, std::byte{0xA5});
    std::copy(physical.begin(), physical.end(), unaligned_storage.begin() + 1);
    const auto unaligned = omega::retail::DecodeSkasTextEnvelope(
        std::span<const std::byte>(unaligned_storage.data() + 1, physical.size()));
    Check(decoded && unaligned && *unaligned == *decoded,
          "SKAS accepts an unaligned backing slice because the format has no "
          "address-alignment rule");

    const auto owned = [&]() {
        auto transient = MakePhysical(logical, 2);
        auto result = omega::retail::DecodeSkasTextEnvelope(transient);
        transient.assign(transient.size(), std::byte{0xFF});
        transient.clear();
        transient.shrink_to_fit();
        return result;
    }();
    Check(owned && owned->logical_text == logical, "SKAS logical text remains owned after source replacement and "
                                                   "destruction");

    const std::string minimum_logical = MakeLogical(omega::retail::kSkasMinimumLogicalTextBytes);
    const auto minimum_physical = MakePhysical(minimum_logical, 3);
    Check(minimum_physical.size() == omega::retail::kSkasMinimumPhysicalBytes &&
              omega::retail::DecodeSkasTextEnvelope(minimum_physical).has_value(),
          "SKAS accepts the simultaneous minimum logical and physical boundaries");

    const std::string maximum_logical = MakeLogical(omega::retail::kSkasMaximumLogicalTextBytes);
    const auto maximum_physical = MakePhysical(maximum_logical, 1);
    Check(maximum_physical.size() == omega::retail::kSkasMaximumPhysicalBytes &&
              omega::retail::DecodeSkasTextEnvelope(maximum_physical).has_value(),
          "SKAS accepts the simultaneous maximum logical and physical boundaries");

    Check(omega::retail::DecodeSkasTextEnvelope(MakePhysical(MakeLogical(5131), 1)).has_value(),
          "SKAS accepts the observed one-byte zero-padding minimum");
    Check(omega::retail::DecodeSkasTextEnvelope(minimum_physical).has_value(),
          "SKAS accepts the observed three-byte zero-padding maximum");

    const auto below_physical = MakePhysical(minimum_logical, 2);
    CheckError(omega::retail::DecodeSkasTextEnvelope(below_physical), omega::asset::DecodeErrorCode::UnsupportedVariant,
               "SKAS rejects physical input below the observed range");
    const auto above_physical = MakePhysical(maximum_logical, 2);
    auto permissive = omega::retail::DefaultSkasDecodeLimits();
    permissive.maximum_input_bytes = std::numeric_limits<std::uint64_t>::max();
    permissive.maximum_items = std::numeric_limits<std::uint64_t>::max();
    permissive.maximum_output_bytes = std::numeric_limits<std::uint64_t>::max();
    CheckError(omega::retail::DecodeSkasTextEnvelope(above_physical, permissive),
               omega::asset::DecodeErrorCode::LimitExceeded,
               "SKAS caller limits cannot widen the fixed physical-input ceiling");

    const auto no_padding = MakePhysical(MakeLogical(5132), 0);
    CheckErrorAt(omega::retail::DecodeSkasTextEnvelope(no_padding), omega::asset::DecodeErrorCode::Malformed,
                 no_padding.size(), "SKAS requires at least one trailing NUL byte");
    const auto excessive_padding = MakePhysical(minimum_logical, 4);
    CheckErrorAt(omega::retail::DecodeSkasTextEnvelope(excessive_padding), omega::asset::DecodeErrorCode::LimitExceeded,
                 minimum_logical.size() + 3U, "SKAS rejects the first byte beyond its fixed zero-padding ceiling");
    auto dirty_padding = physical;
    dirty_padding.back() = std::byte{1};
    CheckErrorAt(omega::retail::DecodeSkasTextEnvelope(dirty_padding), omega::asset::DecodeErrorCode::Malformed,
                 logical.size() + 1U, "SKAS rejects and locates a nonzero byte after its logical end");

    auto hostile_text = physical;
    hostile_text[0] = std::byte{0x09};
    CheckErrorAt(omega::retail::DecodeSkasTextEnvelope(hostile_text), omega::asset::DecodeErrorCode::Malformed, 0,
                 "SKAS rejects ASCII controls outside CRLF");
    hostile_text = physical;
    hostile_text[0] = std::byte{0x7F};
    CheckErrorAt(omega::retail::DecodeSkasTextEnvelope(hostile_text), omega::asset::DecodeErrorCode::Malformed, 0,
                 "SKAS rejects the byte above the printable ASCII ceiling");
    hostile_text = physical;
    hostile_text[0] = std::byte{0x80};
    CheckErrorAt(omega::retail::DecodeSkasTextEnvelope(hostile_text), omega::asset::DecodeErrorCode::Malformed, 0,
                 "SKAS rejects non-ASCII text bytes");
    hostile_text = physical;
    hostile_text[0] = std::byte{'~'};
    Check(omega::retail::DecodeSkasTextEnvelope(hostile_text).has_value(),
          "SKAS accepts the upper printable ASCII boundary opaquely");

    const std::size_t first_cr = logical.find("\r\n");
    hostile_text = physical;
    hostile_text[first_cr] = std::byte{'X'};
    CheckErrorAt(omega::retail::DecodeSkasTextEnvelope(hostile_text), omega::asset::DecodeErrorCode::Malformed,
                 first_cr + 1U, "SKAS rejects a bare line feed");
    hostile_text = physical;
    hostile_text[first_cr + 1U] = std::byte{'X'};
    CheckErrorAt(omega::retail::DecodeSkasTextEnvelope(hostile_text), omega::asset::DecodeErrorCode::Malformed,
                 first_cr, "SKAS rejects a bare carriage return");
    hostile_text = physical;
    hostile_text[logical.size() - 2U] = std::byte{'X'};
    hostile_text[logical.size() - 1U] = std::byte{'Y'};
    CheckError(omega::retail::DecodeSkasTextEnvelope(hostile_text), omega::asset::DecodeErrorCode::Malformed,
               "SKAS requires the final source line to end with CRLF");

    hostile_text = physical;
    hostile_text[first_cr] = std::byte{'X'};
    hostile_text[first_cr + 1U] = std::byte{'Y'};
    CheckError(omega::retail::DecodeSkasTextEnvelope(hostile_text), omega::asset::DecodeErrorCode::UnsupportedVariant,
               "SKAS rejects a 71-line text shape");
    hostile_text = physical;
    hostile_text[3] = std::byte{'\r'};
    hostile_text[4] = std::byte{'\n'};
    CheckError(omega::retail::DecodeSkasTextEnvelope(hostile_text), omega::asset::DecodeErrorCode::UnsupportedVariant,
               "SKAS rejects a 73-line text shape");

    const auto wrong_blank_count = MakePhysical(MakeLogical(5130, 66, 6), 2);
    CheckError(omega::retail::DecodeSkasTextEnvelope(wrong_blank_count),
               omega::asset::DecodeErrorCode::UnsupportedVariant,
               "SKAS rejects a line set outside the fixed blank-line count");
    hostile_text = physical;
    hostile_text[1] = std::byte{'X'};
    CheckError(omega::retail::DecodeSkasTextEnvelope(hostile_text), omega::asset::DecodeErrorCode::UnsupportedVariant,
               "SKAS rejects one fewer single-colon line");
    hostile_text = physical;
    hostile_text[3] = std::byte{':'};
    CheckError(omega::retail::DecodeSkasTextEnvelope(hostile_text), omega::asset::DecodeErrorCode::UnsupportedVariant,
               "SKAS rejects a line containing more than one colon");

    std::string opaque_colons = logical;
    const auto line_starts = LineStarts(opaque_colons);
    opaque_colons[0] = ':';
    opaque_colons[1] = ' ';
    opaque_colons[line_starts[1]] = ' ';
    opaque_colons[line_starts[1] + 1U] = ' ';
    opaque_colons[line_starts[1] + 2U] = ':';
    const auto opaque_colon_result = omega::retail::DecodeSkasTextEnvelope(MakePhysical(opaque_colons, 2));
    Check(opaque_colon_result && LineText(*opaque_colon_result, opaque_colon_result->lines.front()).starts_with(": ") &&
              LineText(*opaque_colon_result, opaque_colon_result->lines[1]) == "  :",
          "SKAS does not assign nonempty label or value semantics around a colon");

    auto limits = omega::retail::DefaultSkasDecodeLimits();
    limits.maximum_input_bytes = physical.size();
    Check(omega::retail::DecodeSkasTextEnvelope(physical, limits).has_value(),
          "SKAS succeeds at the exact caller input-byte budget");
    --limits.maximum_input_bytes;
    CheckError(omega::retail::DecodeSkasTextEnvelope(physical, limits), omega::asset::DecodeErrorCode::LimitExceeded,
               "SKAS rejects one byte below the required caller input budget");

    limits = omega::retail::DefaultSkasDecodeLimits();
    limits.maximum_items = omega::retail::kSkasMaximumDecodedItems;
    Check(omega::retail::DecodeSkasTextEnvelope(physical, limits).has_value(),
          "SKAS succeeds at the exact root-plus-lines item budget");
    --limits.maximum_items;
    CheckError(omega::retail::DecodeSkasTextEnvelope(physical, limits), omega::asset::DecodeErrorCode::LimitExceeded,
               "SKAS rejects one item below the fixed root-plus-lines budget");

    const std::uint64_t output_bytes =
        sizeof(omega::asset::SkasTextEnvelopeIR) + logical.size() + 72U * sizeof(omega::asset::SkasOpaqueTextLineIR);
    limits = omega::retail::DefaultSkasDecodeLimits();
    limits.maximum_output_bytes = output_bytes;
    Check(omega::retail::DecodeSkasTextEnvelope(physical, limits).has_value(),
          "SKAS succeeds at the exact owned-output budget");
    --limits.maximum_output_bytes;
    CheckError(omega::retail::DecodeSkasTextEnvelope(physical, limits), omega::asset::DecodeErrorCode::LimitExceeded,
               "SKAS rejects one byte below its exact owned-output budget");

    CheckError(omega::retail::DecodeSkasTextEnvelope(physical, omega::asset::DecodeLimits{}),
               omega::asset::DecodeErrorCode::LimitExceeded,
               "SKAS treats the shared 4096-byte default as an explicit tighter caller budget");

    limits = omega::retail::DefaultSkasDecodeLimits();
    limits.maximum_string_bytes = static_cast<std::uint32_t>(logical.size());
    Check(omega::retail::DecodeSkasTextEnvelope(physical, limits).has_value(),
          "SKAS succeeds at the exact caller logical-string budget");
    --limits.maximum_string_bytes;
    CheckError(omega::retail::DecodeSkasTextEnvelope(physical, limits), omega::asset::DecodeErrorCode::LimitExceeded,
               "SKAS rejects one byte below the caller logical-string budget");

    limits = omega::retail::DefaultSkasDecodeLimits();
    limits.maximum_scratch_bytes = 0;
    limits.maximum_nesting_depth = 0;
    Check(omega::retail::DecodeSkasTextEnvelope(physical, limits).has_value(),
          "SKAS flat decode uses zero scratch and treats its root as depth zero");

    CheckAllocationFailure(physical, 0);
    CheckAllocationFailure(physical, 1);
    Check(omega::retail::DecodeSkasTextEnvelope(physical).has_value(),
          "SKAS decode succeeds unchanged after allocation-failure recovery");

    if (failures != 0)
        std::cerr << failures << " SKAS text-envelope test(s) failed\n";
    return failures == 0 ? 0 : 1;
}
