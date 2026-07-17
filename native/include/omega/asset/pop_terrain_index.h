#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace omega::asset
{
struct PopTerrainParseLimits
{
    std::uint32_t maximum_records = 1U << 20U;
    std::size_t maximum_name_bytes = 4096;
    std::size_t maximum_owned_name_bytes = 64U * 1024U * 1024U;
};

enum class PopTerrainParseErrorCode
{
    Truncated,
    Malformed,
    Overflow,
    LimitExceeded,
};

struct PopTerrainParseError
{
    PopTerrainParseErrorCode code = PopTerrainParseErrorCode::Malformed;
    std::optional<std::uint64_t> byte_offset;
    std::string message;
};

struct PopTerrainRecord
{
    std::uint32_t kind = 0;
    std::uint32_t index = 0;
    std::string name;
};

class PopTerrainIndex final
{
public:
    // [any thread] Parses only the independently documented TER prefix. The returned value owns
    // all names and is immutable after publication. Later POP sections remain uninterpreted.
    [[nodiscard]] static std::expected<PopTerrainIndex, PopTerrainParseError> Parse(
        std::span<const std::byte> bytes, PopTerrainParseLimits limits = {});

    // [any thread after publication; immutable]
    [[nodiscard]] std::span<const PopTerrainRecord> records() const noexcept { return records_; }
    [[nodiscard]] std::size_t next_section_offset() const noexcept { return next_section_offset_; }
    [[nodiscard]] std::uint32_t nonzero_alignment_record_count() const noexcept
    {
        return nonzero_alignment_record_count_;
    }

private:
    std::vector<PopTerrainRecord> records_;
    std::size_t next_section_offset_ = 0;
    std::uint32_t nonzero_alignment_record_count_ = 0;
};
} // namespace omega::asset
