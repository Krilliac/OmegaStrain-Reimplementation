#include "omega/frontend_presentation/retail_title_compositor.h"

#include "omega/content/front_end_screen_bundle.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <new>
#include <string_view>

namespace omega::frontend::presentation
{
namespace
{
constexpr std::array<Point2F, 4U> kCanonicalCoverCorners{
    Point2F{0.0F, 0.0F},
    Point2F{static_cast<float>(kCanonicalRasterWidth), 0.0F},
    Point2F{static_cast<float>(kCanonicalRasterWidth),
        static_cast<float>(kCanonicalRasterHeight)},
    Point2F{0.0F, static_cast<float>(kCanonicalRasterHeight)},
};

constexpr std::array<asset::FrontendTriangleIR, 2U> kCanonicalCoverTriangles{
    asset::FrontendTriangleIR{
        .position_indices = {0U, 1U, 2U},
        .uv_indices = {0U, 0U, 0U},
        .color_indices = {0U, 0U, 0U},
    },
    asset::FrontendTriangleIR{
        .position_indices = {0U, 2U, 3U},
        .uv_indices = {0U, 0U, 0U},
        .color_indices = {0U, 0U, 0U},
    },
};

[[nodiscard]] bool IsCanonicalWidgetRectangle(
    const asset::FrontendWidgetRectangleIR& rectangle) noexcept
{
    return rectangle.left == -320.0F && rectangle.top == 224.0F &&
           rectangle.width == 640.0F && rectangle.height == 448.0F;
}

[[nodiscard]] bool HasTextSemantics(const asset::FrontendWidgetIR& widget) noexcept
{
    return widget.kind == asset::FrontendWidgetKind::Text ||
           widget.kind == asset::FrontendWidgetKind::Button ||
           widget.text_reference.has_value() || widget.font_reference.has_value() ||
           widget.text_color.has_value() || widget.text_alignment.has_value();
}

[[nodiscard]] bool IsIdentity(const std::array<float, 12U>& transform) noexcept
{
    return transform == kIdentityAffineTransform12.column_vectors;
}

[[nodiscard]] bool HasValidIndices(const asset::FrontendVisualNodeIR& node) noexcept
{
    for (const auto& triangle : node.triangles)
    {
        for (const auto index : triangle.position_indices)
        {
            if (index >= node.positions.size())
                return false;
        }
        for (const auto index : triangle.uv_indices)
        {
            if (index >= node.uvs.size())
                return false;
        }
        for (const auto index : triangle.color_indices)
        {
            if (index >= node.colors.size())
                return false;
        }
    }
    return true;
}

[[nodiscard]] std::expected<Point2F, RetailTitleCompositionError> ProjectPosition(
    const asset::Float3IR& position) noexcept
{
    const auto bridged = TransformPoint(kInterfaceElementAxisBridge, position);
    if (!bridged)
        return std::unexpected(RetailTitleCompositionError::ArithmeticOverflow);
    if (bridged->y != 0.0F)
        return std::unexpected(RetailTitleCompositionError::UnsupportedProjection);

    const double gui_y_double = static_cast<double>(bridged->z) - 1.0;
    constexpr double maximum_float =
        static_cast<double>(std::numeric_limits<float>::max());
    if (!std::isfinite(gui_y_double) || gui_y_double < -maximum_float ||
        gui_y_double > maximum_float)
    {
        return std::unexpected(RetailTitleCompositionError::ArithmeticOverflow);
    }

    const auto raster = GuiToCanonicalRaster(
        Point2F{bridged->x, static_cast<float>(gui_y_double)});
    if (!raster)
        return std::unexpected(RetailTitleCompositionError::ArithmeticOverflow);
    if (raster->x < 0.0F || raster->x > static_cast<float>(kCanonicalRasterWidth) ||
        raster->y < 0.0F || raster->y > static_cast<float>(kCanonicalRasterHeight))
    {
        return std::unexpected(RetailTitleCompositionError::GeometryOutOfBounds);
    }
    return *raster;
}

[[nodiscard]] bool HasCanonicalIdentifierSuffix(
    const std::string_view widget_identifier,
    const std::string_view visual_identifier) noexcept
{
    constexpr std::string_view suffix = "_root";
    return !widget_identifier.empty() &&
           visual_identifier.size() == widget_identifier.size() + suffix.size() &&
           visual_identifier.starts_with(widget_identifier) &&
           visual_identifier.ends_with(suffix);
}
} // namespace

RetailTitleCompositionResult ComposeStaticRetailTitle(
    const content::FrontEndScreenBundle& bundle) noexcept
{
    if (!bundle.presentation_capability().valid())
        return std::unexpected(RetailTitleCompositionError::InvalidRetailCapability);
    if (bundle.key() != content::FrontEndScreenKey::Title)
        return std::unexpected(RetailTitleCompositionError::UnsupportedScreen);

    const auto& widget = bundle.widget_document().root;
    if (widget.kind != asset::FrontendWidgetKind::Container || !widget.visible ||
        !widget.enabled || !IsCanonicalWidgetRectangle(widget.rectangle) ||
        !widget.children.empty() || !widget.binding)
    {
        return std::unexpected(RetailTitleCompositionError::UnsupportedWidgetSemantics);
    }
    if (HasTextSemantics(widget))
        return std::unexpected(RetailTitleCompositionError::UnsupportedTextEncoding);
    if (!widget.binding->actions.empty())
        return std::unexpected(RetailTitleCompositionError::UnsupportedActionSemantics);
    if (!widget.binding->scope_reference.empty() ||
        !widget.binding->resource_reference.empty())
    {
        return std::unexpected(RetailTitleCompositionError::UnsupportedVisualHierarchy);
    }
    if (bundle.visual_scopes().size() != 1U)
        return std::unexpected(RetailTitleCompositionError::UnsupportedVisualHierarchy);
    const auto* const scope = bundle.FindVisualScope({});
    if (scope == nullptr || scope->resources().size() != 1U)
        return std::unexpected(RetailTitleCompositionError::UnsupportedVisualHierarchy);

    const auto* const visual = bundle.ResolveVisualBinding(widget, true);
    if (visual == nullptr || visual != &scope->document().root ||
        !HasCanonicalIdentifierSuffix(widget.identifier, visual->identifier) ||
        !visual->children.empty())
    {
        return std::unexpected(RetailTitleCompositionError::UnsupportedVisualHierarchy);
    }
    if (visual->texture_member.has_value())
    {
        const auto texture = bundle.ResolveVisualTextureBinding(widget, true);
        if (!texture || &texture->scope() != scope ||
            texture->owning_scope() != bundle.primary_scope())
        {
            return std::unexpected(RetailTitleCompositionError::MissingTextureBinding);
        }
        return std::unexpected(RetailTitleCompositionError::UnsupportedTextureSampling);
    }
    if (!scope->textures().empty())
        return std::unexpected(RetailTitleCompositionError::UnsupportedTextureSampling);
    if (!visual->animation_tracks.empty())
        return std::unexpected(RetailTitleCompositionError::UnsupportedAnimation);
    const AffineTransform12 binding_transform{
        .column_vectors = widget.binding->transform_values,
    };
    const AffineTransform12 visual_transform{
        .column_vectors = visual->transform_values,
    };
    const auto composed_transform =
        ComposeAffineTransforms(binding_transform, visual_transform);
    if (!composed_transform)
        return std::unexpected(RetailTitleCompositionError::ArithmeticOverflow);
    if (!IsIdentity(widget.binding->transform_values) ||
        !IsIdentity(visual->transform_values))
    {
        return std::unexpected(RetailTitleCompositionError::UnsupportedTransform);
    }

    if (visual->positions.size() != 4U || visual->uvs.size() != 1U ||
        visual->colors.size() != 1U || visual->triangles.size() != 2U)
    {
        return std::unexpected(RetailTitleCompositionError::IncompleteCoverage);
    }
    if (!HasValidIndices(*visual))
        return std::unexpected(RetailTitleCompositionError::InvalidGeometry);
    if (visual->uvs[0] != asset::FrontendUvIR{0.0F, 0.0F})
        return std::unexpected(RetailTitleCompositionError::UnsupportedRasterization);
    if (visual->triangles[0] != kCanonicalCoverTriangles[0] ||
        visual->triangles[1] != kCanonicalCoverTriangles[1])
    {
        return std::unexpected(RetailTitleCompositionError::IncompleteCoverage);
    }

    for (std::size_t index = 0U; index < visual->positions.size(); ++index)
    {
        const auto raster = ProjectPosition(visual->positions[index]);
        if (!raster)
            return std::unexpected(raster.error());
        if (*raster != kCanonicalCoverCorners[index])
            return std::unexpected(RetailTitleCompositionError::UnsupportedRasterization);
    }

    const auto normalized = ModulateVertexColor(
        visual->colors[0], RgbaF{1.0F, 1.0F, 1.0F, 1.0F});
    if (!normalized)
        return std::unexpected(RetailTitleCompositionError::InvalidGeometry);
    if (visual->colors[0].alpha != 255U || normalized->alpha != 1.0F)
        return std::unexpected(RetailTitleCompositionError::UnsupportedOutputAlpha);

    OwnedRgba8Frame result;
    try
    {
        result.pixels.resize(static_cast<std::size_t>(kRetailTitleFrameByteCount));
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected(RetailTitleCompositionError::AllocationFailure);
    }

    for (std::size_t offset = 0U; offset < result.pixels.size(); offset += 4U)
    {
        result.pixels[offset] = visual->colors[0].red;
        result.pixels[offset + 1U] = visual->colors[0].green;
        result.pixels[offset + 2U] = visual->colors[0].blue;
        result.pixels[offset + 3U] = 255U;
    }
    return result;
}
} // namespace omega::frontend::presentation
