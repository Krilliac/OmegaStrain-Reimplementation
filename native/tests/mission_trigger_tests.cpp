#include "omega/asset/geometry_ir.h"
#include "omega/gameplay/mission_data.h"
#include "omega/gameplay/mission_trigger.h"
#include "omega/gameplay/objective_tracker.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <span>
#include <string_view>

namespace
{
using omega::asset::Float3IR;
using omega::gameplay::AdvanceObjectives;
using omega::gameplay::InitialObjectiveState;
using omega::gameplay::MissionData;
using omega::gameplay::MissionTrigger;
using omega::gameplay::ObjectiveChoice;
using omega::gameplay::ObjectiveDef;
using omega::gameplay::ObjectiveKind;
using omega::gameplay::ObjectiveState;
using omega::gameplay::ObjectiveStatus;
using omega::gameplay::StepMissionTriggers;

int failures = 0;

void Check(const bool condition, const std::string_view message)
{
    if (condition)
        return;
    ++failures;
    std::cerr << "FAILED: " << message << '\n';
}

constexpr std::array<ObjectiveDef, 2U> kObjectives{{
    {1U, "one_menu", "one_map", "", ObjectiveKind::Primary},
    {2U, "two_menu", "two_map", "", ObjectiveKind::Primary},
}};
constexpr MissionData kMission{.level_code = "TEST", .objectives = kObjectives};

// State with objective 2 Active (ready to Pass).
[[nodiscard]] ObjectiveState ActiveTwo()
{
    ObjectiveState state = InitialObjectiveState(kMission);
    const auto step = AdvanceObjectives(kMission, state, {ObjectiveChoice::Add, 2U});
    return step ? step->state : state;
}
} // namespace

int main()
{
    // Entering the trigger radius completes the linked objective and reports a
    // change; the trigger becomes fired (one-shot).
    {
        const ObjectiveState prior = ActiveTwo();
        std::array<MissionTrigger, 1U> triggers{{
            {2U, Float3IR{.x = 10.0F, .y = 0.0F, .z = 0.0F}, 5.0F,
                ObjectiveChoice::Pass, false},
        }};
        const auto result = StepMissionTriggers(
            kMission, prior, std::span<MissionTrigger>{triggers},
            Float3IR{.x = 12.0F, .y = 0.0F, .z = 0.0F});  // 2 units away, inside r=5
        Check(result.changed, "entering the trigger reports a state change");
        Check(result.state.status[1] == ObjectiveStatus::Complete,
            "the linked objective (index 1 / id 2) becomes Complete");
        Check(triggers[0].fired, "the trigger is marked fired (one-shot)");
    }

    // One-shot: a fired trigger does nothing on re-evaluation.
    {
        ObjectiveState prior = ActiveTwo();
        std::array<MissionTrigger, 1U> triggers{{
            {2U, Float3IR{}, 5.0F, ObjectiveChoice::Pass, true},  // already fired
        }};
        const auto result = StepMissionTriggers(kMission, prior,
            std::span<MissionTrigger>{triggers}, Float3IR{});  // right on it
        Check(!result.changed, "a fired trigger does not re-fire");
        Check(result.state == prior, "a fired trigger leaves the state unchanged");
    }

    // Outside the radius: nothing happens, the trigger stays un-fired.
    {
        const ObjectiveState prior = ActiveTwo();
        std::array<MissionTrigger, 1U> triggers{{
            {2U, Float3IR{}, 5.0F, ObjectiveChoice::Pass, false},
        }};
        const auto result = StepMissionTriggers(kMission, prior,
            std::span<MissionTrigger>{triggers},
            Float3IR{.x = 100.0F, .y = 0.0F, .z = 0.0F});
        Check(!result.changed, "a distant player fires nothing");
        Check(!triggers[0].fired, "a distant trigger stays un-fired");
    }

    // A non-finite player position / zero radius never fires.
    {
        const ObjectiveState prior = ActiveTwo();
        std::array<MissionTrigger, 2U> triggers{{
            {2U, Float3IR{}, 0.0F, ObjectiveChoice::Pass, false},  // zero radius
            {2U, Float3IR{}, 5.0F, ObjectiveChoice::Pass, false},
        }};
        const auto result = StepMissionTriggers(kMission, prior,
            std::span<MissionTrigger>{triggers},
            Float3IR{.x = std::numeric_limits<float>::quiet_NaN(),
                .y = 0.0F, .z = 0.0F});
        Check(!result.changed, "non-finite position / zero radius never fires");
        Check(!triggers[0].fired && !triggers[1].fired,
            "no trigger fires on non-finite input");
    }

    // A rejected transition (Pass on an Inactive objective) still fires the
    // trigger one-shot but leaves the state unchanged (fail-soft).
    {
        const ObjectiveState prior = InitialObjectiveState(kMission);  // obj1 Inactive
        std::array<MissionTrigger, 1U> triggers{{
            {1U, Float3IR{}, 5.0F, ObjectiveChoice::Pass, false},
        }};
        const auto result = StepMissionTriggers(kMission, prior,
            std::span<MissionTrigger>{triggers}, Float3IR{});
        Check(!result.changed, "a rejected transition reports no change");
        Check(result.state == prior, "a rejected transition leaves state unchanged");
        Check(triggers[0].fired, "a rejected trigger still fires one-shot");
    }

    if (failures != 0)
    {
        std::cerr << failures << " mission-trigger test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "mission-trigger tests passed\n";
    return EXIT_SUCCESS;
}
