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
} // namespace

int main()
{
    CheckContractAndErrors();
    CheckCapacityOwnershipAndZeroTail();
    CheckValidation();
    if (failures == 0)
        std::cout << "omega_render_mesh_draw_list_tests: all checks passed\n";
    return failures == 0 ? 0 : 1;
}
