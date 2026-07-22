#pragma once

#include "omega/simulation/simulation_world.h"

#include <cstdint>
#include <expected>

namespace omega::gameplay
{
// Project-owned synthetic X/Z volume used only by the native diagnostic
// gameplay shell. It does not describe a recovered retail trigger format,
// coordinate scale, or mission-objective contract.
struct DiagnosticProximityVolumeXZ
{
    std::int64_t min_x = 0;
    std::int64_t max_x = 0;
    std::int64_t min_z = 0;
    std::int64_t max_z = 0;
};

struct DiagnosticProximityTriggerState
{
    bool inside = false;
    bool objective_complete = false;
};

enum class DiagnosticProximityTransition : std::uint8_t
{
    Outside,
    Entered,
    Inside,
    Exited,
};

struct DiagnosticProximityTriggerStep
{
    DiagnosticProximityTriggerState state{};
    DiagnosticProximityTransition transition =
        DiagnosticProximityTransition::Outside;
    bool activated_now = false;
};

enum class DiagnosticProximityTriggerError : std::uint8_t
{
    InvalidVolume,
};

inline constexpr DiagnosticProximityVolumeXZ kProjectDiagnosticObjectiveVolume{
    .min_x = 3,
    .max_x = 5,
    .min_z = -1,
    .max_z = 1,
};

// [any thread; reentrant] Advances one project-owned diagnostic proximity
// trigger from owned values. Bounds are inclusive, Y is ignored, and invalid
// volumes are rejected before a successor is formed. This function allocates
// nothing and retains no state or references.
[[nodiscard]] std::expected<DiagnosticProximityTriggerStep,
    DiagnosticProximityTriggerError>
AdvanceDiagnosticProximityTrigger(DiagnosticProximityVolumeXZ volume,
    DiagnosticProximityTriggerState prior_state,
    simulation::Position3 position) noexcept;
} // namespace omega::gameplay
