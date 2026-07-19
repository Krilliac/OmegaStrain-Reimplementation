#pragma once

#include "omega/simulation/simulation_world.h"

#include <cstdint>
#include <expected>

namespace omega::gameplay
{
// Project-owned digital movement intent for one fixed simulation step. Axis
// values are restricted to -1, 0, or 1; this is diagnostic policy rather than
// a retail control or movement contract.
struct DigitalMoveCommand
{
    std::int8_t lateral = 0;
    std::int8_t longitudinal = 0;

    [[nodiscard]] friend constexpr bool operator==(
        const DigitalMoveCommand&, const DigitalMoveCommand&) noexcept = default;
};

enum class DebugLocomotionPlanError : std::uint8_t
{
    InvalidCommand,
};

// [any thread; reentrant] Purely maps one validated digital command to a
// project-owned translation of exactly one synthetic unit per nonzero axis.
// It performs no allocation and retains no state or references.
[[nodiscard]] std::expected<simulation::Translation3, DebugLocomotionPlanError>
PlanDebugLocomotionStep(DigitalMoveCommand command) noexcept;
} // namespace omega::gameplay
