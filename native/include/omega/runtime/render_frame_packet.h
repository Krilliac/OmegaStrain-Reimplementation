#pragma once

#include "omega/runtime/render_draw_list.h"
#include "omega/runtime/render_mesh_draw_list.h"

#include <chrono>
#include <cstdint>
#include <type_traits>

namespace omega::runtime
{
// Renderer-neutral project-owned clear value. All byte combinations are valid; this assigns no
// blending, color-space, retail-format, or serialized-wire semantics.
struct RenderClearColorRgba8
{
    std::uint8_t red = 0U;
    std::uint8_t green = 0U;
    std::uint8_t blue = 0U;
    std::uint8_t alpha = 0U;

    friend constexpr bool operator==(
        const RenderClearColorRgba8&, const RenderClearColorRgba8&) noexcept = default;
};

inline constexpr RenderClearColorRgba8 kDefaultRenderClearColor{
    .red = 4U,
    .green = 5U,
    .blue = 10U,
    .alpha = 255U,
};

static_assert(sizeof(RenderClearColorRgba8) == 4U);
static_assert(std::is_trivially_copyable_v<RenderClearColorRgba8>);
static_assert(std::is_standard_layout_v<RenderClearColorRgba8>);

// Owned renderer-neutral value copied at the game/render boundary. It contains only project-owned
// host state; no component pointers, retail-format views, SDL types, or reloadable vtables escape
// with it. The current same-thread host consumes it synchronously, but the ownership contract also
// permits a future render queue to move complete immutable copies.
struct RenderFramePacket
{
    std::uint64_t rendered_frame_index = 0;
    std::uint64_t completed_simulation_steps = 0;
    std::chrono::nanoseconds simulated_time{0};
    std::uint32_t alive_entities = 0;
    // Explicit project-owned target clear policy copied with the frame.
    RenderClearColorRgba8 clear_color = kDefaultRenderClearColor;
    // Owned fixed command value. Commands do not pin texture generations; the current synchronous
    // caller keeps every referenced texture resident through consumption.
    RenderDrawList draw_list;
    // Owned fixed indexed-mesh command value. It defaults empty so callers which publish only the
    // existing clear/texture presentation retain exactly that behavior. Commands do not pin mesh
    // generations; the synchronous caller keeps every referenced mesh resident through consumption.
    RenderMeshDrawList mesh_draw_list;
};

static_assert(std::is_trivially_copyable_v<RenderFramePacket>);
static_assert(std::is_standard_layout_v<RenderFramePacket>);
} // namespace omega::runtime
