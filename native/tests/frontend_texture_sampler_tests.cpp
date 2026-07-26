#include "omega/frontend_presentation/retail_frontend_texture_sampler.h"

#include "omega/content/front_end_screen_bundle.h"

#include <bit>
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
using omega::frontend::presentation::RetailFrontEndValidatedTexture;
using omega::frontend::presentation::SampleRetailFrontEndTextureBilinearRepeat;
using omega::frontend::presentation::ValidateRetailFrontEndTexture;

template <typename Type>
concept ExposesMutableValidatedTextureLayout = requires(Type& value) {
    value.indices_ = static_cast<const std::uint8_t*>(nullptr);
    value.palette_size_ = 0U;
    value.width_ = 0U;
    value.height_ = 0U;
};

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

[[nodiscard]] bool ExactlyEqual(
    const RetailFrontEndTextureSamplingResult& left,
    const RetailFrontEndTextureSamplingResult& right) noexcept
{
    if (left.has_value() != right.has_value())
        return false;
    if (!left)
        return left.error() == right.error();

    const auto same_float = [](const float first, const float second) {
        return std::bit_cast<std::uint32_t>(first) ==
               std::bit_cast<std::uint32_t>(second);
    };
    return same_float(left->modulation.red, right->modulation.red) &&
           same_float(left->modulation.green, right->modulation.green) &&
           same_float(left->modulation.blue, right->modulation.blue) &&
           same_float(left->modulation.alpha, right->modulation.alpha) &&
           left->alpha_contribution == right->alpha_contribution;
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

struct FrozenReferenceAxisTaps final
{
    std::uint32_t lower = 0U;
    std::uint32_t upper = 0U;
    double fraction = 0.0;
};

[[nodiscard]] std::uint32_t FrozenReferenceWrapIndex(
    const std::int64_t index, const std::uint32_t extent) noexcept
{
    const std::int64_t signed_extent = static_cast<std::int64_t>(extent);
    std::int64_t wrapped = index % signed_extent;
    if (wrapped < 0)
        wrapped += signed_extent;
    return static_cast<std::uint32_t>(wrapped);
}

[[nodiscard]] FrozenReferenceAxisTaps FrozenReferenceResolveAxis(
    const float normalized_coordinate, const std::uint32_t extent) noexcept
{
    double repeated =
        std::fmod(static_cast<double>(normalized_coordinate), 1.0);
    if (repeated < 0.0)
        repeated += 1.0;

    const double texel_coordinate =
        repeated * static_cast<double>(extent) - 0.5;
    const double lower_double = std::floor(texel_coordinate);
    const auto lower = static_cast<std::int64_t>(lower_double);
    return FrozenReferenceAxisTaps{
        .lower = FrozenReferenceWrapIndex(lower, extent),
        .upper = FrozenReferenceWrapIndex(lower + 1, extent),
        .fraction = texel_coordinate - lower_double,
    };
}

[[nodiscard]] float FrozenReferenceLerp(
    const float left, const float right, const double fraction) noexcept
{
    return static_cast<float>(
        static_cast<double>(left) +
        (static_cast<double>(right) - static_cast<double>(left)) * fraction);
}

[[nodiscard]] omega::frontend::RgbaF FrozenReferenceLerp(
    const omega::frontend::RgbaF& left,
    const omega::frontend::RgbaF& right,
    const double fraction) noexcept
{
    return omega::frontend::RgbaF{
        FrozenReferenceLerp(left.red, right.red, fraction),
        FrozenReferenceLerp(left.green, right.green, fraction),
        FrozenReferenceLerp(left.blue, right.blue, fraction),
        FrozenReferenceLerp(left.alpha, right.alpha, fraction),
    };
}

// This is the pre-fast-path addressing and interpolation algorithm, kept
// test-local so every optimized result can be compared bit for bit with the
// frozen behavior rather than with a second copy of the new branches.
[[nodiscard]] RetailFrontEndTextureSamplingResult FrozenReferenceSample(
    const FrontEndTextureBinding& binding,
    const omega::asset::FrontendUvIR& normalized_st) noexcept
{
    if (!std::isfinite(normalized_st.u) || !std::isfinite(normalized_st.v))
        return std::unexpected(
            RetailFrontEndTextureSamplingError::NonFiniteCoordinate);

    const auto validated = ValidateRetailFrontEndTexture(binding);
    if (!validated)
        return std::unexpected(validated.error());

    const auto& image = binding.image();
    const FrozenReferenceAxisTaps horizontal =
        FrozenReferenceResolveAxis(normalized_st.u, image.width);
    const FrozenReferenceAxisTaps vertical =
        FrozenReferenceResolveAxis(normalized_st.v, image.height);
    const auto upper_left = LookupRetailFrontEndTexel(
        binding, horizontal.lower, vertical.lower);
    const auto upper_right = LookupRetailFrontEndTexel(
        binding, horizontal.upper, vertical.lower);
    const auto lower_left = LookupRetailFrontEndTexel(
        binding, horizontal.lower, vertical.upper);
    const auto lower_right = LookupRetailFrontEndTexel(
        binding, horizontal.upper, vertical.upper);
    if (!upper_left)
        return std::unexpected(upper_left.error());
    if (!upper_right)
        return std::unexpected(upper_right.error());
    if (!lower_left)
        return std::unexpected(lower_left.error());
    if (!lower_right)
        return std::unexpected(lower_right.error());

    const omega::frontend::RgbaF upper =
        FrozenReferenceLerp(upper_left->modulation, upper_right->modulation,
            horizontal.fraction);
    const omega::frontend::RgbaF lower =
        FrozenReferenceLerp(lower_left->modulation, lower_right->modulation,
            horizontal.fraction);
    return omega::frontend::presentation::RetailFrontEndTextureSample{
        .modulation =
            FrozenReferenceLerp(upper, lower, vertical.fraction),
        .alpha_contribution = upper_left->alpha_contribution,
    };
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
    static_assert(!std::is_aggregate_v<RetailFrontEndValidatedTexture>);
    static_assert(
        !std::default_initializable<RetailFrontEndValidatedTexture>);
    static_assert(
        !ExposesMutableValidatedTextureLayout<
            RetailFrontEndValidatedTexture>);
    static_assert(!std::constructible_from<RetailFrontEndValidatedTexture,
        const std::uint8_t*,
        std::uint32_t,
        std::uint32_t,
        std::uint32_t,
        RetailFrontEndTextureAlphaContribution,
        std::array<omega::frontend::RgbaF, 256U>>);
    static_assert(std::copyable<RetailFrontEndValidatedTexture>);
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

// Hoisting validation out of a caller's inner loop must not become a second,
// laxer sampling rule: the validated overload has to agree bit for bit with the
// binding overload and keep every per-texel failure that survives validation.
void TestValidatedLayoutMatchesTheBindingOverload()
{
    std::vector<RawGsRgba8> palette(256U);
    palette[0U] = {0U, 0U, 0U, 0U};
    palette[1U] = {255U, 0U, 0U, 128U};
    palette[2U] = {0U, 255U, 0U, 0U};
    palette[3U] = {0U, 0U, 255U, 128U};
    const auto texture = MakeTexture(IndexedImageEncoding::Indexed8,
        FrontEndTextureAlphaMode::UsesPaletteAlpha, 2U, 2U,
        {0U, 1U, 2U, 3U}, std::move(palette));

    const auto validated = ValidateRetailFrontEndTexture(texture);
    Check(validated.has_value(), "a well-formed binding validates once");
    if (!validated)
        return;

    bool identical = true;
    for (const float u : {-2.75F, -0.125F, 0.0F, 0.25F, 0.5F, 0.75F, 1.0F,
             1.25F, 7.375F})
    {
        for (const float v : {-1.875F, 0.0F, 0.125F, 0.5F, 0.9375F, 3.5F})
        {
            const auto direct = SampleRetailFrontEndTextureBilinearRepeat(
                texture, {.u = u, .v = v});
            const auto hoisted = SampleRetailFrontEndTextureBilinearRepeat(
                *validated, {.u = u, .v = v});
            identical = identical && direct.has_value() &&
                        hoisted.has_value() && direct == hoisted;
        }
    }
    Check(identical,
        "the validated overload reproduces the binding overload exactly");

    CheckError(SampleRetailFrontEndTextureBilinearRepeat(*validated,
                   {.u = std::numeric_limits<float>::quiet_NaN(), .v = 0.0F}),
        RetailFrontEndTextureSamplingError::NonFiniteCoordinate,
        "the validated overload still rejects non-finite coordinates");

    // Palette range is a per-texel property of the payload, not an invariant
    // of the layout, so hoisting must not have hoisted this check away.
    const auto escaping = MakeTexture(IndexedImageEncoding::Indexed4,
        FrontEndTextureAlphaMode::UsesPaletteAlpha, 1U, 1U, {16U});
    const auto escaping_layout = ValidateRetailFrontEndTexture(escaping);
    Check(escaping_layout.has_value(),
        "an in-range layout can still carry an out-of-range index");
    if (escaping_layout)
    {
        CheckError(SampleRetailFrontEndTextureBilinearRepeat(
                       *escaping_layout, {.u = 0.5F, .v = 0.5F}),
            RetailFrontEndTextureSamplingError::PaletteIndexOutOfRange,
            "per-texel palette range survives hoisted validation");
    }

    const auto malformed_layout = ValidateRetailFrontEndTexture(
        MakeTexture(IndexedImageEncoding::Indexed4,
            FrontEndTextureAlphaMode::UsesPaletteAlpha, 2U, 1U, {0U}));
    Check(!malformed_layout &&
            malformed_layout.error() ==
                RetailFrontEndTextureSamplingError::InvalidIndexStorage,
        "hoisted validation fails closed on the same malformed storage");

    const auto empty_layout = ValidateRetailFrontEndTexture(
        MakeTexture(IndexedImageEncoding::Indexed4,
            FrontEndTextureAlphaMode::UsesPaletteAlpha, 0U, 1U, {}));
    Check(!empty_layout &&
            empty_layout.error() ==
                RetailFrontEndTextureSamplingError::EmptyImage,
        "an invalid caller binding cannot produce a sampler token");
    CheckError(SampleRetailFrontEndTextureBilinearRepeat(
                   MakeTexture(IndexedImageEncoding::Indexed4,
                       FrontEndTextureAlphaMode::UsesPaletteAlpha, 0U, 1U, {}),
                   {.u = 0.0F, .v = 0.0F}),
        RetailFrontEndTextureSamplingError::EmptyImage,
        "the public binding sampler rejects invalid state before sampling");
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

[[nodiscard]] std::vector<float> DifferentialCoordinates(
    const std::uint32_t extent)
{
    const float positive_infinity = std::numeric_limits<float>::infinity();
    const float negative_infinity = -positive_infinity;
    const float denormal = std::numeric_limits<float>::denorm_min();
    const float minimum_normal = std::numeric_limits<float>::min();
    std::vector<float> coordinates{
        0.0F,
        -0.0F,
        std::nextafter(0.0F, positive_infinity),
        std::nextafter(0.0F, negative_infinity),
        std::nextafter(1.0F, 0.0F),
        1.0F,
        std::nextafter(1.0F, positive_infinity),
        std::nextafter(-1.0F, 0.0F),
        -1.0F,
        std::nextafter(-1.0F, negative_infinity),
        denormal,
        -denormal,
        minimum_normal,
        -minimum_normal,
        -1'024.75F,
        -64.0F,
        -7.375F,
        -2.0F,
        -1.75F,
        -0.75F,
        -0.5F,
        -0.25F,
        0.125F,
        0.25F,
        0.5F,
        0.75F,
        1.25F,
        2.0F,
        7.375F,
        64.0F,
        1'024.75F,
        std::numeric_limits<float>::lowest(),
        std::nextafter(std::numeric_limits<float>::lowest(), 0.0F),
        std::nextafter(std::numeric_limits<float>::max(), 0.0F),
        std::numeric_limits<float>::max(),
    };
    constexpr std::array<float, 4U> phases{
        0.125F, 0.25F, 0.5F, 0.75F};
    constexpr std::array<float, 9U> periods{
        -4'096.0F,
        -257.0F,
        -2.0F,
        -1.0F,
        0.0F,
        1.0F,
        2.0F,
        257.0F,
        4'096.0F,
    };
    for (const float period : periods)
    {
        for (const float phase : phases)
            coordinates.push_back(period + phase);
    }

    const auto append_neighborhood =
        [&coordinates, positive_infinity, negative_infinity](const float value) {
            coordinates.push_back(std::nextafter(value, negative_infinity));
            coordinates.push_back(value);
            coordinates.push_back(std::nextafter(value, positive_infinity));
        };
    const std::array<std::uint32_t, 4U> texels{
        0U, extent > 1U ? 1U : 0U, extent / 2U, extent - 1U};
    for (const std::uint32_t texel : texels)
    {
        const float boundary = static_cast<float>(
            static_cast<double>(texel) / static_cast<double>(extent));
        const float center = static_cast<float>(
            (static_cast<double>(texel) + 0.5) /
            static_cast<double>(extent));
        append_neighborhood(boundary);
        append_neighborhood(center);
        append_neighborhood(boundary - 1.0F);
        append_neighborhood(center + 1.0F);
    }
    return coordinates;
}

void TestAxisFastPathMatchesFrozenReference()
{
    constexpr std::array<std::uint32_t, 7U> extents{
        1U, 2U, 3U, 255U, 256U, 511U, 512U};

    std::vector<RawGsRgba8> palette(256U);
    for (std::size_t entry = 0U; entry < palette.size(); ++entry)
    {
        palette[entry] = {
            static_cast<std::uint8_t>((entry * 29U + 3U) & 0xFFU),
            static_cast<std::uint8_t>((entry * 71U + 5U) & 0xFFU),
            static_cast<std::uint8_t>((entry * 113U + 7U) & 0xFFU),
            static_cast<std::uint8_t>((entry * 43U + 1U) & 0x7FU),
        };
    }

    bool identical = true;
    for (const std::uint32_t extent : extents)
    {
        std::vector<std::uint8_t> indices(
            static_cast<std::size_t>(extent) * extent);
        for (std::uint32_t y = 0U; y < extent; ++y)
        {
            for (std::uint32_t x = 0U; x < extent; ++x)
            {
                indices[static_cast<std::size_t>(y) * extent + x] =
                    static_cast<std::uint8_t>(
                        (x * 17U + y * 29U + 11U) & 0xFFU);
            }
        }
        const auto texture = MakeTexture(IndexedImageEncoding::Indexed8,
            FrontEndTextureAlphaMode::UsesPaletteAlpha, extent, extent,
            std::move(indices), palette);
        const float first_texel_center =
            static_cast<float>(0.5 / static_cast<double>(extent));
        const std::vector<float> coordinates =
            DifferentialCoordinates(extent);
        for (const float coordinate : coordinates)
        {
            const omega::asset::FrontendUvIR horizontal{
                .u = coordinate, .v = first_texel_center};
            const omega::asset::FrontendUvIR vertical{
                .u = first_texel_center, .v = coordinate};
            identical =
                identical &&
                ExactlyEqual(SampleRetailFrontEndTextureBilinearRepeat(
                                 texture, horizontal),
                    FrozenReferenceSample(texture, horizontal)) &&
                ExactlyEqual(SampleRetailFrontEndTextureBilinearRepeat(
                                 texture, vertical),
                    FrozenReferenceSample(texture, vertical));
        }
    }
    Check(identical,
        "the axis fast path is bit-for-bit identical to the frozen algorithm");
}

void TestNonFinitePrecedesInvalidBinding()
{
    const auto invalid = MakeTexture(IndexedImageEncoding::Indexed4,
        FrontEndTextureAlphaMode::UsesPaletteAlpha, 2U, 1U, {0U});
    CheckError(SampleRetailFrontEndTextureBilinearRepeat(invalid,
                   {.u = std::numeric_limits<float>::quiet_NaN(), .v = 0.0F}),
        RetailFrontEndTextureSamplingError::NonFiniteCoordinate,
        "NaN is rejected before an invalid binding");
    CheckError(SampleRetailFrontEndTextureBilinearRepeat(invalid,
                   {.u = 0.0F, .v = std::numeric_limits<float>::infinity()}),
        RetailFrontEndTextureSamplingError::NonFiniteCoordinate,
        "infinity is rejected before an invalid binding");
}
} // namespace

int main()
{
    TestPublicContract();
    TestIndexedTexelLookup();
    TestBilinearRepeatAndFractions();
    TestExplicitAlphaModes();
    TestFailuresAreTypedAndFailClosed();
    TestValidatedLayoutMatchesTheBindingOverload();
    TestDeterminism();
    TestAxisFastPathMatchesFrozenReference();
    TestNonFinitePrecedesInvalidBinding();

    if (failures != 0)
    {
        std::cerr << failures << " frontend texture sampler test(s) failed\n";
        return 1;
    }
    std::cout << "frontend texture sampler tests passed\n";
    return 0;
}
