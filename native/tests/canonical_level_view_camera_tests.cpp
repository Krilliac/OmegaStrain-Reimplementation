#include "omega/runtime/canonical_level_view_camera.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
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

[[nodiscard]] bool IsIdentity(const omega::asset::Matrix4x4IR& matrix) noexcept
{
    return matrix == omega::asset::kIdentityMatrix4x4IR;
}

[[nodiscard]] bool NearlyEqual(const float lhs, const float rhs) noexcept
{
    return std::abs(lhs - rhs) <= 1.0e-5F * std::max({1.0F, std::abs(lhs), std::abs(rhs)});
}

[[nodiscard]] omega::runtime::CanonicalLevelSceneCell MakeCell(
    const std::uint32_t ordinal, std::vector<omega::asset::Float3IR> positions,
    std::vector<std::uint32_t> triangle_indices)
{
    return omega::runtime::CanonicalLevelSceneCell{
        .source_cell_ordinal = omega::runtime::SourceCellOrdinal{.value = ordinal},
        .render_mesh =
            omega::asset::RenderMeshIR{
                .positions = std::move(positions),
                .triangle_indices = std::move(triangle_indices),
            },
        .local_to_world = omega::asset::kIdentityMatrix4x4IR,
    };
}
} // namespace

int main()
{
    // An empty level publishes the identity camera with zero framed counts rather than
    // rejecting or inventing an extent.
    {
        omega::runtime::CanonicalLevelScene empty;
        auto view = omega::runtime::BuildCanonicalLevelDiagnosticViewCamera(empty);
        Check(view && IsIdentity(view->camera.world_to_view) &&
                  IsIdentity(view->camera.view_to_clip) && view->framed_cells == 0U &&
                  view->framed_positions == 0U &&
                  view->source_axes == std::array<std::uint8_t, 3>{0U, 1U, 2U},
              "an empty canonical scene frames as the identity camera with zero counts");
    }

    // A canonical scene whose only cells are triangle-free is inspected (for finiteness and
    // limits) but does not contribute to the framed union.
    {
        omega::runtime::CanonicalLevelScene scene;
        scene.cells.push_back(MakeCell(0U, {{.x = 5.0F, .y = 5.0F, .z = 5.0F}}, {}));
        auto view = omega::runtime::BuildCanonicalLevelDiagnosticViewCamera(scene);
        Check(view && IsIdentity(view->camera.world_to_view) &&
                  IsIdentity(view->camera.view_to_clip) && view->framed_cells == 0U &&
                  view->framed_positions == 0U,
              "a triangle-free canonical scene frames as the identity camera");
    }

    // One renderable triangle exercises the full framing computation with a hand-checked
    // expected result. Bounds are x in [0,2], y in [0,1], z constant at 0, so x leads, then y,
    // then z becomes the constant-depth stage.
    {
        omega::runtime::CanonicalLevelScene scene;
        scene.cells.push_back(MakeCell(0U,
            {
                {.x = 0.0F, .y = 0.0F, .z = 0.0F},
                {.x = 2.0F, .y = 0.0F, .z = 0.0F},
                {.x = 0.0F, .y = 1.0F, .z = 0.0F},
            },
            {0U, 1U, 2U}));
        auto view = omega::runtime::BuildCanonicalLevelDiagnosticViewCamera(scene);
        Check(view.has_value(), "a single renderable triangle frames successfully");
        if (view)
        {
            Check(view->framed_cells == 1U && view->framed_positions == 3U,
                  "the single renderable cell is counted exactly once");
            Check(view->source_axes == std::array<std::uint8_t, 3>{0U, 1U, 2U},
                  "the largest-extent axis (x) leads, then y, then z becomes depth");

            const omega::asset::Matrix4x4IR& world_to_view = view->camera.world_to_view;
            Check(NearlyEqual(world_to_view.row_major[0], 1.0F) &&
                      NearlyEqual(world_to_view.row_major[3], -1.0F) &&
                      NearlyEqual(world_to_view.row_major[5], 1.0F) &&
                      NearlyEqual(world_to_view.row_major[7], -0.5F) &&
                      NearlyEqual(world_to_view.row_major[10], 1.0F) &&
                      NearlyEqual(world_to_view.row_major[11], 0.0F) &&
                      NearlyEqual(world_to_view.row_major[15], 1.0F),
                  "world_to_view centres x/y on the union and rebases z on its minimum");

            const omega::asset::Matrix4x4IR& view_to_clip = view->camera.view_to_clip;
            Check(NearlyEqual(view_to_clip.row_major[0], 0.8F) &&
                      NearlyEqual(view_to_clip.row_major[5], 0.8F) &&
                      NearlyEqual(view_to_clip.row_major[10], 0.0F) &&
                      NearlyEqual(view_to_clip.row_major[11], 0.5F) &&
                      NearlyEqual(view_to_clip.row_major[15], 1.0F),
                  "view_to_clip applies the fitted uniform planar scale and the fixed "
                  "diagnostic depth for a zero-extent depth axis");

            // Manually project each vertex through world_to_view then view_to_clip and confirm
            // the result lands inside the inset clip volume.
            const std::array<omega::asset::Float3IR, 3> vertices{
                omega::asset::Float3IR{.x = 0.0F, .y = 0.0F, .z = 0.0F},
                omega::asset::Float3IR{.x = 2.0F, .y = 0.0F, .z = 0.0F},
                omega::asset::Float3IR{.x = 0.0F, .y = 1.0F, .z = 0.0F},
            };
            const std::array<std::pair<float, float>, 3> expected_xy{
                std::pair{-0.8F, -0.4F},
                std::pair{0.8F, -0.4F},
                std::pair{-0.8F, 0.4F},
            };
            bool projected_ok = true;
            for (std::size_t index = 0U; index < vertices.size(); ++index)
            {
                const omega::asset::Float3IR& vertex = vertices[index];
                const float view_x = world_to_view.row_major[0] * vertex.x +
                                      world_to_view.row_major[1] * vertex.y +
                                      world_to_view.row_major[2] * vertex.z +
                                      world_to_view.row_major[3];
                const float view_y = world_to_view.row_major[4] * vertex.x +
                                      world_to_view.row_major[5] * vertex.y +
                                      world_to_view.row_major[6] * vertex.z +
                                      world_to_view.row_major[7];
                const float clip_x = view_to_clip.row_major[0] * view_x;
                const float clip_y = view_to_clip.row_major[5] * view_y;
                projected_ok = projected_ok && NearlyEqual(clip_x, expected_xy[index].first) &&
                               NearlyEqual(clip_y, expected_xy[index].second);
            }
            Check(projected_ok,
                  "the composed camera stages project the triangle's vertices to the expected "
                  "inset clip positions");
        }
    }

    // Equal x/y extents break the tie toward the lower axis index (x, then y).
    {
        omega::runtime::CanonicalLevelScene scene;
        scene.cells.push_back(MakeCell(0U,
            {
                {.x = 0.0F, .y = 0.0F, .z = 0.0F},
                {.x = 4.0F, .y = 0.0F, .z = 0.0F},
                {.x = 0.0F, .y = 4.0F, .z = 0.0F},
            },
            {0U, 1U, 2U}));
        auto view = omega::runtime::BuildCanonicalLevelDiagnosticViewCamera(scene);
        Check(view && view->source_axes == std::array<std::uint8_t, 3>{0U, 1U, 2U},
              "equal x/y extents keep the fixed x-then-y-then-z tie-break order");
    }

    // A nondegenerate depth extent produces a nonzero depth scale and the inset depth offset
    // instead of the fixed constant-depth fallback.
    {
        omega::runtime::CanonicalLevelScene scene;
        scene.cells.push_back(MakeCell(0U,
            {
                {.x = 0.0F, .y = 0.0F, .z = 0.0F},
                {.x = 4.0F, .y = 0.0F, .z = 2.0F},
                {.x = 0.0F, .y = 1.0F, .z = 0.0F},
            },
            {0U, 1U, 2U}));
        auto view = omega::runtime::BuildCanonicalLevelDiagnosticViewCamera(scene);
        Check(view.has_value(), "a scene with a nonzero depth extent frames successfully");
        if (view)
        {
            Check(!NearlyEqual(view->camera.view_to_clip.row_major[10], 0.0F),
                  "a nonzero depth extent produces a nonzero depth scale");
            Check(NearlyEqual(view->camera.view_to_clip.row_major[11], 0.1F),
                  "a nonzero depth extent uses the ten-percent depth inset offset instead of "
                  "the fixed diagnostic-depth constant");
        }
    }

    // A canonical cell transform that is not the identity placeholder is rejected rather than
    // silently reinterpreted as a placement claim.
    {
        omega::runtime::CanonicalLevelScene scene;
        auto cell = MakeCell(0U,
            {
                {.x = 0.0F, .y = 0.0F, .z = 0.0F},
                {.x = 1.0F, .y = 0.0F, .z = 0.0F},
                {.x = 0.0F, .y = 1.0F, .z = 0.0F},
            },
            {0U, 1U, 2U});
        cell.local_to_world.row_major[3] = 1.0F;
        scene.cells.push_back(std::move(cell));
        Check(!omega::runtime::BuildCanonicalLevelDiagnosticViewCamera(scene),
              "a non-identity canonical cell transform is rejected");
    }

    // Non-finite coordinates are rejected even in a triangle-free cell, matching the sibling
    // canonical composers' validate-before-allocate discipline.
    {
        omega::runtime::CanonicalLevelScene scene;
        scene.cells.push_back(MakeCell(0U,
            {{.x = std::numeric_limits<float>::infinity(), .y = 0.0F, .z = 0.0F}}, {}));
        Check(!omega::runtime::BuildCanonicalLevelDiagnosticViewCamera(scene),
              "a non-finite coordinate is rejected even in a triangle-free cell");
    }

    // Limits are tighten-only safety maxima, matching every sibling canonical composer.
    {
        omega::runtime::CanonicalLevelViewCameraLimits widened;
        widened.maximum_cells =
            omega::runtime::CanonicalLevelViewCameraLimits{}.maximum_cells + 1U;
        omega::runtime::CanonicalLevelScene empty;
        Check(!omega::runtime::BuildCanonicalLevelDiagnosticViewCamera(empty, widened),
              "callers cannot widen the cell-count safety maximum");
    }
    {
        omega::runtime::CanonicalLevelScene scene;
        scene.cells.push_back(MakeCell(0U, {}, {}));
        scene.cells.push_back(MakeCell(1U, {}, {}));
        omega::runtime::CanonicalLevelViewCameraLimits one_cell;
        one_cell.maximum_cells = 1U;
        Check(!omega::runtime::BuildCanonicalLevelDiagnosticViewCamera(scene, one_cell),
              "a scene above the tightened cell limit is rejected");
    }
    {
        omega::runtime::CanonicalLevelScene scene;
        scene.cells.push_back(MakeCell(0U,
            {
                {.x = 0.0F, .y = 0.0F, .z = 0.0F},
                {.x = 1.0F, .y = 0.0F, .z = 0.0F},
            },
            {}));
        omega::runtime::CanonicalLevelViewCameraLimits one_position;
        one_position.maximum_positions = 1U;
        Check(!omega::runtime::BuildCanonicalLevelDiagnosticViewCamera(scene, one_position),
              "a cell above the tightened position limit is rejected");
    }

    // An extremely small nonzero extent produces a scale outside the representable float range
    // and is rejected rather than silently saturating to infinity.
    {
        omega::runtime::CanonicalLevelScene scene;
        constexpr float kTiny = 1.0e-40F;
        scene.cells.push_back(MakeCell(0U,
            {
                {.x = 0.0F, .y = 0.0F, .z = 0.0F},
                {.x = kTiny, .y = 0.0F, .z = 0.0F},
                {.x = 0.0F, .y = kTiny, .z = 0.0F},
            },
            {0U, 1U, 2U}));
        Check(!omega::runtime::BuildCanonicalLevelDiagnosticViewCamera(scene),
              "a subnormal extent that produces a non-representable scale is rejected");
    }

    // A subnormal *depth*-axis extent (the smallest of the three, so it becomes the depth
    // stage) produces a depth scale outside the representable float range and is rejected.
    {
        omega::runtime::CanonicalLevelScene scene;
        constexpr float kTinyDepth = 1.0e-40F;
        scene.cells.push_back(MakeCell(0U,
            {
                {.x = 0.0F, .y = 0.0F, .z = 0.0F},
                {.x = 2.0F, .y = 0.0F, .z = 0.0F},
                {.x = 0.0F, .y = 1.0F, .z = kTinyDepth},
            },
            {0U, 1U, 2U}));
        Check(!omega::runtime::BuildCanonicalLevelDiagnosticViewCamera(scene),
              "a subnormal depth extent that produces a non-representable depth scale is "
              "rejected");
    }

    return failures;
}
