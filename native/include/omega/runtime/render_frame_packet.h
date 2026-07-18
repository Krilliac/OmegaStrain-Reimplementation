#pragma once

#include <chrono>
#include <cstdint>
#include <type_traits>

namespace omega::runtime
{
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
};

static_assert(std::is_trivially_copyable_v<RenderFramePacket>);
static_assert(std::is_standard_layout_v<RenderFramePacket>);
} // namespace omega::runtime
