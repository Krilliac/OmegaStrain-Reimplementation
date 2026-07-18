#include "omega/retail/skl_container_descriptor.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
using Records = std::vector<std::string>;

struct ExpectedRecord
{
    omega::retail::ObservedByteRange line;
    omega::retail::ObservedByteRange token;
    omega::retail::ObservedByteRange terminator;
};

[[nodiscard]] Records MakeRecords()
{
    return {
        "BONENOSCALE",
        "ROOT",
        "PELVIS  ",
        "PLAYER.SKEL",
        "SPINE",
        "",
        "   ",
        "ARM_L",
        "ARM_R",
        "HEAD",
    };
}

void AppendAscii(std::vector<std::byte>& bytes, const std::string_view text)
{
    for (const char character : text)
    {
        bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
    }
}

[[nodiscard]] std::vector<std::byte> MakeSklWithDelimiter(
    const Records& records, const std::string_view delimiter,
    const bool terminate_final_record = true, const std::size_t zero_tail_bytes = 0)
{
    std::vector<std::byte> bytes;
    for (std::size_t index = 0; index < records.size(); ++index)
    {
        AppendAscii(bytes, records[index]);
        if (terminate_final_record || index + 1U < records.size())
            AppendAscii(bytes, delimiter);
    }
    bytes.resize(bytes.size() + zero_tail_bytes, std::byte{0});
    return bytes;
}

[[nodiscard]] std::vector<std::byte> MakeSkl(
    const Records& records, const omega::retail::SklLineEnding line_ending,
    const bool terminate_final_record = true, const std::size_t zero_tail_bytes = 0)
{
    const std::string_view delimiter =
        line_ending == omega::retail::SklLineEnding::CarriageReturn ? "\r" : "\r\n";
    return MakeSklWithDelimiter(
        records, delimiter, terminate_final_record, zero_tail_bytes);
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
void CheckError(
    const Result& result, const omega::asset::DecodeErrorCode code,
    const std::string_view message)
{
    Check(!result && result.error().code == code, message);
}

[[nodiscard]] bool HasExpectedRecords(
    const omega::retail::SklContainerDescriptor& descriptor,
    const std::span<const ExpectedRecord> expected)
{
    if (descriptor.records.size() != expected.size())
        return false;
    for (std::size_t index = 0; index < expected.size(); ++index)
    {
        const auto& actual = descriptor.records[index];
        if (actual.line_region != expected[index].line ||
            actual.token_region != expected[index].token ||
            actual.terminator_region != expected[index].terminator)
        {
            return false;
        }
    }
    return true;
}

void CheckMalformedToken(Records records, const std::size_t record_index,
    const std::string& token, const std::string_view message)
{
    records[record_index] = token;
    CheckError(omega::retail::InspectSklContainer(MakeSkl(
                   records, omega::retail::SklLineEnding::CarriageReturnLineFeed)),
        omega::asset::DecodeErrorCode::Malformed, message);
}
} // namespace

int SklContainerDescriptorFailureCount()
{
    const auto records = MakeRecords();
    const auto crlf_bytes = MakeSkl(
        records, omega::retail::SklLineEnding::CarriageReturnLineFeed);
    const auto crlf = omega::retail::InspectSklContainer(crlf_bytes);
    const std::array<ExpectedRecord, 10> expected_crlf{{
        {{0, 11}, {0, 11}, {11, 2}},
        {{13, 4}, {13, 4}, {17, 2}},
        {{19, 8}, {19, 6}, {27, 2}},
        {{29, 11}, {29, 11}, {40, 2}},
        {{42, 5}, {42, 5}, {47, 2}},
        {{49, 0}, {49, 0}, {49, 2}},
        {{51, 3}, {51, 0}, {54, 2}},
        {{56, 5}, {56, 5}, {61, 2}},
        {{63, 5}, {63, 5}, {68, 2}},
        {{70, 4}, {70, 4}, {74, 2}},
    }};
    Check(crlf.has_value(), "a ten-record CRLF SKL is accepted");
    if (crlf)
    {
        Check(crlf->line_ending ==
                  omega::retail::SklLineEnding::CarriageReturnLineFeed,
            "SKL publishes the observed CRLF line-ending family");
        Check(HasExpectedRecords(*crlf, expected_crlf),
            "SKL publishes precise CRLF line, token, and terminator ranges");
        Check(crlf->logical_extent.observed_bytes == 76U &&
                  crlf->logical_extent.input_bytes == 76U &&
                  crlf->logical_extent.relation ==
                      omega::retail::ObservedExtentRelation::Exact,
            "SKL accepts an exact physical span without an alignment requirement");
    }

    const auto cr_bytes =
        MakeSkl(records, omega::retail::SklLineEnding::CarriageReturn);
    const auto cr = omega::retail::InspectSklContainer(cr_bytes);
    const std::array<ExpectedRecord, 10> expected_cr{{
        {{0, 11}, {0, 11}, {11, 1}},
        {{12, 4}, {12, 4}, {16, 1}},
        {{17, 8}, {17, 6}, {25, 1}},
        {{26, 11}, {26, 11}, {37, 1}},
        {{38, 5}, {38, 5}, {43, 1}},
        {{44, 0}, {44, 0}, {44, 1}},
        {{45, 3}, {45, 0}, {48, 1}},
        {{49, 5}, {49, 5}, {54, 1}},
        {{55, 5}, {55, 5}, {60, 1}},
        {{61, 4}, {61, 4}, {65, 1}},
    }};
    Check(cr.has_value(), "a ten-record CR-only SKL is accepted");
    if (cr)
    {
        Check(cr->line_ending == omega::retail::SklLineEnding::CarriageReturn,
            "SKL publishes the observed CR-only line-ending family");
        Check(HasExpectedRecords(*cr, expected_cr),
            "SKL publishes precise CR-only line, token, and terminator ranges");
        Check(cr->logical_extent.observed_bytes == 66U &&
                  cr->logical_extent.input_bytes == 66U &&
                  cr->logical_extent.relation ==
                      omega::retail::ObservedExtentRelation::Exact,
            "SKL reports the exact CR-only logical extent");
    }

    const auto omitted_bytes = MakeSkl(
        records, omega::retail::SklLineEnding::CarriageReturnLineFeed, false);
    const auto omitted = omega::retail::InspectSklContainer(omitted_bytes);
    Check(omitted && omitted->records.size() == 10U &&
              omitted->records.back().line_region ==
                  omega::retail::ObservedByteRange{70, 4} &&
              omitted->records.back().token_region ==
                  omega::retail::ObservedByteRange{70, 4} &&
              omitted->records.back().terminator_region ==
                  omega::retail::ObservedByteRange{74, 0} &&
              omitted->logical_extent.observed_bytes == 74U &&
              omitted->logical_extent.relation ==
                  omega::retail::ObservedExtentRelation::Exact,
        "SKL accepts an omitted final delimiter without creating a phantom record");

    const auto zero_tail_bytes = MakeSkl(records,
        omega::retail::SklLineEnding::CarriageReturnLineFeed, true, 7U);
    const auto zero_tail = omega::retail::InspectSklContainer(zero_tail_bytes);
    Check(zero_tail && zero_tail->logical_extent.observed_bytes == crlf_bytes.size() &&
              zero_tail->logical_extent.input_bytes == zero_tail_bytes.size() &&
              zero_tail->logical_extent.relation ==
                  omega::retail::ObservedExtentRelation::ZeroPaddedTail,
        "SKL accepts and reports an arbitrary-length all-zero tail");

    auto alternate_records = records;
    alternate_records[0] = "ALTERNATE";
    alternate_records[3] = "player.skel";
    const auto alternate = omega::retail::InspectSklContainer(MakeSkl(
        alternate_records, omega::retail::SklLineEnding::CarriageReturnLineFeed));
    Check(alternate && alternate->records.size() == records.size(),
        "SKL accepts an alternate profile and a case-insensitive lowercase .skel marker");

    const auto owned = [&]() {
        auto transient_source = MakeSkl(
            records, omega::retail::SklLineEnding::CarriageReturnLineFeed);
        auto decoded = omega::retail::InspectSklContainer(transient_source);
        Check(decoded.has_value(), "transient SKL source decodes before ownership check");
        auto descriptor =
            decoded ? std::move(*decoded) : omega::retail::SklContainerDescriptor{};
        transient_source.assign(transient_source.size(), std::byte{0xFF});
        transient_source.clear();
        transient_source.shrink_to_fit();
        return descriptor;
    }();
    Check(crlf && owned == *crlf,
        "SKL descriptor owns its record metadata after source replacement and destruction");

    const Records mutated_records{
        "ABCDEFGHIJK", "NODE", "JOINTS  ", "AVATAR.SKEL", "CHEST",
        "", "   ", "LEG_L", "LEG_R", "FACE",
    };
    const auto mutated = omega::retail::InspectSklContainer(MakeSkl(
        mutated_records, omega::retail::SklLineEnding::CarriageReturnLineFeed));
    Check(crlf && mutated && *mutated == *crlf,
        "SKL descriptor is immune to same-length valid token mutations");

    CheckError(omega::retail::InspectSklContainer(MakeSklWithDelimiter(records, "\n")),
        omega::asset::DecodeErrorCode::Malformed,
        "SKL rejects an LF-only delimiter family");

    auto mixed_endings = crlf_bytes;
    mixed_endings.erase(mixed_endings.begin() + 12);
    CheckError(omega::retail::InspectSklContainer(mixed_endings),
        omega::asset::DecodeErrorCode::Malformed,
        "SKL rejects mixed CR and CRLF delimiters");

    CheckMalformedToken(records, 1U, "RO\tOT",
        "SKL rejects tab characters inside records");
    CheckMalformedToken(records, 1U, std::string{"RO"} + static_cast<char>(0x1F) + "OT",
        "SKL rejects control characters inside records");
    CheckMalformedToken(records, 1U, "RO/OT",
        "SKL rejects path separators inside records");
    CheckMalformedToken(records, 1U, " ROOT",
        "SKL rejects leading spaces before a nonempty token");
    CheckMalformedToken(records, 1U, "RO OT",
        "SKL rejects embedded spaces inside a token");

    auto long_token_records = records;
    long_token_records[1] = std::string(30U, 'A');
    CheckError(omega::retail::InspectSklContainer(MakeSkl(long_token_records,
                   omega::retail::SklLineEnding::CarriageReturnLineFeed)),
        omega::asset::DecodeErrorCode::UnsupportedVariant,
        "SKL rejects record lengths above the observed 29-byte envelope");

    auto nine_records = records;
    nine_records.resize(9U);
    CheckError(omega::retail::InspectSklContainer(MakeSkl(
                   nine_records, omega::retail::SklLineEnding::CarriageReturnLineFeed)),
        omega::asset::DecodeErrorCode::UnsupportedVariant,
        "SKL rejects fewer than ten records");

    auto sixty_one_records = records;
    sixty_one_records.resize(61U, "NODE");
    CheckError(omega::retail::InspectSklContainer(MakeSkl(sixty_one_records,
                   omega::retail::SklLineEnding::CarriageReturnLineFeed)),
        omega::asset::DecodeErrorCode::UnsupportedVariant,
        "SKL rejects more than sixty records");

    for (const std::string& first_record : {std::string{}, std::string{"PROFILE.V1"}})
    {
        auto invalid_profile = records;
        invalid_profile[0] = first_record;
        CheckError(omega::retail::InspectSklContainer(MakeSkl(invalid_profile,
                       omega::retail::SklLineEnding::CarriageReturnLineFeed)),
            omega::asset::DecodeErrorCode::UnsupportedVariant,
            "SKL rejects a blank or dotted first profile record");
    }

    auto missing_marker = records;
    missing_marker[3] = "PLAYER_SKEL";
    CheckError(omega::retail::InspectSklContainer(MakeSkl(
                   missing_marker, omega::retail::SklLineEnding::CarriageReturnLineFeed)),
        omega::asset::DecodeErrorCode::UnsupportedVariant,
        "SKL rejects a missing dotted marker at zero-based record index three");

    auto moved_marker = missing_marker;
    moved_marker[4] = "PLAYER.SKEL";
    CheckError(omega::retail::InspectSklContainer(MakeSkl(
                   moved_marker, omega::retail::SklLineEnding::CarriageReturnLineFeed)),
        omega::asset::DecodeErrorCode::UnsupportedVariant,
        "SKL rejects a dotted marker moved away from record index three");

    auto duplicate_marker = records;
    duplicate_marker[4] = "OTHER.SKEL";
    CheckError(omega::retail::InspectSklContainer(MakeSkl(
                   duplicate_marker, omega::retail::SklLineEnding::CarriageReturnLineFeed)),
        omega::asset::DecodeErrorCode::UnsupportedVariant,
        "SKL rejects a second dotted marker outside record index three");

    auto early_nul = crlf_bytes;
    early_nul[43] = std::byte{0};
    CheckError(omega::retail::InspectSklContainer(early_nul),
        omega::asset::DecodeErrorCode::Malformed,
        "SKL rejects an early NUL followed by nonzero content");

    auto dirty_tail = crlf_bytes;
    dirty_tail.push_back(std::byte{0});
    dirty_tail.push_back(std::byte{1});
    CheckError(omega::retail::InspectSklContainer(dirty_tail),
        omega::asset::DecodeErrorCode::Malformed,
        "SKL rejects a nonzero byte after the zero-tail boundary");

    auto maximum_records = records;
    maximum_records.resize(60U, "NODE");
    const auto maximum = omega::retail::InspectSklContainer(MakeSkl(
        maximum_records, omega::retail::SklLineEnding::CarriageReturnLineFeed));
    Check(maximum && maximum->records.size() == 60U,
        "SKL accepts the observed maximum of sixty records");

    auto maximum_length_records = records;
    maximum_length_records[1] = std::string(29U, 'A');
    const auto maximum_length = omega::retail::InspectSklContainer(MakeSkl(
        maximum_length_records, omega::retail::SklLineEnding::CarriageReturnLineFeed));
    Check(maximum_length && maximum_length->records[1].token_region.size == 29U,
        "SKL accepts the observed maximum 29-byte record length");

    auto limits = omega::asset::DecodeLimits{};
    limits.maximum_input_bytes = static_cast<std::uint64_t>(crlf_bytes.size());
    Check(omega::retail::InspectSklContainer(crlf_bytes, limits).has_value(),
        "SKL succeeds at the exact input-byte budget");
    limits.maximum_input_bytes = static_cast<std::uint64_t>(crlf_bytes.size() - 1U);
    CheckError(omega::retail::InspectSklContainer(crlf_bytes, limits),
        omega::asset::DecodeErrorCode::LimitExceeded,
        "SKL rejects one byte below the required input-byte budget");

    limits = omega::asset::DecodeLimits{};
    limits.maximum_items = 1U + static_cast<std::uint64_t>(records.size());
    Check(omega::retail::InspectSklContainer(crlf_bytes, limits).has_value(),
        "SKL succeeds at the exact root-plus-records item budget");
    limits.maximum_items = static_cast<std::uint64_t>(records.size());
    CheckError(omega::retail::InspectSklContainer(crlf_bytes, limits),
        omega::asset::DecodeErrorCode::LimitExceeded,
        "SKL rejects one item below the root-plus-records item budget");

    const std::uint64_t exact_output_bytes =
        static_cast<std::uint64_t>(sizeof(omega::retail::SklContainerDescriptor)) +
        static_cast<std::uint64_t>(records.size()) *
            static_cast<std::uint64_t>(sizeof(omega::retail::SklRecordDescriptor));
    limits = omega::asset::DecodeLimits{};
    limits.maximum_output_bytes = exact_output_bytes;
    Check(omega::retail::InspectSklContainer(crlf_bytes, limits).has_value(),
        "SKL succeeds at the exact owned-descriptor output budget");
    limits.maximum_output_bytes = exact_output_bytes - 1U;
    CheckError(omega::retail::InspectSklContainer(crlf_bytes, limits),
        omega::asset::DecodeErrorCode::LimitExceeded,
        "SKL rejects one byte below the owned-descriptor output budget");

    limits = omega::asset::DecodeLimits{};
    limits.maximum_scratch_bytes = 0;
    limits.maximum_nesting_depth = 0;
    Check(omega::retail::InspectSklContainer(crlf_bytes, limits).has_value(),
        "SKL uses zero scratch and treats its descriptor root as nesting depth zero");

    return failures;
}
