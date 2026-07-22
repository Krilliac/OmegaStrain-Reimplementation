#include "diagnostic_actor_marker.h"

#include "omega/runtime/render_draw_list.h"
#include "omega/simulation/simulation_world.h"

#include <array>
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>

namespace
{
using omega::app::PlanProjectDiagnosticActorMarkerDestination;
using omega::app::PlanProjectDiagnosticActorMeshTransform;
using omega::app::PlanProjectDiagnosticFireCueRectangle;
using omega::app::PlanProjectDiagnosticTargetCueRectangles;
using omega::asset::Matrix4x4IR;
using omega::runtime::PointerPositionQ16;
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

[[nodiscard]] constexpr Matrix4x4IR ExpectedMeshTransform(
    const Position3 position) noexcept
{
    Matrix4x4IR transform = omega::asset::kIdentityMatrix4x4IR;
    transform.row_major[3U] =
        static_cast<float>(ClampCoordinate(position.x)) / 32.0F;
    transform.row_major[7U] =
        static_cast<float>(ClampCoordinate(position.z)) / 32.0F;
    return transform;
}

[[nodiscard]] constexpr std::uint32_t ClampCueAxis(
    const std::uint32_t value) noexcept
{
    if (value < 4'096U)
        return 4'096U;
    if (value > 61'440U)
        return 61'440U;
    return value;
}

[[nodiscard]] constexpr std::array<RenderTargetRectQ16, 2U>
ExpectedTargetCues(const std::uint32_t x, const std::uint32_t y) noexcept
{
    return {
        RenderTargetRectQ16{
            .left = x - 4'096U,
            .top = y - 256U,
            .right = x + 4'096U,
            .bottom = y + 256U,
        },
        RenderTargetRectQ16{
            .left = x - 256U,
            .top = y - 4'096U,
            .right = x + 256U,
            .bottom = y + 4'096U,
        },
    };
}

[[nodiscard]] constexpr RenderTargetRectQ16 ExpectedFireCue(
    const std::uint32_t x, const std::uint32_t y) noexcept
{
    return RenderTargetRectQ16{
        .left = x - 768U,
        .top = y - 768U,
        .right = x + 768U,
        .bottom = y + 768U,
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

void CheckPointerCuePosition(
    const std::optional<PointerPositionQ16>& pointer,
    const std::uint32_t expected_x, const std::uint32_t expected_y,
    const std::string_view message)
{
    const auto first_targets =
        PlanProjectDiagnosticTargetCueRectangles(pointer);
    const auto repeat_targets =
        PlanProjectDiagnosticTargetCueRectangles(pointer);
    const RenderTargetRectQ16 first_fire =
        PlanProjectDiagnosticFireCueRectangle(pointer);
    const RenderTargetRectQ16 repeat_fire =
        PlanProjectDiagnosticFireCueRectangle(pointer);
    const auto expected_targets = ExpectedTargetCues(expected_x, expected_y);
    const RenderTargetRectQ16 expected_fire =
        ExpectedFireCue(expected_x, expected_y);

    Check(first_targets == expected_targets &&
              repeat_targets == expected_targets &&
              first_fire == expected_fire && repeat_fire == expected_fire,
        message);
    Check(first_targets[0U].right - first_targets[0U].left == 8'192U &&
              first_targets[0U].bottom - first_targets[0U].top == 512U &&
              first_targets[1U].right - first_targets[1U].left == 512U &&
              first_targets[1U].bottom - first_targets[1U].top == 8'192U,
        "target cue order remains horizontal then vertical with exact nonzero geometry");
    Check(first_fire.right - first_fire.left == 1'536U &&
              first_fire.bottom - first_fire.top == 1'536U,
        "the fire cue remains one exact nonzero square");
    Check(IsValidDestination(first_targets[0U]) &&
              IsValidDestination(first_targets[1U]) &&
              IsValidDestination(first_fire),
        "every absolute pointer cue remains a valid in-bounds target rectangle");
    Check(IsAcceptedByRenderDrawList(first_targets[0U]) &&
              IsAcceptedByRenderDrawList(first_targets[1U]) &&
              IsAcceptedByRenderDrawList(first_fire),
        "every absolute pointer cue is accepted by the renderer-neutral draw list");
}

void CheckPosition(const Position3 position, const std::string_view message)
{
    const RenderTargetRectQ16 first =
        PlanProjectDiagnosticActorMarkerDestination(position);
    const RenderTargetRectQ16 repeat =
        PlanProjectDiagnosticActorMarkerDestination(position);
    const RenderTargetRectQ16 expected = ExpectedDestination(position);
    const Matrix4x4IR first_transform =
        PlanProjectDiagnosticActorMeshTransform(position);
    const Matrix4x4IR repeat_transform =
        PlanProjectDiagnosticActorMeshTransform(position);
    const Matrix4x4IR expected_transform = ExpectedMeshTransform(position);
    Check(first == expected && repeat == expected, message);
    Check(first_transform == expected_transform && repeat_transform == expected_transform,
        "every generated position produces the exact deterministic actor mesh transform");
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
    static_assert(std::is_same_v<decltype(
                      PlanProjectDiagnosticActorMeshTransform(Position3{})),
        Matrix4x4IR>);
    static_assert(noexcept(PlanProjectDiagnosticActorMeshTransform(Position3{})));

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

    constexpr Matrix4x4IR origin_transform =
        PlanProjectDiagnosticActorMeshTransform(Position3{});
    constexpr Matrix4x4IR positive_transform =
        PlanProjectDiagnosticActorMeshTransform(Position3{.x = 1, .z = 1});
    constexpr Matrix4x4IR saturated_transform =
        PlanProjectDiagnosticActorMeshTransform(Position3{
            .x = std::numeric_limits<std::int64_t>::max(),
            .y = std::numeric_limits<std::int64_t>::min(),
            .z = std::numeric_limits<std::int64_t>::min(),
        });
    static_assert(origin_transform == omega::asset::kIdentityMatrix4x4IR);
    static_assert(positive_transform.row_major[3U] == 1.0F / 32.0F &&
                  positive_transform.row_major[7U] == 1.0F / 32.0F);
    static_assert(saturated_transform.row_major[3U] == 31.0F / 32.0F &&
                  saturated_transform.row_major[7U] == -31.0F / 32.0F);
}

void CheckPointerCueContract()
{
    using TargetResult = std::array<RenderTargetRectQ16, 2U>;
    using FireResult = RenderTargetRectQ16;
    static_assert(std::is_same_v<decltype(
                      PlanProjectDiagnosticTargetCueRectangles(
                          std::optional<PointerPositionQ16>{})),
        TargetResult>);
    static_assert(std::is_same_v<decltype(
                      PlanProjectDiagnosticFireCueRectangle(
                          std::optional<PointerPositionQ16>{})),
        FireResult>);
    static_assert(noexcept(PlanProjectDiagnosticTargetCueRectangles(
        std::optional<PointerPositionQ16>{})));
    static_assert(noexcept(PlanProjectDiagnosticFireCueRectangle(
        std::optional<PointerPositionQ16>{})));

    constexpr std::optional<PointerPositionQ16> unavailable;
    constexpr std::optional<PointerPositionQ16> centered{
        PointerPositionQ16{.x = 32'768U, .y = 32'768U}};
    constexpr TargetResult fallback_targets =
        PlanProjectDiagnosticTargetCueRectangles(unavailable);
    constexpr TargetResult centered_targets =
        PlanProjectDiagnosticTargetCueRectangles(centered);
    constexpr FireResult fallback_fire =
        PlanProjectDiagnosticFireCueRectangle(unavailable);
    constexpr FireResult centered_fire =
        PlanProjectDiagnosticFireCueRectangle(centered);
    static_assert(fallback_targets == TargetResult{
        RenderTargetRectQ16{28'672U, 32'512U, 36'864U, 33'024U},
        RenderTargetRectQ16{32'512U, 28'672U, 33'024U, 36'864U},
    });
    static_assert(centered_targets == fallback_targets);
    static_assert(fallback_fire ==
                  RenderTargetRectQ16{32'000U, 32'000U, 33'536U, 33'536U});
    static_assert(centered_fire == fallback_fire);

    constexpr std::optional<PointerPositionQ16> low{
        PointerPositionQ16{.x = 0U, .y = 0U}};
    constexpr std::optional<PointerPositionQ16> high{
        PointerPositionQ16{.x = 65'536U, .y = 65'536U}};
    static_assert(PlanProjectDiagnosticTargetCueRectangles(low) == TargetResult{
        RenderTargetRectQ16{0U, 3'840U, 8'192U, 4'352U},
        RenderTargetRectQ16{3'840U, 0U, 4'352U, 8'192U},
    });
    static_assert(PlanProjectDiagnosticTargetCueRectangles(high) == TargetResult{
        RenderTargetRectQ16{57'344U, 61'184U, 65'536U, 61'696U},
        RenderTargetRectQ16{61'184U, 57'344U, 61'696U, 65'536U},
    });
    static_assert(PlanProjectDiagnosticFireCueRectangle(low) ==
                  RenderTargetRectQ16{3'328U, 3'328U, 4'864U, 4'864U});
    static_assert(PlanProjectDiagnosticFireCueRectangle(high) ==
                  RenderTargetRectQ16{60'672U, 60'672U, 62'208U, 62'208U});
}

void CheckPointerCueBoundarySet()
{
    CheckPointerCuePosition(std::nullopt, 32'768U, 32'768U,
        "an unavailable absolute pointer falls back exactly to target center");

    constexpr std::array<std::uint32_t, 10U> coordinates{
        0U,
        4'095U,
        4'096U,
        4'097U,
        32'768U,
        61'439U,
        61'440U,
        61'441U,
        65'536U,
        std::numeric_limits<std::uint32_t>::max(),
    };
    for (const std::uint32_t x : coordinates)
    {
        for (const std::uint32_t y : coordinates)
        {
            CheckPointerCuePosition(
                PointerPositionQ16{.x = x, .y = y},
                ClampCueAxis(x), ClampCueAxis(y),
                "every boundary and corner pointer sample follows the exact clamp policy");
        }
    }
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
    CheckPointerCueContract();
    CheckPointerCueBoundarySet();
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
