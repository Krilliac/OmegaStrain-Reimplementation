#include "omega/runtime/canonical_level_scene_materials.h"

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

[[nodiscard]] bool AreTightenedLimits(const CanonicalLevelSceneMaterialLimits& limits) noexcept
{
    const CanonicalLevelSceneMaterialLimits maxima;
    return limits.scene.maximum_cells <= maxima.scene.maximum_cells &&
           limits.scene.maximum_positions <= maxima.scene.maximum_positions &&
           limits.scene.maximum_triangle_indices <= maxima.scene.maximum_triangle_indices &&
           limits.scene.maximum_output_bytes <= maxima.scene.maximum_output_bytes &&
           limits.maximum_catalogs <= maxima.maximum_catalogs &&
           limits.maximum_names <= maxima.maximum_names &&
           limits.maximum_materials <= maxima.maximum_materials &&
           limits.maximum_name_bytes <= maxima.maximum_name_bytes &&
           limits.maximum_name_length <= maxima.maximum_name_length &&
           limits.maximum_output_bytes <= maxima.maximum_output_bytes;
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

struct ValidationPlan
{
    std::uint64_t positions = 0U;
    std::uint64_t triangle_indices = 0U;
    std::uint64_t names = 0U;
    std::uint64_t materials = 0U;
    std::uint64_t name_bytes = 0U;
    std::uint64_t scene_output_bytes = sizeof(CanonicalLevelScene);
    std::uint64_t combined_output_bytes = sizeof(CanonicalLevelSceneWithMaterials);
};

[[nodiscard]] std::expected<void, std::string> CheckOutputBudgets(
    const ValidationPlan& plan, const CanonicalLevelSceneMaterialLimits& limits)
{
    if (plan.scene_output_bytes > limits.scene.maximum_output_bytes)
        return std::unexpected(
            "canonical level scene materials exceed the scene output-byte limit");
    if (plan.combined_output_bytes > limits.maximum_output_bytes)
        return std::unexpected(
            "canonical level scene materials exceed the total output-byte limit");
    return {};
}

[[nodiscard]] std::expected<ValidationPlan, std::string> InitializePlan(
    const std::uint64_t cell_count, const std::uint64_t catalog_count,
    const CanonicalLevelSceneMaterialLimits& limits)
{
    if (!AreTightenedLimits(limits))
        return std::unexpected(
            "canonical level scene material limits may only tighten safety maxima");

    if (cell_count != catalog_count)
        return std::unexpected("canonical level scene materials require matching "
                               "spatial and material cell counts");
    if (cell_count > limits.scene.maximum_cells)
        return std::unexpected("canonical level scene materials exceed the scene cell limit");
    if (cell_count > limits.maximum_catalogs)
        return std::unexpected("canonical level scene materials exceed the catalog limit");
    if (cell_count > std::numeric_limits<std::uint32_t>::max())
        return std::unexpected("canonical level scene material source ordinal count is invalid");

    ValidationPlan plan;
    std::uint64_t cell_bytes = 0U;
    std::uint64_t catalog_bytes = 0U;
    if (!Multiply(cell_count, sizeof(CanonicalLevelSceneCell), cell_bytes) ||
        !Multiply(catalog_count, sizeof(CanonicalLevelCellMaterialCatalog), catalog_bytes) ||
        !Add(plan.scene_output_bytes, cell_bytes, plan.scene_output_bytes) ||
        !Add(plan.combined_output_bytes, cell_bytes, plan.combined_output_bytes) ||
        !Add(plan.combined_output_bytes, catalog_bytes, plan.combined_output_bytes))
        return std::unexpected("canonical level scene material output byte size overflows");

    auto output_budgets = CheckOutputBudgets(plan, limits);
    if (!output_budgets)
        return std::unexpected(std::move(output_budgets.error()));
    return plan;
}

[[nodiscard]] std::expected<void, std::string> AccumulateGeometry(
    const std::uint64_t position_count, const std::uint64_t triangle_index_count,
    const CanonicalLevelSceneMaterialLimits& limits, ValidationPlan& plan)
{
    if (!Add(plan.positions, position_count, plan.positions))
        return std::unexpected("canonical level scene material position count overflows");
    if (plan.positions > limits.scene.maximum_positions)
        return std::unexpected("canonical level scene materials exceed the position limit");
    if (!Add(plan.triangle_indices, triangle_index_count, plan.triangle_indices))
        return std::unexpected("canonical level scene material triangle-index count overflows");
    if (plan.triangle_indices > limits.scene.maximum_triangle_indices)
        return std::unexpected("canonical level scene materials exceed the triangle-index limit");

    std::uint64_t position_bytes = 0U;
    std::uint64_t index_bytes = 0U;
    if (!Multiply(position_count, sizeof(asset::Float3IR), position_bytes) ||
        !Multiply(triangle_index_count, sizeof(std::uint32_t), index_bytes) ||
        !Add(plan.scene_output_bytes, position_bytes, plan.scene_output_bytes) ||
        !Add(plan.scene_output_bytes, index_bytes, plan.scene_output_bytes) ||
        !Add(plan.combined_output_bytes, position_bytes, plan.combined_output_bytes) ||
        !Add(plan.combined_output_bytes, index_bytes, plan.combined_output_bytes))
        return std::unexpected("canonical level scene material output byte size overflows");
    return CheckOutputBudgets(plan, limits);
}

[[nodiscard]] std::expected<void, std::string> MeasureSpatialMesh(
    const asset::SpatialMeshIR& mesh, const CanonicalLevelSceneMaterialLimits& limits,
    ValidationPlan& plan)
{
    std::uint64_t triangle_indices = 0U;
    if (!Multiply(mesh.triangles.size(), 3U, triangle_indices))
        return std::unexpected("canonical level scene material triangle-index count overflows");
    return AccumulateGeometry(mesh.vertices.size(), triangle_indices, limits, plan);
}

[[nodiscard]] std::expected<void, std::string> ValidateRenderMesh(
    const asset::RenderMeshIR& mesh, const CanonicalLevelSceneMaterialLimits& limits,
    ValidationPlan& plan)
{
    if (mesh.triangle_indices.size() % 3U != 0U)
        return std::unexpected("canonical level scene material mesh has an incomplete triangle");

    auto geometry =
        AccumulateGeometry(mesh.positions.size(), mesh.triangle_indices.size(), limits, plan);
    if (!geometry)
        return std::unexpected(std::move(geometry.error()));

    for (const asset::Float3IR& position : mesh.positions)
    {
        if (!std::isfinite(position.x) || !std::isfinite(position.y) ||
            !std::isfinite(position.z))
            return std::unexpected(
                "canonical level scene material position is non-finite");
    }
    for (const std::uint32_t index : mesh.triangle_indices)
    {
        if (index >= mesh.positions.size())
            return std::unexpected(
                "canonical level scene material triangle index is invalid");
    }
    return {};
}

[[nodiscard]] std::expected<void, std::string> ValidateMaterialCatalog(
    const asset::MaterialCatalogIR& catalog, const CanonicalLevelSceneMaterialLimits& limits,
    ValidationPlan& plan)
{
    if (!Add(plan.names, catalog.names.size(), plan.names) ||
        plan.names > limits.maximum_names)
        return std::unexpected("canonical level scene materials exceed the name limit");
    if (!Add(plan.materials, catalog.materials.size(), plan.materials) ||
        plan.materials > limits.maximum_materials)
        return std::unexpected("canonical level scene materials exceed the material limit");

    std::uint64_t name_object_bytes = 0U;
    std::uint64_t material_object_bytes = 0U;
    if (!Multiply(catalog.names.size(), sizeof(std::string), name_object_bytes) ||
        !Multiply(catalog.materials.size(), sizeof(asset::MaterialCatalogEntryIR),
                  material_object_bytes) ||
        !Add(plan.combined_output_bytes, name_object_bytes, plan.combined_output_bytes) ||
        !Add(plan.combined_output_bytes, material_object_bytes, plan.combined_output_bytes))
        return std::unexpected("canonical level scene material output byte size overflows");
    auto output_budgets = CheckOutputBudgets(plan, limits);
    if (!output_budgets)
        return std::unexpected(std::move(output_budgets.error()));

    for (const std::string& name : catalog.names)
    {
        if (name.size() > limits.maximum_name_length)
            return std::unexpected(
                "canonical level scene material name exceeds the length limit");
        if (!Add(plan.name_bytes, name.size(), plan.name_bytes))
            return std::unexpected("canonical level scene material name-byte count overflows");
        if (plan.name_bytes > limits.maximum_name_bytes)
            return std::unexpected(
                "canonical level scene materials exceed the name-byte limit");
        if (!Add(plan.combined_output_bytes, name.size(), plan.combined_output_bytes))
            return std::unexpected("canonical level scene material output byte size overflows");
        output_budgets = CheckOutputBudgets(plan, limits);
        if (!output_budgets)
            return std::unexpected(std::move(output_budgets.error()));
    }

    for (const asset::MaterialCatalogEntryIR& material : catalog.materials)
    {
        if (material.name_count > material.name_indices.size())
            return std::unexpected("canonical level scene material name count is invalid");
        for (std::size_t name_index = 0U; name_index < material.name_count; ++name_index)
        {
            if (material.name_indices[name_index] >= catalog.names.size())
                return std::unexpected("canonical level scene material name index is invalid");
        }
    }
    return {};
}

[[nodiscard]] std::expected<ValidationPlan, std::string> Preflight(
    const asset::LevelContentIR& content, const CanonicalLevelSceneMaterialLimits& limits)
{
    auto plan = InitializePlan(content.spatial.terrain_cells.size(),
                               content.material_catalogs.terrain_cells.size(), limits);
    if (!plan)
        return std::unexpected(std::move(plan.error()));

    for (std::size_t cell_index = 0U; cell_index < content.spatial.terrain_cells.size();
         ++cell_index)
    {
        const asset::SpatialMeshIR& mesh = content.spatial.terrain_cells[cell_index];
        auto geometry = MeasureSpatialMesh(mesh, limits, *plan);
        if (!geometry)
            return std::unexpected(std::move(geometry.error()));

        const asset::MaterialCatalogIR& catalog =
            content.material_catalogs.terrain_cells[cell_index];
        auto material = ValidateMaterialCatalog(catalog, limits, *plan);
        if (!material)
            return std::unexpected(std::move(material.error()));
    }
    return plan;
}

[[nodiscard]] std::expected<ValidationPlan, std::string> Preflight(
    const CanonicalLevelSceneWithMaterials& content,
    const CanonicalLevelSceneMaterialLimits& limits)
{
    auto plan = InitializePlan(content.scene.cells.size(), content.material_catalogs.size(), limits);
    if (!plan)
        return std::unexpected(std::move(plan.error()));

    if (!IsFiniteMatrix(content.scene.camera.world_to_view) ||
        !IsFiniteMatrix(content.scene.camera.view_to_clip))
        return std::unexpected("canonical level scene material camera is non-finite");

    for (std::size_t index = 0U; index < content.scene.cells.size(); ++index)
    {
        const SourceCellOrdinal expected{.value = static_cast<std::uint32_t>(index)};
        const CanonicalLevelSceneCell& cell = content.scene.cells[index];
        const CanonicalLevelCellMaterialCatalog& material_catalog =
            content.material_catalogs[index];
        if (cell.source_cell_ordinal != expected ||
            material_catalog.source_cell_ordinal != expected)
            return std::unexpected("canonical level scene material source ordinal is invalid");
        if (!IsFiniteMatrix(cell.local_to_world))
            return std::unexpected("canonical level scene material cell transform is non-finite");

        auto geometry = ValidateRenderMesh(cell.render_mesh, limits, *plan);
        if (!geometry)
            return std::unexpected(std::move(geometry.error()));
        auto material = ValidateMaterialCatalog(material_catalog.catalog, limits, *plan);
        if (!material)
            return std::unexpected(std::move(material.error()));
    }
    return plan;
}
} // namespace

std::expected<void, std::string> ValidateCanonicalLevelSceneMaterialAssociation(
    const CanonicalLevelSceneWithMaterials& content,
    const CanonicalLevelSceneMaterialLimits& limits)
{
    auto preflight = Preflight(content, limits);
    if (!preflight)
        return std::unexpected(std::move(preflight.error()));
    return {};
}

std::expected<CanonicalLevelSceneWithMaterials, std::string> BuildCanonicalLevelSceneWithMaterials(
    const asset::LevelContentIR& content, const CanonicalLevelSceneMaterialLimits& limits)
{
    auto preflight = Preflight(content, limits);
    if (!preflight)
        return std::unexpected(std::move(preflight.error()));

    auto scene = BuildCanonicalLevelScene(content.spatial, limits.scene);
    if (!scene)
        return std::unexpected(std::move(scene.error()));

    try
    {
        CanonicalLevelSceneWithMaterials result{.scene = std::move(*scene)};
        result.material_catalogs.reserve(content.material_catalogs.terrain_cells.size());
        for (std::size_t index = 0U; index < result.scene.cells.size(); ++index)
        {
            result.material_catalogs.push_back(CanonicalLevelCellMaterialCatalog{
                .source_cell_ordinal = result.scene.cells[index].source_cell_ordinal,
                .catalog = content.material_catalogs.terrain_cells[index],
            });
        }
        auto association = ValidateCanonicalLevelSceneMaterialAssociation(result, limits);
        if (!association)
            return std::unexpected(std::move(association.error()));
        return result;
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected("canonical level scene material allocation failed");
    }
    catch (const std::length_error&)
    {
        return std::unexpected("canonical level scene materials exceed host container capacity");
    }
}
} // namespace omega::runtime
