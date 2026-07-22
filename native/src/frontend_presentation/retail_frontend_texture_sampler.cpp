#include "omega/frontend_presentation/retail_frontend_texture_sampler.h"

#include "omega/content/front_end_screen_bundle.h"

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace omega::frontend::presentation
{
namespace
{
struct ValidatedLayout final
{
    const asset::IndexedImageIR* image = nullptr;
    RetailFrontEndTextureAlphaContribution alpha_contribution =
        RetailFrontEndTextureAlphaContribution::Identity;
};

struct AxisTaps final
{
    std::uint32_t lower = 0U;
    std::uint32_t upper = 0U;
    double fraction = 0.0;
};

[[nodiscard]] std::expected<ValidatedLayout,
    RetailFrontEndTextureSamplingError>
Validate(const content::FrontEndTextureBinding& binding) noexcept
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

    return ValidatedLayout{
        .image = &image,
        .alpha_contribution = alpha_contribution,
    };
}

[[nodiscard]] RetailFrontEndTextureSamplingResult LookupValidated(
    const ValidatedLayout& layout,
    const std::uint32_t x,
    const std::uint32_t y) noexcept
{
    const auto& image = *layout.image;
    const std::uint64_t offset =
        static_cast<std::uint64_t>(y) * image.width + x;
    const std::uint8_t palette_index =
        image.indices[static_cast<std::size_t>(offset)];
    if (palette_index >= image.palette.size())
        return std::unexpected(
            RetailFrontEndTextureSamplingError::PaletteIndexOutOfRange);

    RgbaF modulation = NormalizeGsColor(image.palette[palette_index]);
    if (layout.alpha_contribution ==
        RetailFrontEndTextureAlphaContribution::Identity)
    {
        modulation.alpha = 1.0F;
    }
    return RetailFrontEndTextureSample{
        .modulation = modulation,
        .alpha_contribution = layout.alpha_contribution,
    };
}

[[nodiscard]] std::uint32_t WrapIndex(
    const std::int64_t index, const std::uint32_t extent) noexcept
{
    const std::int64_t signed_extent = static_cast<std::int64_t>(extent);
    std::int64_t wrapped = index % signed_extent;
    if (wrapped < 0)
        wrapped += signed_extent;
    return static_cast<std::uint32_t>(wrapped);
}

[[nodiscard]] AxisTaps ResolveAxis(
    const float normalized_coordinate, const std::uint32_t extent) noexcept
{
    double repeated = std::fmod(static_cast<double>(normalized_coordinate), 1.0);
    if (repeated < 0.0)
        repeated += 1.0;

    const double texel_coordinate =
        repeated * static_cast<double>(extent) - 0.5;
    const double lower_double = std::floor(texel_coordinate);
    const auto lower = static_cast<std::int64_t>(lower_double);
    return AxisTaps{
        .lower = WrapIndex(lower, extent),
        .upper = WrapIndex(lower + 1, extent),
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
    const auto layout = Validate(binding);
    if (!layout)
        return std::unexpected(layout.error());
    if (x >= layout->image->width || y >= layout->image->height)
        return std::unexpected(
            RetailFrontEndTextureSamplingError::TexelCoordinateOutOfRange);
    return LookupValidated(*layout, x, y);
}

RetailFrontEndTextureSamplingResult
SampleRetailFrontEndTextureBilinearRepeat(
    const content::FrontEndTextureBinding& binding,
    const asset::FrontendUvIR& normalized_st) noexcept
{
    if (!std::isfinite(normalized_st.u) || !std::isfinite(normalized_st.v))
        return std::unexpected(
            RetailFrontEndTextureSamplingError::NonFiniteCoordinate);

    const auto layout = Validate(binding);
    if (!layout)
        return std::unexpected(layout.error());

    const AxisTaps horizontal = ResolveAxis(normalized_st.u, layout->image->width);
    const AxisTaps vertical = ResolveAxis(normalized_st.v, layout->image->height);
    const auto upper_left =
        LookupValidated(*layout, horizontal.lower, vertical.lower);
    const auto upper_right =
        LookupValidated(*layout, horizontal.upper, vertical.lower);
    const auto lower_left =
        LookupValidated(*layout, horizontal.lower, vertical.upper);
    const auto lower_right =
        LookupValidated(*layout, horizontal.upper, vertical.upper);
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
        .alpha_contribution = layout->alpha_contribution,
    };
}
} // namespace omega::frontend::presentation
