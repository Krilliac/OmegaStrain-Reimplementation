#include "omega/gameplay/minsk_mission.h"
#include "omega/gameplay/mission_data.h"
#include "omega/gameplay/objective_tracker.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <iostream>
#include <string_view>

namespace
{
using omega::gameplay::AdvanceObjectives;
using omega::gameplay::AreObjectivesComplete;
using omega::gameplay::FindObjectiveIndex;
using omega::gameplay::InitialObjectiveState;
using omega::gameplay::IsObjectiveComplete;
using omega::gameplay::MinskMissionData;
using omega::gameplay::MissionData;
using omega::gameplay::ObjectiveChoice;
using omega::gameplay::ObjectiveDef;
using omega::gameplay::ObjectiveError;
using omega::gameplay::ObjectiveEvent;
using omega::gameplay::ObjectiveKind;
using omega::gameplay::ObjectiveState;
using omega::gameplay::ObjectiveStatus;

int failures = 0;

void Check(const bool condition, const std::string_view message)
{
    if (condition)
        return;
    ++failures;
    std::cerr << "FAILED: " << message << '\n';
}

// A small synthetic mission: two primaries + one optional.
constexpr std::array<ObjectiveDef, 3U> kSyntheticObjectives{{
    {1U, "one_menu", "one_map", "v1", ObjectiveKind::Primary},
    {2U, "two_menu", "two_map", "", ObjectiveKind::Primary},
    {9U, "opt_menu", "opt_map", "", ObjectiveKind::Optional},
}};
constexpr MissionData kSynthetic{
    .level_code = "TEST",
    .objectives = kSyntheticObjectives,
};

[[nodiscard]] ObjectiveState Apply(const MissionData& mission, ObjectiveState state,
    const ObjectiveChoice choice, const std::uint16_t id)
{
    const auto step = AdvanceObjectives(mission, state, {choice, id});
    Check(step.has_value(), "expected a successful transition");
    return step ? step->state : state;
}
} // namespace

int main()
{
    const MissionData& mission = kSynthetic;

    // Initial state: all inactive, count matches.
    ObjectiveState state = InitialObjectiveState(mission);
    Check(state.count == 3U, "initial state has one slot per objective");
    Check(state.status[0] == ObjectiveStatus::Inactive &&
              state.status[1] == ObjectiveStatus::Inactive &&
              state.status[2] == ObjectiveStatus::Inactive,
        "initial objectives are inactive");
    Check(!AreObjectivesComplete(mission, state),
        "an all-inactive mission is not complete");

    // FindObjectiveIndex.
    Check(FindObjectiveIndex(mission, 2U).value_or(99U) == 1U,
        "find returns the objective's slot index");
    Check(!FindObjectiveIndex(mission, 42U).has_value(),
        "find fails for an unknown id");

    // Unknown id is rejected.
    Check(AdvanceObjectives(mission, state, {ObjectiveChoice::Add, 42U}).error() ==
              ObjectiveError::UnknownObjective,
        "add of an unknown objective is rejected");

    // Pass/Fail on an inactive objective is an invalid transition.
    Check(AdvanceObjectives(mission, state, {ObjectiveChoice::Pass, 1U}).error() ==
              ObjectiveError::InvalidTransition,
        "pass on an inactive objective is invalid");
    Check(AdvanceObjectives(mission, state, {ObjectiveChoice::Fail, 1U}).error() ==
              ObjectiveError::InvalidTransition,
        "fail on an inactive objective is invalid");

    // Add: Inactive -> Active, emits hud/map/voice from the def.
    {
        const auto step = AdvanceObjectives(mission, state, {ObjectiveChoice::Add, 1U});
        Check(step.has_value(), "add succeeds");
        Check(step->state.status[0] == ObjectiveStatus::Active, "add activates the objective");
        Check(step->hud_add == "one_menu" && step->map_add == "one_map" &&
                  step->voice_cue == "v1",
            "add emits the objective's hud/map/voice");
        Check(step->hud_remove.empty() && step->map_remove.empty(),
            "add emits no removals");
        state = step->state;
    }

    // Idempotent add on an active objective: no effects.
    {
        const auto step = AdvanceObjectives(mission, state, {ObjectiveChoice::Add, 1U});
        Check(step.has_value() && step->state == state &&
                  step->hud_add.empty() && step->voice_cue.empty(),
            "re-adding an active objective is an idempotent no-effect success");
    }

    // Check is a pure query: no mutation, no effects.
    {
        const auto step = AdvanceObjectives(mission, state, {ObjectiveChoice::Check, 1U});
        Check(step.has_value() && step->state == state && step->hud_add.empty() &&
                  step->hud_remove.empty() && step->voice_cue.empty(),
            "check does not mutate or emit");
    }

    // Pass: Active -> Complete, emits removals + completion voice.
    {
        const auto step = AdvanceObjectives(mission, state, {ObjectiveChoice::Pass, 1U});
        Check(step.has_value(), "pass succeeds");
        Check(step->state.status[0] == ObjectiveStatus::Complete, "pass completes the objective");
        Check(step->hud_remove == "one_menu" && step->map_remove == "one_map",
            "pass emits the objective's hud/map removal");
        state = step->state;
    }
    Check(IsObjectiveComplete(mission, state, 1U), "objective 1 reads complete");
    Check(!IsObjectiveComplete(mission, state, 2U), "objective 2 is not complete");

    // Idempotent pass on a complete objective.
    Check(AdvanceObjectives(mission, state, {ObjectiveChoice::Pass, 1U})->state == state,
        "re-passing a complete objective is idempotent");

    // Not complete while primary #2 is still inactive... activate + complete it.
    Check(!AreObjectivesComplete(mission, state),
        "mission incomplete while a primary is inactive-but-not-all-done"),
    state = Apply(mission, state, ObjectiveChoice::Add, 2U);
    Check(!AreObjectivesComplete(mission, state),
        "mission incomplete while primary #2 is active");
    state = Apply(mission, state, ObjectiveChoice::Pass, 2U);
    Check(AreObjectivesComplete(mission, state),
        "mission complete once every non-inactive primary is complete");

    // The optional objective's state does not affect primary completion.
    state = Apply(mission, state, ObjectiveChoice::Add, 9U);
    Check(AreObjectivesComplete(mission, state),
        "an active optional objective does not block completion");

    // Fail path: fail the optional; primaries still complete.
    {
        const auto step = AdvanceObjectives(mission, state, {ObjectiveChoice::Fail, 9U});
        Check(step.has_value() && step->state.status[2] == ObjectiveStatus::Failed,
            "fail marks the objective failed");
        Check(step->hud_remove == "opt_menu" && step->voice_cue.empty(),
            "fail emits hud/map removal and no voice");
        state = step->state;
    }
    Check(AreObjectivesComplete(mission, state),
        "a failed optional does not block primary completion");

    // A failed PRIMARY blocks completion.
    {
        ObjectiveState s = InitialObjectiveState(mission);
        s = Apply(mission, s, ObjectiveChoice::Add, 1U);
        s = Apply(mission, s, ObjectiveChoice::Pass, 1U);
        s = Apply(mission, s, ObjectiveChoice::Add, 2U);
        const auto step = AdvanceObjectives(mission, s, {ObjectiveChoice::Fail, 2U});
        Check(step.has_value(), "fail a primary succeeds");
        Check(!AreObjectivesComplete(mission, step->state),
            "a failed primary blocks mission completion");
    }

    // Remove: any -> Inactive, idempotent when already inactive.
    {
        ObjectiveState s = InitialObjectiveState(mission);
        s = Apply(mission, s, ObjectiveChoice::Add, 1U);
        const auto removed = AdvanceObjectives(mission, s, {ObjectiveChoice::Remove, 1U});
        Check(removed.has_value() && removed->state.status[0] == ObjectiveStatus::Inactive &&
                  removed->hud_remove == "one_menu",
            "remove deactivates and emits removal");
        Check(AdvanceObjectives(mission, removed->state, {ObjectiveChoice::Remove, 1U})->state ==
                  removed->state,
            "removing an inactive objective is idempotent");
    }

    // State/mission mismatch is rejected.
    {
        ObjectiveState mismatched = state;
        mismatched.count = 99U;
        Check(AdvanceObjectives(mission, mismatched, {ObjectiveChoice::Check, 1U}).error() ==
                  ObjectiveError::StateMissionMismatch,
            "a state/mission count mismatch is rejected");
    }

    // Determinism: the same sequence yields identical snapshots.
    {
        ObjectiveState a = InitialObjectiveState(mission);
        ObjectiveState b = InitialObjectiveState(mission);
        for (const std::uint16_t id : {std::uint16_t{1U}, std::uint16_t{2U}})
        {
            a = Apply(mission, a, ObjectiveChoice::Add, id);
            b = Apply(mission, b, ObjectiveChoice::Add, id);
        }
        Check(a == b, "identical event sequences produce identical state snapshots");
    }

    // The real MINSK mission loads the expected objective ids (obj9 absent).
    {
        const MissionData& minsk = MinskMissionData();
        Check(minsk.level_code == "MINSK", "minsk mission is MINSK");
        Check(minsk.objectives.size() == 12U, "minsk has 12 objectives (obj9 absent)");
        Check(FindObjectiveIndex(minsk, 1U).has_value() &&
                  FindObjectiveIndex(minsk, 13U).has_value() &&
                  !FindObjectiveIndex(minsk, 9U).has_value(),
            "minsk objective ids match the extracted set (1..8,10..13)");
        Check(minsk.objectives[0].menu_key == "obj1_menu" &&
                  minsk.objectives[0].map_key == "obj1_map",
            "minsk objective keys are the real extracted identifiers");
        ObjectiveState ms = InitialObjectiveState(minsk);
        Check(ms.count == 12U, "minsk initial state has 12 slots");
        Check(AdvanceObjectives(minsk, ms, {ObjectiveChoice::Add, 1U}).has_value(),
            "minsk objective 1 activates");
    }

    if (failures != 0)
    {
        std::cerr << failures << " objective tracker test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "objective tracker tests passed\n";
    return EXIT_SUCCESS;
}
