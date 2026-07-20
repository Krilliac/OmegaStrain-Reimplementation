#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace omega::asset
{
struct SkasOpaqueTextLineIR
{
    // Byte range within SkasTextEnvelopeIR::logical_text, excluding the
    // validated source terminator that immediately follows the range.
    std::uint32_t text_offset = 0;
    std::uint32_t text_bytes = 0;
    std::uint8_t terminator_bytes = 0;

    bool operator==(const SkasOpaqueTextLineIR &) const = default;
};

// Canonical, fully owned SKAS structural text envelope. Logical text remains
// exact and opaque, including every CRLF pair. The validated trailing NUL
// region is represented only by count; labels, values, relationships, and SKA
// association are deliberately unassigned.
struct SkasTextEnvelopeIR
{
    std::uint32_t padding_bytes = 0;
    std::uint32_t blank_line_count = 0;
    std::uint32_t single_colon_line_count = 0;
    std::string logical_text;
    std::vector<SkasOpaqueTextLineIR> lines;

    bool operator==(const SkasTextEnvelopeIR &) const = default;
};
} // namespace omega::asset
