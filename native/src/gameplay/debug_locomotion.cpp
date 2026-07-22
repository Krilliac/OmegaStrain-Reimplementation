#include "omega/gameplay/debug_locomotion.h"
#include "omega/debug/subsystem_entry_break.h"

#include <cstdint>

namespace omega::gameplay
{
std::expected<simulation::Translation3, DebugLocomotionPlanError>
PlanDebugLocomotionStep(const DigitalMoveCommand command) noexcept
{
    OMEGA_DEBUG_BREAK_SUBSYSTEM_ENTRY("omega_gameplay");
    constexpr std::int8_t kMinimumAxis = -1;
    constexpr std::int8_t kMaximumAxis = 1;
    if (command.lateral < kMinimumAxis || command.lateral > kMaximumAxis ||
        command.longitudinal < kMinimumAxis ||
        command.longitudinal > kMaximumAxis)
    {
        return std::unexpected(DebugLocomotionPlanError::InvalidCommand);
    }

    return simulation::Translation3{
        .dx = static_cast<std::int64_t>(command.lateral),
        .dy = 0,
        .dz = static_cast<std::int64_t>(command.longitudinal),
    };
}
} // namespace omega::gameplay
