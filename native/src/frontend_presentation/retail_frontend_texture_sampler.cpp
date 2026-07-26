#include "omega/frontend_presentation/retail_frontend_texture_sampler.h"

#include "omega/content/front_end_screen_bundle.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace omega::frontend::presentation
{
namespace
{
using ValidatedTextureAccess =
    detail::RetailFrontEndValidatedTextureAccess;

struct AxisTaps final
{
    std::uint32_t lower = 0U;
    std::uint32_t upper = 0U;
    double fraction = 0.0;
};

[[nodiscard]] RetailFrontEndTextureSamplingResult LookupValidated(
    const std::uint8_t* const indices,
    const std::uint32_t palette_size,
    const std::uint32_t width,
    const std::array<RgbaF, 256U>& normalized_palette,
    const RetailFrontEndTextureAlphaContribution alpha_contribution,
    const std::uint32_t x,
    const std::uint32_t y) noexcept
{
    const std::uint64_t offset =
        static_cast<std::uint64_t>(y) * width + x;
    const std::uint8_t palette_index =
        indices[static_cast<std::size_t>(offset)];
    if (static_cast<std::uint32_t>(palette_index) >= palette_size)
    {
        return std::unexpected(
            RetailFrontEndTextureSamplingError::PaletteIndexOutOfRange);
    }

    return RetailFrontEndTextureSample{
        .modulation = normalized_palette[palette_index],
        .alpha_contribution = alpha_contribution,
    };
}

[[nodiscard]] AxisTaps ResolveAxis(
    const float normalized_coordinate, const std::uint32_t extent) noexcept
{
    const double coordinate = static_cast<double>(normalized_coordinate);
    double repeated = coordinate;
    if (!(normalized_coordinate >= 0.0F && normalized_coordinate < 1.0F))
        repeated = std::fmod(coordinate, 1.0);
    if (repeated < 0.0)
        repeated += 1.0;

    const double texel_coordinate =
        repeated * static_cast<double>(extent) - 0.5;
    const double lower_double = std::floor(texel_coordinate);
    const auto lower = static_cast<std::int64_t>(lower_double);
    const auto signed_extent = static_cast<std::int64_t>(extent);
    // Validation proves extent is positive. Repeating yields [0, 1] (the
    // inclusive upper endpoint is possible when adding a tiny negative
    // remainder rounds to 1.0), so lower is bounded to [-1, extent - 1].
    const std::uint32_t lower_index =
        lower < 0 ? extent - 1U : static_cast<std::uint32_t>(lower);
    const std::uint32_t upper_index =
        lower >= signed_extent - 1
            ? 0U
            : static_cast<std::uint32_t>(lower + 1);
    return AxisTaps{
        .lower = lower_index,
        .upper = upper_index,
        .fraction = texel_coordinate - lower_double,
    };
}

[[nodiscard]] float Lerp(
    const float left, const float right, const double fraction) noexcept
{
    return static_cast<float>(
        static_cast<double>(left) +
        (static_cast<double>(right) - static_cast<double>(left)) * fraction);
}

[[nodiscard]] RgbaF Lerp(
    const RgbaF& left, const RgbaF& right, const double fraction) noexcept
{
    return RgbaF{
        Lerp(left.red, right.red, fraction),
        Lerp(left.green, right.green, fraction),
        Lerp(left.blue, right.blue, fraction),
        Lerp(left.alpha, right.alpha, fraction),
    };
}
} // namespace

RetailFrontEndTextureSamplingResult LookupRetailFrontEndTexel(
    const content::FrontEndTextureBinding& binding,
    const std::uint32_t x,
    const std::uint32_t y) noexcept
{
    const auto layout = ValidateRetailFrontEndTexture(binding);
    if (!layout)
        return std::unexpected(layout.error());
    if (x >= ValidatedTextureAccess::Width(*layout) ||
        y >= ValidatedTextureAccess::Height(*layout))
    {
        return std::unexpected(
            RetailFrontEndTextureSamplingError::TexelCoordinateOutOfRange);
    }
    return LookupValidated(ValidatedTextureAccess::Indices(*layout),
        ValidatedTextureAccess::PaletteSize(*layout),
        ValidatedTextureAccess::Width(*layout),
        ValidatedTextureAccess::NormalizedPalette(*layout),
        ValidatedTextureAccess::AlphaContribution(*layout), x, y);
}

RetailFrontEndTextureValidationResult ValidateRetailFrontEndTexture(
    const content::FrontEndTextureBinding& binding) noexcept
{
    const auto& image = binding.image();
    if (image.width == 0U || image.height == 0U)
        return std::unexpected(RetailFrontEndTextureSamplingError::EmptyImage);
    if (image.width > kRetailFrontEndTextureMaximumDimension ||
        image.height > kRetailFrontEndTextureMaximumDimension)
    {
        return std::unexpected(
            RetailFrontEndTextureSamplingError::DimensionLimitExceeded);
    }

    std::size_t expected_palette_size = 0U;
    switch (binding.sampling_encoding())
    {
    case asset::IndexedImageEncoding::Indexed4:
        expected_palette_size = 16U;
        break;
    case asset::IndexedImageEncoding::Indexed8:
        expected_palette_size = 256U;
        break;
    default:
        return std::unexpected(
            RetailFrontEndTextureSamplingError::UnsupportedEncoding);
    }
    if (image.source_encoding != binding.sampling_encoding())
        return std::unexpected(
            RetailFrontEndTextureSamplingError::UnsupportedEncoding);

    const std::uint64_t expected_index_count =
        static_cast<std::uint64_t>(image.width) *
        static_cast<std::uint64_t>(image.height);
    if (expected_index_count > kRetailFrontEndTextureMaximumPixels ||
        image.indices.size() != expected_index_count)
    {
        return std::unexpected(
            RetailFrontEndTextureSamplingError::InvalidIndexStorage);
    }
    if (image.palette.size() != expected_palette_size)
        return std::unexpected(
            RetailFrontEndTextureSamplingError::InvalidPaletteStorage);

    RetailFrontEndTextureAlphaContribution alpha_contribution;
    switch (binding.alpha_mode())
    {
    case content::FrontEndTextureAlphaMode::IgnoresTextureAlpha:
        alpha_contribution = RetailFrontEndTextureAlphaContribution::Identity;
        break;
    case content::FrontEndTextureAlphaMode::UsesPaletteAlpha:
        alpha_contribution = RetailFrontEndTextureAlphaContribution::Palette;
        break;
    default:
        return std::unexpected(
            RetailFrontEndTextureSamplingError::UnsupportedAlphaMode);
    }

    std::array<RgbaF, 256U> normalized_palette{};
    // Exactly the value the per-tap path used to recompute: the normalized GS
    // color, with the Identity alpha substitution already applied. Both are
    // pure functions of data that cannot change while the layout is valid, so
    // resolving them here yields the identical float for every tap.
    for (std::size_t entry = 0U; entry < image.palette.size(); ++entry)
    {
        RgbaF modulation = NormalizeGsColor(image.palette[entry]);
        if (alpha_contribution ==
            RetailFrontEndTextureAlphaContribution::Identity)
        {
            modulation.alpha = 1.0F;
        }
        normalized_palette[entry] = modulation;
    }
    return ValidatedTextureAccess::Create(image.indices.data(),
        static_cast<std::uint32_t>(image.palette.size()), image.width,
        image.height, alpha_contribution, std::move(normalized_palette));
}

RetailFrontEndTextureSamplingResult
SampleRetailFrontEndTextureBilinearRepeat(
    const RetailFrontEndValidatedTexture& texture,
    const asset::FrontendUvIR& normalized_st) noexcept
{
    if (!std::isfinite(normalized_st.u) || !std::isfinite(normalized_st.v))
        return std::unexpected(
            RetailFrontEndTextureSamplingError::NonFiniteCoordinate);

    const AxisTaps horizontal = ResolveAxis(
        normalized_st.u, ValidatedTextureAccess::Width(texture));
    const AxisTaps vertical = ResolveAxis(
        normalized_st.v, ValidatedTextureAccess::Height(texture));
    const std::uint8_t* const indices =
        ValidatedTextureAccess::Indices(texture);
    const std::uint32_t palette_size =
        ValidatedTextureAccess::PaletteSize(texture);
    const std::uint32_t width = ValidatedTextureAccess::Width(texture);
    const auto& normalized_palette =
        ValidatedTextureAccess::NormalizedPalette(texture);
    const RetailFrontEndTextureAlphaContribution alpha_contribution =
        ValidatedTextureAccess::AlphaContribution(texture);
    const auto upper_left = LookupValidated(indices, palette_size, width,
        normalized_palette, alpha_contribution, horizontal.lower,
        vertical.lower);
    const auto upper_right = LookupValidated(indices, palette_size, width,
        normalized_palette, alpha_contribution, horizontal.upper,
        vertical.lower);
    const auto lower_left = LookupValidated(indices, palette_size, width,
        normalized_palette, alpha_contribution, horizontal.lower,
        vertical.upper);
    const auto lower_right = LookupValidated(indices, palette_size, width,
        normalized_palette, alpha_contribution, horizontal.upper,
        vertical.upper);
    if (!upper_left)
        return std::unexpected(upper_left.error());
    if (!upper_right)
        return std::unexpected(upper_right.error());
    if (!lower_left)
        return std::unexpected(lower_left.error());
    if (!lower_right)
        return std::unexpected(lower_right.error());

    const RgbaF upper = Lerp(
        upper_left->modulation, upper_right->modulation, horizontal.fraction);
    const RgbaF lower = Lerp(
        lower_left->modulation, lower_right->modulation, horizontal.fraction);
    return RetailFrontEndTextureSample{
        .modulation = Lerp(upper, lower, vertical.fraction),
        .alpha_contribution = alpha_contribution,
    };
}

RetailFrontEndTextureSamplingResult
SampleRetailFrontEndTextureBilinearRepeat(
    const content::FrontEndTextureBinding& binding,
    const asset::FrontendUvIR& normalized_st) noexcept
{
    if (!std::isfinite(normalized_st.u) || !std::isfinite(normalized_st.v))
        return std::unexpected(
            RetailFrontEndTextureSamplingError::NonFiniteCoordinate);

    const auto layout = ValidateRetailFrontEndTexture(binding);
    if (!layout)
        return std::unexpected(layout.error());
    return SampleRetailFrontEndTextureBilinearRepeat(*layout, normalized_st);
}
} // namespace omega::frontend::presentation
