#include "omega/gameplay/diagnostic_proximity_trigger.h"

namespace omega::gameplay
{
std::expected<DiagnosticProximityTriggerStep, DiagnosticProximityTriggerError>
AdvanceDiagnosticProximityTrigger(const DiagnosticProximityVolumeXZ volume,
    const DiagnosticProximityTriggerState prior_state,
    const simulation::Position3 position) noexcept
{
    if (volume.min_x > volume.max_x || volume.min_z > volume.max_z)
    {
        return std::unexpected(DiagnosticProximityTriggerError::InvalidVolume);
    }

    const bool inside = position.x >= volume.min_x &&
                        position.x <= volume.max_x &&
                        position.z >= volume.min_z &&
                        position.z <= volume.max_z;

    DiagnosticProximityTransition transition =
        DiagnosticProximityTransition::Outside;
    if (prior_state.inside)
    {
        transition = inside ? DiagnosticProximityTransition::Inside
                            : DiagnosticProximityTransition::Exited;
    }
    else if (inside)
    {
        transition = DiagnosticProximityTransition::Entered;
    }

    return DiagnosticProximityTriggerStep{
        .state =
            DiagnosticProximityTriggerState{
                .inside = inside,
                .objective_complete =
                    prior_state.objective_complete || inside,
            },
        .transition = transition,
        .activated_now = !prior_state.objective_complete && inside,
    };
}
} // namespace omega::gameplay
