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
using omega::gameplay::IsObjectiveComplete;
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

    // A walkable mini-mission: three trigger volumes over obj2/obj3/obj4 (all
    // Active). The player visits each in sequence -> each objective completes
    // one-shot, and by the end all three are complete (multi-objective HUD
    // progression). Re-entering a fired volume does nothing.
    {
        static constexpr std::array<ObjectiveDef, 4U> seq_objectives{{
            {1U, "o1m", "o1p", "", ObjectiveKind::Primary},
            {2U, "o2m", "o2p", "", ObjectiveKind::Primary},
            {3U, "o3m", "o3p", "", ObjectiveKind::Primary},
            {4U, "o4m", "o4p", "", ObjectiveKind::Primary},
        }};
        static constexpr MissionData seq_mission{
            .level_code = "SEQ", .objectives = seq_objectives};
        ObjectiveState state = InitialObjectiveState(seq_mission);
        for (const std::uint16_t id : {std::uint16_t{2U}, std::uint16_t{3U},
                 std::uint16_t{4U}})
        {
            const auto added =
                AdvanceObjectives(seq_mission, state, {ObjectiveChoice::Add, id});
            if (added)
                state = added->state;
        }
        std::array<MissionTrigger, 3U> triggers{{
            {2U, Float3IR{.x = 0.0F, .y = 0.0F, .z = 0.0F}, 5.0F,
                ObjectiveChoice::Pass, false},
            {3U, Float3IR{.x = 50.0F, .y = 0.0F, .z = 0.0F}, 5.0F,
                ObjectiveChoice::Pass, false},
            {4U, Float3IR{.x = 100.0F, .y = 0.0F, .z = 0.0F}, 5.0F,
                ObjectiveChoice::Pass, false},
        }};
        auto visit = [&](const float x) {
            const auto result = StepMissionTriggers(seq_mission, state,
                std::span<MissionTrigger>{triggers},
                Float3IR{.x = x, .y = 0.0F, .z = 0.0F});
            state = result.state;
            return result.changed;
        };
        Check(visit(0.0F) && IsObjectiveComplete(seq_mission, state, 2U),
            "visiting the first volume completes obj2");
        Check(triggers[0].fired && !triggers[1].fired && !triggers[2].fired,
            "only the obj2 trigger fires at the first volume");
        Check(!visit(0.0F), "the obj2 trigger is one-shot on re-entry");
        Check(visit(50.0F) && IsObjectiveComplete(seq_mission, state, 3U),
            "visiting the second volume completes obj3");
        Check(visit(100.0F) && IsObjectiveComplete(seq_mission, state, 4U),
            "visiting the third volume completes obj4");
        Check(IsObjectiveComplete(seq_mission, state, 2U) &&
                IsObjectiveComplete(seq_mission, state, 3U) &&
                IsObjectiveComplete(seq_mission, state, 4U),
            "all three objectives complete after visiting all triggers");
    }

    if (failures != 0)
    {
        std::cerr << failures << " mission-trigger test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "mission-trigger tests passed\n";
    return EXIT_SUCCESS;
}
