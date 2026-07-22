#include "omega/gameplay/diagnostic_target_fire.h"

namespace omega::gameplay
{
std::expected<DiagnosticTargetFireStep, DiagnosticTargetFireError>
AdvanceDiagnosticTargetFire(const DiagnosticAimTargetQ16 target,
    const DiagnosticTargetFireState prior_state,
    const DiagnosticTargetFireInput input) noexcept
{
    if (target.left > target.right || target.right > kDiagnosticAimExtent ||
        target.top > target.bottom || target.bottom > kDiagnosticAimExtent)
    {
        return std::unexpected(DiagnosticTargetFireError::InvalidTarget);
    }

    if (input.pointer &&
        (input.pointer->x > kDiagnosticAimExtent ||
            input.pointer->y > kDiagnosticAimExtent))
    {
        return std::unexpected(DiagnosticTargetFireError::PointerOutOfRange);
    }

    if (!input.enabled || prior_state.target_complete)
    {
        return DiagnosticTargetFireStep{
            .state =
                DiagnosticTargetFireState{
                    .acquired = false,
                    .target_complete = prior_state.target_complete,
                },
            .attempted_now = false,
            .hit_now = false,
        };
    }

    const bool acquired = input.target_held && input.pointer &&
                          input.pointer->x >= target.left &&
                          input.pointer->x <= target.right &&
                          input.pointer->y >= target.top &&
                          input.pointer->y <= target.bottom;
    const bool attempted_now = input.fire_pressed;
    const bool hit_now = attempted_now && acquired;

    return DiagnosticTargetFireStep{
        .state =
            DiagnosticTargetFireState{
                .acquired = acquired && !hit_now,
                .target_complete = hit_now,
            },
        .attempted_now = attempted_now,
        .hit_now = hit_now,
    };
}
} // namespace omega::gameplay
