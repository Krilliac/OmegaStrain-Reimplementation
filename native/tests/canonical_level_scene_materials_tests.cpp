#include "omega/runtime/canonical_level_scene_materials.h"

#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace
{
int failures = 0;

void Check(const bool condition, const std::string_view message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

[[nodiscard]] omega::asset::SpatialMeshIR MakeTriangleMesh(const float scale = 1.0F)
{
    return omega::asset::SpatialMeshIR{
        .vertices =
            {
                {.x = 0.0F, .y = 0.0F, .z = 0.0F},
                {.x = scale, .y = 0.0F, .z = 0.0F},
                {.x = 0.0F, .y = scale, .z = 0.0F},
            },
        .triangles =
            {
                {.vertex_indices = {0U, 1U, 2U}},
            },
    };
}

[[nodiscard]] omega::asset::MaterialCatalogIR MakeCatalog(const std::string& name)
{
    return omega::asset::MaterialCatalogIR{
        .names = {name},
        .materials = {omega::asset::MaterialCatalogEntryIR{
            .name_indices = {0U, 0U, 0U},
            .name_count = 1U,
        }},
    };
}
} // namespace

int main()
{
    // Empty content.
    auto empty = omega::runtime::BuildCanonicalLevelSceneWithMaterials({});
    Check(empty && empty->scene.cells.empty() && empty->material_catalogs.empty(),
          "empty level content produces an empty paired scene and material list");

    // Matching two-cell content: scene and materials line up index for index.
    omega::asset::LevelContentIR content{
        .spatial = {.terrain_cells = {MakeTriangleMesh(), MakeTriangleMesh(2.0F)}},
        .material_catalogs = {.terrain_cells = {MakeCatalog("A"), MakeCatalog("BC")}},
    };
    constexpr std::uint64_t exact_scene_output_bytes =
        sizeof(omega::runtime::CanonicalLevelScene) +
        2U * sizeof(omega::runtime::CanonicalLevelSceneCell) +
        6U * sizeof(omega::asset::Float3IR) + 6U * sizeof(std::uint32_t);
    constexpr std::uint64_t exact_output_bytes =
        sizeof(omega::runtime::CanonicalLevelSceneWithMaterials) +
        2U * sizeof(omega::runtime::CanonicalLevelSceneCell) +
        2U * sizeof(omega::runtime::CanonicalLevelCellMaterialCatalog) +
        6U * sizeof(omega::asset::Float3IR) + 6U * sizeof(std::uint32_t) +
        2U * sizeof(std::string) + 2U * sizeof(omega::asset::MaterialCatalogEntryIR) + 3U;
    auto paired = omega::runtime::BuildCanonicalLevelSceneWithMaterials(content);
    Check(paired.has_value(), "matching cell counts build a paired canonical scene");
    if (paired)
    {
        Check(paired->scene.cells.size() == 2U && paired->material_catalogs.size() == 2U,
              "scene mesh count and material catalog count both equal the manifest "
              "cell count");
        Check(paired->material_catalogs[0].catalog.names == std::vector<std::string>{"A"} &&
                  paired->material_catalogs[1].catalog.names == std::vector<std::string>{"BC"},
              "material catalogs retain the documented source-cell order");
        Check(paired->scene.cells[0].source_cell_ordinal ==
                      omega::runtime::SourceCellOrdinal{.value = 0U} &&
                  paired->material_catalogs[0].source_cell_ordinal ==
                      paired->scene.cells[0].source_cell_ordinal &&
                  paired->scene.cells[1].source_cell_ordinal ==
                      omega::runtime::SourceCellOrdinal{.value = 1U} &&
                  paired->material_catalogs[1].source_cell_ordinal ==
                      paired->scene.cells[1].source_cell_ordinal,
              "scene cells and material catalogs preserve matching explicit source "
              "ordinals");
        Check(omega::runtime::ValidateCanonicalLevelSceneMaterialAssociation(*paired).has_value(),
              "the generated material association validates");

        auto mismatched_ordinal = *paired;
        mismatched_ordinal.material_catalogs[0].source_cell_ordinal.value = 1U;
        Check(!omega::runtime::ValidateCanonicalLevelSceneMaterialAssociation(mismatched_ordinal),
              "an equal-cardinality material association with a mismatched ordinal "
              "is rejected");

        auto jointly_relabelled = *paired;
        jointly_relabelled.scene.cells[0].source_cell_ordinal.value = 1U;
        jointly_relabelled.material_catalogs[0].source_cell_ordinal.value = 1U;
        Check(!omega::runtime::ValidateCanonicalLevelSceneMaterialAssociation(jointly_relabelled),
              "matching but noncanonical duplicate ordinals do not masquerade as provenance");

        auto association_limits = omega::runtime::CanonicalLevelSceneMaterialLimits{};
        association_limits.maximum_catalogs = 1U;
        Check(!omega::runtime::ValidateCanonicalLevelSceneMaterialAssociation(*paired,
                                                                              association_limits),
              "association validation is bounded by tighten-only cell limits");

        association_limits = {};
        association_limits.scene.maximum_cells = 1U;
        Check(!omega::runtime::ValidateCanonicalLevelSceneMaterialAssociation(*paired,
                                                                              association_limits),
              "direct association validation enforces the canonical scene cell limit");

        association_limits = {};
        association_limits.scene.maximum_positions = 5U;
        Check(!omega::runtime::ValidateCanonicalLevelSceneMaterialAssociation(*paired,
                                                                              association_limits),
              "direct association validation enforces the aggregate position limit");

        association_limits = {};
        association_limits.scene.maximum_triangle_indices = 5U;
        Check(!omega::runtime::ValidateCanonicalLevelSceneMaterialAssociation(*paired,
                                                                              association_limits),
              "direct association validation enforces the aggregate triangle-index limit");

        association_limits = {};
        association_limits.scene.maximum_output_bytes = exact_scene_output_bytes;
        association_limits.maximum_output_bytes = exact_output_bytes;
        Check(omega::runtime::ValidateCanonicalLevelSceneMaterialAssociation(
                  *paired, association_limits)
                  .has_value(),
              "direct association validation accepts exact ABI-local logical output budgets");
        --association_limits.scene.maximum_output_bytes;
        Check(!omega::runtime::ValidateCanonicalLevelSceneMaterialAssociation(*paired,
                                                                              association_limits),
              "direct association validation enforces the scene output-byte limit");

        association_limits = {};
        association_limits.maximum_names = 1U;
        Check(!omega::runtime::ValidateCanonicalLevelSceneMaterialAssociation(*paired,
                                                                              association_limits),
              "direct association validation enforces the nested name count limit");

        association_limits = {};
        association_limits.maximum_materials = 1U;
        Check(!omega::runtime::ValidateCanonicalLevelSceneMaterialAssociation(*paired,
                                                                              association_limits),
              "direct association validation enforces the nested material count limit");

        association_limits = {};
        association_limits.maximum_name_bytes = 2U;
        Check(!omega::runtime::ValidateCanonicalLevelSceneMaterialAssociation(*paired,
                                                                              association_limits),
              "direct association validation enforces the aggregate name-byte limit");

        association_limits = {};
        association_limits.maximum_name_length = 1U;
        Check(!omega::runtime::ValidateCanonicalLevelSceneMaterialAssociation(*paired,
                                                                              association_limits),
              "direct association validation enforces the per-name length limit");

        association_limits = {};
        association_limits.maximum_output_bytes = exact_output_bytes - 1U;
        Check(!omega::runtime::ValidateCanonicalLevelSceneMaterialAssociation(*paired,
                                                                              association_limits),
              "direct association validation enforces the combined output-byte limit");

        auto invalid_direct_material = *paired;
        invalid_direct_material.material_catalogs[1].catalog.materials[0].name_indices[0] = 2U;
        Check(!omega::runtime::ValidateCanonicalLevelSceneMaterialAssociation(
                  invalid_direct_material),
              "direct association validation rejects a mutated material name reference");

        auto nonfinite_direct_position = *paired;
        nonfinite_direct_position.scene.cells[0].render_mesh.positions[0].x =
            std::numeric_limits<float>::infinity();
        Check(!omega::runtime::ValidateCanonicalLevelSceneMaterialAssociation(
                  nonfinite_direct_position),
              "direct association validation rejects a mutated non-finite position");

        auto invalid_direct_index = *paired;
        invalid_direct_index.scene.cells[1].render_mesh.triangle_indices[0] = 99U;
        Check(!omega::runtime::ValidateCanonicalLevelSceneMaterialAssociation(
                  invalid_direct_index),
              "direct association validation rejects a mutated triangle index");

        auto incomplete_direct_triangle = *paired;
        incomplete_direct_triangle.scene.cells[0].render_mesh.triangle_indices.pop_back();
        Check(!omega::runtime::ValidateCanonicalLevelSceneMaterialAssociation(
                  incomplete_direct_triangle),
              "direct association validation rejects an incomplete triangle");

        auto nonfinite_direct_transform = *paired;
        nonfinite_direct_transform.scene.cells[0].local_to_world.row_major[0] =
            std::numeric_limits<float>::quiet_NaN();
        Check(!omega::runtime::ValidateCanonicalLevelSceneMaterialAssociation(
                  nonfinite_direct_transform),
              "direct association validation rejects a non-finite cell transform");

        auto nonfinite_direct_camera = *paired;
        nonfinite_direct_camera.scene.camera.world_to_view.row_major[0] =
            std::numeric_limits<float>::quiet_NaN();
        Check(!omega::runtime::ValidateCanonicalLevelSceneMaterialAssociation(
                  nonfinite_direct_camera),
              "direct association validation rejects a non-finite camera transform");
    }

    // Mismatched cardinality is rejected before any composition.
    omega::asset::LevelContentIR mismatched{
        .spatial = {.terrain_cells = {MakeTriangleMesh()}},
        .material_catalogs = {.terrain_cells = {MakeCatalog("a"), MakeCatalog("b")}},
    };
    Check(!omega::runtime::BuildCanonicalLevelSceneWithMaterials(mismatched),
          "mismatched spatial and material cell counts are rejected");

    // Spatial-side validation failures propagate.
    omega::asset::LevelContentIR invalid_spatial{
        .spatial = {.terrain_cells = {MakeTriangleMesh()}},
        .material_catalogs = {.terrain_cells = {MakeCatalog("a")}},
    };
    invalid_spatial.spatial.terrain_cells[0].triangles[0].vertex_indices[0] = 99U;
    Check(!omega::runtime::BuildCanonicalLevelSceneWithMaterials(invalid_spatial),
          "an invalid spatial triangle index rejects the whole paired build");

    auto invalid_material = content;
    invalid_material.material_catalogs.terrain_cells[1].materials[0].name_indices[0] = 2U;
    Check(!omega::runtime::BuildCanonicalLevelSceneWithMaterials(invalid_material),
          "an invalid material name reference rejects the paired build");

    auto limits = omega::runtime::CanonicalLevelSceneMaterialLimits{};
    limits.maximum_catalogs = 1U;
    Check(!omega::runtime::BuildCanonicalLevelSceneWithMaterials(content, limits),
          "the catalog limit is cumulative and enforced");

    limits = {};
    limits.maximum_names = 1U;
    Check(!omega::runtime::BuildCanonicalLevelSceneWithMaterials(content, limits),
          "the nested name count limit is cumulative and enforced");

    limits = {};
    limits.maximum_materials = 1U;
    Check(!omega::runtime::BuildCanonicalLevelSceneWithMaterials(content, limits),
          "the nested material count limit is cumulative and enforced");

    limits = {};
    limits.maximum_name_bytes = 3U;
    Check(omega::runtime::BuildCanonicalLevelSceneWithMaterials(content, limits).has_value(),
          "the exact aggregate name-byte limit succeeds");
    limits.maximum_name_bytes = 2U;
    Check(!omega::runtime::BuildCanonicalLevelSceneWithMaterials(content, limits),
          "one below the aggregate name-byte limit fails");

    limits = {};
    limits.maximum_name_length = 1U;
    Check(!omega::runtime::BuildCanonicalLevelSceneWithMaterials(content, limits),
          "the per-name length limit is enforced inside each catalog");

    limits = {};
    limits.maximum_output_bytes = exact_output_bytes;
    Check(omega::runtime::BuildCanonicalLevelSceneWithMaterials(content, limits).has_value(),
          "the exact combined scene and material output-byte limit succeeds");
    limits.maximum_output_bytes = exact_output_bytes - 1U;
    Check(!omega::runtime::BuildCanonicalLevelSceneWithMaterials(content, limits),
          "one below the combined scene and material output-byte limit fails");

    limits = {};
    ++limits.maximum_catalogs;
    Check(!omega::runtime::BuildCanonicalLevelSceneWithMaterials(content, limits),
          "callers cannot widen material safety maxima");

    return failures;
}
