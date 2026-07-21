#include "diagnostic_actor_marker.h"

#include "omega/runtime/render_draw_list.h"
#include "omega/simulation/simulation_world.h"

#include <array>
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <limits>
#include <span>
#include <string_view>
#include <type_traits>

namespace
{
using omega::app::PlanProjectDiagnosticActorMarkerDestination;
using omega::runtime::RenderDrawList;
using omega::runtime::RenderSourceRectQ16;
using omega::runtime::RenderTargetRectQ16;
using omega::runtime::RenderTextureBlitCommand;
using omega::runtime::RenderTextureFilterMode;
using omega::runtime::RenderTextureFitMode;
using omega::runtime::RenderTextureHandle;
using omega::simulation::Position3;

int failures = 0;

void Check(const bool condition, const std::string_view message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

[[nodiscard]] constexpr std::int64_t ClampCoordinate(
    const std::int64_t value) noexcept
{
    if (value < -31)
        return -31;
    if (value > 31)
        return 31;
    return value;
}

[[nodiscard]] constexpr RenderTargetRectQ16 ExpectedDestination(
    const Position3 position) noexcept
{
    constexpr std::int64_t center = 32'768;
    constexpr std::int64_t unit = 1'024;
    constexpr std::int64_t half_extent = 1'024;
    const std::int64_t center_x = center + ClampCoordinate(position.x) * unit;
    const std::int64_t center_y = center - ClampCoordinate(position.z) * unit;
    return RenderTargetRectQ16{
        .left = static_cast<std::uint32_t>(center_x - half_extent),
        .top = static_cast<std::uint32_t>(center_y - half_extent),
        .right = static_cast<std::uint32_t>(center_x + half_extent),
        .bottom = static_cast<std::uint32_t>(center_y + half_extent),
    };
}

[[nodiscard]] constexpr bool IsValidDestination(
    const RenderTargetRectQ16 destination) noexcept
{
    return destination.left < destination.right &&
           destination.top < destination.bottom &&
           destination.right <= omega::runtime::kNormalizedRenderExtent &&
           destination.bottom <= omega::runtime::kNormalizedRenderExtent;
}

[[nodiscard]] bool IsAcceptedByRenderDrawList(
    const RenderTargetRectQ16 destination) noexcept
{
    constexpr RenderTextureHandle handle{
        .pool_identity = 1U,
        .generation = 1U,
        .slot_index = 0U,
    };
    constexpr RenderSourceRectQ16 full_source{
        .left = 0U,
        .top = 0U,
        .right = omega::runtime::kNormalizedRenderExtent,
        .bottom = omega::runtime::kNormalizedRenderExtent,
    };
    const RenderTextureBlitCommand command{
        .texture = handle,
        .source = full_source,
        .destination = destination,
        .fit_mode = RenderTextureFitMode::Stretch,
        .filter_mode = RenderTextureFilterMode::Nearest,
    };
    const auto draw_list = RenderDrawList::Create(std::span{&command, 1U});
    return draw_list && draw_list->commands().size() == 1U &&
           draw_list->commands().front() == command;
}

void CheckPosition(const Position3 position, const std::string_view message)
{
    const RenderTargetRectQ16 first =
        PlanProjectDiagnosticActorMarkerDestination(position);
    const RenderTargetRectQ16 repeat =
        PlanProjectDiagnosticActorMarkerDestination(position);
    const RenderTargetRectQ16 expected = ExpectedDestination(position);
    Check(first == expected && repeat == expected, message);
    Check(first.right - first.left == 2'048U &&
              first.bottom - first.top == 2'048U,
        "every generated position retains the fixed marker extent");
    Check(IsValidDestination(first),
        "every generated position produces a valid bounded half-open rectangle");
    Check(IsAcceptedByRenderDrawList(first),
        "every generated position is accepted by the renderer-neutral draw list");
}

void CheckContract()
{
    using Result = RenderTargetRectQ16;
    static_assert(std::is_same_v<decltype(
                      PlanProjectDiagnosticActorMarkerDestination(Position3{})),
        Result>);
    static_assert(noexcept(
        PlanProjectDiagnosticActorMarkerDestination(Position3{})));

    constexpr RenderTargetRectQ16 origin =
        PlanProjectDiagnosticActorMarkerDestination(Position3{});
    constexpr RenderTargetRectQ16 positive_x =
        PlanProjectDiagnosticActorMarkerDestination(Position3{.x = 1});
    constexpr RenderTargetRectQ16 positive_z =
        PlanProjectDiagnosticActorMarkerDestination(Position3{.z = 1});
    constexpr RenderTargetRectQ16 positive_corner =
        PlanProjectDiagnosticActorMarkerDestination(Position3{
            .x = std::numeric_limits<std::int64_t>::max(),
            .y = std::numeric_limits<std::int64_t>::min(),
            .z = std::numeric_limits<std::int64_t>::max(),
        });
    constexpr RenderTargetRectQ16 negative_corner =
        PlanProjectDiagnosticActorMarkerDestination(Position3{
            .x = std::numeric_limits<std::int64_t>::min(),
            .y = std::numeric_limits<std::int64_t>::max(),
            .z = std::numeric_limits<std::int64_t>::min(),
        });
    static_assert(origin == RenderTargetRectQ16{31'744U, 31'744U, 33'792U, 33'792U});
    static_assert(positive_x ==
                  RenderTargetRectQ16{32'768U, 31'744U, 34'816U, 33'792U});
    static_assert(positive_z ==
                  RenderTargetRectQ16{31'744U, 30'720U, 33'792U, 32'768U});
    static_assert(positive_corner ==
                  RenderTargetRectQ16{63'488U, 0U, 65'536U, 2'048U});
    static_assert(negative_corner ==
                  RenderTargetRectQ16{0U, 63'488U, 2'048U, 65'536U});
}

void CheckCompleteVisibleGrid()
{
    for (std::int64_t x = -31; x <= 31; ++x)
    {
        for (std::int64_t z = -31; z <= 31; ++z)
        {
            CheckPosition(Position3{.x = x, .z = z},
                "every visible X/Z pair maps exactly and deterministically");
        }
    }
}

void CheckCompleteInt16AxisSweeps()
{
    constexpr std::int32_t minimum =
        std::numeric_limits<std::int16_t>::min();
    constexpr std::int32_t maximum =
        std::numeric_limits<std::int16_t>::max();
    for (std::int32_t raw = minimum; raw <= maximum; ++raw)
    {
        const std::int64_t coordinate = raw;
        CheckPosition(Position3{.x = coordinate},
            "every int16 X value follows the exact clamp policy");
        CheckPosition(Position3{.z = coordinate},
            "every int16 Z value follows the exact clamp policy");
    }
}

void CheckSignedExtremesAndYInvariance()
{
    constexpr std::array coordinates{
        std::numeric_limits<std::int64_t>::min(),
        std::int64_t{-32},
        std::int64_t{-31},
        std::int64_t{-1},
        std::int64_t{0},
        std::int64_t{1},
        std::int64_t{31},
        std::int64_t{32},
        std::numeric_limits<std::int64_t>::max(),
    };
    constexpr std::array y_values{
        std::numeric_limits<std::int64_t>::min(),
        std::int64_t{-1},
        std::int64_t{0},
        std::int64_t{1},
        std::numeric_limits<std::int64_t>::max(),
    };

    for (const std::int64_t x : coordinates)
    {
        for (const std::int64_t z : coordinates)
        {
            const RenderTargetRectQ16 y_zero =
                PlanProjectDiagnosticActorMarkerDestination(
                    Position3{.x = x, .z = z});
            for (const std::int64_t y : y_values)
            {
                const Position3 position{.x = x, .y = y, .z = z};
                CheckPosition(position,
                    "signed extremes and clamp transitions map exactly");
                Check(PlanProjectDiagnosticActorMarkerDestination(position) == y_zero,
                    "Y is ignored for every generated signed-extreme X/Z pair");
            }
        }
    }
}

void CheckAxisDirectionAndSaturation()
{
    for (std::int64_t x = -31; x < 31; ++x)
    {
        const RenderTargetRectQ16 current =
            PlanProjectDiagnosticActorMarkerDestination(Position3{.x = x});
        const RenderTargetRectQ16 next =
            PlanProjectDiagnosticActorMarkerDestination(Position3{.x = x + 1});
        Check(next.left == current.left + 1'024U &&
                  next.right == current.right + 1'024U &&
                  next.top == current.top && next.bottom == current.bottom,
            "positive in-range X advances only horizontal edges to the right");
    }

    for (std::int64_t z = -31; z < 31; ++z)
    {
        const RenderTargetRectQ16 current =
            PlanProjectDiagnosticActorMarkerDestination(Position3{.z = z});
        const RenderTargetRectQ16 next =
            PlanProjectDiagnosticActorMarkerDestination(Position3{.z = z + 1});
        Check(next.top + 1'024U == current.top &&
                  next.bottom + 1'024U == current.bottom &&
                  next.left == current.left && next.right == current.right,
            "positive in-range Z advances only vertical edges upward");
    }

    const RenderTargetRectQ16 minimum_x =
        PlanProjectDiagnosticActorMarkerDestination(Position3{.x = -31});
    const RenderTargetRectQ16 maximum_x =
        PlanProjectDiagnosticActorMarkerDestination(Position3{.x = 31});
    const RenderTargetRectQ16 minimum_z =
        PlanProjectDiagnosticActorMarkerDestination(Position3{.z = -31});
    const RenderTargetRectQ16 maximum_z =
        PlanProjectDiagnosticActorMarkerDestination(Position3{.z = 31});
    Check(minimum_x == PlanProjectDiagnosticActorMarkerDestination(Position3{
                           .x = std::numeric_limits<std::int64_t>::min()}) &&
              maximum_x == PlanProjectDiagnosticActorMarkerDestination(Position3{
                               .x = std::numeric_limits<std::int64_t>::max()}),
        "X saturates at both visible edges");
    Check(minimum_z == PlanProjectDiagnosticActorMarkerDestination(Position3{
                           .z = std::numeric_limits<std::int64_t>::min()}) &&
              maximum_z == PlanProjectDiagnosticActorMarkerDestination(Position3{
                               .z = std::numeric_limits<std::int64_t>::max()}),
        "Z saturates at both visible edges");
}
} // namespace

int main()
{
    CheckContract();
    CheckCompleteVisibleGrid();
    CheckCompleteInt16AxisSweeps();
    CheckSignedExtremesAndYInvariance();
    CheckAxisDirectionAndSaturation();

    if (failures != 0)
    {
        std::cerr << failures << " diagnostic actor marker test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "diagnostic actor marker tests passed\n";
    return EXIT_SUCCESS;
}
