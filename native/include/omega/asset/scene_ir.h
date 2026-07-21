#pragma once

#include "omega/asset/geometry_ir.h"
#include "omega/asset/render_mesh_ir.h"

#include <cstdint>
#include <vector>

namespace omega::asset
{
struct SceneMeshInstanceIR
{
    std::uint32_t render_mesh_index = 0U;
    Matrix4x4IR local_to_world = kIdentityMatrix4x4IR;

    bool operator==(const SceneMeshInstanceIR&) const = default;
};

struct SceneCameraIR
{
    Matrix4x4IR world_to_clip = kIdentityMatrix4x4IR;

    bool operator==(const SceneCameraIR&) const = default;
};

// Fully owned, renderer-neutral project scene value. It is safe to move or copy across a future
// render boundary because it contains no borrowed input spans, platform objects, or vtables.
struct SceneIR
{
    std::vector<RenderMeshIR> render_meshes;
    std::vector<SceneMeshInstanceIR> mesh_instances;
    SceneCameraIR camera;

    bool operator==(const SceneIR&) const = default;
};
} // namespace omega::asset
