#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
using Bytes = std::vector<std::uint8_t>;

constexpr std::size_t kHeaderBytes = 208U;
constexpr std::size_t kSitesOffset = 208U;
constexpr std::size_t kInvocationsOffset = 216U;
constexpr std::size_t kEventsOffset = 264U;
constexpr std::size_t kSubmissionsOffset = 348U;
constexpr std::size_t kDrawsOffset = 420U;
constexpr std::size_t kMembershipsOffset = 471U;
constexpr std::size_t kFixtureBytes = 495U;

enum class TerminalStatus : std::uint32_t
{
    Complete = 0U,
    TelemetryOverflow = 1U,
    TelemetryDropped = 2U,
    VmReset = 3U,
    SavestateDiscontinuity = 4U,
    QueueExhausted = 5U,
    ProducerAborted = 6U,
    OutputFailure = 7U,
    InternalFailure = 8U,
};

enum class CommitState : std::uint32_t
{
    Aborted = 0U,
    Committed = 1U,
};

enum class EventKind : std::uint8_t
{
    Enter = 0U,
    Exit = 1U,
};

enum class DrawDisposition : std::uint8_t
{
    Submitted = 0U,
    Skipped = 1U,
};

enum class FailureCounter : std::size_t
{
    OverflowRecords = 0U,
    DroppedRecords = 1U,
    VmResetCount = 2U,
    SavestateDiscontinuityCount = 3U,
    QueueExhaustionCount = 4U,
    OutputFailureCount = 5U,
    InternalFailureCount = 6U,
};

struct LimitEntry
{
    std::string_view name;
    std::uint64_t value;
};

constexpr std::array<LimitEntry, 20> kHardLimits{{
    {"fragment_bytes", 4U * 1024U * 1024U},
    {"total_input_bytes", 32U * 1024U * 1024U},
    {"fragments", 8U},
    {"manifest_bytes", 64U * 1024U},
    {"site_map_bytes", 1024U * 1024U},
    {"string_bytes", 256U},
    {"frames", 600U},
    {"sites", 4'096U},
    {"nesting_depth", 256U},
    {"invocations", 4'096U},
    {"events", 8'192U},
    {"submissions", 131'072U},
    {"draws", 32'768U},
    {"edges", 131'072U},
    {"failure_records", 131'072U},
    {"lookup_work", 1'048'576U},
    {"scratch_bytes", 16U * 1024U * 1024U},
    {"private_output_bytes", 16U * 1024U * 1024U},
    {"public_aggregate_rows", 8U},
    {"public_output_bytes", 4U * 1024U},
}};

using LimitValues = std::array<std::uint64_t, kHardLimits.size()>;

static_assert(static_cast<std::uint32_t>(TerminalStatus::Complete) == 0U);
static_assert(static_cast<std::uint32_t>(TerminalStatus::TelemetryOverflow) == 1U);
static_assert(static_cast<std::uint32_t>(TerminalStatus::TelemetryDropped) == 2U);
static_assert(static_cast<std::uint32_t>(TerminalStatus::VmReset) == 3U);
static_assert(static_cast<std::uint32_t>(TerminalStatus::SavestateDiscontinuity) == 4U);
static_assert(static_cast<std::uint32_t>(TerminalStatus::QueueExhausted) == 5U);
static_assert(static_cast<std::uint32_t>(TerminalStatus::ProducerAborted) == 6U);
static_assert(static_cast<std::uint32_t>(TerminalStatus::OutputFailure) == 7U);
static_assert(static_cast<std::uint32_t>(TerminalStatus::InternalFailure) == 8U);
static_assert(static_cast<std::uint32_t>(CommitState::Aborted) == 0U);
static_assert(static_cast<std::uint32_t>(CommitState::Committed) == 1U);
static_assert(static_cast<std::uint8_t>(EventKind::Enter) == 0U);
static_assert(static_cast<std::uint8_t>(EventKind::Exit) == 1U);
static_assert(static_cast<std::uint8_t>(DrawDisposition::Submitted) == 0U);
static_assert(static_cast<std::uint8_t>(DrawDisposition::Skipped) == 1U);
static_assert(static_cast<std::size_t>(FailureCounter::OverflowRecords) == 0U);
static_assert(static_cast<std::size_t>(FailureCounter::DroppedRecords) == 1U);
static_assert(static_cast<std::size_t>(FailureCounter::VmResetCount) == 2U);
static_assert(
    static_cast<std::size_t>(FailureCounter::SavestateDiscontinuityCount) == 3U);
static_assert(static_cast<std::size_t>(FailureCounter::QueueExhaustionCount) == 4U);
static_assert(static_cast<std::size_t>(FailureCounter::OutputFailureCount) == 5U);
static_assert(static_cast<std::size_t>(FailureCounter::InternalFailureCount) == 6U);
static_assert(kHeaderBytes == kSitesOffset);

class BoundedWriter
{
public:
    explicit BoundedWriter(const std::size_t limit) : limit_(limit)
    {
        output_.reserve(std::min(limit, kFixtureBytes));
    }

    [[nodiscard]] bool AppendU8(const std::uint8_t value)
    {
        return AppendBytes(&value, 1U);
    }

    [[nodiscard]] bool AppendU32(const std::uint32_t value)
    {
        const std::array<std::uint8_t, 4> encoded{
            static_cast<std::uint8_t>(value),
            static_cast<std::uint8_t>(value >> 8U),
            static_cast<std::uint8_t>(value >> 16U),
            static_cast<std::uint8_t>(value >> 24U),
        };
        return AppendBytes(encoded.data(), encoded.size());
    }

    template <std::size_t Size>
    [[nodiscard]] bool AppendArray(const std::array<std::uint8_t, Size>& value)
    {
        return AppendBytes(value.data(), value.size());
    }

    [[nodiscard]] bool AppendAscii(const std::string_view value)
    {
        const auto* const begin =
            reinterpret_cast<const std::uint8_t*>(value.data());
        return AppendBytes(begin, value.size());
    }

    [[nodiscard]] std::size_t Size() const noexcept
    {
        return output_.size();
    }

    [[nodiscard]] Bytes Take()
    {
        return std::move(output_);
    }

private:
    [[nodiscard]] bool AppendBytes(
        const std::uint8_t* const begin, const std::size_t size)
    {
        if (output_.size() > limit_ || size > limit_ - output_.size())
            return false;
        output_.insert(output_.end(), begin, begin + size);
        return true;
    }

    std::size_t limit_;
    Bytes output_;
};

[[nodiscard]] constexpr LimitValues HardLimitValues()
{
    LimitValues result{};
    for (std::size_t index = 0; index < kHardLimits.size(); ++index)
        result[index] = kHardLimits[index].value;
    return result;
}

[[nodiscard]] constexpr bool LimitsAreValid(const LimitValues& values)
{
    for (std::size_t index = 0; index < values.size(); ++index)
    {
        if (values[index] > kHardLimits[index].value)
            return false;
    }
    return true;
}

[[nodiscard]] constexpr std::optional<FailureCounter> FailureForStatus(
    const TerminalStatus status)
{
    switch (status)
    {
    case TerminalStatus::TelemetryOverflow:
        return FailureCounter::OverflowRecords;
    case TerminalStatus::TelemetryDropped:
        return FailureCounter::DroppedRecords;
    case TerminalStatus::VmReset:
        return FailureCounter::VmResetCount;
    case TerminalStatus::SavestateDiscontinuity:
        return FailureCounter::SavestateDiscontinuityCount;
    case TerminalStatus::QueueExhausted:
        return FailureCounter::QueueExhaustionCount;
    case TerminalStatus::OutputFailure:
        return FailureCounter::OutputFailureCount;
    case TerminalStatus::InternalFailure:
        return FailureCounter::InternalFailureCount;
    case TerminalStatus::Complete:
    case TerminalStatus::ProducerAborted:
        return std::nullopt;
    }
    return std::nullopt;
}

[[nodiscard]] bool AppendU32s(
    BoundedWriter& writer, const std::initializer_list<std::uint32_t> values)
{
    for (const std::uint32_t value : values)
    {
        if (!writer.AppendU32(value))
            return false;
    }
    return true;
}

[[nodiscard]] std::optional<Bytes> BuildSyntheticProducerFragment(
    const std::size_t fragment_limit)
{
    if (fragment_limit > kHardLimits.front().value)
        return std::nullopt;

    constexpr std::string_view magic = "OMEGAFRPHASE0002";
    constexpr std::array<std::uint8_t, 32> capture_domain{
        0x01U, 0x02U, 0x03U, 0x04U, 0x05U, 0x06U, 0x07U, 0x08U,
        0x09U, 0x0aU, 0x0bU, 0x0cU, 0x0dU, 0x0eU, 0x0fU, 0x10U,
        0x11U, 0x12U, 0x13U, 0x14U, 0x15U, 0x16U, 0x17U, 0x18U,
        0x19U, 0x1aU, 0x1bU, 0x1cU, 0x1dU, 0x1eU, 0x1fU, 0x20U,
    };
    constexpr std::array<std::uint8_t, 32> runtime_configuration{
        0x21U, 0x22U, 0x23U, 0x24U, 0x25U, 0x26U, 0x27U, 0x28U,
        0x29U, 0x2aU, 0x2bU, 0x2cU, 0x2dU, 0x2eU, 0x2fU, 0x30U,
        0x31U, 0x32U, 0x33U, 0x34U, 0x35U, 0x36U, 0x37U, 0x38U,
        0x39U, 0x3aU, 0x3bU, 0x3cU, 0x3dU, 0x3eU, 0x3fU, 0x40U,
    };
    constexpr std::array<std::uint8_t, 32> site_map_digest{
        0xd9U, 0xa8U, 0xb5U, 0x4eU, 0xd3U, 0xe6U, 0x44U, 0xc7U,
        0xb5U, 0x95U, 0x62U, 0x39U, 0xa6U, 0x95U, 0x06U, 0xbcU,
        0x55U, 0x2bU, 0xbdU, 0x3eU, 0xfbU, 0x03U, 0xffU, 0xd7U,
        0xafU, 0x82U, 0x29U, 0x13U, 0x58U, 0xf2U, 0xa7U, 0x89U,
    };
    constexpr std::array<std::uint32_t, 6> counts{2U, 2U, 4U, 3U, 3U, 3U};

    BoundedWriter writer(fragment_limit);
    if (!writer.AppendAscii(magic) ||
        !writer.AppendU32(2U) ||
        !writer.AppendArray(capture_domain) ||
        !writer.AppendArray(runtime_configuration) ||
        !writer.AppendArray(site_map_digest) ||
        !AppendU32s(
            writer,
            {
                3U,
                3U,
                static_cast<std::uint32_t>(TerminalStatus::Complete),
                static_cast<std::uint32_t>(CommitState::Committed),
            }))
    {
        return std::nullopt;
    }
    for (std::size_t index = 0; index < 7U; ++index)
    {
        if (!writer.AppendU32(0U))
            return std::nullopt;
    }
    for (const std::uint32_t count : counts)
    {
        if (!writer.AppendU32(count))
            return std::nullopt;
    }
    for (const std::uint32_t count : counts)
    {
        if (!writer.AppendU32(count))
            return std::nullopt;
    }
    if (writer.Size() != kSitesOffset ||
        !AppendU32s(writer, {1U, 2U}) ||
        writer.Size() != kInvocationsOffset)
    {
        return std::nullopt;
    }

    for (const auto& row : std::array{
             std::array<std::uint32_t, 6>{1U, 1U, 0U, 0U, 1U, 4U},
             std::array<std::uint32_t, 6>{2U, 2U, 0U, 1U, 2U, 3U},
         })
    {
        for (const std::uint32_t value : row)
        {
            if (!writer.AppendU32(value))
                return std::nullopt;
        }
    }
    if (writer.Size() != kEventsOffset)
        return std::nullopt;

    struct Event
    {
        std::array<std::uint32_t, 4> prefix;
        EventKind kind;
        std::uint32_t reserved;
    };
    constexpr std::array<Event, 4> events{{
        {{1U, 1U, 1U, 1U}, EventKind::Enter, 0U},
        {{2U, 2U, 1U, 2U}, EventKind::Enter, 0U},
        {{3U, 6U, 2U, 2U}, EventKind::Exit, 0U},
        {{4U, 9U, 3U, 1U}, EventKind::Exit, 0U},
    }};
    for (const Event& row : events)
    {
        for (const std::uint32_t value : row.prefix)
        {
            if (!writer.AppendU32(value))
                return std::nullopt;
        }
        if (!writer.AppendU8(static_cast<std::uint8_t>(row.kind)) ||
            !writer.AppendU32(row.reserved))
        {
            return std::nullopt;
        }
    }
    if (writer.Size() != kSubmissionsOffset)
        return std::nullopt;

    for (const auto& row : std::array{
             std::array<std::uint32_t, 6>{1U, 3U, 1U, 2U, 0U, 3U},
             std::array<std::uint32_t, 6>{2U, 4U, 2U, 2U, 3U, 2U},
             std::array<std::uint32_t, 6>{3U, 7U, 2U, 1U, 5U, 1U},
         })
    {
        for (const std::uint32_t value : row)
        {
            if (!writer.AppendU32(value))
                return std::nullopt;
        }
    }
    if (writer.Size() != kDrawsOffset)
        return std::nullopt;

    struct Draw
    {
        std::array<std::uint32_t, 3> prefix;
        DrawDisposition disposition;
        std::uint32_t reserved;
    };
    constexpr std::array<Draw, 3> draws{{
        {{1U, 5U, 2U}, DrawDisposition::Submitted, 0U},
        {{2U, 8U, 2U}, DrawDisposition::Submitted, 0U},
        {{3U, 10U, 3U}, DrawDisposition::Skipped, 0U},
    }};
    for (const Draw& row : draws)
    {
        for (const std::uint32_t value : row.prefix)
        {
            if (!writer.AppendU32(value))
                return std::nullopt;
        }
        if (!writer.AppendU8(static_cast<std::uint8_t>(row.disposition)) ||
            !writer.AppendU32(row.reserved))
        {
            return std::nullopt;
        }
    }
    if (writer.Size() != kMembershipsOffset)
        return std::nullopt;

    for (const auto& row : std::array{
             std::array<std::uint32_t, 2>{1U, 1U},
             std::array<std::uint32_t, 2>{2U, 1U},
             std::array<std::uint32_t, 2>{3U, 2U},
         })
    {
        for (const std::uint32_t value : row)
        {
            if (!writer.AppendU32(value))
                return std::nullopt;
        }
    }
    if (writer.Size() != kFixtureBytes)
        return std::nullopt;
    return writer.Take();
}

[[nodiscard]] std::string Hex(const Bytes& bytes)
{
    constexpr std::string_view digits = "0123456789abcdef";
    std::string output;
    output.reserve(bytes.size() * 2U + 1U);
    for (const std::uint8_t value : bytes)
    {
        output.push_back(digits[value >> 4U]);
        output.push_back(digits[value & 0x0fU]);
    }
    output.push_back('\n');
    return output;
}

[[nodiscard]] std::optional<std::string> ReadFile(const char* const path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return std::nullopt;
    return std::string{
        std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

[[nodiscard]] std::string ExpectedContract()
{
    std::string output =
        "schema=openomega-frontend-phase-wire-contract-v1\n"
        "magic=OMEGAFRPHASE0002\n"
        "version=2\n"
        "fixture_bytes=495\n"
        "header_bytes=208\n"
        "row_widths=4,24,21,24,17,8\n"
        "offset.sites=208\n"
        "offset.invocations=216\n"
        "offset.events=264\n"
        "offset.submissions=348\n"
        "offset.draws=420\n"
        "offset.memberships=471\n"
        "offset.end=495\n"
        "enum.terminal_status=Complete:0,TelemetryOverflow:1,"
        "TelemetryDropped:2,VmReset:3,SavestateDiscontinuity:4,"
        "QueueExhausted:5,ProducerAborted:6,OutputFailure:7,"
        "InternalFailure:8\n"
        "enum.commit_state=Aborted:0,Committed:1\n"
        "enum.event_kind=Enter:0,Exit:1\n"
        "enum.draw_disposition=Submitted:0,Skipped:1\n"
        "failure_counter_precedence=TelemetryOverflow:overflow_records,"
        "TelemetryDropped:dropped_records,VmReset:vm_reset_count,"
        "SavestateDiscontinuity:savestate_discontinuity_count,"
        "QueueExhausted:queue_exhaustion_count,"
        "OutputFailure:output_failure_count,"
        "InternalFailure:internal_failure_count\n";
    for (const LimitEntry& limit : kHardLimits)
    {
        output += "limit.";
        output += limit.name;
        output += '=';
        output += std::to_string(limit.value);
        output += '\n';
    }
    return output;
}

[[nodiscard]] bool CheckFailureMappings()
{
    constexpr std::array<std::pair<TerminalStatus, FailureCounter>, 7> expected{{
        {TerminalStatus::TelemetryOverflow, FailureCounter::OverflowRecords},
        {TerminalStatus::TelemetryDropped, FailureCounter::DroppedRecords},
        {TerminalStatus::VmReset, FailureCounter::VmResetCount},
        {TerminalStatus::SavestateDiscontinuity,
         FailureCounter::SavestateDiscontinuityCount},
        {TerminalStatus::QueueExhausted, FailureCounter::QueueExhaustionCount},
        {TerminalStatus::OutputFailure, FailureCounter::OutputFailureCount},
        {TerminalStatus::InternalFailure, FailureCounter::InternalFailureCount},
    }};
    if (FailureForStatus(TerminalStatus::Complete).has_value() ||
        FailureForStatus(TerminalStatus::ProducerAborted).has_value())
    {
        return false;
    }
    for (const auto& [status, failure] : expected)
    {
        if (FailureForStatus(status) != failure)
            return false;
    }
    return true;
}

[[nodiscard]] bool CheckEveryHardLimitBoundary()
{
    const LimitValues exact = HardLimitValues();
    if (!LimitsAreValid(exact))
        return false;
    for (std::size_t index = 0; index < exact.size(); ++index)
    {
        LimitValues above = exact;
        if (above[index] == std::numeric_limits<std::uint64_t>::max())
            return false;
        ++above[index];
        if (LimitsAreValid(above))
            return false;
    }
    return true;
}
} // namespace

int main(const int argc, const char* const argv[])
{
    if (argc != 3)
    {
        std::cerr << "front-end phase wire contract: FAILED\n";
        return 1;
    }

    const std::optional<std::string> expected_hex = ReadFile(argv[1]);
    const std::optional<std::string> expected_contract = ReadFile(argv[2]);
    std::optional<Bytes> first = BuildSyntheticProducerFragment(kFixtureBytes);
    std::optional<Bytes> second = BuildSyntheticProducerFragment(kFixtureBytes);
    if (!expected_hex || !expected_contract ||
        expected_hex->size() != kFixtureBytes * 2U + 1U ||
        expected_hex->back() != '\n' ||
        expected_hex->find('\r') != std::string::npos ||
        *expected_contract != ExpectedContract() ||
        !CheckFailureMappings() ||
        !CheckEveryHardLimitBoundary() ||
        !first ||
        !second ||
        first->size() != kFixtureBytes ||
        *first != *second ||
        Hex(*first) != *expected_hex ||
        BuildSyntheticProducerFragment(kFixtureBytes - 1U).has_value() ||
        BuildSyntheticProducerFragment(
            static_cast<std::size_t>(kHardLimits.front().value + 1U))
            .has_value())
    {
        std::cerr << "front-end phase wire contract: FAILED\n";
        return 1;
    }

    const std::uint8_t preserved_first_byte = second->front();
    first->front() ^= 0xffU;
    if (second->front() != preserved_first_byte || *first == *second)
    {
        std::cerr << "front-end phase wire contract: FAILED\n";
        return 1;
    }

    std::cout << "front-end phase wire contract: OK\n";
    return 0;
}
