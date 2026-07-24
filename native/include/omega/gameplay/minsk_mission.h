#pragma once

#include "omega/gameplay/mission_data.h"

namespace omega::gameplay
{
// [any thread; reentrant] The declarative MissionData for the MINSK level,
// re-expressed from the objective identifiers extracted from MINSK's
// OBJECTIVES.SO / PRAGUE.SO script modules. Returns a view over static storage
// that lives for the program's duration.
//
// Provenance / honesty: the objN_menu / objN_map keys are the real extracted
// identifiers (obj1..obj8, obj10..obj13 -- obj9 is absent in the script data).
// The objectives are all marked Primary because the extracted data does not
// carry a proven primary/optional flag; classifying secondaries needs more RE.
// Per-objective voice cues (the 8_x_1 tokens) exist in OBJECTIVES.SO but their
// objective binding is ambiguous in the extracted table, so they are left
// empty rather than guessed. The plant-the-beacons task (BeaconSetUp/
// BeaconPlanted/MIN_BEACONS) is identified but not modelled here because the
// MIN_BEACONS count is not yet recovered.
[[nodiscard]] const MissionData& MinskMissionData() noexcept;
} // namespace omega::gameplay
