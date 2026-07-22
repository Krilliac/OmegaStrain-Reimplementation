#include "omega/runtime/diagnostic_actor_scene.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <iostream>
#include <string_view>
#include <type_traits>

namespace
{
int failures = 0;

void Check(const bool condition, const char* message)
{
    if (condition)
        return;
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
}
} // namespace

int main()
{
    using omega::runtime::BuildProjectDiagnosticActorMesh;

    static_assert(noexcept(BuildProjectDiagnosticActorMesh()));
    static_assert(std::is_same_v<decltype(BuildProjectDiagnosticActorMesh()),
        std::expected<omega::asset::RenderMeshIR, std::string_view>>);
    static_assert(omega::runtime::kProjectDiagnosticActorPositionCount == 3U);
    static_assert(
        omega::runtime::kProjectDiagnosticActorTriangleIndexCount == 3U);
    static_assert(omega::runtime::kProjectDiagnosticActorMeshLogicalBytes == 48U);
    static_assert(omega::runtime::kProjectDiagnosticActorMeshAllocationError ==
                  "project diagnostic actor mesh allocation failed");

    auto first = BuildProjectDiagnosticActorMesh();
    auto second = BuildProjectDiagnosticActorMesh();
    Check(first.has_value() && second.has_value(),
        "the fixed diagnostic actor geometry builds deterministically");
    if (!first || !second)
        return EXIT_FAILURE;

    constexpr float extent = 1.0F / 32.0F;
    constexpr float half_extent = extent / 2.0F;
    constexpr float depth = 0.5F;
    const std::uint64_t logical_bytes =
        static_cast<std::uint64_t>(first->positions.size()) *
            sizeof(omega::asset::Float3IR) +
        static_cast<std::uint64_t>(first->triangle_indices.size()) *
            sizeof(std::uint32_t);
    Check(first->positions.size() == 3U &&
              first->triangle_indices.size() == 3U &&
              logical_bytes == 48U,
        "the owned mesh contains exactly three positions, three indices, and 48 logical bytes");
    Check(first->positions[0] ==
                  omega::asset::Float3IR{
                      .x = -extent, .y = -half_extent, .z = depth} &&
              first->positions[1] ==
                  omega::asset::Float3IR{
                      .x = extent, .y = -half_extent, .z = depth} &&
              first->positions[2] ==
                  omega::asset::Float3IR{
                      .x = 0.0F, .y = extent, .z = depth},
        "the project triangle has the exact small origin-centered geometry");
    Check(first->triangle_indices[0] == 0U &&
              first->triangle_indices[1] == 1U &&
              first->triangle_indices[2] == 2U,
        "the triangle preserves exact project-authored index order");

    bool all_finite = true;
    for (const omega::asset::Float3IR& position : first->positions)
    {
        all_finite = all_finite && std::isfinite(position.x) &&
                     std::isfinite(position.y) && std::isfinite(position.z);
    }
    const float centroid_x =
        (first->positions[0].x + first->positions[1].x +
            first->positions[2].x) /
        3.0F;
    const float centroid_y =
        (first->positions[0].y + first->positions[1].y +
            first->positions[2].y) /
        3.0F;
    const float centroid_z =
        (first->positions[0].z + first->positions[1].z +
            first->positions[2].z) /
        3.0F;
    Check(all_finite && centroid_x == 0.0F && centroid_y == 0.0F &&
              centroid_z == depth,
        "all coordinates are finite with an origin-centered X/Y centroid and fixed depth");

    Check(first->positions.data() != second->positions.data() &&
              first->triangle_indices.data() != second->triangle_indices.data() &&
              *first == *second,
        "independent builds own distinct deterministic position and index storage");
    first->positions[0].x = 1.0F;
    first->triangle_indices[0] = 2U;
    Check(second->positions[0].x == -extent &&
              second->triangle_indices[0] == 0U,
        "mutating one returned mesh cannot alias another returned mesh");

    if (failures == 0)
        std::cout << "omega_diagnostic_actor_scene_tests: all checks passed\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
