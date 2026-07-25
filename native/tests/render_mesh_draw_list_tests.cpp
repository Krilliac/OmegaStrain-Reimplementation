#include "omega/runtime/render_mesh_draw_list.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <iostream>
#include <limits>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

namespace omega::runtime::detail
{
struct RenderMeshDrawListTestAccess final
{
    static const auto& Storage(const RenderMeshDrawList& list) noexcept
    {
        return list.commands_;
    }
};
} // namespace omega::runtime::detail

namespace
{
using omega::asset::Float3IR;
using omega::asset::Matrix4x4IR;
using omega::runtime::IsBoxPossiblyVisible;
using omega::runtime::RenderMeshColorRgba8;
using omega::runtime::RenderMeshDrawCommand;
using omega::runtime::RenderMeshDrawList;
using omega::runtime::RenderMeshDrawListError;
using omega::runtime::RenderMeshDrawListErrorCode;
using omega::runtime::RenderMeshHandle;
using omega::runtime::RenderMeshRasterMode;

int failures = 0;

void Check(const bool condition, const std::string_view message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

template <typename Value>
void CheckError(const std::expected<Value, RenderMeshDrawListError>& result,
    const RenderMeshDrawListErrorCode code, const std::string_view message)
{
    Check(!result && result.error().code == code &&
              result.error().message == omega::runtime::RenderMeshDrawListErrorMessage(code),
        message);
}

[[nodiscard]] constexpr RenderMeshHandle Handle(const std::uint32_t index = 0U) noexcept
{
    return RenderMeshHandle{
        .pool_identity = 7U,
        .generation = 11U,
        .slot_index = index,
    };
}

[[nodiscard]] constexpr RenderMeshDrawCommand Command(const std::uint32_t index = 0U,
    const RenderMeshRasterMode raster_mode = RenderMeshRasterMode::Fill) noexcept
{
    return RenderMeshDrawCommand{
        .mesh = Handle(index),
        .object_to_clip = omega::asset::kIdentityMatrix4x4IR,
        .color = {
            .red = static_cast<std::uint8_t>(index),
            .green = 20U,
            .blue = 30U,
            .alpha = 255U,
        },
        .raster_mode = raster_mode,
    };
}

void CheckOptionalTexture()
{
    // The default draw command carries an invalid texture handle, selecting the
    // flat pipeline; a valid handle round-trips through the draw list unchanged
    // so the textured mesh pipeline can be selected per draw (Gap-A slice).
    Check(!RenderMeshDrawCommand{}.texture.valid(),
        "a default mesh draw command has no (flat) texture");

    RenderMeshDrawCommand textured = Command(0U);
    textured.texture = omega::runtime::RenderTextureHandle{
        .pool_identity = 3U,
        .generation = 5U,
        .slot_index = 9U,
    };
    Check(textured.texture.valid(), "an assigned texture handle is valid");

    const std::array<RenderMeshDrawCommand, 1U> caller{textured};
    auto created = RenderMeshDrawList::Create(caller);
    Check(created && created->size() == 1U &&
              created->commands()[0].texture == textured.texture &&
              created->commands()[0] == textured,
        "a textured mesh draw command round-trips its texture handle");
}

void CheckContractAndErrors()
{
    static_assert(sizeof(RenderMeshColorRgba8) == 4U);
    static_assert(std::is_trivially_copyable_v<RenderMeshColorRgba8>);
    static_assert(std::is_standard_layout_v<RenderMeshColorRgba8>);
    static_assert(sizeof(RenderMeshRasterMode) == 1U);
    static_assert(std::is_trivially_copyable_v<RenderMeshDrawCommand>);
    static_assert(std::is_standard_layout_v<RenderMeshDrawCommand>);
    static_assert(std::is_trivially_copyable_v<RenderMeshDrawList>);
    static_assert(std::is_standard_layout_v<RenderMeshDrawList>);
    static_assert(std::is_nothrow_copy_constructible_v<RenderMeshDrawList>);
    static_assert(std::is_nothrow_copy_assignable_v<RenderMeshDrawList>);
    static_assert(std::is_same_v<decltype(std::declval<const RenderMeshDrawList&>().commands()),
        std::span<const RenderMeshDrawCommand>>);
    static_assert(noexcept(RenderMeshDrawList::Create(
        std::declval<std::span<const RenderMeshDrawCommand>>())));

    Check(omega::runtime::kMaximumRenderMeshDrawsPerFrame == 64U,
        "the fixed mesh draw capacity is explicit");
    struct ExpectedError
    {
        RenderMeshDrawListErrorCode code;
        std::string_view name;
        std::string_view message;
    };
    constexpr std::array errors{
        ExpectedError{RenderMeshDrawListErrorCode::CapacityExceeded, "capacity-exceeded",
            "render mesh draw list command capacity is exceeded"},
        ExpectedError{RenderMeshDrawListErrorCode::InvalidMeshHandle, "invalid-mesh-handle",
            "render mesh draw list handle is invalid"},
        ExpectedError{RenderMeshDrawListErrorCode::NonFiniteTransform, "non-finite-transform",
            "render mesh draw list transform is non-finite"},
        ExpectedError{RenderMeshDrawListErrorCode::InvalidRasterMode, "invalid-raster-mode",
            "render mesh draw list raster mode is invalid"},
    };
    for (const ExpectedError& error : errors)
    {
        Check(omega::runtime::RenderMeshDrawListErrorCodeName(error.code) == error.name,
            "every mesh draw-list error has a fixed name");
        Check(omega::runtime::RenderMeshDrawListErrorMessage(error.code) == error.message,
            "every mesh draw-list error has a fixed message");
    }
}

void CheckCapacityOwnershipAndZeroTail()
{
    const RenderMeshDrawList empty;
    Check(empty.empty() && empty.size() == 0U && empty.commands().empty(),
        "a default mesh draw list is empty");
    const auto& empty_storage =
        omega::runtime::detail::RenderMeshDrawListTestAccess::Storage(empty);
    for (const RenderMeshDrawCommand& command : empty_storage)
        Check(command == RenderMeshDrawCommand{}, "default inactive commands are zero values");
    for (const std::byte byte : std::as_bytes(std::span{empty_storage}))
        Check(byte == std::byte{0}, "default inactive command bytes are zero");

    std::array<RenderMeshDrawCommand, 64U> maximum{};
    for (std::uint32_t index = 0U; index < maximum.size(); ++index)
        maximum[index] = Command(index);
    auto accepted = RenderMeshDrawList::Create(maximum);
    Check(accepted && accepted->size() == maximum.size(), "the exact draw capacity is accepted");

    std::array<RenderMeshDrawCommand, 65U> overflow{};
    overflow.fill(Command());
    overflow[0].mesh = {};
    CheckError(RenderMeshDrawList::Create(overflow),
        RenderMeshDrawListErrorCode::CapacityExceeded,
        "capacity plus one is rejected before command validation");

    const std::array original{
        Command(1U, RenderMeshRasterMode::Fill),
        Command(2U, RenderMeshRasterMode::Wireframe),
        Command(1U, RenderMeshRasterMode::Fill),
    };
    auto caller = original;
    auto created = RenderMeshDrawList::Create(caller);
    caller.fill({});
    Check(created && created->commands().size() == original.size() &&
              created->commands()[0] == original[0] &&
              created->commands()[1] == original[1] &&
              created->commands()[2] == original[2],
        "creation owns an ordered copy including duplicate handles, transforms, colors, and modes");
    if (created)
    {
        const auto& storage =
            omega::runtime::detail::RenderMeshDrawListTestAccess::Storage(*created);
        const auto tail = std::span{storage}.subspan(created->size());
        for (const RenderMeshDrawCommand& command : tail)
            Check(command == RenderMeshDrawCommand{}, "inactive tail commands remain zero values");
        for (const std::byte byte : std::as_bytes(tail))
            Check(byte == std::byte{0}, "inactive tail bytes remain zero");
    }
}

void CheckValidation()
{
    auto invalid_handle = Command();
    invalid_handle.mesh = {};
    invalid_handle.object_to_clip.row_major[0] =
        std::numeric_limits<float>::quiet_NaN();
    invalid_handle.raster_mode = static_cast<RenderMeshRasterMode>(0xffU);
    CheckError(RenderMeshDrawList::Create(std::span{&invalid_handle, 1U}),
        RenderMeshDrawListErrorCode::InvalidMeshHandle,
        "an invalid handle is rejected before later command fields");

    for (std::size_t element = 0U; element < 16U; ++element)
    {
        auto nonfinite = Command();
        nonfinite.object_to_clip.row_major[element] =
            element % 2U == 0U ? std::numeric_limits<float>::infinity()
                                : std::numeric_limits<float>::quiet_NaN();
        nonfinite.raster_mode = static_cast<RenderMeshRasterMode>(0xffU);
        CheckError(RenderMeshDrawList::Create(std::span{&nonfinite, 1U}),
            RenderMeshDrawListErrorCode::NonFiniteTransform,
            "every object-to-clip matrix element must be finite before raster validation");
    }

    auto invalid_mode = Command();
    invalid_mode.raster_mode = static_cast<RenderMeshRasterMode>(0xffU);
    CheckError(RenderMeshDrawList::Create(std::span{&invalid_mode, 1U}),
        RenderMeshDrawListErrorCode::InvalidRasterMode,
        "an undeclared raster mode is rejected");

    auto later_invalid_handle = Command(2U);
    later_invalid_handle.mesh = {};
    const std::array source_order{invalid_mode, later_invalid_handle};
    CheckError(RenderMeshDrawList::Create(source_order),
        RenderMeshDrawListErrorCode::InvalidRasterMode,
        "commands are validated completely in source order");

    auto transparent_fill = Command(0U, RenderMeshRasterMode::Fill);
    transparent_fill.color = {};
    auto opaque_wire = Command(1U, RenderMeshRasterMode::Wireframe);
    opaque_wire.color = {.red = 255U, .green = 255U, .blue = 255U, .alpha = 255U};
    const std::array boundaries{transparent_fill, opaque_wire};
    Check(RenderMeshDrawList::Create(boundaries).has_value(),
        "both raster modes and all RGBA byte boundaries are accepted as project values");
}
[[nodiscard]] constexpr Float3IR Point(const float x, const float y, const float z) noexcept
{
    return Float3IR{.x = x, .y = y, .z = z};
}

// A project-owned test transform written out literally so this test needs no link dependency
// on the camera module. It reproduces runtime::PerspectiveProjection for a 90-degree vertical
// field of view, square aspect, near = 1, far = 101: f = 1/tan(45 degrees) = 1,
// z_scale = far/(far - near) = 1.01, z_bias = -(far * near)/(far - near) = -1.01. Row-major,
// column-vector, left-handed (+Z forward), Direct3D clip depth [0, 1]. The camera sits at the
// origin looking along +Z, so world_to_view is the identity and object_to_clip is this matrix
// alone. It maps a point (x, y, z) to clip = (x, y, 1.01z - 1.01, z), which makes the visible
// region |x| <= z, |y| <= z, 1 <= z <= 101.
constexpr Matrix4x4IR kTestProjection{
    .row_major = {
        1.0F, 0.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 1.01F, -1.01F,
        0.0F, 0.0F, 1.0F, 0.0F,
    },
};

[[nodiscard]] bool Visible(const Float3IR& minimum, const Float3IR& maximum) noexcept
{
    return IsBoxPossiblyVisible(kTestProjection, minimum, maximum);
}

// Does any single corner of the box land inside kTestProjection's clip volume? This restates
// the clip volume directly, corner by corner, instead of intersecting per-plane outside-sets,
// so it is an independent check of the plane rule (not of the arithmetic). It deliberately
// promotes the same float matrix values to double in the same order as the implementation, so
// a corner sitting exactly on a plane cannot disagree merely through rounding.
[[nodiscard]] bool AnyCornerInside(const Float3IR& minimum, const Float3IR& maximum) noexcept
{
    const std::array<float, 2U> xs{minimum.x, maximum.x};
    const std::array<float, 2U> ys{minimum.y, maximum.y};
    const std::array<float, 2U> zs{minimum.z, maximum.z};
    for (const float z : zs)
    {
        for (const float y : ys)
        {
            for (const float x : xs)
            {
                const double clip_x = static_cast<double>(x);
                const double clip_y = static_cast<double>(y);
                const double clip_z = static_cast<double>(1.01F) * static_cast<double>(z) +
                                      static_cast<double>(-1.01F);
                const double clip_w = static_cast<double>(z);
                if (clip_x >= -clip_w && clip_x <= clip_w && clip_y >= -clip_w &&
                    clip_y <= clip_w && clip_z >= 0.0 && clip_z <= clip_w)
                {
                    return true;
                }
            }
        }
    }
    return false;
}

void CheckFrustumCulling()
{
    static_assert(noexcept(IsBoxPossiblyVisible(std::declval<const Matrix4x4IR&>(),
        std::declval<const Float3IR&>(), std::declval<const Float3IR&>())));
    static_assert(std::is_same_v<decltype(IsBoxPossiblyVisible(
                                     std::declval<const Matrix4x4IR&>(),
                                     std::declval<const Float3IR&>(),
                                     std::declval<const Float3IR&>())),
        bool>);

    Check(Visible(Point(-1.0F, -1.0F, 9.0F), Point(1.0F, 1.0F, 11.0F)),
        "a box centred in the view volume is possibly visible");

    // Far outside each of the six clip planes in turn, every corner shares one rejection.
    Check(!Visible(Point(-100.0F, -1.0F, 9.0F), Point(-90.0F, 1.0F, 11.0F)),
        "a box far beyond the left clip plane is culled");
    Check(!Visible(Point(90.0F, -1.0F, 9.0F), Point(100.0F, 1.0F, 11.0F)),
        "a box far beyond the right clip plane is culled");
    Check(!Visible(Point(-1.0F, -100.0F, 9.0F), Point(1.0F, -90.0F, 11.0F)),
        "a box far below the bottom clip plane is culled");
    Check(!Visible(Point(-1.0F, 90.0F, 9.0F), Point(1.0F, 100.0F, 11.0F)),
        "a box far above the top clip plane is culled");
    Check(!Visible(Point(-1.0F, -1.0F, -11.0F), Point(1.0F, 1.0F, -9.0F)),
        "a box entirely behind the camera is culled at the near plane");
    Check(!Visible(Point(-1.0F, -1.0F, 200.0F), Point(1.0F, 1.0F, 300.0F)),
        "a box entirely beyond the far plane is culled");

    // Straddling a plane leaves corners on its inside, so no plane rejects the whole box.
    Check(Visible(Point(-20.0F, -1.0F, 9.0F), Point(0.0F, 1.0F, 11.0F)),
        "a box straddling the left clip plane is possibly visible");
    Check(Visible(Point(0.0F, -1.0F, 9.0F), Point(20.0F, 1.0F, 11.0F)),
        "a box straddling the right clip plane is possibly visible");
    Check(Visible(Point(-1.0F, -20.0F, 9.0F), Point(1.0F, 0.0F, 11.0F)),
        "a box straddling the bottom clip plane is possibly visible");
    Check(Visible(Point(-1.0F, 0.0F, 9.0F), Point(1.0F, 20.0F, 11.0F)),
        "a box straddling the top clip plane is possibly visible");
    Check(Visible(Point(-1.0F, -1.0F, -5.0F), Point(1.0F, 1.0F, 20.0F)),
        "a box straddling the near clip plane is possibly visible");
    Check(Visible(Point(-1.0F, -1.0F, 50.0F), Point(1.0F, 1.0F, 200.0F)),
        "a box straddling the far clip plane is possibly visible");

    // The rejection is strict, so a face resting exactly on a plane is still inside it. Both
    // boxes here are flat in z, which is well formed and gets the ordinary test.
    Check(Visible(Point(-30.0F, -1.0F, 10.0F), Point(-10.0F, 1.0F, 10.0F)),
        "a box whose near face lies exactly on the left clip plane is possibly visible");
    Check(!Visible(Point(-30.0F, -1.0F, 10.0F), Point(-10.001F, 1.0F, 10.0F)),
        "a flat box just past the left clip plane is culled");

    // The classic false-cull case. All eight corners of this box are outside the frustum, but
    // each is outside a DIFFERENT plane, so no single plane rejects the box and it must stay
    // visible. A naive "any corner is outside" test drops it, which makes the room around the
    // camera vanish the moment the camera steps inside its bounds.
    Check(Visible(Point(-1000.0F, -1000.0F, -1000.0F), Point(1000.0F, 1000.0F, 1000.0F)),
        "a huge box enclosing the camera is possibly visible");
    Check(!AnyCornerInside(Point(-1000.0F, -1000.0F, -1000.0F),
              Point(1000.0F, 1000.0F, 1000.0F)),
        "the enclosing box really does have every corner outside the frustum");

    // Under the identity transform w is 1, so the clip volume is |x| <= 1, |y| <= 1,
    // 0 <= z <= 1 -- the same rule with no projection applied.
    Check(IsBoxPossiblyVisible(omega::asset::kIdentityMatrix4x4IR, Point(-0.5F, -0.5F, 0.25F),
              Point(0.5F, 0.5F, 0.75F)),
        "a box inside the identity clip volume is possibly visible");
    Check(!IsBoxPossiblyVisible(omega::asset::kIdentityMatrix4x4IR, Point(2.0F, -0.5F, 0.25F),
              Point(3.0F, 0.5F, 0.75F)),
        "a box beyond x = w is culled under the identity transform");

    // Fail soft: anything that cannot be evaluated, or that describes no volume, stays visible
    // rather than being culled. The reference box below is culled when it is well formed.
    const Float3IR culled_minimum = Point(-100.0F, -1.0F, 9.0F);
    const Float3IR culled_maximum = Point(-90.0F, 1.0F, 11.0F);
    Check(!Visible(culled_minimum, culled_maximum), "the fail-soft reference box is culled");
    for (std::size_t element = 0U; element < 16U; ++element)
    {
        Matrix4x4IR nonfinite = kTestProjection;
        nonfinite.row_major[element] = element % 2U == 0U
                                           ? std::numeric_limits<float>::quiet_NaN()
                                           : std::numeric_limits<float>::infinity();
        Check(IsBoxPossiblyVisible(nonfinite, culled_minimum, culled_maximum),
            "every non-finite transform element keeps an otherwise culled box visible");
    }
    Check(Visible(Point(std::numeric_limits<float>::quiet_NaN(), -1.0F, 9.0F), culled_maximum),
        "a NaN box minimum keeps an otherwise culled box visible");
    Check(Visible(culled_minimum, Point(-90.0F, std::numeric_limits<float>::quiet_NaN(), 11.0F)),
        "a NaN box maximum keeps an otherwise culled box visible");
    Check(Visible(Point(-std::numeric_limits<float>::infinity(), -1.0F, 9.0F), culled_maximum),
        "an infinite box bound keeps an otherwise culled box visible");
    Check(Visible(culled_maximum, culled_minimum), "an inverted box is visible");
    Check(Visible(Point(0.0F, 0.0F, 9.0F), Point(0.0F, 0.0F, 9.0F)),
        "a zero-extent box inside the frustum is possibly visible");

    // Exhaustive one-directional guard over a coarse grid spanning the frustum and well past
    // it: whenever a corner is genuinely inside the clip volume, the box must NOT be culled.
    // This is the property that matters -- the predicate may keep an invisible box, but it
    // must never drop a visible one.
    for (int center_x = -6; center_x <= 6; ++center_x)
    {
        for (int center_y = -6; center_y <= 6; ++center_y)
        {
            for (int center_z = -6; center_z <= 6; ++center_z)
            {
                const Float3IR minimum = Point(static_cast<float>(center_x) * 20.0F - 5.0F,
                    static_cast<float>(center_y) * 20.0F - 5.0F,
                    static_cast<float>(center_z) * 20.0F - 5.0F);
                const Float3IR maximum = Point(static_cast<float>(center_x) * 20.0F + 5.0F,
                    static_cast<float>(center_y) * 20.0F + 5.0F,
                    static_cast<float>(center_z) * 20.0F + 5.0F);
                if (AnyCornerInside(minimum, maximum))
                {
                    Check(Visible(minimum, maximum),
                        "a box with a corner inside the frustum is never culled");
                }
            }
        }
    }
}
} // namespace

int main()
{
    CheckContractAndErrors();
    CheckCapacityOwnershipAndZeroTail();
    CheckValidation();
    CheckOptionalTexture();
    CheckFrustumCulling();
    if (failures == 0)
        std::cout << "omega_render_mesh_draw_list_tests: all checks passed\n";
    return failures == 0 ? 0 : 1;
}
