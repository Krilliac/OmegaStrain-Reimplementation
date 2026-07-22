#pragma once

#include <cstdint>
#include <vector>

namespace omega::asset
{
enum class IndexedImageEncoding : std::uint8_t
{
    Indexed4,
    Indexed8,
};

// The palette retains the retail numeric RGBA channels. In particular, alpha is
// the raw GS coefficient for which 0x80 represents 1.0; it is neither
// premultiplied nor expanded to 0..255.
struct RawGsRgba8
{
    std::uint8_t red = 0;
    std::uint8_t green = 0;
    std::uint8_t blue = 0;
    std::uint8_t alpha = 0;

    bool operator==(const RawGsRgba8&) const = default;
};

// Renderer-neutral, fully owned indexed pixels. Each pixel occupies one byte
// even when its source encoding is indexed-4. Palette entries are in logical
// index order after the retail CSM1 mapping.
struct IndexedImageIR
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    IndexedImageEncoding source_encoding = IndexedImageEncoding::Indexed8;
    std::vector<std::uint8_t> indices;
    std::vector<RawGsRgba8> palette;

    bool operator==(const IndexedImageIR&) const = default;
};
} // namespace omega::asset
