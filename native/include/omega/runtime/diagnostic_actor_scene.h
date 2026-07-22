#pragma once

#include "omega/asset/render_mesh_ir.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string_view>

namespace omega::runtime
{
inline constexpr std::size_t kProjectDiagnosticActorPositionCount = 3U;
inline constexpr std::size_t kProjectDiagnosticActorTriangleIndexCount = 3U;
inline constexpr std::uint64_t kProjectDiagnosticActorMeshLogicalBytes =
    kProjectDiagnosticActorPositionCount * sizeof(asset::Float3IR) +
    kProjectDiagnosticActorTriangleIndexCount * sizeof(std::uint32_t);
inline constexpr std::string_view kProjectDiagnosticActorMeshAllocationError =
    "project diagnostic actor mesh allocation failed";

// [any thread; reentrant] Builds one small project-authored diagnostic triangle. The returned IR
// owns its storage and contains no retail geometry, coordinate, scale, material, or animation
// claim. Allocation failure is translated to fixed path-free text and no partial value escapes.
[[nodiscard]] std::expected<asset::RenderMeshIR, std::string_view>
BuildProjectDiagnosticActorMesh() noexcept;
} // namespace omega::runtime
