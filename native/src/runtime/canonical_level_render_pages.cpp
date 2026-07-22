#include "omega/runtime/canonical_level_render_pages.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace omega::runtime
{
namespace
{
[[nodiscard]] bool Add(const std::uint64_t left, const std::uint64_t right,
                       std::uint64_t& output) noexcept
{
    if (right > std::numeric_limits<std::uint64_t>::max() - left)
        return false;
    output = left + right;
    return true;
}

[[nodiscard]] bool Multiply(const std::uint64_t left, const std::uint64_t right,
                            std::uint64_t& output) noexcept
{
    if (left != 0U && right > std::numeric_limits<std::uint64_t>::max() / left)
        return false;
    output = left * right;
    return true;
}

[[nodiscard]] bool IsFiniteMatrix(const asset::Matrix4x4IR& matrix) noexcept
{
    for (const float element : matrix.row_major)
    {
        if (!std::isfinite(element))
            return false;
    }
    return true;
}

[[nodiscard]] bool AreTightenedLimits(const CanonicalLevelRenderPageLimits& limits) noexcept
{
    const CanonicalLevelRenderPageLimits maxima;
    return limits.maximum_source_cells <= maxima.maximum_source_cells &&
           limits.maximum_renderable_cells <= maxima.maximum_renderable_cells &&
           limits.maximum_pages <= maxima.maximum_pages &&
           limits.maximum_meshes_per_page <= maxima.maximum_meshes_per_page &&
           limits.maximum_positions <= maxima.maximum_positions &&
           limits.maximum_triangle_indices <= maxima.maximum_triangle_indices &&
           limits.maximum_output_bytes <= maxima.maximum_output_bytes;
}

struct RenderPagePlan
{
    std::uint64_t renderable_cells = 0U;
    std::uint64_t non_renderable_cells = 0U;
    std::uint64_t page_count = 0U;
    std::uint64_t inspected_positions = 0U;
    std::uint64_t inspected_triangle_indices = 0U;
    std::uint64_t output_positions = 0U;
    std::uint64_t output_triangle_indices = 0U;
    std::uint64_t logical_output_bytes = sizeof(CanonicalLevelRenderPages);
};

[[nodiscard]] std::expected<RenderPagePlan, std::string> Preflight(
    const CanonicalLevelScene& canonical, const CanonicalLevelRenderPageLimits& limits)
{
    if (!AreTightenedLimits(limits))
        return std::unexpected("canonical level render page limits may only tighten safety maxima");
    if (limits.maximum_meshes_per_page == 0U)
        return std::unexpected("canonical level render page mesh capacity must be nonzero");

    const std::uint64_t source_cells = static_cast<std::uint64_t>(canonical.cells.size());
    if (source_cells > limits.maximum_source_cells)
        return std::unexpected("canonical level render pages exceed the source-cell limit");
    if (!IsFiniteMatrix(canonical.camera.world_to_view) ||
        !IsFiniteMatrix(canonical.camera.view_to_clip))
        return std::unexpected("canonical level render page camera is non-finite");

    RenderPagePlan plan;
    for (std::size_t cell_index = 0U; cell_index < canonical.cells.size(); ++cell_index)
    {
        const CanonicalLevelSceneCell& cell = canonical.cells[cell_index];
        if (cell.source_cell_ordinal !=
            SourceCellOrdinal{.value = static_cast<std::uint32_t>(cell_index)})
            return std::unexpected("canonical level render page source ordinal is invalid");
        if (!IsFiniteMatrix(cell.local_to_world))
            return std::unexpected("canonical level render page cell transform is non-finite");

        const asset::RenderMeshIR& mesh = cell.render_mesh;
        if (!Add(plan.inspected_positions, mesh.positions.size(), plan.inspected_positions) ||
            plan.inspected_positions > limits.maximum_positions)
            return std::unexpected("canonical level render pages exceed the position limit");
        if (!Add(plan.inspected_triangle_indices, mesh.triangle_indices.size(),
                 plan.inspected_triangle_indices) ||
            plan.inspected_triangle_indices > limits.maximum_triangle_indices)
            return std::unexpected("canonical level render pages exceed the triangle-index limit");

        for (const asset::Float3IR& position : mesh.positions)
        {
            if (!std::isfinite(position.x) || !std::isfinite(position.y) ||
                !std::isfinite(position.z))
                return std::unexpected("canonical level render page position is non-finite");
        }
        if (mesh.triangle_indices.size() % 3U != 0U)
            return std::unexpected("canonical level render page mesh has an incomplete triangle");
        for (const std::uint32_t index : mesh.triangle_indices)
        {
            if (index >= mesh.positions.size())
                return std::unexpected("canonical level render page triangle index is invalid");
        }

        if (mesh.positions.empty() || mesh.triangle_indices.empty())
            ++plan.non_renderable_cells;
        else
        {
            ++plan.renderable_cells;
            if (!Add(plan.output_positions, mesh.positions.size(), plan.output_positions) ||
                !Add(plan.output_triangle_indices, mesh.triangle_indices.size(),
                     plan.output_triangle_indices))
                return std::unexpected("canonical level render page output count overflows");
        }
    }
    if (plan.renderable_cells > limits.maximum_renderable_cells)
        return std::unexpected("canonical level render pages exceed the renderable-cell limit");

    plan.page_count = plan.renderable_cells / limits.maximum_meshes_per_page;
    if (plan.renderable_cells % limits.maximum_meshes_per_page != 0U)
        ++plan.page_count;
    if (plan.page_count > limits.maximum_pages)
        return std::unexpected("canonical level render pages exceed the page limit");

    std::uint64_t page_bytes = 0U;
    std::uint64_t non_renderable_bytes = 0U;
    std::uint64_t mesh_bytes = 0U;
    std::uint64_t instance_bytes = 0U;
    std::uint64_t mapping_bytes = 0U;
    std::uint64_t position_bytes = 0U;
    std::uint64_t index_bytes = 0U;
    if (!Multiply(plan.page_count, sizeof(CanonicalLevelRenderPage), page_bytes) ||
        !Multiply(plan.non_renderable_cells, sizeof(SourceCellOrdinal), non_renderable_bytes) ||
        !Multiply(plan.renderable_cells, sizeof(asset::RenderMeshIR), mesh_bytes) ||
        !Multiply(plan.renderable_cells, sizeof(asset::SceneMeshInstanceIR), instance_bytes) ||
        !Multiply(plan.renderable_cells, sizeof(CanonicalLevelRenderMeshMapping), mapping_bytes) ||
        !Multiply(plan.output_positions, sizeof(asset::Float3IR), position_bytes) ||
        !Multiply(plan.output_triangle_indices, sizeof(std::uint32_t), index_bytes) ||
        !Add(plan.logical_output_bytes, page_bytes, plan.logical_output_bytes) ||
        !Add(plan.logical_output_bytes, non_renderable_bytes, plan.logical_output_bytes) ||
        !Add(plan.logical_output_bytes, mesh_bytes, plan.logical_output_bytes) ||
        !Add(plan.logical_output_bytes, instance_bytes, plan.logical_output_bytes) ||
        !Add(plan.logical_output_bytes, mapping_bytes, plan.logical_output_bytes) ||
        !Add(plan.logical_output_bytes, position_bytes, plan.logical_output_bytes) ||
        !Add(plan.logical_output_bytes, index_bytes, plan.logical_output_bytes))
        return std::unexpected("canonical level render page output byte size overflows");
    if (plan.logical_output_bytes > limits.maximum_output_bytes)
        return std::unexpected("canonical level render pages exceed the output-byte limit");
    return plan;
}
} // namespace

std::expected<CanonicalLevelRenderPages, std::string> BuildCanonicalLevelRenderPages(
    const CanonicalLevelScene& canonical, const CanonicalLevelRenderPageLimits& limits)
{
    auto plan = Preflight(canonical, limits);
    if (!plan)
        return std::unexpected(std::move(plan.error()));

    try
    {
        CanonicalLevelRenderPages output;
        output.pages.reserve(static_cast<std::size_t>(plan->page_count));
        output.non_renderable_source_cells.reserve(
            static_cast<std::size_t>(plan->non_renderable_cells));

        for (const CanonicalLevelSceneCell& cell : canonical.cells)
        {
            if (cell.render_mesh.positions.empty() || cell.render_mesh.triangle_indices.empty())
            {
                output.non_renderable_source_cells.push_back(cell.source_cell_ordinal);
                continue;
            }

            if (output.pages.empty() ||
                output.pages.back().scene.render_meshes.size() == limits.maximum_meshes_per_page)
            {
                CanonicalLevelRenderPage page;
                page.scene.camera = canonical.camera;
                const std::uint64_t remaining =
                    plan->renderable_cells - static_cast<std::uint64_t>(output.pages.size()) *
                                                 limits.maximum_meshes_per_page;
                const auto page_capacity =
                    static_cast<std::size_t>(std::min(remaining, limits.maximum_meshes_per_page));
                page.scene.render_meshes.reserve(page_capacity);
                page.scene.mesh_instances.reserve(page_capacity);
                page.mesh_mappings.reserve(page_capacity);
                output.pages.push_back(std::move(page));
            }

            CanonicalLevelRenderPage& page = output.pages.back();
            const auto render_mesh_index =
                static_cast<std::uint32_t>(page.scene.render_meshes.size());
            page.scene.render_meshes.push_back(cell.render_mesh);
            page.scene.mesh_instances.push_back(asset::SceneMeshInstanceIR{
                .render_mesh_index = render_mesh_index,
                .local_to_world = cell.local_to_world,
            });
            page.mesh_mappings.push_back(CanonicalLevelRenderMeshMapping{
                .source_cell_ordinal = cell.source_cell_ordinal,
                .render_mesh_index = render_mesh_index,
            });
        }
        return output;
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected("canonical level render page allocation failed");
    }
    catch (const std::length_error&)
    {
        return std::unexpected("canonical level render pages exceed host container capacity");
    }
}
} // namespace omega::runtime
