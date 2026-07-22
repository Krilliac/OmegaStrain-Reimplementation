#pragma once

#include "omega/asset/scene_ir.h"

#include <cstdint>
#include <expected>
#include <string>
#include <vector>

namespace omega::runtime
{
// Diagnostic budgets for the snapshot itself, independent of whatever limits produced the input
// `SceneIR`.
struct SceneStructureSnapshotLimits
{
    std::uint64_t maximum_render_meshes = 4096U;
    std::uint64_t maximum_output_bytes = 16ULL * 1024ULL * 1024ULL;
};

// A structural fingerprint of a `SceneIR`. It never stores or derives a source path, archive
// member name, host filesystem detail, or raw retail byte range: every field is either a count
// already owned by the scene or a one-way digest folded from already-decoded project floats and
// indices. The digest cannot be inverted to recover those floats; it only lets two snapshots be
// compared for structural equality.
struct SceneStructureSnapshot
{
    std::uint64_t render_mesh_count = 0U;
    std::uint64_t mesh_instance_count = 0U;
    std::uint64_t total_position_count = 0U;
    std::uint64_t total_triangle_index_count = 0U;
    // Source-order per-mesh counts, matching `SceneIR::render_meshes` exactly.
    std::vector<std::uint64_t> render_mesh_position_counts;
    std::vector<std::uint64_t> render_mesh_triangle_index_counts;
    // Source-order per-instance render-mesh reference, matching `SceneIR::mesh_instances` exactly.
    std::vector<std::uint32_t> mesh_instance_render_mesh_indices;
    bool camera_is_identity = false;
    // FNV-1a-derived digest folded over render-mesh counts, position and triangle-index bit
    // patterns, instance render-mesh indices and transforms, and the camera matrices, all in
    // source order.
    std::uint64_t structure_digest = 0U;

    bool operator==(const SceneStructureSnapshot&) const = default;
};

// [any thread; reentrant] Builds a bounded structural snapshot of `scene`. Rejects before any
// output allocation if the scene's render-mesh count or the snapshot's own logical output size
// exceeds `limits`.
[[nodiscard]] std::expected<SceneStructureSnapshot, std::string> BuildSceneStructureSnapshot(
    const asset::SceneIR& scene,
    const SceneStructureSnapshotLimits& limits = SceneStructureSnapshotLimits{});
} // namespace omega::runtime
