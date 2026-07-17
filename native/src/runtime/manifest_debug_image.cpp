#include "omega/runtime/manifest_debug_image.h"

#include <algorithm>
#include <array>
#include <limits>

namespace omega::runtime
{
namespace
{
constexpr std::uint32_t kCellPixels = 12;
constexpr std::uint32_t kTileInset = 2;
constexpr std::uint64_t kMaximumCells = 1ULL << 20U;
constexpr std::uint64_t kMaximumImageBytes = 64ULL * 1024ULL * 1024ULL;

[[nodiscard]] std::uint32_t HashCell(const asset::LevelCellSourceIR& cell) noexcept
{
    std::uint32_t hash = 2166136261U;
    const auto mix = [&hash](const std::uint8_t value) {
        hash ^= value;
        hash *= 16777619U;
    };
    for (unsigned shift = 0; shift < 32; shift += 8)
    {
        mix(static_cast<std::uint8_t>((cell.observed_kind >> shift) & 0xFFU));
        mix(static_cast<std::uint8_t>((cell.observed_index >> shift) & 0xFFU));
    }
    for (const unsigned char value : cell.data_hog_entry)
        mix(value);
    return hash;
}

void SetPixel(ManifestDebugImage& image, const std::uint32_t x, const std::uint32_t y,
    const std::array<std::byte, 4>& color) noexcept
{
    const std::size_t offset =
        (static_cast<std::size_t>(y) * image.width + x) * color.size();
    for (std::size_t channel = 0; channel < color.size(); ++channel)
        image.rgba8_pixels[offset + channel] = color[channel];
}
} // namespace

std::expected<ManifestDebugImage, std::string> BuildManifestDebugImage(
    const asset::LevelManifestIR& manifest)
{
    const std::uint64_t cell_count = std::max<std::uint64_t>(1U, manifest.terrain_cells.size());
    if (cell_count > kMaximumCells)
        return std::unexpected("manifest debug view exceeds the cell safety limit");

    std::uint64_t columns = 1;
    while (columns * columns < cell_count)
        ++columns;
    const std::uint64_t rows = (cell_count + columns - 1U) / columns;
    if (columns > std::numeric_limits<std::uint32_t>::max() / kCellPixels ||
        rows > std::numeric_limits<std::uint32_t>::max() / kCellPixels)
        return std::unexpected("manifest debug view dimensions overflow");

    const std::uint64_t width = columns * kCellPixels;
    const std::uint64_t height = rows * kCellPixels;
    if (width > kMaximumImageBytes / 4U ||
        height > kMaximumImageBytes / (width * 4U))
        return std::unexpected("manifest debug view exceeds the image byte limit");

    ManifestDebugImage image{
        .width = static_cast<std::uint32_t>(width),
        .height = static_cast<std::uint32_t>(height),
        .rgba8_pixels = std::vector<std::byte>(
            static_cast<std::size_t>(width * height * 4U), std::byte{0}),
    };
    constexpr std::array background{
        std::byte{8}, std::byte{12}, std::byte{24}, std::byte{255}};
    for (std::uint32_t y = 0; y < image.height; ++y)
        for (std::uint32_t x = 0; x < image.width; ++x)
            SetPixel(image, x, y, background);

    for (std::size_t index = 0; index < manifest.terrain_cells.size(); ++index)
    {
        const auto& cell = manifest.terrain_cells[index];
        const std::uint32_t hash = HashCell(cell);
        const std::array color{
            static_cast<std::byte>(64U + (hash & 0x7FU)),
            static_cast<std::byte>(64U + ((hash >> 8U) & 0x7FU)),
            static_cast<std::byte>(64U + ((hash >> 16U) & 0x7FU)),
            std::byte{255},
        };
        const auto column = static_cast<std::uint32_t>(index % columns);
        const auto row = static_cast<std::uint32_t>(index / columns);
        const std::uint32_t origin_x = column * kCellPixels + kTileInset;
        const std::uint32_t origin_y = row * kCellPixels + kTileInset;
        for (std::uint32_t y = origin_y; y < origin_y + kCellPixels - 2U * kTileInset; ++y)
            for (std::uint32_t x = origin_x; x < origin_x + kCellPixels - 2U * kTileInset; ++x)
                SetPixel(image, x, y, color);
    }
    return image;
}
} // namespace omega::runtime
