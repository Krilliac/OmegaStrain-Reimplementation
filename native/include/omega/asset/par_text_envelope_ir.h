#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace omega::asset
{
enum class ParDeclaredVersion : std::uint8_t
{
    Version1_3,
    Version1_4,
    Version1_5,
    Version1_7,
    Version1_8,
    Version1_9,
    Version2_0,
    Version2_1,
};

struct ParOpaqueTextLineIR
{
    std::uint32_t text_offset = 0;
    std::uint32_t text_bytes = 0;
    // The validated source grammar permits either an exact CRLF pair or no final
    // terminator.
    std::uint8_t terminator_bytes = 0;

    bool operator==(const ParOpaqueTextLineIR&) const = default;
};

// Canonical, fully owned PAR text envelope. The text remains exact and opaque,
// including every CRLF pair and the original version-token spelling. Line
// ranges address logical_text and omit their zero- or two-byte terminators.
// Validated trailing NUL padding is represented only by count.
struct ParTextEnvelopeIR
{
    ParDeclaredVersion declared_version = ParDeclaredVersion::Version1_3;
    std::uint32_t padding_bytes = 0;
    std::string logical_text;
    std::vector<ParOpaqueTextLineIR> lines;

    bool operator==(const ParTextEnvelopeIR&) const = default;
};
} // namespace omega::asset
