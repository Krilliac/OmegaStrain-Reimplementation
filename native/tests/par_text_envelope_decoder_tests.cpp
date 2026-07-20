#include "omega/retail/par_text_envelope_decoder.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <new>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace par_test_allocation
{
constexpr std::size_t kDisabled = static_cast<std::size_t>(-1);
std::size_t allocations_before_failure = kDisabled;

void Arm(const std::size_t allocations_to_allow) noexcept
{
    allocations_before_failure = allocations_to_allow;
}

void Disarm() noexcept
{
    allocations_before_failure = kDisabled;
}
} // namespace par_test_allocation

void* operator new(const std::size_t size)
{
    if (par_test_allocation::allocations_before_failure != par_test_allocation::kDisabled)
    {
        if (par_test_allocation::allocations_before_failure == 0)
        {
            par_test_allocation::Disarm();
            throw std::bad_alloc{};
        }
        --par_test_allocation::allocations_before_failure;
    }
    if (void* allocation = std::malloc(size == 0 ? 1 : size))
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
using DecodeResult = omega::asset::DecodeResult<omega::asset::ParTextEnvelopeIR>;

int failures = 0;

void Check(const bool condition, const std::string_view message)
{
    if (condition)
        return;
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

void CheckError(const DecodeResult& result, const omega::asset::DecodeErrorCode code,
                const std::string_view message)
{
    if (result)
    {
        Check(false, message);
        return;
    }
    Check(result.error().code == code && result.error().message.find("PAR") == 0, message);
    Check(result.error().message.find('/') == std::string::npos &&
              result.error().message.find('\\') == std::string::npos,
          "PAR errors contain no filesystem path");
}

[[nodiscard]] std::string MakeLogical(const std::string_view version = "1.300000",
                                      const std::string_view marker_whitespace = {},
                                      const std::string_view body = "opaque body\r\nfinal")
{
    std::string logical;
    logical.reserve(version.size() + marker_whitespace.size() + 10 + body.size());
    logical.append(version);
    logical.append(marker_whitespace);
    logical.append(";version\r\n");
    logical.append(body);
    return logical;
}

[[nodiscard]] std::vector<std::byte> MakePhysical(const std::string_view logical,
                                                  const std::size_t physical_bytes = 2048)
{
    std::vector<std::byte> bytes(physical_bytes, std::byte{0});
    if (logical.size() > bytes.size())
        return {};
    for (std::size_t index = 0; index < logical.size(); ++index)
        bytes[index] = static_cast<std::byte>(static_cast<unsigned char>(logical[index]));
    return bytes;
}

[[nodiscard]] std::string_view LineText(const omega::asset::ParTextEnvelopeIR& envelope,
                                        const omega::asset::ParOpaqueTextLineIR line)
{
    return std::string_view(envelope.logical_text).substr(line.text_offset, line.text_bytes);
}

[[nodiscard]] std::string WithLogicalSize(const std::size_t logical_bytes,
                                          const std::string_view version = "1.300000")
{
    std::string logical = MakeLogical(version, {}, {});
    if (logical.size() < logical_bytes)
        logical.append(logical_bytes - logical.size(), 'A');
    return logical;
}

void CheckAllocationFailure(const std::span<const std::byte> bytes,
                            const std::size_t allocations_to_allow)
{
    std::optional<DecodeResult> decoded;
    bool threw = false;
    par_test_allocation::Arm(allocations_to_allow);
    try
    {
        decoded.emplace(omega::retail::DecodeParTextEnvelope(bytes));
    }
    catch (...)
    {
        threw = true;
    }
    par_test_allocation::Disarm();
    Check(!threw, "PAR decoder catches an injected owning-allocation failure");
    Check(decoded.has_value(), "PAR allocation-failure result remains observable");
    if (decoded)
    {
        CheckError(*decoded, omega::asset::DecodeErrorCode::LimitExceeded,
                   "PAR allocation failure maps to a typed path-free decode error");
    }
}
} // namespace

int main()
{
    struct VersionCase
    {
        std::string_view token;
        omega::asset::ParDeclaredVersion version;
    };
    constexpr std::array<VersionCase, 8> versions{{
        {"1.300000", omega::asset::ParDeclaredVersion::Version1_3},
        {"1.400000", omega::asset::ParDeclaredVersion::Version1_4},
        {"1.500000", omega::asset::ParDeclaredVersion::Version1_5},
        {"1.700000", omega::asset::ParDeclaredVersion::Version1_7},
        {"1.800000", omega::asset::ParDeclaredVersion::Version1_8},
        {"1.900000", omega::asset::ParDeclaredVersion::Version1_9},
        {"2.000000", omega::asset::ParDeclaredVersion::Version2_0},
        {"2.100000", omega::asset::ParDeclaredVersion::Version2_1},
    }};

    for (const VersionCase version : versions)
    {
        const std::string logical = MakeLogical(version.token);
        const auto decoded = omega::retail::DecodeParTextEnvelope(MakePhysical(logical));
        Check(decoded && decoded->declared_version == version.version &&
                  decoded->logical_text == logical,
              "PAR accepts and maps one exact observed six-decimal version token");
    }

    const std::string logical = MakeLogical("1.300000", "\t\v\f ", "alpha\r\nbeta");
    auto physical = MakePhysical(logical);
    const auto decoded = omega::retail::DecodeParTextEnvelope(physical);
    Check(decoded.has_value(), "PAR accepts scanner-compatible same-line ASCII whitespace");
    if (decoded)
    {
        Check(decoded->logical_text == logical &&
                  decoded->padding_bytes == physical.size() - logical.size(),
              "PAR owns the exact logical text and reports only the omitted "
              "zero-tail count");
        Check(decoded->lines.size() == 3 &&
                  LineText(*decoded, decoded->lines[0]) == "1.300000\t\v\f ;version" &&
                  decoded->lines[0].terminator_bytes == 2 &&
                  LineText(*decoded, decoded->lines[1]) == "alpha" &&
                  decoded->lines[1].terminator_bytes == 2 &&
                  LineText(*decoded, decoded->lines[2]) == "beta" &&
                  decoded->lines[2].terminator_bytes == 0,
              "PAR publishes exact source-order opaque line ranges and terminators");
    }

    const auto repeated = omega::retail::DecodeParTextEnvelope(physical);
    Check(decoded && repeated && *decoded == *repeated,
          "PAR decoding is deterministic for identical input");

    std::vector<std::byte> unaligned_storage(physical.size() + 1U, std::byte{0xA5});
    std::copy(physical.begin(), physical.end(), unaligned_storage.begin() + 1);
    const auto unaligned = omega::retail::DecodeParTextEnvelope(
        std::span<const std::byte>(unaligned_storage.data() + 1, physical.size()));
    Check(decoded && unaligned && *unaligned == *decoded,
          "PAR accepts an unaligned backing slice because the format has no "
          "address-alignment rule");

    const auto owned = [&]() {
        const std::string transient_logical = MakeLogical("2.100000", {}, "A;B=C\r\nraw");
        auto transient = MakePhysical(transient_logical);
        auto result = omega::retail::DecodeParTextEnvelope(transient);
        Check(result.has_value(), "transient PAR source decodes before ownership check");
        transient.assign(transient.size(), std::byte{0xFF});
        transient.clear();
        transient.shrink_to_fit();
        return result;
    }();
    Check(owned && owned->logical_text == MakeLogical("2.100000", {}, "A;B=C\r\nraw"),
          "PAR output remains owned after source replacement and destruction");

    std::string ascii_boundary = MakeLogical("1.800000", {}, {});
    ascii_boundary.push_back(static_cast<char>(0x7F));
    Check(omega::retail::DecodeParTextEnvelope(MakePhysical(ascii_boundary)).has_value(),
          "PAR accepts the upper seven-bit ASCII boundary opaquely");
    std::string non_ascii = ascii_boundary;
    non_ascii.back() = static_cast<char>(0x80);
    CheckError(omega::retail::DecodeParTextEnvelope(MakePhysical(non_ascii)),
               omega::asset::DecodeErrorCode::Malformed, "PAR rejects the first non-ASCII byte");

    for (const std::string_view unsupported :
         {"1.600000", "1.3", "01.300000", "1.3000000", "2.200000"})
    {
        CheckError(omega::retail::DecodeParTextEnvelope(MakePhysical(MakeLogical(unsupported))),
                   omega::asset::DecodeErrorCode::UnsupportedVariant,
                   "PAR rejects numeric spellings outside the exact observed token set");
    }
    for (const std::string_view malformed : {"", "A.300000", "1..30000", ".300000"})
    {
        CheckError(omega::retail::DecodeParTextEnvelope(MakePhysical(MakeLogical(malformed))),
                   omega::asset::DecodeErrorCode::Malformed,
                   "PAR rejects a malformed declared-version token");
    }

    CheckError(omega::retail::DecodeParTextEnvelope(MakePhysical("1.300000;Version\r\nbody")),
               omega::asset::DecodeErrorCode::Malformed,
               "PAR requires the lowercase version marker");
    CheckError(omega::retail::DecodeParTextEnvelope(MakePhysical("1.300000;version \r\nbody")),
               omega::asset::DecodeErrorCode::Malformed,
               "PAR rejects whitespace after the version marker");
    CheckError(omega::retail::DecodeParTextEnvelope(MakePhysical("1.300000;version-extra\r\nbody")),
               omega::asset::DecodeErrorCode::Malformed,
               "PAR rejects content between the version marker and CRLF");
    CheckError(omega::retail::DecodeParTextEnvelope(MakePhysical("1.300000\r\n;version\r\nbody")),
               omega::asset::DecodeErrorCode::Malformed,
               "PAR keeps the version marker on the proven first line");
    CheckError(omega::retail::DecodeParTextEnvelope(MakePhysical("1.300000;version\nbody")),
               omega::asset::DecodeErrorCode::Malformed,
               "PAR rejects an LF-only first-line terminator");
    CheckError(omega::retail::DecodeParTextEnvelope(MakePhysical("1.300000;version\rbody")),
               omega::asset::DecodeErrorCode::Malformed,
               "PAR rejects a CR-only first-line terminator");
    CheckError(omega::retail::DecodeParTextEnvelope(MakePhysical("1.300000;version\r\nbody\nnext")),
               omega::asset::DecodeErrorCode::Malformed, "PAR rejects a lone body line feed");
    CheckError(omega::retail::DecodeParTextEnvelope(MakePhysical("1.300000;version\r\nbody\rnext")),
               omega::asset::DecodeErrorCode::Malformed, "PAR rejects a lone body carriage return");

    const std::string maximum_padding_logical = MakeLogical("1.900000", {}, {});
    const auto maximum_padding =
        MakePhysical(maximum_padding_logical, maximum_padding_logical.size() + 2040);
    const auto maximum_padding_result = omega::retail::DecodeParTextEnvelope(maximum_padding);
    Check(maximum_padding_result && maximum_padding_result->padding_bytes == 2040,
          "PAR accepts the fixed 2040-byte zero-padding ceiling");
    CheckError(omega::retail::DecodeParTextEnvelope(
                   MakePhysical(maximum_padding_logical, maximum_padding_logical.size() + 2041)),
               omega::asset::DecodeErrorCode::LimitExceeded,
               "PAR rejects zero padding one byte above the fixed ceiling");

    const std::string one_padding_logical = WithLogicalSize(2047);
    const auto one_padding =
        omega::retail::DecodeParTextEnvelope(MakePhysical(one_padding_logical, 2048));
    Check(one_padding && one_padding->padding_bytes == 1,
          "PAR accepts the observed one-byte padding minimum");

    const std::string maximum_physical_logical = WithLogicalSize(2056);
    const auto maximum_physical =
        omega::retail::DecodeParTextEnvelope(MakePhysical(maximum_physical_logical, 4096));
    Check(maximum_physical && maximum_physical->padding_bytes == 2040,
          "PAR accepts the fixed 4096-byte physical-input ceiling");
    CheckError(omega::retail::DecodeParTextEnvelope(MakePhysical(maximum_physical_logical, 4097)),
               omega::asset::DecodeErrorCode::LimitExceeded,
               "PAR rejects physical input one byte above the fixed ceiling");
    CheckError(omega::retail::DecodeParTextEnvelope(MakePhysical(MakeLogical(), 2047)),
               omega::asset::DecodeErrorCode::UnsupportedVariant,
               "PAR rejects physical input below the observed range");

    std::string no_padding_logical = WithLogicalSize(2048);
    CheckError(omega::retail::DecodeParTextEnvelope(MakePhysical(no_padding_logical, 2048)),
               omega::asset::DecodeErrorCode::Malformed,
               "PAR requires at least one trailing NUL byte");
    auto dirty_tail = MakePhysical(logical);
    dirty_tail.back() = std::byte{1};
    CheckError(omega::retail::DecodeParTextEnvelope(dirty_tail),
               omega::asset::DecodeErrorCode::Malformed,
               "PAR rejects a nonzero byte after the first NUL");

    std::string maximum_lines = MakeLogical("1.300000", {}, {});
    for (std::size_t index = 0; index < 2038; ++index)
        maximum_lines.append("\r\n");
    maximum_lines.push_back('X');
    const auto maximum_line_result =
        omega::retail::DecodeParTextEnvelope(MakePhysical(maximum_lines, 4096));
    Check(maximum_lines.size() == 4095 && maximum_line_result &&
              maximum_line_result->lines.size() == 2040,
          "PAR accepts the hard derived logical-text and line-count ceilings");

    auto limits = omega::asset::DecodeLimits{};
    limits.maximum_input_bytes = physical.size();
    Check(omega::retail::DecodeParTextEnvelope(physical, limits).has_value(),
          "PAR succeeds at the exact caller input-byte budget");
    --limits.maximum_input_bytes;
    CheckError(omega::retail::DecodeParTextEnvelope(physical, limits),
               omega::asset::DecodeErrorCode::LimitExceeded,
               "PAR rejects one byte below the required caller input budget");

    limits = omega::asset::DecodeLimits{};
    limits.maximum_string_bytes = static_cast<std::uint32_t>(logical.size());
    Check(omega::retail::DecodeParTextEnvelope(physical, limits).has_value(),
          "PAR succeeds at the exact caller logical-string budget");
    --limits.maximum_string_bytes;
    CheckError(omega::retail::DecodeParTextEnvelope(physical, limits),
               omega::asset::DecodeErrorCode::LimitExceeded,
               "PAR rejects one byte below the required caller logical-string budget");

    const std::uint64_t item_count = 1 + (decoded ? decoded->lines.size() : 0);
    limits = omega::asset::DecodeLimits{};
    limits.maximum_items = item_count;
    Check(omega::retail::DecodeParTextEnvelope(physical, limits).has_value(),
          "PAR succeeds at the exact root-plus-lines item budget");
    --limits.maximum_items;
    CheckError(omega::retail::DecodeParTextEnvelope(physical, limits),
               omega::asset::DecodeErrorCode::LimitExceeded,
               "PAR rejects one item below the root-plus-lines budget");

    const std::uint64_t output_bytes =
        sizeof(omega::asset::ParTextEnvelopeIR) + logical.size() +
        (decoded ? decoded->lines.size() : 0) * sizeof(omega::asset::ParOpaqueTextLineIR);
    limits = omega::asset::DecodeLimits{};
    limits.maximum_output_bytes = output_bytes;
    Check(omega::retail::DecodeParTextEnvelope(physical, limits).has_value(),
          "PAR succeeds at the exact owned-output byte budget");
    --limits.maximum_output_bytes;
    CheckError(omega::retail::DecodeParTextEnvelope(physical, limits),
               omega::asset::DecodeErrorCode::LimitExceeded,
               "PAR rejects one byte below the owned-output budget");

    limits = omega::asset::DecodeLimits{};
    limits.maximum_scratch_bytes = 0;
    limits.maximum_nesting_depth = 0;
    Check(omega::retail::DecodeParTextEnvelope(physical, limits).has_value(),
          "PAR uses zero scratch and treats its root as nesting depth zero");

    CheckAllocationFailure(physical, 0);
    CheckAllocationFailure(physical, 1);
    Check(omega::retail::DecodeParTextEnvelope(physical).has_value(),
          "PAR decode succeeds unchanged after allocation-failure recovery");

    if (failures != 0)
        std::cerr << failures << " PAR text-envelope test(s) failed\n";
    return failures == 0 ? 0 : 1;
}
