#include "omega/frontend_presentation/retail_frontend_texture_sampler.h"

#include "omega/content/front_end_screen_bundle.h"

#include <cmath>
#include <concepts>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace omega::content::detail
{
struct FrontEndScreenBundleTestAccess final
{
    [[nodiscard]] static FrontEndTextureBinding MakeTexture(
        asset::IndexedImageIR image,
        const asset::IndexedImageEncoding sampling_encoding,
        const FrontEndTextureAlphaMode alpha_mode)
    {
        return FrontEndTextureBinding(
            std::move(image), sampling_encoding, alpha_mode);
    }
};
} // namespace omega::content::detail

namespace
{
using omega::asset::IndexedImageEncoding;
using omega::asset::IndexedImageIR;
using omega::asset::RawGsRgba8;
using omega::content::FrontEndTextureAlphaMode;
using omega::content::FrontEndTextureBinding;
using omega::content::detail::FrontEndScreenBundleTestAccess;
using omega::frontend::presentation::LookupRetailFrontEndTexel;
using omega::frontend::presentation::RetailFrontEndTextureAlphaContribution;
using omega::frontend::presentation::RetailFrontEndTextureSamplingError;
using omega::frontend::presentation::RetailFrontEndTextureSamplingResult;
using omega::frontend::presentation::SampleRetailFrontEndTextureBilinearRepeat;

int failures = 0;

void Check(const bool condition, const std::string_view message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

[[nodiscard]] bool NearlyEqual(
    const float left, const float right, const float epsilon = 0.000'01F) noexcept
{
    return std::fabs(left - right) <= epsilon;
}

void CheckError(const RetailFrontEndTextureSamplingResult& result,
    const RetailFrontEndTextureSamplingError expected,
    const std::string_view message)
{
    Check(!result && result.error() == expected, message);
}

[[nodiscard]] std::size_t PaletteSize(const IndexedImageEncoding encoding)
{
    return encoding == IndexedImageEncoding::Indexed4 ? 16U : 256U;
}

[[nodiscard]] FrontEndTextureBinding MakeTexture(
    const IndexedImageEncoding encoding,
    const FrontEndTextureAlphaMode alpha_mode,
    const std::uint32_t width,
    const std::uint32_t height,
    std::vector<std::uint8_t> indices,
    std::vector<RawGsRgba8> palette = {})
{
    if (palette.empty())
        palette.resize(PaletteSize(encoding));
    return FrontEndScreenBundleTestAccess::MakeTexture(
        IndexedImageIR{
            .width = width,
            .height = height,
            .source_encoding = encoding,
            .indices = std::move(indices),
            .palette = std::move(palette),
        },
        encoding, alpha_mode);
}

void TestPublicContract()
{
    static_assert(std::same_as<decltype(LookupRetailFrontEndTexel(
                                   std::declval<const FrontEndTextureBinding&>(),
                                   0U, 0U)),
        RetailFrontEndTextureSamplingResult>);
    static_assert(noexcept(LookupRetailFrontEndTexel(
        std::declval<const FrontEndTextureBinding&>(), 0U, 0U)));
    static_assert(std::same_as<decltype(SampleRetailFrontEndTextureBilinearRepeat(
                                   std::declval<const FrontEndTextureBinding&>(),
                                   std::declval<const omega::asset::FrontendUvIR&>())),
        RetailFrontEndTextureSamplingResult>);
    static_assert(noexcept(SampleRetailFrontEndTextureBilinearRepeat(
        std::declval<const FrontEndTextureBinding&>(),
        std::declval<const omega::asset::FrontendUvIR&>())));
    static_assert(
        omega::frontend::presentation::kRetailFrontEndTextureMaximumDimension ==
        512U);
}

void TestIndexedTexelLookup()
{
    std::vector<RawGsRgba8> indexed4_palette(16U);
    indexed4_palette[0U] = {1U, 2U, 3U, 128U};
    indexed4_palette[15U] = {128U, 64U, 32U, 64U};
    const auto indexed4 = MakeTexture(IndexedImageEncoding::Indexed4,
        FrontEndTextureAlphaMode::UsesPaletteAlpha, 2U, 1U, {0U, 15U},
        std::move(indexed4_palette));

    const auto first = LookupRetailFrontEndTexel(indexed4, 0U, 0U);
    const auto last = LookupRetailFrontEndTexel(indexed4, 1U, 0U);
    Check(first && NearlyEqual(first->modulation.red, 1.0F / 255.0F) &&
            NearlyEqual(first->modulation.alpha, 1.0F),
        "indexed-4 lookup returns the first logical palette entry");
    Check(last && NearlyEqual(last->modulation.red, 128.0F / 255.0F) &&
            NearlyEqual(last->modulation.green, 64.0F / 255.0F) &&
            NearlyEqual(last->modulation.alpha, 0.5F) &&
            last->alpha_contribution ==
                RetailFrontEndTextureAlphaContribution::Palette,
        "indexed-4 lookup returns the expanded high logical index");

    std::vector<RawGsRgba8> indexed8_palette(256U);
    indexed8_palette[255U] = {7U, 11U, 13U, 128U};
    const auto indexed8 = MakeTexture(IndexedImageEncoding::Indexed8,
        FrontEndTextureAlphaMode::UsesPaletteAlpha, 1U, 1U, {255U},
        std::move(indexed8_palette));
    const auto high = LookupRetailFrontEndTexel(indexed8, 0U, 0U);
    Check(high && NearlyEqual(high->modulation.red, 7.0F / 255.0F) &&
            NearlyEqual(high->modulation.green, 11.0F / 255.0F) &&
            NearlyEqual(high->modulation.blue, 13.0F / 255.0F),
        "indexed-8 lookup admits the full byte palette range");
}

void TestBilinearRepeatAndFractions()
{
    std::vector<RawGsRgba8> palette(256U);
    palette[0U] = {0U, 0U, 0U, 0U};
    palette[1U] = {255U, 0U, 0U, 128U};
    palette[2U] = {0U, 255U, 0U, 0U};
    palette[3U] = {0U, 0U, 255U, 128U};
    const auto texture = MakeTexture(IndexedImageEncoding::Indexed8,
        FrontEndTextureAlphaMode::UsesPaletteAlpha, 2U, 2U,
        {0U, 1U, 2U, 3U}, std::move(palette));

    const auto center = SampleRetailFrontEndTextureBilinearRepeat(
        texture, {.u = 0.5F, .v = 0.5F});
    Check(center && NearlyEqual(center->modulation.red, 0.25F) &&
            NearlyEqual(center->modulation.green, 0.25F) &&
            NearlyEqual(center->modulation.blue, 0.25F) &&
            NearlyEqual(center->modulation.alpha, 0.5F),
        "four taps interpolate both axes and palette alpha");

    const auto texel_center = SampleRetailFrontEndTextureBilinearRepeat(
        texture, {.u = 0.25F, .v = 0.25F});
    Check(texel_center &&
            texel_center->modulation ==
                omega::frontend::RgbaF{0.0F, 0.0F, 0.0F, 0.0F},
        "half-texel subtraction maps a normalized texel center exactly");

    const auto repeated_positive = SampleRetailFrontEndTextureBilinearRepeat(
        texture, {.u = 1.25F, .v = 2.25F});
    const auto repeated_negative = SampleRetailFrontEndTextureBilinearRepeat(
        texture, {.u = -0.75F, .v = -1.75F});
    Check(repeated_positive == texel_center && repeated_negative == texel_center,
        "positive and negative normalized coordinates repeat by whole periods");

    std::vector<RawGsRgba8> edge_palette(256U);
    edge_palette[0U] = {0U, 0U, 0U, 128U};
    edge_palette[1U] = {255U, 0U, 0U, 128U};
    const auto edge_texture = MakeTexture(IndexedImageEncoding::Indexed8,
        FrontEndTextureAlphaMode::UsesPaletteAlpha, 2U, 1U, {0U, 1U},
        std::move(edge_palette));
    const auto left_edge = SampleRetailFrontEndTextureBilinearRepeat(
        edge_texture, {.u = 0.0F, .v = 0.5F});
    const auto repeated_edge = SampleRetailFrontEndTextureBilinearRepeat(
        edge_texture, {.u = 1.0F, .v = 0.5F});
    Check(left_edge && NearlyEqual(left_edge->modulation.red, 0.5F) &&
            repeated_edge == left_edge,
        "exact repeat edge blends the last and first texels at one half");
}

void TestExplicitAlphaModes()
{
    std::vector<RawGsRgba8> palette(16U);
    palette[3U] = {25U, 50U, 75U, 32U};
    const auto uses_alpha = MakeTexture(IndexedImageEncoding::Indexed4,
        FrontEndTextureAlphaMode::UsesPaletteAlpha, 1U, 1U, {3U}, palette);
    const auto ignores_alpha = MakeTexture(IndexedImageEncoding::Indexed4,
        FrontEndTextureAlphaMode::IgnoresTextureAlpha, 1U, 1U, {3U},
        std::move(palette));

    const auto uses = LookupRetailFrontEndTexel(uses_alpha, 0U, 0U);
    const auto ignores = LookupRetailFrontEndTexel(ignores_alpha, 0U, 0U);
    Check(uses && NearlyEqual(uses->modulation.alpha, 0.25F) &&
            uses->alpha_contribution ==
                RetailFrontEndTextureAlphaContribution::Palette,
        "TCC palette-alpha mode exposes the normalized palette multiplier");
    Check(ignores && NearlyEqual(ignores->modulation.alpha, 1.0F) &&
            ignores->alpha_contribution ==
                RetailFrontEndTextureAlphaContribution::Identity &&
            uses && ignores->modulation.red == uses->modulation.red &&
            ignores->modulation.green == uses->modulation.green &&
            ignores->modulation.blue == uses->modulation.blue,
        "TCC ignored-alpha mode preserves RGB and returns an explicit identity multiplier");
}

void TestFailuresAreTypedAndFailClosed()
{
    CheckError(LookupRetailFrontEndTexel(
                   MakeTexture(IndexedImageEncoding::Indexed4,
                       FrontEndTextureAlphaMode::UsesPaletteAlpha, 0U, 1U, {}),
                   0U, 0U),
        RetailFrontEndTextureSamplingError::EmptyImage,
        "zero extent is rejected");

    CheckError(LookupRetailFrontEndTexel(
                   MakeTexture(IndexedImageEncoding::Indexed4,
                       FrontEndTextureAlphaMode::UsesPaletteAlpha, 513U, 1U, {}),
                   0U, 0U),
        RetailFrontEndTextureSamplingError::DimensionLimitExceeded,
        "the frontend decoder dimension ceiling is rechecked");

    CheckError(LookupRetailFrontEndTexel(
                   MakeTexture(IndexedImageEncoding::Indexed4,
                       FrontEndTextureAlphaMode::UsesPaletteAlpha, 2U, 1U, {0U}),
                   0U, 0U),
        RetailFrontEndTextureSamplingError::InvalidIndexStorage,
        "index storage must exactly cover the image");

    CheckError(LookupRetailFrontEndTexel(
                   MakeTexture(IndexedImageEncoding::Indexed4,
                       FrontEndTextureAlphaMode::UsesPaletteAlpha, 1U, 1U, {0U},
                       std::vector<RawGsRgba8>(15U)),
                   0U, 0U),
        RetailFrontEndTextureSamplingError::InvalidPaletteStorage,
        "indexed-4 palette cardinality is exact");

    CheckError(LookupRetailFrontEndTexel(
                   MakeTexture(IndexedImageEncoding::Indexed4,
                       FrontEndTextureAlphaMode::UsesPaletteAlpha, 1U, 1U, {16U}),
                   0U, 0U),
        RetailFrontEndTextureSamplingError::PaletteIndexOutOfRange,
        "expanded indexed-4 values cannot escape their palette");

    const auto ordinary = MakeTexture(IndexedImageEncoding::Indexed8,
        FrontEndTextureAlphaMode::UsesPaletteAlpha, 1U, 1U, {0U});
    CheckError(LookupRetailFrontEndTexel(ordinary, 1U, 0U),
        RetailFrontEndTextureSamplingError::TexelCoordinateOutOfRange,
        "direct lookup rejects an out-of-range coordinate");
    CheckError(SampleRetailFrontEndTextureBilinearRepeat(ordinary,
                   {.u = std::numeric_limits<float>::infinity(), .v = 0.0F}),
        RetailFrontEndTextureSamplingError::NonFiniteCoordinate,
        "bilinear sampling rejects infinite coordinates");
    CheckError(SampleRetailFrontEndTextureBilinearRepeat(ordinary,
                   {.u = 0.0F, .v = std::numeric_limits<float>::quiet_NaN()}),
        RetailFrontEndTextureSamplingError::NonFiniteCoordinate,
        "bilinear sampling rejects NaN coordinates");

    constexpr auto invalid_encoding = static_cast<IndexedImageEncoding>(0xFFU);
    CheckError(LookupRetailFrontEndTexel(
                   MakeTexture(invalid_encoding,
                       FrontEndTextureAlphaMode::UsesPaletteAlpha, 1U, 1U, {0U},
                       std::vector<RawGsRgba8>(256U)),
                   0U, 0U),
        RetailFrontEndTextureSamplingError::UnsupportedEncoding,
        "unrecognized sampling encodings fail closed");

    constexpr auto invalid_alpha_mode =
        static_cast<FrontEndTextureAlphaMode>(0xFFU);
    CheckError(LookupRetailFrontEndTexel(
                   MakeTexture(IndexedImageEncoding::Indexed8,
                       invalid_alpha_mode, 1U, 1U, {0U}),
                   0U, 0U),
        RetailFrontEndTextureSamplingError::UnsupportedAlphaMode,
        "unrecognized TCC alpha modes fail closed");
}

void TestDeterminism()
{
    std::vector<RawGsRgba8> palette(256U);
    palette[1U] = {17U, 31U, 63U, 32U};
    palette[2U] = {71U, 89U, 107U, 64U};
    palette[3U] = {127U, 149U, 173U, 96U};
    palette[4U] = {191U, 223U, 251U, 128U};
    const auto texture = MakeTexture(IndexedImageEncoding::Indexed8,
        FrontEndTextureAlphaMode::UsesPaletteAlpha, 2U, 2U,
        {1U, 2U, 3U, 4U}, std::move(palette));
    const auto reference = SampleRetailFrontEndTextureBilinearRepeat(
        texture, {.u = -8.125F, .v = 19.875F});
    Check(reference.has_value(), "determinism fixture samples successfully");
    for (std::uint32_t iteration = 0U; iteration < 1'024U; ++iteration)
    {
        Check(SampleRetailFrontEndTextureBilinearRepeat(
                  texture, {.u = -8.125F, .v = 19.875F}) == reference,
            "repeated sampling is byte-for-byte deterministic");
    }
}
} // namespace

int main()
{
    TestPublicContract();
    TestIndexedTexelLookup();
    TestBilinearRepeatAndFractions();
    TestExplicitAlphaModes();
    TestFailuresAreTypedAndFailClosed();
    TestDeterminism();

    if (failures != 0)
    {
        std::cerr << failures << " frontend texture sampler test(s) failed\n";
        return 1;
    }
    std::cout << "frontend texture sampler tests passed\n";
    return 0;
}
