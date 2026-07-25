#pragma once

#include "omega/asset/geometry_ir.h"
#include "omega/runtime/render_mesh.h"
#include "omega/runtime/render_texture.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>
#include <type_traits>

namespace omega::runtime
{
inline constexpr std::size_t kMaximumRenderMeshDrawsPerFrame = 64U;

// Project-owned constant mesh color. All byte combinations are valid and assign no retail material,
// lighting, blending, or color-space semantics.
struct RenderMeshColorRgba8
{
    std::uint8_t red = 0U;
    std::uint8_t green = 0U;
    std::uint8_t blue = 0U;
    std::uint8_t alpha = 0U;

    friend constexpr bool operator==(
        const RenderMeshColorRgba8&, const RenderMeshColorRgba8&) noexcept = default;
};

enum class RenderMeshRasterMode : std::uint8_t
{
    Fill = 0U,
    Wireframe = 1U,
};

struct RenderMeshDrawCommand
{
    RenderMeshHandle mesh;
    asset::Matrix4x4IR object_to_clip;
    RenderMeshColorRgba8 color;
    RenderMeshRasterMode raster_mode = RenderMeshRasterMode::Fill;
    // Optional albedo texture sampled with the mesh's per-vertex UVs. A default
    // (invalid) handle selects the flat solid-color mesh pipeline; a valid handle
    // selects the textured pipeline. The referenced texture generation is kept
    // resident by the caller through synchronous consumption, like the mesh.
    RenderTextureHandle texture{};

    friend constexpr bool operator==(
        const RenderMeshDrawCommand&, const RenderMeshDrawCommand&) noexcept = default;
};

enum class RenderMeshDrawListErrorCode
{
    CapacityExceeded,
    InvalidMeshHandle,
    NonFiniteTransform,
    InvalidRasterMode,
};

[[nodiscard]] constexpr std::string_view RenderMeshDrawListErrorCodeName(
    const RenderMeshDrawListErrorCode code) noexcept
{
    switch (code)
    {
    case RenderMeshDrawListErrorCode::CapacityExceeded:
        return "capacity-exceeded";
    case RenderMeshDrawListErrorCode::InvalidMeshHandle:
        return "invalid-mesh-handle";
    case RenderMeshDrawListErrorCode::NonFiniteTransform:
        return "non-finite-transform";
    case RenderMeshDrawListErrorCode::InvalidRasterMode:
        return "invalid-raster-mode";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view RenderMeshDrawListErrorMessage(
    const RenderMeshDrawListErrorCode code) noexcept
{
    switch (code)
    {
    case RenderMeshDrawListErrorCode::CapacityExceeded:
        return "render mesh draw list command capacity is exceeded";
    case RenderMeshDrawListErrorCode::InvalidMeshHandle:
        return "render mesh draw list handle is invalid";
    case RenderMeshDrawListErrorCode::NonFiniteTransform:
        return "render mesh draw list transform is non-finite";
    case RenderMeshDrawListErrorCode::InvalidRasterMode:
        return "render mesh draw list raster mode is invalid";
    }
    return "render mesh draw list error is unknown";
}

struct RenderMeshDrawListError
{
    RenderMeshDrawListErrorCode code = RenderMeshDrawListErrorCode::CapacityExceeded;
    std::string_view message = RenderMeshDrawListErrorMessage(code);
};

namespace detail
{
struct RenderMeshDrawListTestAccess;
}

// Fixed owned renderer-neutral draw value. Commands retain generation identities but do not pin
// resources; the caller keeps referenced mesh generations resident through synchronous consumption.
class RenderMeshDrawList final
{
public:
    // Clears complete backing storage, including padding, so inactive commands never retain an
    // earlier handle or transform when the list is copied as raw bytes.
    RenderMeshDrawList() noexcept;

    [[nodiscard]] static std::expected<RenderMeshDrawList, RenderMeshDrawListError> Create(
        std::span<const RenderMeshDrawCommand> commands) noexcept;

    [[nodiscard]] std::span<const RenderMeshDrawCommand> commands() const noexcept
    {
        return {commands_.data(), static_cast<std::size_t>(count_)};
    }

    [[nodiscard]] std::uint32_t size() const noexcept { return count_; }
    [[nodiscard]] bool empty() const noexcept { return count_ == 0U; }

private:
    friend struct detail::RenderMeshDrawListTestAccess;

    std::array<RenderMeshDrawCommand, kMaximumRenderMeshDrawsPerFrame> commands_{};
    std::uint32_t count_ = 0U;
};

// [any thread; reentrant] Conservative frustum test: is the axis-aligned box spanning
// [minimum, maximum] possibly visible under `object_to_clip`?
//
// The box's eight corners are transformed to homogeneous clip space using this project's
// convention -- row-major storage, column-vector multiply, left-handed with a Direct3D clip
// volume of -w <= x <= w, -w <= y <= w, 0 <= z <= w (see runtime::PerspectiveProjection,
// runtime::ComposeObjectToClip, and the mul(ModelViewProj, position) in the mesh vertex
// shader). The box is rejected only when all eight corners lie strictly outside the SAME
// clip plane. Corners are compared homogeneously and are never divided by w, so a box that
// spans the camera plane -- where w changes sign -- is still classified correctly.
//
// The test is conservative in one direction only: it NEVER reports a visible box as
// invisible, and it is allowed to report an invisible box as visible. A box whose corners
// are each outside a different plane -- most importantly a large box that encloses the
// camera -- is reported visible, which is correct; the common "any corner is outside" test
// wrongly rejects it and makes the room around the camera disappear.
//
// This does NOT model: occlusion (a box fully hidden behind other geometry is still
// reported visible), per-triangle or mesh-shape culling (only the box is considered, never
// the geometry inside it), clipping of any kind (nothing is split or trimmed -- the result
// is a single yes/no), and it derives no screen extent, distance, or level of detail. It is
// a pure predicate: it reads no state, allocates nothing, and touches no GPU resource.
//
// Fail soft: a non-finite matrix, a non-finite box bound, or an inverted box (any minimum
// component greater than its maximum) returns true (visible). A zero-extent box -- a point,
// or a flat box such as a floor quad -- is well formed and is tested like any other.
[[nodiscard]] bool IsBoxPossiblyVisible(const asset::Matrix4x4IR& object_to_clip,
    const asset::Float3IR& minimum, const asset::Float3IR& maximum) noexcept;

static_assert(sizeof(RenderMeshColorRgba8) == 4U);
static_assert(std::is_trivially_copyable_v<RenderMeshColorRgba8>);
static_assert(std::is_standard_layout_v<RenderMeshColorRgba8>);
static_assert(sizeof(RenderMeshRasterMode) == 1U);
static_assert(std::is_trivially_copyable_v<RenderMeshDrawCommand>);
static_assert(std::is_standard_layout_v<RenderMeshDrawCommand>);
static_assert(std::is_trivially_copyable_v<RenderMeshDrawList>);
static_assert(std::is_standard_layout_v<RenderMeshDrawList>);
} // namespace omega::runtime
