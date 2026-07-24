#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace omega::gameplay
{
// Project-owned, declarative mission data. These are NOT a recovered retail
// mission-state representation or the retail script VM; they are a native
// re-expression of the objective identifiers extracted from the level's .SO
// script modules (obj1..obj13 with objN_menu / objN_map keys + voice cues; see
// analysis of OBJECTIVES.SO). The native gameplay drives these deterministically.

inline constexpr std::size_t kMaxMissionObjectives = 32U;

enum class ObjectiveKind : std::uint8_t
{
    Primary,   // required for mission success
    Optional,
};

// One objective definition. String views reference static mission data that
// outlives any state derived from it; the schema owns no allocations.
struct ObjectiveDef
{
    std::uint16_t id = 0U;
    std::string_view menu_key;   // objN_menu  (HUD/objective-screen label key)
    std::string_view map_key;    // objN_map   (map-marker key)
    std::string_view voice_cue;  // e.g. "8_4_1"  (played on activation/completion; may be empty)
    ObjectiveKind kind = ObjectiveKind::Primary;

    [[nodiscard]] friend constexpr bool operator==(
        const ObjectiveDef&, const ObjectiveDef&) noexcept = default;
};

// A mission's declarative content. `objectives` references static storage.
struct MissionData
{
    std::string_view level_code;                 // e.g. "MINSK"
    std::span<const ObjectiveDef> objectives;    // ordered; index is the state slot
    std::uint16_t beacon_task_count = 0U;        // MIN_BEACONS (0 = no beacon task)
    std::uint16_t beacon_objective_id = 0U;      // objective completed when all beacons planted
    std::string_view beacon_voice_cue;           // played when the beacon task completes
};
} // namespace omega::gameplay
