#include "front_end.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <new>
#include <stdexcept>
#include <string_view>
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

constexpr Color kBackgroundColor{std::byte{8U}, std::byte{12U}, std::byte{24U}, std::byte{255U}};
constexpr Color kCyanColor{std::byte{112U}, std::byte{220U}, std::byte{255U}, std::byte{255U}};
constexpr Color kSlateColor{std::byte{28U}, std::byte{38U}, std::byte{58U}, std::byte{255U}};
constexpr Color kAmberColor{std::byte{255U}, std::byte{196U}, std::byte{64U}, std::byte{255U}};

// Project-authored 3x5 block masks. Each row's three mask bits are read
// left-to-right after integer promotion; no font or external glyph data is
// involved. This table is also the complete ASCII allow-list used by
// profile-label projection.
constexpr std::array<Glyph, 44U> kProjectGlyphs{{
    {'A', {{0b010U, 0b101U, 0b111U, 0b101U, 0b101U}}}, {'B', {{0b110U, 0b101U, 0b110U, 0b101U, 0b110U}}},
    {'C', {{0b011U, 0b100U, 0b100U, 0b100U, 0b011U}}}, {'D', {{0b110U, 0b101U, 0b101U, 0b101U, 0b110U}}},
    {'E', {{0b111U, 0b100U, 0b110U, 0b100U, 0b111U}}}, {'F', {{0b111U, 0b100U, 0b110U, 0b100U, 0b100U}}},
    {'G', {{0b011U, 0b100U, 0b101U, 0b101U, 0b011U}}}, {'H', {{0b101U, 0b101U, 0b111U, 0b101U, 0b101U}}},
    {'I', {{0b111U, 0b010U, 0b010U, 0b010U, 0b111U}}}, {'J', {{0b001U, 0b001U, 0b001U, 0b101U, 0b010U}}},
    {'K', {{0b101U, 0b101U, 0b110U, 0b101U, 0b101U}}}, {'L', {{0b100U, 0b100U, 0b100U, 0b100U, 0b111U}}},
    {'M', {{0b101U, 0b111U, 0b111U, 0b101U, 0b101U}}}, {'N', {{0b101U, 0b111U, 0b111U, 0b111U, 0b101U}}},
    {'O', {{0b010U, 0b101U, 0b101U, 0b101U, 0b010U}}}, {'P', {{0b110U, 0b101U, 0b110U, 0b100U, 0b100U}}},
    {'Q', {{0b010U, 0b101U, 0b101U, 0b111U, 0b011U}}}, {'R', {{0b110U, 0b101U, 0b110U, 0b101U, 0b101U}}},
    {'S', {{0b011U, 0b100U, 0b010U, 0b001U, 0b110U}}}, {'T', {{0b111U, 0b010U, 0b010U, 0b010U, 0b010U}}},
    {'U', {{0b101U, 0b101U, 0b101U, 0b101U, 0b111U}}}, {'V', {{0b101U, 0b101U, 0b101U, 0b101U, 0b010U}}},
    {'W', {{0b101U, 0b101U, 0b101U, 0b111U, 0b101U}}}, {'X', {{0b101U, 0b101U, 0b010U, 0b101U, 0b101U}}},
    {'Y', {{0b101U, 0b101U, 0b010U, 0b010U, 0b010U}}}, {'Z', {{0b111U, 0b001U, 0b010U, 0b100U, 0b111U}}},
    {'0', {{0b111U, 0b101U, 0b101U, 0b101U, 0b111U}}}, {'1', {{0b010U, 0b110U, 0b010U, 0b010U, 0b111U}}},
    {'2', {{0b110U, 0b001U, 0b010U, 0b100U, 0b111U}}}, {'3', {{0b110U, 0b001U, 0b010U, 0b001U, 0b110U}}},
    {'4', {{0b101U, 0b101U, 0b111U, 0b001U, 0b001U}}}, {'5', {{0b111U, 0b100U, 0b110U, 0b001U, 0b110U}}},
    {'6', {{0b011U, 0b100U, 0b110U, 0b101U, 0b010U}}}, {'7', {{0b111U, 0b001U, 0b010U, 0b010U, 0b010U}}},
    {'8', {{0b010U, 0b101U, 0b010U, 0b101U, 0b010U}}}, {'9', {{0b010U, 0b101U, 0b011U, 0b001U, 0b110U}}},
    {'/', {{0b001U, 0b001U, 0b010U, 0b100U, 0b100U}}}, {'-', {{0b000U, 0b000U, 0b111U, 0b000U, 0b000U}}},
    {'.', {{0b000U, 0b000U, 0b000U, 0b000U, 0b010U}}}, {'+', {{0b000U, 0b010U, 0b111U, 0b010U, 0b000U}}},
    {'>', {{0b100U, 0b010U, 0b001U, 0b010U, 0b100U}}}, {'_', {{0b000U, 0b000U, 0b000U, 0b000U, 0b111U}}},
    {'?', {{0b110U, 0b001U, 0b010U, 0b000U, 0b010U}}}, {' ', {{0b000U, 0b000U, 0b000U, 0b000U, 0b000U}}},
}};

static_assert(kBorderPixels * 2U < kFrontEndImageWidth);
static_assert(kBorderPixels * 2U < kFrontEndImageHeight);

void SetPixel(runtime::DebugImage &image, const std::uint32_t x, const std::uint32_t y, const Color &color) noexcept
{
    const std::size_t offset = (static_cast<std::size_t>(y) * image.width + x) * color.size();
    for (std::size_t channel = 0U; channel < color.size(); ++channel)
        image.rgba8_pixels[offset + channel] = color[channel];
}

// Half-open integer rectangle. Every call below uses compile-time in-range
// coordinates.
void FillRectangle(runtime::DebugImage &image, const std::uint32_t left, const std::uint32_t top,
                   const std::uint32_t right, const std::uint32_t bottom, const Color &color) noexcept
{
    for (std::uint32_t y = top; y < bottom; ++y)
    {
        for (std::uint32_t x = left; x < right; ++x)
            SetPixel(image, x, y, color);
    }
}

void DrawGlyph(runtime::DebugImage &image, const GlyphRows &rows, const std::uint32_t origin_x,
               const std::uint32_t origin_y) noexcept
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
            FillRectangle(image, left, top, left + kGlyphScale, top + kGlyphScale, kCyanColor);
        }
    }
}

const GlyphRows &FindGlyph(const char symbol) noexcept
{
    for (const auto &glyph : kProjectGlyphs)
    {
        if (glyph.symbol == symbol)
            return glyph.rows;
    }
    return kProjectGlyphs[kProjectGlyphs.size() - 2U].rows;
}

[[nodiscard]] bool IsSupportedProjectFontAscii(const char symbol) noexcept
{
    return std::ranges::any_of(kProjectGlyphs,
                               [symbol](const Glyph &glyph) noexcept { return glyph.symbol == symbol; });
}

void DrawText(runtime::DebugImage &image, const std::string_view label, const std::uint32_t origin_x,
              const std::uint32_t origin_y) noexcept
{
    for (std::size_t index = 0U; index < label.size(); ++index)
    {
        DrawGlyph(image, FindGlyph(label[index]), origin_x + static_cast<std::uint32_t>(index) * kGlyphAdvance,
                  origin_y);
    }
}

template <std::size_t Size>
void DrawLabel(runtime::DebugImage &image, const char (&label)[Size], const std::uint32_t origin_x,
               const std::uint32_t origin_y) noexcept
{
    static_assert(Size > 0U);
    DrawText(image, std::string_view(label, Size - 1U), origin_x, origin_y);
}

void DrawLabel(runtime::DebugImage &image, const FrontEndLabel &label, const std::uint32_t origin_x,
               const std::uint32_t origin_y) noexcept
{
    DrawText(image, std::string_view(label.cells.data(), std::min<std::size_t>(label.length, label.cells.size())),
             origin_x, origin_y);
}

void DrawUnsigned(runtime::DebugImage &image, const std::uint16_t value, const std::uint32_t origin_x,
                  const std::uint32_t origin_y) noexcept
{
    std::array<char, 5U> digits{};
    const auto converted = std::to_chars(digits.data(), digits.data() + digits.size(), value);
    if (converted.ec == std::errc{})
    {
        DrawText(image, std::string_view(digits.data(), static_cast<std::size_t>(converted.ptr - digits.data())),
                 origin_x, origin_y);
    }
}

[[nodiscard]] std::uint32_t DecodeUtf8Scalar(const std::string_view text, std::size_t &cursor) noexcept
{
    const auto *const bytes = reinterpret_cast<const unsigned char *>(text.data());
    const std::uint8_t first = bytes[cursor++];
    if (first <= 0x7fU)
        return first;

    std::size_t continuation_count = 0U;
    std::uint32_t scalar = 0U;
    std::uint32_t minimum = 0U;
    if (first >= 0xc2U && first <= 0xdfU)
    {
        continuation_count = 1U;
        scalar = first & 0x1fU;
        minimum = 0x80U;
    }
    else if (first >= 0xe0U && first <= 0xefU)
    {
        continuation_count = 2U;
        scalar = first & 0x0fU;
        minimum = 0x800U;
    }
    else if (first >= 0xf0U && first <= 0xf4U)
    {
        continuation_count = 3U;
        scalar = first & 0x07U;
        minimum = 0x10000U;
    }
    else
    {
        return 0xfffdU;
    }

    if (continuation_count > text.size() - cursor)
        return 0xfffdU;
    const std::size_t continuation_begin = cursor;
    for (std::size_t index = 0U; index < continuation_count; ++index)
    {
        const std::uint8_t next = bytes[cursor];
        if (next < 0x80U || next > 0xbfU)
        {
            cursor = continuation_begin;
            return 0xfffdU;
        }
        scalar = (scalar << 6U) | (next & 0x3fU);
        ++cursor;
    }
    if (scalar < minimum || scalar > 0x10ffffU || (scalar >= 0xd800U && scalar <= 0xdfffU))
    {
        cursor = continuation_begin;
        return 0xfffdU;
    }
    return scalar;
}

[[nodiscard]] FrontEndLabel ProjectLabel(const std::string_view text) noexcept
{
    FrontEndLabel result{};
    std::size_t cursor = 0U;
    while (cursor < text.size())
    {
        const std::uint32_t scalar = DecodeUtf8Scalar(text, cursor);
        if (result.length == result.cells.size())
        {
            result.truncated = true;
            break;
        }

        char cell = '?';
        if (scalar <= 0x7fU)
        {
            cell = static_cast<char>(scalar);
            if (cell >= 'a' && cell <= 'z')
                cell = static_cast<char>(cell - 'a' + 'A');
            if (!IsSupportedProjectFontAscii(cell))
                cell = '?';
        }
        result.cells[result.length++] = cell;
    }
    return result;
}

[[nodiscard]] runtime::DebugImage BuildDiagnosticCardBase()
{
    constexpr std::size_t output_bytes = static_cast<std::size_t>(kFrontEndImageWidth) *
                                         static_cast<std::size_t>(kFrontEndImageHeight) * kChannelsPerPixel;

    runtime::DebugImage image{
        .width = kFrontEndImageWidth,
        .height = kFrontEndImageHeight,
        .rgba8_pixels = std::vector<std::byte>(output_bytes),
    };

    FillRectangle(image, 0U, 0U, image.width, image.height, kBackgroundColor);

    // Fully opaque two-pixel frame.
    FillRectangle(image, 0U, 0U, image.width, kBorderPixels, kCyanColor);
    FillRectangle(image, 0U, image.height - kBorderPixels, image.width, image.height, kCyanColor);
    FillRectangle(image, 0U, kBorderPixels, kBorderPixels, image.height - kBorderPixels, kCyanColor);
    FillRectangle(image, image.width - kBorderPixels, kBorderPixels, image.width, image.height - kBorderPixels,
                  kCyanColor);
    return image;
}

void DrawOpenOmegaHeader(runtime::DebugImage &image) noexcept
{
    FillRectangle(image, 6U, 6U, 122U, 20U, kSlateColor);
    DrawLabel(image, "OPEN", 8U, 8U);
    DrawLabel(image, "OMEGA", 8U, 14U);
    FillRectangle(image, 36U, 10U, 116U, 16U, kAmberColor);
}

[[nodiscard]] std::size_t TopologyPayloadSize(const std::uint32_t width, const std::uint32_t height,
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

[[nodiscard]] asset::TextureStoragePlaneIR MakeTopologyPlane(const asset::TextureTransferElementEncoding encoding,
                                                             const std::uint32_t width = 1U,
                                                             const std::uint32_t height = 1U)
{
    return asset::TextureStoragePlaneIR{
        .width = width,
        .height = height,
        .element_encoding = encoding,
        .bytes = std::vector<std::byte>(TopologyPayloadSize(width, height, encoding), std::byte{0x5aU}),
    };
}

[[nodiscard]] asset::TexturePaletteStorageIR MakeTopologyPalette(const std::uint32_t width, const std::uint32_t height)
{
    asset::TexturePaletteStorageIR palette{
        .width = width,
        .height = height,
        .entries = std::vector<std::array<std::byte, 4U>>(static_cast<std::size_t>(width) * height),
    };
    for (auto &entry : palette.entries)
    {
        entry = {std::byte{0x35U}, std::byte{0x71U}, std::byte{0xa4U}, std::byte{0xffU}};
    }
    return palette;
}

[[nodiscard]] asset::TextureStorageIR MakeProjectDiagnosticTopologyStorage()
{
    asset::TextureStorageBlockIR first{
        .planes =
            {
                MakeTopologyPlane(asset::TextureTransferElementEncoding::Packed4, 3U, 3U),
                MakeTopologyPlane(asset::TextureTransferElementEncoding::Packed8, 2U, 3U),
                MakeTopologyPlane(asset::TextureTransferElementEncoding::Packed24, 2U, 2U),
                MakeTopologyPlane(asset::TextureTransferElementEncoding::Packed32, 2U, 2U),
            },
        .palette = MakeTopologyPalette(2U, 2U),
    };
    asset::TextureStorageBlockIR second{
        .planes =
            {
                MakeTopologyPlane(asset::TextureTransferElementEncoding::Packed32),
                MakeTopologyPlane(asset::TextureTransferElementEncoding::Packed4, 3U, 1U),
            },
    };
    asset::TextureStorageBlockIR third{
        .planes =
            {
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

[[nodiscard]] constexpr runtime::TextureStorageTopologyDebugImageError TopologyAllocationError() noexcept
{
    constexpr auto code = runtime::TextureStorageTopologyDebugImageErrorCode::AllocationFailed;
    return runtime::TextureStorageTopologyDebugImageError{
        .code = code,
        .message = runtime::TextureStorageTopologyDebugImageErrorMessage(code),
    };
}
} // namespace

std::expected<FrontEndStartupModel, FrontEndModelError> MakeFrontEndStartupModel(
    const std::span<const profiles::ProfileSummary> summaries) noexcept
{
    if (summaries.size() > kFrontEndMaximumProfiles)
        return std::unexpected(FrontEndModelError::TooManyProfiles);

    for (std::size_t index = 1U; index < summaries.size(); ++index)
    {
        if (!(summaries[index - 1U].id < summaries[index].id))
            return std::unexpected(FrontEndModelError::UnsortedProfiles);
    }

    FrontEndStartupModel model{
        .total_profiles = static_cast<std::uint16_t>(summaries.size()),
        .visible_profiles =
            static_cast<std::uint8_t>(std::min<std::size_t>(summaries.size(), kFrontEndVisibleProfiles)),
    };
    for (std::size_t index = 0U; index < model.visible_profiles; ++index)
    {
        model.profiles[index] = FrontEndProfile{
            .id = summaries[index].id,
            .label = ProjectLabel(summaries[index].metadata.display_name),
        };
    }
    return model;
}

runtime::DebugImage BuildProjectFrontEndMainImage(const runtime::ContentStartupStage content_stage,
                                                  const std::uint16_t profile_count)
{
    runtime::DebugImage image = BuildDiagnosticCardBase();

    DrawOpenOmegaHeader(image);
    DrawLabel(image, "CONTENT", 8U, 22U);
    switch (content_stage)
    {
    case runtime::ContentStartupStage::DataMounted:
        DrawLabel(image, "DATA", 40U, 22U);
        break;
    case runtime::ContentStartupStage::LevelContent:
        DrawLabel(image, "LEVEL", 40U, 22U);
        break;
    case runtime::ContentStartupStage::NoContent:
    default:
        DrawLabel(image, "NONE", 40U, 22U);
        break;
    }
    DrawLabel(image, "PROFILES", 68U, 22U);
    DrawUnsigned(image, profile_count, 104U, 22U);

    constexpr std::array row_tops{28U, 38U, 48U, 58U};
    constexpr std::array<std::string_view, kFrontEndMainRowCount> row_labels{"START DIAGNOSTIC", "PROFILES", "CONTROLS",
                                                                             "ASSET TOPOLOGY"};
    for (std::size_t row = 0U; row < row_tops.size(); ++row)
    {
        FillRectangle(image, 8U, row_tops[row], 120U, row_tops[row] + 8U, kSlateColor);
        FillRectangle(image, 8U, row_tops[row], 12U, row_tops[row] + 8U, kCyanColor);
        DrawText(image, row_labels[row], 16U, row_tops[row] + 1U);
    }

    return image;
}

runtime::DebugImage BuildProjectFrontEndProfilesImage(const FrontEndStartupModel &profiles)
{
    runtime::DebugImage image = BuildDiagnosticCardBase();
    DrawOpenOmegaHeader(image);
    DrawLabel(image, "NATIVE PROFILES", 38U, 11U);

    FillRectangle(image, 8U, 24U, 120U, 57U, kSlateColor);
    if (profiles.visible_profiles == 0U || profiles.total_profiles == 0U)
    {
        DrawLabel(image, "NO NATIVE PROFILES", 28U, 38U);
    }
    else
    {
        constexpr std::array label_rows{28U, 38U, 48U};
        const std::size_t visible = std::min<std::size_t>(profiles.visible_profiles, kFrontEndVisibleProfiles);
        for (std::size_t index = 0U; index < visible; ++index)
        {
            DrawLabel(image, profiles.profiles[index].label, 16U, label_rows[index]);
            if (profiles.profiles[index].label.truncated)
                DrawLabel(image, ">", 113U, label_rows[index]);
        }
    }

    FillRectangle(image, 8U, 59U, 120U, 68U, kSlateColor);
    if (profiles.visible_profiles == 0U || profiles.total_profiles == 0U)
    {
        DrawLabel(image, "F1/ENTER RETURN", 12U, 61U);
    }
    else if (profiles.total_profiles > profiles.visible_profiles)
    {
        DrawLabel(image, "+", 12U, 61U);
        DrawUnsigned(image, static_cast<std::uint16_t>(profiles.total_profiles - profiles.visible_profiles), 16U, 61U);
        DrawLabel(image, " MORE", 32U, 61U);
        DrawLabel(image, "SELECT", 88U, 61U);
    }
    else
    {
        DrawLabel(image, "F1/ENTER SELECT", 12U, 61U);
    }
    return image;
}

runtime::DebugImage BuildProjectFrontEndDiagnosticPlayImage()
{
    runtime::DebugImage image = BuildDiagnosticCardBase();

    DrawOpenOmegaHeader(image);
    DrawLabel(image, "DIAGNOSTIC PLAY", 34U, 24U);

    FillRectangle(image, 8U, 34U, 120U, 52U, kSlateColor);
    DrawLabel(image, "NO LEVEL IMAGE", 36U, 40U);

    FillRectangle(image, 8U, 56U, 120U, 68U, kSlateColor);
    DrawLabel(image, "F1/ENTER MENU", 12U, 59U);
    DrawLabel(image, "ESC QUIT", 84U, 59U);
    return image;
}

runtime::DebugImage BuildProjectFrontEndControlsImage()
{
    runtime::DebugImage image = BuildDiagnosticCardBase();

    DrawOpenOmegaHeader(image);
    DrawLabel(image, "CONTROLS", 42U, 11U);

    FillRectangle(image, 8U, 24U, 120U, 52U, kSlateColor);
    DrawLabel(image, "W/UP FORWARD", 12U, 25U);
    DrawLabel(image, "S/DOWN REVERSE", 12U, 32U);
    DrawLabel(image, "A LEFT", 12U, 39U);
    DrawLabel(image, "D RIGHT", 12U, 46U);

    FillRectangle(image, 8U, 54U, 120U, 68U, kSlateColor);
    DrawLabel(image, "F1/ENTER RETURN", 12U, 55U);
    DrawLabel(image, "ESC QUIT", 12U, 62U);
    return image;
}

std::expected<runtime::DebugImage, runtime::TextureStorageTopologyDebugImageError>
BuildProjectFrontEndAssetTopologyImage()
{
    try
    {
        const asset::TextureStorageIR storage = MakeProjectDiagnosticTopologyStorage();
        return runtime::BuildTextureStorageTopologyDebugImage(storage);
    }
    catch (const std::bad_alloc &)
    {
        return std::unexpected(TopologyAllocationError());
    }
    catch (const std::length_error &)
    {
        return std::unexpected(TopologyAllocationError());
    }
}
} // namespace omega::app
