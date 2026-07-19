#include "diagnostic_menu.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace omega::app
{
namespace
{
using Color = std::array<std::byte, 4U>;

constexpr std::uint32_t kChannelsPerPixel = 4U;
constexpr std::uint32_t kBorderPixels = 2U;
constexpr std::uint32_t kGlyphScale = 2U;
constexpr std::uint32_t kGlyphWidth = 3U;
constexpr std::uint32_t kGlyphHeight = 5U;
constexpr std::uint32_t kGlyphAdvance = 8U;

constexpr Color kBackgroundColor{
    std::byte{8U}, std::byte{12U}, std::byte{24U}, std::byte{255U}};
constexpr Color kCyanColor{
    std::byte{112U}, std::byte{220U}, std::byte{255U}, std::byte{255U}};
constexpr Color kSlateColor{
    std::byte{28U}, std::byte{38U}, std::byte{58U}, std::byte{255U}};
constexpr Color kAmberColor{
    std::byte{255U}, std::byte{196U}, std::byte{64U}, std::byte{255U}};

// Project-authored 3x5 block masks for the literal diagnostic marker "DEV". Each row's three mask
// bits are read left-to-right after integer promotion; no font or external glyph data is involved.
constexpr std::array<std::array<std::uint8_t, kGlyphHeight>, 3U> kDevGlyphs{{
    {{0b110U, 0b101U, 0b101U, 0b101U, 0b110U}},
    {{0b111U, 0b100U, 0b110U, 0b100U, 0b111U}},
    {{0b101U, 0b101U, 0b101U, 0b101U, 0b010U}},
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
    const std::array<std::uint8_t, kGlyphHeight>& rows,
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
} // namespace

runtime::DebugImage BuildProjectDiagnosticMenuImage()
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

    // Synthetic title region: a literal DEV marker and an inert amber status bar.
    FillRectangle(image, 6U, 6U, 122U, 20U, kSlateColor);
    std::uint32_t glyph_x = 8U;
    for (const auto& glyph : kDevGlyphs)
    {
        DrawGlyph(image, glyph, glyph_x, 8U);
        glyph_x += kGlyphAdvance;
    }
    FillRectangle(image, 36U, 10U, 116U, 16U, kAmberColor);

    // Three project-owned geometric rows. They deliberately carry no item labels, selection,
    // activation, navigation, or retail-menu meaning.
    FillRectangle(image, 8U, 28U, 120U, 38U, kSlateColor);
    FillRectangle(image, 8U, 28U, 12U, 38U, kCyanColor);
    FillRectangle(image, 8U, 43U, 104U, 53U, kSlateColor);
    FillRectangle(image, 8U, 43U, 12U, 53U, kCyanColor);
    FillRectangle(image, 8U, 58U, 88U, 68U, kSlateColor);
    FillRectangle(image, 8U, 58U, 12U, 68U, kCyanColor);

    return image;
}
} // namespace omega::app
