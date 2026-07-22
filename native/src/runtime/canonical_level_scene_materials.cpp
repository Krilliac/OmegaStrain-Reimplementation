#include "omega/runtime/canonical_level_scene_materials.h"

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

[[nodiscard]] std::expected<void, std::string> Preflight(
    const asset::LevelContentIR& content, const CanonicalLevelSceneMaterialLimits& limits)
{
    if (!AreTightenedLimits(limits))
        return std::unexpected(
            "canonical level scene material limits may only tighten safety maxima");

    const std::uint64_t cell_count =
        static_cast<std::uint64_t>(content.spatial.terrain_cells.size());
    if (cell_count != static_cast<std::uint64_t>(content.material_catalogs.terrain_cells.size()))
        return std::unexpected("canonical level scene materials require matching "
                               "spatial and material cell counts");
    if (cell_count > limits.maximum_catalogs)
        return std::unexpected("canonical level scene materials exceed the catalog limit");

    std::uint64_t logical_output_bytes = sizeof(CanonicalLevelSceneWithMaterials);
    std::uint64_t cell_bytes = 0U;
    std::uint64_t catalog_bytes = 0U;
    if (!Multiply(cell_count, sizeof(CanonicalLevelSceneCell), cell_bytes) ||
        !Multiply(cell_count, sizeof(CanonicalLevelCellMaterialCatalog), catalog_bytes) ||
        !Add(logical_output_bytes, cell_bytes, logical_output_bytes) ||
        !Add(logical_output_bytes, catalog_bytes, logical_output_bytes))
        return std::unexpected("canonical level scene material output byte size overflows");

    std::uint64_t total_names = 0U;
    std::uint64_t total_materials = 0U;
    std::uint64_t total_name_bytes = 0U;
    for (std::size_t cell_index = 0U; cell_index < content.spatial.terrain_cells.size();
         ++cell_index)
    {
        const asset::SpatialMeshIR& mesh = content.spatial.terrain_cells[cell_index];
        std::uint64_t position_bytes = 0U;
        std::uint64_t triangle_indices = 0U;
        std::uint64_t index_bytes = 0U;
        if (!Multiply(mesh.vertices.size(), sizeof(asset::Float3IR), position_bytes) ||
            !Multiply(mesh.triangles.size(), 3U, triangle_indices) ||
            !Multiply(triangle_indices, sizeof(std::uint32_t), index_bytes) ||
            !Add(logical_output_bytes, position_bytes, logical_output_bytes) ||
            !Add(logical_output_bytes, index_bytes, logical_output_bytes))
            return std::unexpected("canonical level scene material output byte size overflows");

        const asset::MaterialCatalogIR& catalog =
            content.material_catalogs.terrain_cells[cell_index];
        if (!Add(total_names, catalog.names.size(), total_names) ||
            total_names > limits.maximum_names)
            return std::unexpected("canonical level scene materials exceed the name limit");
        if (!Add(total_materials, catalog.materials.size(), total_materials) ||
            total_materials > limits.maximum_materials)
            return std::unexpected("canonical level scene materials exceed the material limit");

        std::uint64_t name_object_bytes = 0U;
        std::uint64_t material_object_bytes = 0U;
        if (!Multiply(catalog.names.size(), sizeof(std::string), name_object_bytes) ||
            !Multiply(catalog.materials.size(), sizeof(asset::MaterialCatalogEntryIR),
                      material_object_bytes) ||
            !Add(logical_output_bytes, name_object_bytes, logical_output_bytes) ||
            !Add(logical_output_bytes, material_object_bytes, logical_output_bytes))
            return std::unexpected("canonical level scene material output byte size overflows");

        for (const std::string& name : catalog.names)
        {
            if (name.size() > limits.maximum_name_length)
                return std::unexpected(
                    "canonical level scene material name exceeds the length limit");
            if (!Add(total_name_bytes, name.size(), total_name_bytes) ||
                total_name_bytes > limits.maximum_name_bytes ||
                !Add(logical_output_bytes, name.size(), logical_output_bytes))
                return std::unexpected(
                    "canonical level scene materials exceed the name-byte limit");
        }
        for (const asset::MaterialCatalogEntryIR& material : catalog.materials)
        {
            if (material.name_count > material.name_indices.size())
                return std::unexpected("canonical level scene material name count is invalid");
            for (std::uint8_t name_index = 0U; name_index < material.name_count; ++name_index)
            {
                if (material.name_indices[name_index] >= catalog.names.size())
                    return std::unexpected("canonical level scene material name index is invalid");
            }
        }
    }
    if (logical_output_bytes > limits.maximum_output_bytes)
        return std::unexpected(
            "canonical level scene materials exceed the total output-byte limit");
    return {};
}
} // namespace

std::expected<void, std::string> ValidateCanonicalLevelSceneMaterialAssociation(
    const CanonicalLevelSceneWithMaterials& content)
{
    if (content.scene.cells.size() != content.material_catalogs.size())
        return std::unexpected("canonical level scene material association cardinality is invalid");
    if (content.scene.cells.size() > std::numeric_limits<std::uint32_t>::max())
        return std::unexpected("canonical level scene material source ordinal count is invalid");
    for (std::size_t index = 0U; index < content.scene.cells.size(); ++index)
    {
        const SourceCellOrdinal expected{.value = static_cast<std::uint32_t>(index)};
        if (content.scene.cells[index].source_cell_ordinal != expected ||
            content.material_catalogs[index].source_cell_ordinal != expected)
            return std::unexpected("canonical level scene material source ordinal is invalid");
    }
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
        auto association = ValidateCanonicalLevelSceneMaterialAssociation(result);
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
