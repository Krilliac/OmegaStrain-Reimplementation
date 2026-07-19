#include "omega/runtime/texture_storage_topology_debug_image.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <stdexcept>
#include <vector>

namespace omega::runtime
{
namespace
{
constexpr std::uint32_t kTilePixels = 32U;
constexpr std::uint64_t kChannelsPerPixel = 4U;
constexpr std::uint64_t kMaximumColumns = 8U;
constexpr std::uint64_t kMaximumPlaneMarkersPerBlock = 64U;
constexpr std::array kBackgroundColor{
    std::byte{8}, std::byte{12}, std::byte{24}, std::byte{255}};
constexpr std::array kBorderColor{
    std::byte{28}, std::byte{38}, std::byte{58}, std::byte{255}};
constexpr std::array kTopologyColor{
    std::byte{112}, std::byte{220}, std::byte{255}, std::byte{255}};
constexpr std::array kPaletteColor{
    std::byte{255}, std::byte{196}, std::byte{64}, std::byte{255}};

struct ImagePlan
{
    std::uint32_t columns = 0U;
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    std::uint64_t output_bytes = 0U;
};

[[nodiscard]] constexpr TextureStorageTopologyDebugImageError Error(
    const TextureStorageTopologyDebugImageErrorCode code) noexcept
{
    return TextureStorageTopologyDebugImageError{
        .code = code,
        .message = TextureStorageTopologyDebugImageErrorMessage(code),
    };
}

[[nodiscard]] bool Add(
    const std::uint64_t left, const std::uint64_t right, std::uint64_t& output) noexcept
{
    if (right > std::numeric_limits<std::uint64_t>::max() - left)
        return false;
    output = left + right;
    return true;
}

[[nodiscard]] bool Multiply(
    const std::uint64_t left, const std::uint64_t right, std::uint64_t& output) noexcept
{
    if (left != 0U && right > std::numeric_limits<std::uint64_t>::max() / left)
        return false;
    output = left * right;
    return true;
}

[[nodiscard]] bool IsValidSampleEncoding(
    const asset::TextureSampleEncoding encoding) noexcept
{
    switch (encoding)
    {
    case asset::TextureSampleEncoding::Indexed4:
    case asset::TextureSampleEncoding::Indexed8:
    case asset::TextureSampleEncoding::Packed24:
    case asset::TextureSampleEncoding::Packed32:
        return true;
    }
    return false;
}

[[nodiscard]] bool IsValidTransferElementEncoding(
    const asset::TextureTransferElementEncoding encoding) noexcept
{
    switch (encoding)
    {
    case asset::TextureTransferElementEncoding::Packed4:
    case asset::TextureTransferElementEncoding::Packed8:
    case asset::TextureTransferElementEncoding::Packed24:
    case asset::TextureTransferElementEncoding::Packed32:
        return true;
    }
    return false;
}

[[nodiscard]] bool PlaneByteSize(const asset::TextureStoragePlaneIR& plane,
    std::uint64_t& output) noexcept
{
    std::uint64_t area = 0U;
    if (!Multiply(plane.width, plane.height, area))
        return false;

    switch (plane.element_encoding)
    {
    case asset::TextureTransferElementEncoding::Packed4:
        output = area / 2U + area % 2U;
        return true;
    case asset::TextureTransferElementEncoding::Packed8:
        output = area;
        return true;
    case asset::TextureTransferElementEncoding::Packed24:
        return Multiply(area, 3U, output);
    case asset::TextureTransferElementEncoding::Packed32:
        return Multiply(area, 4U, output);
    }
    return false;
}

[[nodiscard]] std::expected<ImagePlan, TextureStorageTopologyDebugImageError> Preflight(
    const asset::TextureStorageIR& storage,
    const TextureStorageTopologyDebugImageLimits& limits) noexcept
{
    if (storage.width == 0U || storage.height == 0U)
    {
        return std::unexpected(
            Error(TextureStorageTopologyDebugImageErrorCode::InvalidTextureDimensions));
    }
    if (!IsValidSampleEncoding(storage.sample_encoding))
    {
        return std::unexpected(
            Error(TextureStorageTopologyDebugImageErrorCode::InvalidSampleEncoding));
    }
    if (storage.blocks.empty())
    {
        return std::unexpected(
            Error(TextureStorageTopologyDebugImageErrorCode::EmptyBlockSet));
    }

    const std::uint64_t block_count = static_cast<std::uint64_t>(storage.blocks.size());
    if (block_count > limits.maximum_blocks)
    {
        return std::unexpected(
            Error(TextureStorageTopologyDebugImageErrorCode::BlockLimitExceeded));
    }

    std::uint64_t plane_count = 0U;
    std::uint64_t palette_entry_count = 0U;
    for (const asset::TextureStorageBlockIR& block : storage.blocks)
    {
        if (block.planes.empty())
        {
            return std::unexpected(
                Error(TextureStorageTopologyDebugImageErrorCode::EmptyPlaneSet));
        }
        const std::uint64_t block_plane_count =
            static_cast<std::uint64_t>(block.planes.size());
        if (block_plane_count > kMaximumPlaneMarkersPerBlock)
        {
            return std::unexpected(Error(
                TextureStorageTopologyDebugImageErrorCode::PlaneMarkerCapacityExceeded));
        }
        if (!Add(plane_count, block_plane_count, plane_count) ||
            plane_count > limits.maximum_planes)
        {
            return std::unexpected(
                Error(TextureStorageTopologyDebugImageErrorCode::PlaneLimitExceeded));
        }

        for (const asset::TextureStoragePlaneIR& plane : block.planes)
        {
            if (plane.width == 0U || plane.height == 0U)
            {
                return std::unexpected(Error(
                    TextureStorageTopologyDebugImageErrorCode::InvalidPlaneDimensions));
            }
            if (!IsValidTransferElementEncoding(plane.element_encoding))
            {
                return std::unexpected(Error(TextureStorageTopologyDebugImageErrorCode::
                        InvalidTransferElementEncoding));
            }

            std::uint64_t expected_bytes = 0U;
            if (!PlaneByteSize(plane, expected_bytes))
            {
                return std::unexpected(Error(
                    TextureStorageTopologyDebugImageErrorCode::PlaneByteSizeOverflow));
            }
            if (expected_bytes != static_cast<std::uint64_t>(plane.bytes.size()))
            {
                return std::unexpected(Error(
                    TextureStorageTopologyDebugImageErrorCode::PlaneByteSizeMismatch));
            }
        }

        if (block.palette)
        {
            const asset::TexturePaletteStorageIR& palette = *block.palette;
            if (palette.width == 0U || palette.height == 0U)
            {
                return std::unexpected(Error(
                    TextureStorageTopologyDebugImageErrorCode::InvalidPaletteDimensions));
            }
            const std::uint64_t expected_entries =
                static_cast<std::uint64_t>(palette.width) * palette.height;
            if (expected_entries != static_cast<std::uint64_t>(palette.entries.size()))
            {
                return std::unexpected(Error(
                    TextureStorageTopologyDebugImageErrorCode::PaletteEntryCountMismatch));
            }
            if (!Add(palette_entry_count, expected_entries, palette_entry_count) ||
                palette_entry_count > limits.maximum_palette_entries)
            {
                return std::unexpected(Error(
                    TextureStorageTopologyDebugImageErrorCode::PaletteEntryLimitExceeded));
            }
        }
    }

    const std::uint64_t columns = std::min(block_count, kMaximumColumns);
    const std::uint64_t rows =
        block_count / columns + (block_count % columns != 0U ? 1U : 0U);
    std::uint64_t width = 0U;
    std::uint64_t height = 0U;
    if (!Multiply(columns, kTilePixels, width) ||
        !Multiply(rows, kTilePixels, height) ||
        width > std::numeric_limits<std::uint32_t>::max() ||
        height > std::numeric_limits<std::uint32_t>::max())
    {
        return std::unexpected(
            Error(TextureStorageTopologyDebugImageErrorCode::ImageDimensionOverflow));
    }

    std::uint64_t pixel_count = 0U;
    std::uint64_t output_bytes = 0U;
    if (!Multiply(width, height, pixel_count) ||
        !Multiply(pixel_count, kChannelsPerPixel, output_bytes))
    {
        return std::unexpected(
            Error(TextureStorageTopologyDebugImageErrorCode::ImageByteSizeOverflow));
    }
    if (output_bytes > limits.maximum_output_bytes ||
        output_bytes > std::numeric_limits<std::size_t>::max())
    {
        return std::unexpected(
            Error(TextureStorageTopologyDebugImageErrorCode::OutputByteLimitExceeded));
    }

    return ImagePlan{
        .columns = static_cast<std::uint32_t>(columns),
        .width = static_cast<std::uint32_t>(width),
        .height = static_cast<std::uint32_t>(height),
        .output_bytes = output_bytes,
    };
}

void SetPixel(DebugImage& image, const std::uint32_t x, const std::uint32_t y,
    const std::array<std::byte, 4>& color) noexcept
{
    const std::size_t offset =
        (static_cast<std::size_t>(y) * image.width + x) * color.size();
    for (std::size_t channel = 0U; channel < color.size(); ++channel)
        image.rgba8_pixels[offset + channel] = color[channel];
}

void Fill(DebugImage& image, const std::array<std::byte, 4>& color) noexcept
{
    for (std::size_t offset = 0U; offset < image.rgba8_pixels.size();
         offset += color.size())
    {
        for (std::size_t channel = 0U; channel < color.size(); ++channel)
            image.rgba8_pixels[offset + channel] = color[channel];
    }
}

void DrawBorder(DebugImage& image, const std::uint32_t origin_x,
    const std::uint32_t origin_y) noexcept
{
    const std::uint32_t maximum_x = origin_x + kTilePixels - 1U;
    const std::uint32_t maximum_y = origin_y + kTilePixels - 1U;
    for (std::uint32_t offset = 0U; offset < kTilePixels; ++offset)
    {
        SetPixel(image, origin_x + offset, origin_y, kBorderColor);
        SetPixel(image, origin_x + offset, maximum_y, kBorderColor);
        SetPixel(image, origin_x, origin_y + offset, kBorderColor);
        SetPixel(image, maximum_x, origin_y + offset, kBorderColor);
    }
}

[[nodiscard]] std::uint8_t SampleMask(const asset::TextureSampleEncoding encoding) noexcept
{
    switch (encoding)
    {
    case asset::TextureSampleEncoding::Indexed4:
        return 0x1U;
    case asset::TextureSampleEncoding::Indexed8:
        return 0x9U;
    case asset::TextureSampleEncoding::Packed24:
        return 0x7U;
    case asset::TextureSampleEncoding::Packed32:
        return 0xfU;
    }
    return 0U;
}

[[nodiscard]] std::uint8_t PlaneMask(
    const asset::TextureTransferElementEncoding encoding) noexcept
{
    switch (encoding)
    {
    case asset::TextureTransferElementEncoding::Packed4:
        return 0x1U;
    case asset::TextureTransferElementEncoding::Packed8:
        return 0x9U;
    case asset::TextureTransferElementEncoding::Packed24:
        return 0x7U;
    case asset::TextureTransferElementEncoding::Packed32:
        return 0xfU;
    }
    return 0U;
}

void DrawMask(DebugImage& image, const std::uint32_t origin_x,
    const std::uint32_t origin_y, const std::uint8_t mask) noexcept
{
    for (std::uint32_t bit = 0U; bit < 4U; ++bit)
    {
        if ((mask & static_cast<std::uint8_t>(1U << bit)) == 0U)
            continue;
        SetPixel(image, origin_x + bit % 2U, origin_y + bit / 2U, kTopologyColor);
    }
}

void DrawPaletteMarker(DebugImage& image, const std::uint32_t origin_x,
    const std::uint32_t origin_y) noexcept
{
    constexpr std::array<std::array<std::uint32_t, 2>, 5> offsets{{
        {27U, 26U},
        {26U, 27U},
        {27U, 27U},
        {28U, 27U},
        {27U, 28U},
    }};
    for (const auto& offset : offsets)
        SetPixel(image, origin_x + offset[0], origin_y + offset[1], kPaletteColor);
}
} // namespace

std::expected<DebugImage, TextureStorageTopologyDebugImageError>
BuildTextureStorageTopologyDebugImage(const asset::TextureStorageIR& storage,
    const TextureStorageTopologyDebugImageLimits& limits)
{
    const auto planned = Preflight(storage, limits);
    if (!planned)
        return std::unexpected(planned.error());

    try
    {
        DebugImage image{
            .width = planned->width,
            .height = planned->height,
            .rgba8_pixels =
                std::vector<std::byte>(static_cast<std::size_t>(planned->output_bytes)),
        };
        Fill(image, kBackgroundColor);

        for (std::size_t block_index = 0U; block_index < storage.blocks.size();
             ++block_index)
        {
            const std::uint32_t column =
                static_cast<std::uint32_t>(block_index % planned->columns);
            const std::uint32_t row =
                static_cast<std::uint32_t>(block_index / planned->columns);
            const std::uint32_t origin_x = column * kTilePixels;
            const std::uint32_t origin_y = row * kTilePixels;

            DrawBorder(image, origin_x, origin_y);
            DrawMask(image, origin_x + 4U, origin_y + 4U,
                SampleMask(storage.sample_encoding));

            const asset::TextureStorageBlockIR& block = storage.blocks[block_index];
            for (std::size_t plane_index = 0U; plane_index < block.planes.size();
                 ++plane_index)
            {
                const std::uint32_t marker_x =
                    origin_x + 8U + 2U * static_cast<std::uint32_t>(plane_index % 8U);
                const std::uint32_t marker_y =
                    origin_y + 8U + 2U * static_cast<std::uint32_t>(plane_index / 8U);
                DrawMask(image, marker_x, marker_y,
                    PlaneMask(block.planes[plane_index].element_encoding));
            }

            if (block.palette)
                DrawPaletteMarker(image, origin_x, origin_y);
        }
        return image;
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected(
            Error(TextureStorageTopologyDebugImageErrorCode::AllocationFailed));
    }
    catch (const std::length_error&)
    {
        return std::unexpected(
            Error(TextureStorageTopologyDebugImageErrorCode::AllocationFailed));
    }
}
} // namespace omega::runtime
