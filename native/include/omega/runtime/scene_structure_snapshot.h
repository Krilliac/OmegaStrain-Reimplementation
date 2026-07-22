#pragma once

#include "omega/asset/scene_ir.h"

#include <cstdint>
#include <expected>
#include <string>

namespace omega::runtime
{
inline constexpr std::uint32_t kSceneStructureSnapshotSchemaVersion = 1U;

// Versioned aggregate-only diagnostic. It contains no source identity, per-mesh
// row, coordinate, index value, transform value, or content digest. Equality
// compares only these aggregate fields; it does not prove scene equality,
// authenticate content, or establish provenance.
struct SceneStructureSnapshot
{
    std::uint32_t schema_version = kSceneStructureSnapshotSchemaVersion;
    std::uint64_t render_mesh_count = 0U;
    std::uint64_t mesh_instance_count = 0U;
    std::uint64_t total_position_count = 0U;
    std::uint64_t total_triangle_index_count = 0U;
    bool camera_is_identity = false;

    bool operator==(const SceneStructureSnapshot&) const = default;
};

// Fixed diagnostic safety maxima. Callers may tighten but never widen them.
struct SceneStructureSnapshotLimits
{
    std::uint64_t maximum_render_meshes = 4096U;
    std::uint64_t maximum_mesh_instances = 4096U;
    std::uint64_t maximum_positions = 1ULL << 20U;
    std::uint64_t maximum_triangle_indices = 6ULL << 20U;
    // ABI-local logical value footprint, not a serialization size or exact
    // heap-allocation cap.
    std::uint64_t maximum_output_bytes = sizeof(SceneStructureSnapshot);
};

// [any thread; reentrant] Builds a constant-size aggregate after bounding every
// traversed collection. It performs no dynamic output allocation.
[[nodiscard]] std::expected<SceneStructureSnapshot, std::string> BuildSceneStructureSnapshot(
    const asset::SceneIR& scene,
    const SceneStructureSnapshotLimits& limits = SceneStructureSnapshotLimits{});
} // namespace omega::runtime
