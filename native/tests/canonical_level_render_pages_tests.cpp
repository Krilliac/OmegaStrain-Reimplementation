#include "omega/runtime/canonical_level_render_pages.h"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <utility>
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

[[nodiscard]] omega::asset::SpatialMeshIR MakeTriangleMesh(const float x = 1.0F)
{
    return omega::asset::SpatialMeshIR{
        .vertices =
            {
                {.x = 0.0F, .y = 0.0F, .z = 0.0F},
                {.x = x, .y = 0.0F, .z = 0.0F},
                {.x = 0.0F, .y = 1.0F, .z = 0.0F},
            },
        .triangles = {{.vertex_indices = {0U, 1U, 2U}}},
    };
}

[[nodiscard]] omega::runtime::CanonicalLevelScene BuildCells(const std::size_t count)
{
    omega::asset::LevelSpatialIR spatial;
    spatial.terrain_cells.reserve(count);
    for (std::size_t index = 0U; index < count; ++index)
        spatial.terrain_cells.push_back(MakeTriangleMesh(static_cast<float>(index + 1U)));
    auto canonical = omega::runtime::BuildCanonicalLevelScene(spatial);
    if (!canonical)
    {
        ++failures;
        return {};
    }
    return std::move(*canonical);
}

[[nodiscard]] std::size_t RenderableCellCount(
    const omega::runtime::CanonicalLevelRenderPages& pages)
{
    std::size_t count = 0U;
    for (const omega::runtime::CanonicalLevelRenderPage& page : pages.pages)
        count += page.scene.render_meshes.size();
    return count;
}
} // namespace

int main()
{
    auto with_empty_gap = BuildCells(4U);
    with_empty_gap.cells[1].render_mesh = {};
    with_empty_gap.cells[3].render_mesh.triangle_indices.clear();
    auto gap_pages = omega::runtime::BuildCanonicalLevelRenderPages(with_empty_gap);
    Check(gap_pages && gap_pages->pages.size() == 1U &&
              gap_pages->pages[0].scene.render_meshes.size() == 2U &&
              gap_pages->pages[0].scene.mesh_instances.size() == 2U &&
              gap_pages->pages[0].mesh_mappings.size() == 2U,
          "empty and triangle-free cells are omitted from renderer-facing meshes");
    if (gap_pages)
    {
        Check(gap_pages->pages[0].mesh_mappings[0] ==
                      omega::runtime::CanonicalLevelRenderMeshMapping{
                          .source_cell_ordinal = {.value = 0U}, .render_mesh_index = 0U} &&
                  gap_pages->pages[0].mesh_mappings[1] ==
                      omega::runtime::CanonicalLevelRenderMeshMapping{
                          .source_cell_ordinal = {.value = 2U}, .render_mesh_index = 1U},
              "page-local renderer indices retain independent source ordinals "
              "across an empty gap");
        Check(gap_pages->non_renderable_source_cells ==
                  std::vector<omega::runtime::SourceCellOrdinal>({{.value = 1U}, {.value = 3U}}),
              "every omitted source cell retains its explicit ordinal");
        Check(!gap_pages->pages[0].scene.render_meshes[0].positions.empty() &&
                  !gap_pages->pages[0].scene.render_meshes[0].triangle_indices.empty() &&
                  !gap_pages->pages[0].scene.render_meshes[1].positions.empty() &&
                  !gap_pages->pages[0].scene.render_meshes[1].triangle_indices.empty(),
              "every published renderer mesh satisfies the host's nonempty upload "
              "precondition");
    }

    auto sixty_four = BuildCells(64U);
    auto sixty_four_pages = omega::runtime::BuildCanonicalLevelRenderPages(sixty_four);
    Check(sixty_four_pages && sixty_four_pages->pages.size() == 2U &&
              sixty_four_pages->pages[0].scene.render_meshes.size() == 63U &&
              sixty_four_pages->pages[1].scene.render_meshes.size() == 1U &&
              sixty_four_pages->pages[1].mesh_mappings[0].source_cell_ordinal ==
                  omega::runtime::SourceCellOrdinal{.value = 63U} &&
              sixty_four_pages->pages[1].mesh_mappings[0].render_mesh_index == 0U,
          "64 source cells page as 63 plus 1 without reusing a renderer index as "
          "source identity");

    auto two_ninety_nine = BuildCells(299U);
    auto two_ninety_nine_pages = omega::runtime::BuildCanonicalLevelRenderPages(two_ninety_nine);
    Check(two_ninety_nine_pages && two_ninety_nine_pages->pages.size() == 5U &&
              RenderableCellCount(*two_ninety_nine_pages) == 299U &&
              two_ninety_nine_pages->pages[4].scene.render_meshes.size() == 47U &&
              two_ninety_nine_pages->pages[4].mesh_mappings.back().source_cell_ordinal ==
                  omega::runtime::SourceCellOrdinal{.value = 298U} &&
              two_ninety_nine_pages->pages[4].mesh_mappings.back().render_mesh_index == 46U,
          "a generated 299-cell level fits five bounded pages with final "
          "provenance intact");
    if (two_ninety_nine_pages)
    {
        bool every_page_bounded = true;
        for (const omega::runtime::CanonicalLevelRenderPage& page : two_ninety_nine_pages->pages)
        {
            every_page_bounded =
                every_page_bounded &&
                page.scene.render_meshes.size() <=
                    omega::runtime::kMaximumCanonicalLevelMeshesPerPage &&
                page.scene.render_meshes.size() == page.scene.mesh_instances.size() &&
                page.scene.render_meshes.size() == page.mesh_mappings.size();
        }
        Check(every_page_bounded, "every generated page respects the existing 63-mesh boundary");
    }

    auto page_limits = omega::runtime::CanonicalLevelRenderPageLimits{};
    page_limits.maximum_meshes_per_page = 10U;
    page_limits.maximum_pages = 7U;
    Check(omega::runtime::BuildCanonicalLevelRenderPages(sixty_four, page_limits).has_value(),
          "an exact tightened seven-page limit succeeds");
    page_limits.maximum_pages = 6U;
    Check(!omega::runtime::BuildCanonicalLevelRenderPages(sixty_four, page_limits),
          "one below the required page limit fails");

    page_limits = {};
    page_limits.maximum_source_cells = 63U;
    Check(!omega::runtime::BuildCanonicalLevelRenderPages(sixty_four, page_limits),
          "the source-cell inspection limit is enforced");

    page_limits = {};
    page_limits.maximum_renderable_cells = 63U;
    Check(!omega::runtime::BuildCanonicalLevelRenderPages(sixty_four, page_limits),
          "the renderable-cell output limit is enforced");

    page_limits = {};
    page_limits.maximum_positions = 192U;
    page_limits.maximum_triangle_indices = 192U;
    Check(omega::runtime::BuildCanonicalLevelRenderPages(sixty_four, page_limits).has_value(),
          "exact position and triangle-index work limits succeed");
    page_limits.maximum_positions = 191U;
    Check(!omega::runtime::BuildCanonicalLevelRenderPages(sixty_four, page_limits),
          "one below the position work limit fails");
    page_limits = {};
    page_limits.maximum_triangle_indices = 191U;
    Check(!omega::runtime::BuildCanonicalLevelRenderPages(sixty_four, page_limits),
          "one below the triangle-index work limit fails");

    constexpr std::uint64_t exact_output_bytes =
        sizeof(omega::runtime::CanonicalLevelRenderPages) +
        2U * sizeof(omega::runtime::CanonicalLevelRenderPage) +
        64U * (sizeof(omega::asset::RenderMeshIR) + sizeof(omega::asset::SceneMeshInstanceIR) +
               sizeof(omega::runtime::CanonicalLevelRenderMeshMapping)) +
        192U * sizeof(omega::asset::Float3IR) + 192U * sizeof(std::uint32_t);
    page_limits = {};
    page_limits.maximum_output_bytes = exact_output_bytes;
    Check(omega::runtime::BuildCanonicalLevelRenderPages(sixty_four, page_limits).has_value(),
          "the exact renderer-page output-byte limit succeeds");
    --page_limits.maximum_output_bytes;
    Check(!omega::runtime::BuildCanonicalLevelRenderPages(sixty_four, page_limits),
          "one below the renderer-page output-byte limit fails");

    page_limits = {};
    page_limits.maximum_meshes_per_page = 64U;
    Check(!omega::runtime::BuildCanonicalLevelRenderPages(sixty_four, page_limits),
          "callers cannot widen the fixed 63-mesh page maximum");

    auto invalid_ordinal = with_empty_gap;
    invalid_ordinal.cells[2].source_cell_ordinal.value = 1U;
    Check(!omega::runtime::BuildCanonicalLevelRenderPages(invalid_ordinal),
          "duplicate or reordered source ordinals fail before renderer allocation");

    return failures;
}
