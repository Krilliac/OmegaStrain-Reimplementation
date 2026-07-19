#include "diagnostic_menu.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <new>
#include <stdexcept>
#include <utility>
#include <vector>

namespace omega::app
{
namespace
{
using Color = std::array<std::byte, 4U>;

constexpr std::uint32_t kChannelsPerPixel = 4U;
constexpr std::uint32_t kBorderPixels = 2U;
constexpr std::uint32_t kGlyphScale = 1U;
constexpr std::uint32_t kGlyphWidth = 3U;
constexpr std::uint32_t kGlyphHeight = 5U;
constexpr std::uint32_t kGlyphAdvance = 4U;

using GlyphRows = std::array<std::uint8_t, kGlyphHeight>;

struct Glyph
{
    char symbol;
    GlyphRows rows;
};

constexpr Color kBackgroundColor{
    std::byte{8U}, std::byte{12U}, std::byte{24U}, std::byte{255U}};
constexpr Color kCyanColor{
    std::byte{112U}, std::byte{220U}, std::byte{255U}, std::byte{255U}};
constexpr Color kSlateColor{
    std::byte{28U}, std::byte{38U}, std::byte{58U}, std::byte{255U}};
constexpr Color kAmberColor{
    std::byte{255U}, std::byte{196U}, std::byte{64U}, std::byte{255U}};

// Project-authored 3x5 block masks for the diagnostic labels below. Each row's three mask bits are
// read left-to-right after integer promotion; no font or external glyph data is involved.
constexpr std::array<Glyph, 25U> kDiagnosticGlyphs{{
    {'A', {{0b010U, 0b101U, 0b111U, 0b101U, 0b101U}}},
    {'C', {{0b011U, 0b100U, 0b100U, 0b100U, 0b011U}}},
    {'D', {{0b110U, 0b101U, 0b101U, 0b101U, 0b110U}}},
    {'E', {{0b111U, 0b100U, 0b110U, 0b100U, 0b111U}}},
    {'F', {{0b111U, 0b100U, 0b110U, 0b100U, 0b100U}}},
    {'G', {{0b011U, 0b100U, 0b101U, 0b101U, 0b011U}}},
    {'H', {{0b101U, 0b101U, 0b111U, 0b101U, 0b101U}}},
    {'I', {{0b111U, 0b010U, 0b010U, 0b010U, 0b111U}}},
    {'L', {{0b100U, 0b100U, 0b100U, 0b100U, 0b111U}}},
    {'M', {{0b101U, 0b111U, 0b111U, 0b101U, 0b101U}}},
    {'N', {{0b101U, 0b111U, 0b111U, 0b111U, 0b101U}}},
    {'O', {{0b010U, 0b101U, 0b101U, 0b101U, 0b010U}}},
    {'P', {{0b110U, 0b101U, 0b110U, 0b100U, 0b100U}}},
    {'Q', {{0b010U, 0b101U, 0b101U, 0b111U, 0b011U}}},
    {'R', {{0b110U, 0b101U, 0b110U, 0b101U, 0b101U}}},
    {'S', {{0b011U, 0b100U, 0b010U, 0b001U, 0b110U}}},
    {'T', {{0b111U, 0b010U, 0b010U, 0b010U, 0b010U}}},
    {'U', {{0b101U, 0b101U, 0b101U, 0b101U, 0b111U}}},
    {'V', {{0b101U, 0b101U, 0b101U, 0b101U, 0b010U}}},
    {'W', {{0b101U, 0b101U, 0b101U, 0b111U, 0b101U}}},
    {'Y', {{0b101U, 0b101U, 0b010U, 0b010U, 0b010U}}},
    {'1', {{0b010U, 0b110U, 0b010U, 0b010U, 0b111U}}},
    {'2', {{0b110U, 0b001U, 0b010U, 0b100U, 0b111U}}},
    {'/', {{0b001U, 0b001U, 0b010U, 0b100U, 0b100U}}},
    {' ', {{0b000U, 0b000U, 0b000U, 0b000U, 0b000U}}},
}};

static_assert(kBorderPixels * 2U < kDiagnosticMenuImageWidth);
static_assert(kBorderPixels * 2U < kDiagnosticMenuImageHeight);

void SetPixel(runtime::DebugImage& image, const std::uint32_t x,
    const std::uint32_t y, const Color& color) noexcept
{
    const std::size_t offset =
        (static_cast<std::size_t>(y) * image.width + x) * color.size();
    for (std::size_t channel = 0U; channel < color.size(); ++channel)
        image.rgba8_pixels[offset + channel] = color[channel];
}

// Half-open integer rectangle. Every call below uses compile-time in-range coordinates.
void FillRectangle(runtime::DebugImage& image, const std::uint32_t left,
    const std::uint32_t top, const std::uint32_t right,
    const std::uint32_t bottom, const Color& color) noexcept
{
    for (std::uint32_t y = top; y < bottom; ++y)
    {
        for (std::uint32_t x = left; x < right; ++x)
            SetPixel(image, x, y, color);
    }
}

void DrawGlyph(runtime::DebugImage& image,
    const GlyphRows& rows,
    const std::uint32_t origin_x, const std::uint32_t origin_y) noexcept
{
    for (std::uint32_t row = 0U; row < kGlyphHeight; ++row)
    {
        for (std::uint32_t column = 0U; column < kGlyphWidth; ++column)
        {
            const std::uint32_t bit = 1U << (kGlyphWidth - 1U - column);
            if ((static_cast<std::uint32_t>(rows[row]) & bit) == 0U)
                continue;

            const std::uint32_t left = origin_x + column * kGlyphScale;
            const std::uint32_t top = origin_y + row * kGlyphScale;
            FillRectangle(image, left, top, left + kGlyphScale,
                top + kGlyphScale, kCyanColor);
        }
    }
}

const GlyphRows& FindGlyph(const char symbol) noexcept
{
    for (const auto& glyph : kDiagnosticGlyphs)
    {
        if (glyph.symbol == symbol)
            return glyph.rows;
    }
    return kDiagnosticGlyphs.back().rows;
}

template <std::size_t Size>
void DrawLabel(runtime::DebugImage& image, const char (&label)[Size],
    const std::uint32_t origin_x, const std::uint32_t origin_y) noexcept
{
    static_assert(Size > 0U);
    for (std::size_t index = 0U; index + 1U < Size; ++index)
    {
        DrawGlyph(image, FindGlyph(label[index]),
            origin_x + static_cast<std::uint32_t>(index) * kGlyphAdvance,
            origin_y);
    }
}

[[nodiscard]] runtime::DebugImage BuildDiagnosticCardBase()
{
    constexpr std::size_t output_bytes =
        static_cast<std::size_t>(kDiagnosticMenuImageWidth) *
        static_cast<std::size_t>(kDiagnosticMenuImageHeight) * kChannelsPerPixel;

    runtime::DebugImage image{
        .width = kDiagnosticMenuImageWidth,
        .height = kDiagnosticMenuImageHeight,
        .rgba8_pixels = std::vector<std::byte>(output_bytes),
    };

    FillRectangle(image, 0U, 0U, image.width, image.height, kBackgroundColor);

    // Fully opaque two-pixel frame.
    FillRectangle(image, 0U, 0U, image.width, kBorderPixels, kCyanColor);
    FillRectangle(image, 0U, image.height - kBorderPixels,
        image.width, image.height, kCyanColor);
    FillRectangle(image, 0U, kBorderPixels, kBorderPixels,
        image.height - kBorderPixels, kCyanColor);
    FillRectangle(image, image.width - kBorderPixels, kBorderPixels,
        image.width, image.height - kBorderPixels, kCyanColor);
    return image;
}

void DrawOpenOmegaHeader(runtime::DebugImage& image) noexcept
{
    FillRectangle(image, 6U, 6U, 122U, 20U, kSlateColor);
    DrawLabel(image, "OPEN", 8U, 8U);
    DrawLabel(image, "OMEGA", 8U, 14U);
    FillRectangle(image, 36U, 10U, 116U, 16U, kAmberColor);
}

[[nodiscard]] std::size_t TopologyPayloadSize(const std::uint32_t width,
    const std::uint32_t height,
    const asset::TextureTransferElementEncoding encoding) noexcept
{
    const std::size_t area = static_cast<std::size_t>(width) * height;
    switch (encoding)
    {
    case asset::TextureTransferElementEncoding::Packed4:
        return area / 2U + area % 2U;
    case asset::TextureTransferElementEncoding::Packed8:
        return area;
    case asset::TextureTransferElementEncoding::Packed24:
        return area * 3U;
    case asset::TextureTransferElementEncoding::Packed32:
        return area * 4U;
    }
    return 0U;
}

[[nodiscard]] asset::TextureStoragePlaneIR MakeTopologyPlane(
    const asset::TextureTransferElementEncoding encoding,
    const std::uint32_t width = 1U, const std::uint32_t height = 1U)
{
    return asset::TextureStoragePlaneIR{
        .width = width,
        .height = height,
        .element_encoding = encoding,
        .bytes = std::vector<std::byte>(
            TopologyPayloadSize(width, height, encoding), std::byte{0x5aU}),
    };
}

[[nodiscard]] asset::TexturePaletteStorageIR MakeTopologyPalette(
    const std::uint32_t width, const std::uint32_t height)
{
    asset::TexturePaletteStorageIR palette{
        .width = width,
        .height = height,
        .entries = std::vector<std::array<std::byte, 4U>>(
            static_cast<std::size_t>(width) * height),
    };
    for (auto& entry : palette.entries)
    {
        entry = {std::byte{0x35U}, std::byte{0x71U},
            std::byte{0xa4U}, std::byte{0xffU}};
    }
    return palette;
}

[[nodiscard]] asset::TextureStorageIR MakeProjectDiagnosticTopologyStorage()
{
    asset::TextureStorageBlockIR first{
        .planes = {
            MakeTopologyPlane(asset::TextureTransferElementEncoding::Packed4, 3U, 3U),
            MakeTopologyPlane(asset::TextureTransferElementEncoding::Packed8, 2U, 3U),
            MakeTopologyPlane(asset::TextureTransferElementEncoding::Packed24, 2U, 2U),
            MakeTopologyPlane(asset::TextureTransferElementEncoding::Packed32, 2U, 2U),
        },
        .palette = MakeTopologyPalette(2U, 2U),
    };
    asset::TextureStorageBlockIR second{
        .planes = {
            MakeTopologyPlane(asset::TextureTransferElementEncoding::Packed32),
            MakeTopologyPlane(asset::TextureTransferElementEncoding::Packed4, 3U, 1U),
        },
    };
    asset::TextureStorageBlockIR third{
        .planes = {
            MakeTopologyPlane(asset::TextureTransferElementEncoding::Packed8, 3U, 2U),
        },
        .palette = MakeTopologyPalette(1U, 2U),
    };
    return asset::TextureStorageIR{
        .width = 64U,
        .height = 32U,
        .sample_encoding = asset::TextureSampleEncoding::Indexed8,
        .blocks = {std::move(first), std::move(second), std::move(third)},
    };
}

[[nodiscard]] constexpr runtime::TextureStorageTopologyDebugImageError
TopologyAllocationError() noexcept
{
    constexpr auto code =
        runtime::TextureStorageTopologyDebugImageErrorCode::AllocationFailed;
    return runtime::TextureStorageTopologyDebugImageError{
        .code = code,
        .message = runtime::TextureStorageTopologyDebugImageErrorMessage(code),
    };
}
} // namespace

runtime::DebugImage BuildProjectDiagnosticMenuImage()
{
    runtime::DebugImage image = BuildDiagnosticCardBase();

    // Synthetic title region and fixed control legend.
    DrawOpenOmegaHeader(image);
    DrawLabel(image, "W/S SELECT", 8U, 22U);
    DrawLabel(image, "F1 START", 52U, 22U);
    DrawLabel(image, "ESC QUIT", 88U, 22U);

    // Three project-owned diagnostic rows. Selection remains a host-side geometric overlay.
    FillRectangle(image, 8U, 28U, 120U, 38U, kSlateColor);
    FillRectangle(image, 8U, 28U, 12U, 38U, kCyanColor);
    DrawLabel(image, "START DIAGNOSTIC", 16U, 30U);
    FillRectangle(image, 8U, 43U, 104U, 53U, kSlateColor);
    FillRectangle(image, 8U, 43U, 12U, 53U, kCyanColor);
    DrawLabel(image, "CONTROLS", 16U, 45U);
    FillRectangle(image, 8U, 58U, 88U, 68U, kSlateColor);
    FillRectangle(image, 8U, 58U, 12U, 68U, kCyanColor);
    DrawLabel(image, "ASSET TOPOLOGY", 16U, 60U);

    return image;
}

runtime::DebugImage BuildProjectDiagnosticControlsImage()
{
    runtime::DebugImage image = BuildDiagnosticCardBase();

    DrawOpenOmegaHeader(image);
    DrawLabel(image, "CONTROLS", 42U, 11U);

    FillRectangle(image, 8U, 24U, 120U, 52U, kSlateColor);
    DrawLabel(image, "W FORWARD", 12U, 25U);
    DrawLabel(image, "S REVERSE", 12U, 32U);
    DrawLabel(image, "A LEFT", 12U, 39U);
    DrawLabel(image, "D RIGHT", 12U, 46U);

    FillRectangle(image, 8U, 54U, 120U, 68U, kSlateColor);
    DrawLabel(image, "F1 RETURN", 12U, 55U);
    DrawLabel(image, "ESC QUIT", 12U, 62U);
    return image;
}

std::expected<runtime::DebugImage,
    runtime::TextureStorageTopologyDebugImageError>
BuildProjectDiagnosticAssetTopologyImage()
{
    try
    {
        const asset::TextureStorageIR storage =
            MakeProjectDiagnosticTopologyStorage();
        return runtime::BuildTextureStorageTopologyDebugImage(storage);
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected(TopologyAllocationError());
    }
    catch (const std::length_error&)
    {
        return std::unexpected(TopologyAllocationError());
    }
}
} // namespace omega::app
