#include "omega/runtime/texture_storage_topology_debug_image.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <iostream>
#include <limits>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
using omega::asset::TexturePaletteStorageIR;
using omega::asset::TextureSampleEncoding;
using omega::asset::TextureStorageBlockIR;
using omega::asset::TextureStorageIR;
using omega::asset::TextureStoragePlaneIR;
using omega::asset::TextureTransferElementEncoding;
using omega::runtime::DebugImage;
using omega::runtime::TextureStorageTopologyDebugImageError;
using omega::runtime::TextureStorageTopologyDebugImageErrorCode;
using omega::runtime::TextureStorageTopologyDebugImageLimits;

constexpr std::uint32_t kTilePixels = 32U;
constexpr std::array kBackgroundColor{
    std::byte{8}, std::byte{12}, std::byte{24}, std::byte{255}};
constexpr std::array kBorderColor{
    std::byte{28}, std::byte{38}, std::byte{58}, std::byte{255}};
constexpr std::array kTopologyColor{
    std::byte{112}, std::byte{220}, std::byte{255}, std::byte{255}};
constexpr std::array kPaletteColor{
    std::byte{255}, std::byte{196}, std::byte{64}, std::byte{255}};

int failures = 0;

void Check(const bool condition, const std::string_view message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

[[nodiscard]] std::size_t PayloadSize(const std::uint32_t width,
    const std::uint32_t height, const TextureTransferElementEncoding encoding)
{
    const std::size_t area = static_cast<std::size_t>(width) * height;
    switch (encoding)
    {
    case TextureTransferElementEncoding::Packed4:
        return area / 2U + area % 2U;
    case TextureTransferElementEncoding::Packed8:
        return area;
    case TextureTransferElementEncoding::Packed24:
        return area * 3U;
    case TextureTransferElementEncoding::Packed32:
        return area * 4U;
    }
    return 0U;
}

[[nodiscard]] TextureStoragePlaneIR MakePlane(const TextureTransferElementEncoding encoding,
    const std::uint32_t width = 1U, const std::uint32_t height = 1U,
    const std::byte fill = std::byte{0x5a})
{
    return TextureStoragePlaneIR{
        .width = width,
        .height = height,
        .element_encoding = encoding,
        .bytes = std::vector<std::byte>(PayloadSize(width, height, encoding), fill),
    };
}

[[nodiscard]] TexturePaletteStorageIR MakePalette(const std::uint32_t width,
    const std::uint32_t height, const std::byte fill = std::byte{0x35})
{
    TexturePaletteStorageIR palette{
        .width = width,
        .height = height,
        .entries = std::vector<std::array<std::byte, 4>>(
            static_cast<std::size_t>(width) * height),
    };
    for (auto& entry : palette.entries)
        entry = {fill, std::byte{0x71}, std::byte{0xa4}, std::byte{0xff}};
    return palette;
}

[[nodiscard]] TextureStorageBlockIR MakeMinimalBlock()
{
    return TextureStorageBlockIR{
        .planes = {MakePlane(TextureTransferElementEncoding::Packed8)},
    };
}

[[nodiscard]] TextureStorageIR MakeMinimalStorage()
{
    return TextureStorageIR{
        .width = 1U,
        .height = 1U,
        .sample_encoding = TextureSampleEncoding::Indexed4,
        .blocks = {MakeMinimalBlock()},
    };
}

[[nodiscard]] TextureStorageIR MakeCanonicalStorage()
{
    TextureStorageBlockIR first{
        .planes = {
            MakePlane(TextureTransferElementEncoding::Packed4, 3U, 3U),
            MakePlane(TextureTransferElementEncoding::Packed8, 2U, 3U),
            MakePlane(TextureTransferElementEncoding::Packed24, 2U, 2U),
            MakePlane(TextureTransferElementEncoding::Packed32, 2U, 2U),
        },
        .palette = MakePalette(2U, 2U),
    };
    TextureStorageBlockIR second{
        .planes = {
            MakePlane(TextureTransferElementEncoding::Packed32),
            MakePlane(TextureTransferElementEncoding::Packed4, 3U, 1U),
        },
    };
    TextureStorageBlockIR third{
        .planes = {MakePlane(TextureTransferElementEncoding::Packed8, 3U, 2U)},
        .palette = MakePalette(1U, 2U),
    };
    return TextureStorageIR{
        .width = 64U,
        .height = 32U,
        .sample_encoding = TextureSampleEncoding::Indexed8,
        .blocks = {std::move(first), std::move(second), std::move(third)},
    };
}

[[nodiscard]] std::array<std::byte, 4> PixelAt(
    const DebugImage& image, const std::uint32_t x, const std::uint32_t y)
{
    const std::size_t offset = (static_cast<std::size_t>(y) * image.width + x) * 4U;
    return {image.rgba8_pixels[offset], image.rgba8_pixels[offset + 1U],
        image.rgba8_pixels[offset + 2U], image.rgba8_pixels[offset + 3U]};
}

[[nodiscard]] std::size_t CountColor(
    const DebugImage& image, const std::array<std::byte, 4>& color)
{
    std::size_t count = 0U;
    for (std::uint32_t y = 0U; y < image.height; ++y)
    {
        for (std::uint32_t x = 0U; x < image.width; ++x)
            count += PixelAt(image, x, y) == color ? 1U : 0U;
    }
    return count;
}

[[nodiscard]] std::uint64_t Fnv1a64(const DebugImage& image) noexcept
{
    std::uint64_t hash = 14695981039346656037ULL;
    for (const std::byte value : image.rgba8_pixels)
    {
        hash ^= std::to_integer<std::uint8_t>(value);
        hash *= 1099511628211ULL;
    }
    return hash;
}

void CheckMask(const DebugImage& image, const std::uint32_t origin_x,
    const std::uint32_t origin_y, const std::uint8_t mask,
    const std::string_view message)
{
    bool matches = true;
    for (std::uint32_t bit = 0U; bit < 4U; ++bit)
    {
        const auto expected = (mask & static_cast<std::uint8_t>(1U << bit)) != 0U
                                  ? kTopologyColor
                                  : kBackgroundColor;
        matches = matches &&
                  PixelAt(image, origin_x + bit % 2U, origin_y + bit / 2U) == expected;
    }
    Check(matches, message);
}

void CheckError(const TextureStorageIR& storage,
    const TextureStorageTopologyDebugImageLimits& limits,
    const TextureStorageTopologyDebugImageErrorCode code,
    const std::string_view message)
{
    const std::expected<DebugImage, TextureStorageTopologyDebugImageError> result =
        omega::runtime::BuildTextureStorageTopologyDebugImage(storage, limits);
    Check(!result && result.error().code == code &&
              result.error().message ==
                  omega::runtime::TextureStorageTopologyDebugImageErrorMessage(code),
        message);
}

void CheckErrorContract()
{
    struct ErrorContract
    {
        TextureStorageTopologyDebugImageErrorCode code;
        std::string_view name;
        std::string_view message;
    };
    constexpr std::array contracts{
        ErrorContract{TextureStorageTopologyDebugImageErrorCode::InvalidTextureDimensions,
            "invalid-texture-dimensions",
            "texture storage topology image requires nonzero texture dimensions"},
        ErrorContract{TextureStorageTopologyDebugImageErrorCode::InvalidSampleEncoding,
            "invalid-sample-encoding",
            "texture storage topology image sample encoding is invalid"},
        ErrorContract{TextureStorageTopologyDebugImageErrorCode::EmptyBlockSet,
            "empty-block-set", "texture storage topology image requires at least one block"},
        ErrorContract{TextureStorageTopologyDebugImageErrorCode::BlockLimitExceeded,
            "block-limit-exceeded", "texture storage topology image exceeds the block limit"},
        ErrorContract{TextureStorageTopologyDebugImageErrorCode::EmptyPlaneSet,
            "empty-plane-set",
            "texture storage topology image requires every block to contain a plane"},
        ErrorContract{TextureStorageTopologyDebugImageErrorCode::PlaneMarkerCapacityExceeded,
            "plane-marker-capacity-exceeded",
            "texture storage topology image exceeds the per-block plane-marker capacity"},
        ErrorContract{TextureStorageTopologyDebugImageErrorCode::PlaneLimitExceeded,
            "plane-limit-exceeded", "texture storage topology image exceeds the plane limit"},
        ErrorContract{TextureStorageTopologyDebugImageErrorCode::InvalidPlaneDimensions,
            "invalid-plane-dimensions",
            "texture storage topology image requires nonzero plane dimensions"},
        ErrorContract{TextureStorageTopologyDebugImageErrorCode::InvalidTransferElementEncoding,
            "invalid-transfer-element-encoding",
            "texture storage topology image transfer-element encoding is invalid"},
        ErrorContract{TextureStorageTopologyDebugImageErrorCode::PlaneByteSizeOverflow,
            "plane-byte-size-overflow",
            "texture storage topology image plane byte size overflows"},
        ErrorContract{TextureStorageTopologyDebugImageErrorCode::PlaneByteSizeMismatch,
            "plane-byte-size-mismatch",
            "texture storage topology image plane byte size does not match its rectangle"},
        ErrorContract{TextureStorageTopologyDebugImageErrorCode::InvalidPaletteDimensions,
            "invalid-palette-dimensions",
            "texture storage topology image requires nonzero palette dimensions"},
        ErrorContract{TextureStorageTopologyDebugImageErrorCode::PaletteEntryCountMismatch,
            "palette-entry-count-mismatch",
            "texture storage topology image palette entry count does not match its rectangle"},
        ErrorContract{TextureStorageTopologyDebugImageErrorCode::PaletteEntryLimitExceeded,
            "palette-entry-limit-exceeded",
            "texture storage topology image exceeds the palette-entry limit"},
        ErrorContract{TextureStorageTopologyDebugImageErrorCode::ImageDimensionOverflow,
            "image-dimension-overflow", "texture storage topology image dimensions overflow"},
        ErrorContract{TextureStorageTopologyDebugImageErrorCode::ImageByteSizeOverflow,
            "image-byte-size-overflow", "texture storage topology image byte size overflows"},
        ErrorContract{TextureStorageTopologyDebugImageErrorCode::OutputByteLimitExceeded,
            "output-byte-limit-exceeded",
            "texture storage topology image exceeds the output-byte limit"},
        ErrorContract{TextureStorageTopologyDebugImageErrorCode::AllocationFailed,
            "allocation-failed", "texture storage topology image allocation failed"},
    };
    for (const ErrorContract& contract : contracts)
    {
        Check(omega::runtime::TextureStorageTopologyDebugImageErrorCodeName(contract.code) ==
                  contract.name &&
              omega::runtime::TextureStorageTopologyDebugImageErrorMessage(contract.code) ==
                  contract.message,
            "every topology-image error has its frozen name and message");
        const TextureStorageTopologyDebugImageError error{
            .code = contract.code,
            .message =
                omega::runtime::TextureStorageTopologyDebugImageErrorMessage(contract.code),
        };
        Check(error.message == contract.message,
            "topology-image errors carry only fixed category text");
    }

    constexpr auto unknown =
        static_cast<TextureStorageTopologyDebugImageErrorCode>(255);
    Check(omega::runtime::TextureStorageTopologyDebugImageErrorCodeName(unknown) ==
                  "unknown" &&
              omega::runtime::TextureStorageTopologyDebugImageErrorMessage(unknown) ==
                  "texture storage topology image error is unknown",
        "unknown topology-image error values fail closed to fixed text");

    constexpr TextureStorageTopologyDebugImageLimits defaults{};
    Check(defaults.maximum_blocks == 4096U && defaults.maximum_planes == 262144U &&
              defaults.maximum_palette_entries == 1048576U &&
              defaults.maximum_output_bytes == 64ULL * 1024ULL * 1024ULL,
        "topology-image default diagnostic budgets are frozen");
}

void CheckCanonicalImage()
{
    TextureStorageIR canonical = MakeCanonicalStorage();
    const auto first = omega::runtime::BuildTextureStorageTopologyDebugImage(canonical);
    const auto second = omega::runtime::BuildTextureStorageTopologyDebugImage(canonical);
    Check(first && first->width == 96U && first->height == 32U &&
              first->rgba8_pixels.size() == 12288U &&
              first->pixels().size() == first->rgba8_pixels.size(),
        "the canonical three-block fixture produces the frozen owned 96x32 image");
    Check(first && second && first->rgba8_pixels == second->rgba8_pixels,
        "two topology-image builds are byte deterministic");
    if (!first)
        return;

    const std::size_t background = CountColor(*first, kBackgroundColor);
    const std::size_t border = CountColor(*first, kBorderColor);
    const std::size_t topology = CountColor(*first, kTopologyColor);
    const std::size_t palette = CountColor(*first, kPaletteColor);
    Check(background == 2667U && border == 372U && topology == 23U &&
              palette == 10U &&
              background + border + topology + palette == first->width * first->height,
        "the canonical fixture has the frozen four-color population");
    Check(Fnv1a64(*first) == 0xb56c8db088c5a9feULL,
        "the canonical fixture has the frozen FNV-1a-64 signature");

    bool all_opaque = true;
    bool only_frozen_colors = true;
    for (std::uint32_t y = 0U; y < first->height; ++y)
    {
        for (std::uint32_t x = 0U; x < first->width; ++x)
        {
            const auto pixel = PixelAt(*first, x, y);
            all_opaque = all_opaque && pixel[3] == std::byte{255};
            only_frozen_colors = only_frozen_colors &&
                                 (pixel == kBackgroundColor || pixel == kBorderColor ||
                                     pixel == kTopologyColor || pixel == kPaletteColor);
        }
    }
    Check(all_opaque && only_frozen_colors,
        "every topology-image pixel is opaque and belongs to the frozen palette");

    CheckMask(*first, 4U, 4U, 0x9U,
        "the canonical Indexed8 marker uses the frozen row-major mask");
    CheckMask(*first, 8U, 8U, 0x1U,
        "the first Packed4 plane occupies the first grid marker");
    CheckMask(*first, 10U, 8U, 0x9U,
        "the second Packed8 plane occupies the next source-order grid marker");
    CheckMask(*first, 12U, 8U, 0x7U,
        "the third Packed24 plane uses its frozen grid mask");
    CheckMask(*first, 14U, 8U, 0xfU,
        "the fourth Packed32 plane uses its frozen grid mask");

    constexpr std::array<std::array<std::uint32_t, 2>, 5> palette_offsets{{
        {27U, 26U}, {26U, 27U}, {27U, 27U}, {28U, 27U}, {27U, 28U},
    }};
    bool plus_matches = true;
    for (const auto& offset : palette_offsets)
        plus_matches = plus_matches && PixelAt(*first, offset[0], offset[1]) == kPaletteColor;
    plus_matches = plus_matches && PixelAt(*first, 26U, 26U) == kBackgroundColor &&
                   PixelAt(*first, 28U, 28U) == kBackgroundColor;
    Check(plus_matches, "palette presence draws only the frozen amber plus marker");

    auto payload_changed = canonical;
    for (auto& block : payload_changed.blocks)
    {
        for (auto& plane : block.planes)
        {
            for (std::byte& value : plane.bytes)
                value = value == std::byte{0} ? std::byte{0xff} : std::byte{0};
        }
        if (block.palette)
        {
            for (auto& entry : block.palette->entries)
                entry = {std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
        }
    }
    const auto payload_changed_image =
        omega::runtime::BuildTextureStorageTopologyDebugImage(payload_changed);
    Check(payload_changed_image &&
              payload_changed_image->rgba8_pixels == first->rgba8_pixels,
        "plane and palette payload values never affect topology pixels");

    auto dimension_changed = canonical;
    dimension_changed.width = 7U;
    dimension_changed.height = 9U;
    dimension_changed.blocks[0].planes[0].width = 1U;
    dimension_changed.blocks[0].planes[0].height = 9U;
    dimension_changed.blocks[0].planes[1].width = 1U;
    dimension_changed.blocks[0].planes[1].height = 6U;
    dimension_changed.blocks[0].planes[2].width = 1U;
    dimension_changed.blocks[0].planes[2].height = 4U;
    dimension_changed.blocks[0].planes[3].width = 4U;
    dimension_changed.blocks[0].planes[3].height = 1U;
    dimension_changed.blocks[0].palette->width = 1U;
    dimension_changed.blocks[0].palette->height = 4U;
    dimension_changed.blocks[1].planes[1].width = 1U;
    dimension_changed.blocks[1].planes[1].height = 3U;
    dimension_changed.blocks[2].planes[0].width = 1U;
    dimension_changed.blocks[2].planes[0].height = 6U;
    dimension_changed.blocks[2].palette->width = 2U;
    dimension_changed.blocks[2].palette->height = 1U;
    const auto dimension_changed_image =
        omega::runtime::BuildTextureStorageTopologyDebugImage(dimension_changed);
    Check(dimension_changed_image &&
              dimension_changed_image->rgba8_pixels == first->rgba8_pixels,
        "valid texture, plane, and palette dimensions are validation-only topology inputs");

    const auto owned_pixels = first->rgba8_pixels;
    canonical.width = 999U;
    canonical.blocks.clear();
    Check(first->rgba8_pixels == owned_pixels,
        "the returned image owns every pixel after its borrowed source mutates");
}

void CheckOrderingAndPacking()
{
    TextureStorageIR ordered = MakeCanonicalStorage();
    const auto ordered_image =
        omega::runtime::BuildTextureStorageTopologyDebugImage(ordered);
    std::swap(ordered.blocks[0], ordered.blocks[1]);
    const auto reordered_image =
        omega::runtime::BuildTextureStorageTopologyDebugImage(ordered);
    Check(ordered_image && reordered_image &&
              ordered_image->rgba8_pixels != reordered_image->rgba8_pixels &&
              PixelAt(*ordered_image, 27U, 27U) == kPaletteColor &&
              PixelAt(*reordered_image, 27U, 27U) == kBackgroundColor &&
              PixelAt(*ordered_image, kTilePixels + 27U, 27U) == kBackgroundColor &&
              PixelAt(*reordered_image, kTilePixels + 27U, 27U) == kPaletteColor,
        "block reordering moves complete topology markers in source order");

    TextureStorageIR nine = MakeMinimalStorage();
    nine.blocks.assign(9U, MakeMinimalBlock());
    const auto packed = omega::runtime::BuildTextureStorageTopologyDebugImage(nine);
    Check(packed && packed->width == 256U && packed->height == 64U &&
              packed->rgba8_pixels.size() == 256U * 64U * 4U,
        "nine blocks pack into eight columns and two 32-pixel rows");
    Check(packed && PixelAt(*packed, 0U, 32U) == kBorderColor &&
              PixelAt(*packed, 31U, 63U) == kBorderColor &&
              PixelAt(*packed, 32U, 32U) == kBackgroundColor &&
              PixelAt(*packed, 255U, 63U) == kBackgroundColor,
        "only the ninth source block occupies the second contact-sheet row");
}

void CheckEveryEncodingMarker()
{
    struct SampleCase
    {
        TextureSampleEncoding encoding;
        std::uint8_t mask;
    };
    constexpr std::array samples{
        SampleCase{TextureSampleEncoding::Indexed4, 0x1U},
        SampleCase{TextureSampleEncoding::Indexed8, 0x9U},
        SampleCase{TextureSampleEncoding::Packed24, 0x7U},
        SampleCase{TextureSampleEncoding::Packed32, 0xfU},
    };
    for (const SampleCase sample : samples)
    {
        TextureStorageIR storage = MakeMinimalStorage();
        storage.sample_encoding = sample.encoding;
        const auto image = omega::runtime::BuildTextureStorageTopologyDebugImage(storage);
        Check(image.has_value(), "every declared sample encoding produces a topology image");
        if (image)
            CheckMask(*image, 4U, 4U, sample.mask,
                "every sample encoding has its frozen marker mask");
    }

    TextureStorageIR planes = MakeMinimalStorage();
    planes.sample_encoding = TextureSampleEncoding::Packed32;
    planes.blocks[0].planes = {
        MakePlane(TextureTransferElementEncoding::Packed4),
        MakePlane(TextureTransferElementEncoding::Packed8),
        MakePlane(TextureTransferElementEncoding::Packed24),
        MakePlane(TextureTransferElementEncoding::Packed32),
    };
    // A palette on a direct-sample fixture is accepted: no unassigned correlation is inferred.
    planes.blocks[0].palette = MakePalette(1U, 1U);
    const auto image = omega::runtime::BuildTextureStorageTopologyDebugImage(planes);
    Check(image.has_value(),
        "sample, transfer, and palette combinations remain deliberately uncorrelated");
    if (image)
    {
        constexpr std::array masks{0x1U, 0x9U, 0x7U, 0xfU};
        for (std::size_t index = 0U; index < masks.size(); ++index)
        {
            CheckMask(*image, 8U + 2U * static_cast<std::uint32_t>(index), 8U,
                static_cast<std::uint8_t>(masks[index]),
                "plane source order and every transfer marker mask are frozen");
        }
    }
}

void CheckValidationAndLimits()
{
    const TextureStorageTopologyDebugImageLimits defaults{};

    TextureStorageIR invalid = MakeMinimalStorage();
    invalid.width = 0U;
    invalid.sample_encoding = static_cast<TextureSampleEncoding>(255);
    invalid.blocks.clear();
    CheckError(invalid, defaults,
        TextureStorageTopologyDebugImageErrorCode::InvalidTextureDimensions,
        "texture dimensions have first validation priority");

    invalid = MakeMinimalStorage();
    invalid.sample_encoding = static_cast<TextureSampleEncoding>(255);
    invalid.blocks.clear();
    CheckError(invalid, defaults,
        TextureStorageTopologyDebugImageErrorCode::InvalidSampleEncoding,
        "sample encoding is checked before block presence");

    invalid = MakeMinimalStorage();
    invalid.blocks.clear();
    CheckError(invalid, defaults, TextureStorageTopologyDebugImageErrorCode::EmptyBlockSet,
        "a topology image requires at least one source block");

    TextureStorageTopologyDebugImageLimits limits = defaults;
    limits.maximum_blocks = 0U;
    invalid = MakeMinimalStorage();
    invalid.blocks[0].planes.clear();
    CheckError(invalid, limits,
        TextureStorageTopologyDebugImageErrorCode::BlockLimitExceeded,
        "the block budget is checked before block contents");

    limits = defaults;
    invalid = MakeMinimalStorage();
    invalid.blocks[0].planes.clear();
    CheckError(invalid, limits, TextureStorageTopologyDebugImageErrorCode::EmptyPlaneSet,
        "every block must contain a plane");

    invalid = MakeMinimalStorage();
    invalid.blocks[0].planes.assign(
        65U, MakePlane(TextureTransferElementEncoding::Packed8));
    limits.maximum_planes = 0U;
    CheckError(invalid, limits,
        TextureStorageTopologyDebugImageErrorCode::PlaneMarkerCapacityExceeded,
        "the fixed 64-marker cap precedes the cumulative plane budget");

    invalid.blocks[0].planes.resize(64U);
    limits = defaults;
    limits.maximum_planes = 64U;
    const auto exact_64_planes =
        omega::runtime::BuildTextureStorageTopologyDebugImage(invalid, limits);
    Check(exact_64_planes.has_value(),
        "exactly 64 planes fill the complete bounded marker grid");
    if (exact_64_planes)
    {
        CheckMask(*exact_64_planes, 8U, 10U, 0x9U,
            "plane eight wraps to the first marker in the second grid row");
        CheckMask(*exact_64_planes, 22U, 22U, 0x9U,
            "plane 63 occupies the final marker in the bounded 8x8 grid");
    }
    limits.maximum_planes = 63U;
    CheckError(invalid, limits,
        TextureStorageTopologyDebugImageErrorCode::PlaneLimitExceeded,
        "the cumulative plane budget rejects one below its exact bound");

    invalid = MakeMinimalStorage();
    invalid.blocks[0].planes[0].width = 0U;
    invalid.blocks[0].planes[0].element_encoding =
        static_cast<TextureTransferElementEncoding>(255);
    invalid.blocks[0].planes[0].bytes.clear();
    CheckError(invalid, defaults,
        TextureStorageTopologyDebugImageErrorCode::InvalidPlaneDimensions,
        "plane dimensions precede transfer encoding and byte-size validation");

    invalid = MakeMinimalStorage();
    invalid.blocks[0].planes[0].element_encoding =
        static_cast<TextureTransferElementEncoding>(255);
    invalid.blocks[0].planes[0].bytes.clear();
    CheckError(invalid, defaults,
        TextureStorageTopologyDebugImageErrorCode::InvalidTransferElementEncoding,
        "invalid transfer-element encodings fail before byte sizing");

    invalid = MakeMinimalStorage();
    invalid.blocks[0].planes[0] = TextureStoragePlaneIR{
        .width = std::numeric_limits<std::uint32_t>::max(),
        .height = std::numeric_limits<std::uint32_t>::max(),
        .element_encoding = TextureTransferElementEncoding::Packed32,
    };
    CheckError(invalid, defaults,
        TextureStorageTopologyDebugImageErrorCode::PlaneByteSizeOverflow,
        "packed plane byte-size arithmetic is overflow checked");

    invalid = MakeMinimalStorage();
    invalid.blocks[0].planes[0] = TextureStoragePlaneIR{
        .width = 1U,
        .height = 1U,
        .element_encoding = TextureTransferElementEncoding::Packed8,
    };
    invalid.blocks[0].palette = TexturePaletteStorageIR{};
    CheckError(invalid, defaults,
        TextureStorageTopologyDebugImageErrorCode::PlaneByteSizeMismatch,
        "plane rectangle mismatch precedes palette validation");

    invalid = MakeMinimalStorage();
    invalid.blocks[0].palette = TexturePaletteStorageIR{
        .width = 0U,
        .height = 1U,
    };
    CheckError(invalid, defaults,
        TextureStorageTopologyDebugImageErrorCode::InvalidPaletteDimensions,
        "present palettes require nonzero dimensions");

    invalid.blocks[0].palette = TexturePaletteStorageIR{
        .width = 2U,
        .height = 1U,
        .entries = {std::array<std::byte, 4>{}},
    };
    limits = defaults;
    limits.maximum_palette_entries = 0U;
    CheckError(invalid, limits,
        TextureStorageTopologyDebugImageErrorCode::PaletteEntryCountMismatch,
        "palette rectangle mismatch precedes the cumulative entry budget");

    invalid.blocks[0].palette = MakePalette(1U, 1U);
    limits = defaults;
    limits.maximum_palette_entries = 1U;
    Check(omega::runtime::BuildTextureStorageTopologyDebugImage(invalid, limits).has_value(),
        "the exact palette-entry budget succeeds");
    limits.maximum_palette_entries = 0U;
    CheckError(invalid, limits,
        TextureStorageTopologyDebugImageErrorCode::PaletteEntryLimitExceeded,
        "one below the exact palette-entry budget is rejected");

    TextureStorageIR bounded = MakeMinimalStorage();
    limits = TextureStorageTopologyDebugImageLimits{
        .maximum_blocks = 1U,
        .maximum_planes = 1U,
        .maximum_palette_entries = 0U,
        .maximum_output_bytes = 32U * 32U * 4U,
    };
    Check(omega::runtime::BuildTextureStorageTopologyDebugImage(bounded, limits).has_value(),
        "exact block, plane, zero-palette, and output budgets succeed");
    limits.maximum_output_bytes -= 1U;
    CheckError(bounded, limits,
        TextureStorageTopologyDebugImageErrorCode::OutputByteLimitExceeded,
        "one below the exact output-byte budget is rejected");

    TextureStorageIR cumulative = MakeMinimalStorage();
    cumulative.blocks.assign(2U, MakeMinimalBlock());
    for (auto& block : cumulative.blocks)
        block.palette = MakePalette(1U, 1U);
    const TextureStorageTopologyDebugImageLimits cumulative_exact{
        .maximum_blocks = 2U,
        .maximum_planes = 2U,
        .maximum_palette_entries = 2U,
        .maximum_output_bytes = 64U * 32U * 4U,
    };
    Check(omega::runtime::BuildTextureStorageTopologyDebugImage(
              cumulative, cumulative_exact)
              .has_value(),
        "exact cumulative block, plane, palette-entry, and output budgets succeed");
    auto cumulative_below = cumulative_exact;
    cumulative_below.maximum_blocks = 1U;
    CheckError(cumulative, cumulative_below,
        TextureStorageTopologyDebugImageErrorCode::BlockLimitExceeded,
        "one below the cumulative block budget is rejected");
    cumulative_below = cumulative_exact;
    cumulative_below.maximum_planes = 1U;
    CheckError(cumulative, cumulative_below,
        TextureStorageTopologyDebugImageErrorCode::PlaneLimitExceeded,
        "one below the cumulative plane budget is rejected");
    cumulative_below = cumulative_exact;
    cumulative_below.maximum_palette_entries = 1U;
    CheckError(cumulative, cumulative_below,
        TextureStorageTopologyDebugImageErrorCode::PaletteEntryLimitExceeded,
        "one below the cumulative palette-entry budget is rejected");
    cumulative_below = cumulative_exact;
    cumulative_below.maximum_output_bytes -= 1U;
    CheckError(cumulative, cumulative_below,
        TextureStorageTopologyDebugImageErrorCode::OutputByteLimitExceeded,
        "one below the cumulative fixture output budget is rejected");

    limits = defaults;
    limits.maximum_planes = 0U;
    invalid = MakeMinimalStorage();
    invalid.blocks[0].planes[0].width = 0U;
    CheckError(invalid, limits,
        TextureStorageTopologyDebugImageErrorCode::PlaneLimitExceeded,
        "the cumulative plane budget precedes per-plane validation");
}
} // namespace

int main()
{
    CheckErrorContract();
    CheckCanonicalImage();
    CheckOrderingAndPacking();
    CheckEveryEncodingMarker();
    CheckValidationAndLimits();

    if (failures == 0)
        std::cout << "omega_texture_storage_topology_debug_image_tests: passed\n";
    return failures == 0 ? 0 : 1;
}
