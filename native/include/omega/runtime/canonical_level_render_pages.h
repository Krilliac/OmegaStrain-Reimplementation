#pragma once

#include "omega/asset/scene_ir.h"
#include "omega/runtime/canonical_level_scene.h"
#include "omega/runtime/render_mesh_draw_list.h"

#include <cstdint>
#include <expected>
#include <string>
#include <vector>

namespace omega::runtime
{
inline constexpr std::uint64_t kMaximumCanonicalLevelMeshesPerPage =
    static_cast<std::uint64_t>(kMaximumRenderMeshDrawsPerFrame - 1U);
static_assert(kMaximumCanonicalLevelMeshesPerPage == 63U);

struct CanonicalLevelRenderPageLimits
{
    std::uint64_t maximum_source_cells = 4096U;
    std::uint64_t maximum_renderable_cells = 4096U;
    std::uint64_t maximum_pages = 4096U;
    std::uint64_t maximum_meshes_per_page = kMaximumCanonicalLevelMeshesPerPage;
    std::uint64_t maximum_positions = 1ULL << 20U;
    std::uint64_t maximum_triangle_indices = 6ULL << 20U;
    std::uint64_t maximum_output_bytes = 128ULL * 1024ULL * 1024ULL;
};

// Page-local render_mesh_index is intentionally separate from
// source_cell_ordinal.
struct CanonicalLevelRenderMeshMapping
{
    SourceCellOrdinal source_cell_ordinal;
    std::uint32_t render_mesh_index = 0U;

    bool operator==(const CanonicalLevelRenderMeshMapping&) const = default;
};

struct CanonicalLevelRenderPage
{
    asset::SceneIR scene;
    std::vector<CanonicalLevelRenderMeshMapping> mesh_mappings;

    bool operator==(const CanonicalLevelRenderPage&) const = default;
};

struct CanonicalLevelRenderPages
{
    std::vector<CanonicalLevelRenderPage> pages;
    // Source-order ordinals retained for cells that have no complete triangle to
    // upload.
    std::vector<SourceCellOrdinal> non_renderable_source_cells;

    bool operator==(const CanonicalLevelRenderPages&) const = default;
};

// [any worker thread; reentrant] Validates canonical cells, omits
// non-renderable cells, and copies renderable cells into source-order pages of
// at most 63 meshes. Every page-local mesh retains an explicit mapping back to
// the canonical source ordinal. Limits are tighten-only safety maxima.
[[nodiscard]] std::expected<CanonicalLevelRenderPages, std::string> BuildCanonicalLevelRenderPages(
    const CanonicalLevelScene& canonical,
    const CanonicalLevelRenderPageLimits& limits = CanonicalLevelRenderPageLimits{});
} // namespace omega::runtime
