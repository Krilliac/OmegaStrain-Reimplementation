#include "omega/gameplay/minsk_mission.h"

#include <array>

namespace omega::gameplay
{
namespace
{
// Real objective identifiers extracted from MINSK OBJECTIVES.SO (obj9 absent).
// menu_key = objN_menu, map_key = objN_map. voice_cue intentionally empty (see
// header: the 8_x_1 cues exist but their objective binding is unproven).
constexpr std::array<ObjectiveDef, 12U> kMinskObjectives{{
    {1U, "obj1_menu", "obj1_map", "", ObjectiveKind::Primary},
    {2U, "obj2_menu", "obj2_map", "", ObjectiveKind::Primary},
    {3U, "obj3_menu", "obj3_map", "", ObjectiveKind::Primary},
    {4U, "obj4_menu", "obj4_map", "", ObjectiveKind::Primary},
    {5U, "obj5_menu", "obj5_map", "", ObjectiveKind::Primary},
    {6U, "obj6_menu", "obj6_map", "", ObjectiveKind::Primary},
    {7U, "obj7_menu", "obj7_map", "", ObjectiveKind::Primary},
    {8U, "obj8_menu", "obj8_map", "", ObjectiveKind::Primary},
    {10U, "obj10_menu", "obj10_map", "", ObjectiveKind::Primary},
    {11U, "obj11_menu", "obj11_map", "", ObjectiveKind::Primary},
    {12U, "obj12_menu", "obj12_map", "", ObjectiveKind::Primary},
    {13U, "obj13_menu", "obj13_map", "", ObjectiveKind::Primary},
}};

constexpr MissionData kMinskMission{
    .level_code = "MINSK",
    .objectives = kMinskObjectives,
    .beacon_task_count = 0U,
    .beacon_objective_id = 0U,
    .beacon_voice_cue = "",
};
} // namespace

const MissionData& MinskMissionData() noexcept
{
    return kMinskMission;
}
} // namespace omega::gameplay
