#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace omega::asset
{
// Sampling/storage families only. Channel order, color space, alpha conversion, nibble order,
// palette permutation, and texel swizzle remain deliberately unassigned.
enum class TextureSampleEncoding : std::uint8_t
{
    Indexed4,
    Indexed8,
    Packed24,
    Packed32,
};

enum class TextureTransferElementEncoding : std::uint8_t
{
    Packed4,
    Packed8,
    Packed24,
    Packed32,
};

struct TextureStoragePlaneIR
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    TextureTransferElementEncoding element_encoding =
        TextureTransferElementEncoding::Packed8;
    std::vector<std::byte> bytes;

    bool operator==(const TextureStoragePlaneIR&) const = default;
};

struct TexturePaletteStorageIR
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    // Four source bytes per entry, in source order. No RGBA/channel interpretation is implied.
    std::vector<std::array<std::byte, 4>> entries;

    bool operator==(const TexturePaletteStorageIR&) const = default;
};

struct TextureStorageBlockIR
{
    // Source order is preserved. Mip, slice, face, frame, and animation meaning is unassigned.
    std::vector<TextureStoragePlaneIR> planes;
    std::optional<TexturePaletteStorageIR> palette;

    bool operator==(const TextureStorageBlockIR&) const = default;
};

// Canonical, fully owned texture storage. It contains neither retail offsets/views nor renderer
// or GPU objects. Display-ready pixel expansion is a separate, independently validated policy.
struct TextureStorageIR
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    TextureSampleEncoding sample_encoding = TextureSampleEncoding::Indexed8;
    // Source order is preserved. Block purpose remains deliberately unassigned.
    std::vector<TextureStorageBlockIR> blocks;

    bool operator==(const TextureStorageIR&) const = default;
};
} // namespace omega::asset
