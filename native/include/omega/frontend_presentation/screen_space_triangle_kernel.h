#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>

namespace omega::frontend::presentation
{
// Project safety ceilings, not retail-format claims. Callers may tighten but
// cannot raise them.
inline constexpr std::uint32_t kScreenSpaceTriangleMaximumAffineChannels = 16U;
inline constexpr std::uint32_t kScreenSpaceTriangleMaximumClipWidth = 8'192U;
inline constexpr std::uint32_t kScreenSpaceTriangleMaximumClipHeight = 8'192U;
inline constexpr std::uint64_t kScreenSpaceTriangleMaximumCoveredPixels = 4'194'304ULL;
inline constexpr float kScreenSpaceTriangleMaximumCoordinateMagnitude = 1'048'576.0F;

struct ScreenSpaceTriangleLimits final
{
    std::uint32_t maximum_affine_channels =
        kScreenSpaceTriangleMaximumAffineChannels;
    std::uint32_t maximum_clip_width = kScreenSpaceTriangleMaximumClipWidth;
    std::uint32_t maximum_clip_height = kScreenSpaceTriangleMaximumClipHeight;
    std::uint64_t maximum_covered_pixels =
        kScreenSpaceTriangleMaximumCoveredPixels;
    float maximum_coordinate_magnitude =
        kScreenSpaceTriangleMaximumCoordinateMagnitude;

    bool operator==(const ScreenSpaceTriangleLimits&) const = default;
};

// Half-open clip bounds. Covered integer-lattice samples satisfy
// left <= x < right and top <= y < bottom.
struct ScreenSpaceClipRect final
{
    std::int32_t left = 0;
    std::int32_t top = 0;
    std::int32_t right = 0;
    std::int32_t bottom = 0;

    bool operator==(const ScreenSpaceClipRect&) const = default;
};

// All values are already projected screen-space inputs. This type assigns no
// projection, depth, texture, blend, address, or render-state semantics. Each
// vertex must expose the same number of affine channels. The borrowed channel
// views need only remain valid until RasterizeScreenSpaceTriangle returns.
struct ScreenSpaceTriangleVertex final
{
    float x = 0.0F;
    float y = 0.0F;
    std::span<const float> affine_channels;
    float s = 0.0F;
    float t = 0.0F;
    float q = 1.0F;
};

struct ScreenSpaceTriangleSample final
{
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::array<float, kScreenSpaceTriangleMaximumAffineChannels>
        affine_channels{};
    std::uint32_t affine_channel_count = 0U;
    float s = 0.0F;
    float t = 0.0F;
    float q = 1.0F;
    float s_over_q = 0.0F;
    float t_over_q = 0.0F;

    [[nodiscard]] std::span<const float> AffineChannels() const noexcept
    {
        return std::span<const float>(affine_channels.data(),
            static_cast<std::size_t>(affine_channel_count));
    }

    bool operator==(const ScreenSpaceTriangleSample&) const = default;
};

enum class ScreenSpaceTriangleVisitControl : std::uint8_t
{
    Continue = 0U,
    Stop,
};

using ScreenSpaceTriangleVisitor = ScreenSpaceTriangleVisitControl (*)(
    void* context, const ScreenSpaceTriangleSample& sample) noexcept;

enum class ScreenSpaceTriangleError : std::uint8_t
{
    InvalidLimits = 0U,
    InvalidClip,
    LimitExceeded,
    NonFiniteInput,
    InconsistentAffineChannelCount,
    DegenerateTriangle,
    ArithmeticFailure,
    PerspectiveDivideFailure,
    MissingVisitor,
    VisitorStopped,
    InvalidVisitorResult,
};

struct ScreenSpaceTriangleSummary final
{
    std::uint64_t covered_pixel_count = 0U;

    bool operator==(const ScreenSpaceTriangleSummary&) const = default;
};

using ScreenSpaceTriangleResult =
    std::expected<ScreenSpaceTriangleSummary, ScreenSpaceTriangleError>;

// [any thread; stateless/reentrant] Borrows exactly three already-projected
// vertices, snapshots their bounded values into fixed stack storage, and
// visits covered integer-lattice samples in increasing y then increasing x.
// Coverage uses ceil scan conversion: top/left are inclusive and bottom/right
// are exclusive. Winding does not affect coverage or visit order.
//
// Affine channels and S/T/Q are plane-interpolated. S/Q and T/Q are returned
// only after an exact nonzero, finite Q check. A preflight pass validates every
// covered sample and the complete pixel budget before the first callback, so
// kernel failures never expose a partial callback prefix. VisitorStopped is an
// explicit caller-requested prefix. The callback must not retain the sample
// reference; it may copy the owned value. The kernel performs no allocation,
// retains no input or output, owns no framebuffer, and is hot-reload-safe.
[[nodiscard]] ScreenSpaceTriangleResult RasterizeScreenSpaceTriangle(
    std::span<const ScreenSpaceTriangleVertex, 3U> vertices,
    ScreenSpaceClipRect clip,
    ScreenSpaceTriangleVisitor visitor,
    void* visitor_context,
    ScreenSpaceTriangleLimits limits = {}) noexcept;
} // namespace omega::frontend::presentation
