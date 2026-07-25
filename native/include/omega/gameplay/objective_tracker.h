#pragma once

#include "omega/gameplay/mission_data.h"

#include <array>
#include <cstdint>
#include <expected>
#include <optional>
#include <string_view>

namespace omega::gameplay
{
// Per-objective runtime status. Project-owned; deterministic.
enum class ObjectiveStatus : std::uint8_t
{
    Inactive,   // defined but not yet given to the player
    Active,     // shown, awaiting completion
    Complete,
    Failed,
};

// Host-API OBJCHOICE, 1:1 with the retail objective calls extracted from the
// scripts: AddObjective->Add, Succeed->Pass, Fail->Fail, DestroyObjectives->
// Remove, IsObjectiveComplete->Check. Check is a pure query issued as an event
// only for symmetry; it never mutates state.
enum class ObjectiveChoice : std::uint8_t
{
    Add,
    Remove,
    Pass,
    Fail,
    Check,
};

struct ObjectiveEvent
{
    ObjectiveChoice choice = ObjectiveChoice::Check;
    std::uint16_t id = 0U;

    [[nodiscard]] friend constexpr bool operator==(
        const ObjectiveEvent&, const ObjectiveEvent&) noexcept = default;
};

// Snapshot-comparable state: one status per mission objective slot (by the
// objective's index in MissionData::objectives). Fixed capacity, no heap.
struct ObjectiveState
{
    std::array<ObjectiveStatus, kMaxMissionObjectives> status{};
    std::uint8_t count = 0U;   // number of objective slots in use

    [[nodiscard]] friend constexpr bool operator==(
        const ObjectiveState&, const ObjectiveState&) noexcept = default;
};

// The effects a successful transition produces (the mission driver / HUD /
// audio consume these). String views borrow the MissionData definition.
struct ObjectiveStep
{
    ObjectiveState state{};
    std::string_view hud_add;      // objN_menu to show, or empty
    std::string_view hud_remove;   // objN_menu to hide, or empty
    std::string_view map_add;      // objN_map to add, or empty
    std::string_view map_remove;   // objN_map to remove, or empty
    std::string_view voice_cue;    // voice cue to play, or empty

    [[nodiscard]] friend constexpr bool operator==(
        const ObjectiveStep&, const ObjectiveStep&) noexcept = default;
};

enum class ObjectiveError : std::uint8_t
{
    UnknownObjective,     // id not in the mission
    InvalidTransition,    // e.g. Pass on an Inactive objective
    StateMissionMismatch, // state.count does not match the mission
};

// [any thread; reentrant] The initial state for a mission: every objective
// Inactive. Allocates nothing.
[[nodiscard]] ObjectiveState InitialObjectiveState(const MissionData& mission) noexcept;

// [any thread; reentrant] Index of `id` in the mission's objective list.
[[nodiscard]] std::optional<std::size_t> FindObjectiveIndex(
    const MissionData& mission, std::uint16_t id) noexcept;

// [any thread; reentrant] Advances one objective transition from owned values.
// Add: Inactive->Active (emits hud_add/map_add + voice). Pass: Active->Complete
// (emits hud_remove/map_remove + the objective's completion voice). Fail:
// Active->Failed (emits hud_remove/map_remove). Remove: any->Inactive (emits
// hud_remove/map_remove). Check: no mutation, no effects (query symmetry).
// Re-issuing a choice that leaves status unchanged (Add on Active, Pass on
// Complete) is an idempotent no-effect success. Unknown id, a
// state/mission size mismatch, or an invalid transition (e.g. Pass/Fail on an
// Inactive objective) is rejected before a successor is formed. Allocates
// nothing; retains no state or references.
[[nodiscard]] std::expected<ObjectiveStep, ObjectiveError> AdvanceObjectives(
    const MissionData& mission, ObjectiveState prior,
    ObjectiveEvent event) noexcept;

// [any thread; reentrant] Pure queries over the state.
[[nodiscard]] bool IsObjectiveComplete(const MissionData& mission,
    const ObjectiveState& state, std::uint16_t id) noexcept;

// True when every PRIMARY objective that is not Inactive is Complete, and at
// least one primary objective has been completed (mirrors AreObjectivesComplete
// only counting the required objectives; an all-Inactive mission is not yet
// complete).
[[nodiscard]] bool AreObjectivesComplete(
    const MissionData& mission, const ObjectiveState& state) noexcept;

// Mission-level outcome derived from the per-objective statuses. Project-owned
// and deterministic: this is NOT a recovered retail end-of-mission result. The
// retail summary bookkeeping (rank/score, agent experience, unlock flags, the
// debrief screen) is not modelled -- only the win/lose/undecided decision is.
enum class MissionOutcome : std::uint8_t
{
    InProgress,   // no decision yet
    Succeeded,    // every primary objective Complete
    Failed,       // at least one primary objective Failed
};

// [any thread; reentrant] Derives the mission outcome from the objective set.
// Only ObjectiveKind::Primary objectives decide a mission: a secondary/optional
// objective in ANY status -- including Failed -- neither blocks success nor
// fails the mission. Failure dominates: one Failed primary yields Failed even
// when every other primary is Complete. Succeeded requires at least one primary
// objective and every primary Complete; a primary still Inactive or Active
// leaves the mission InProgress. A mission with no primary objectives at all --
// including an empty objective set, e.g. a level whose objectives failed to
// load -- is InProgress, never a vacuous Succeeded. Fail-soft: a state whose
// count does not describe this mission decides nothing and reads InProgress
// (the same mismatch rejection AreObjectivesComplete applies).
// Not modelled here: player death, lives/respawn, mission timers, alarm state,
// abort/extraction, objective scoring, and any HUD/audio reaction -- this is
// the pure decision only. Allocates nothing; retains no state or references.
[[nodiscard]] MissionOutcome EvaluateMissionOutcome(
    const MissionData& mission, const ObjectiveState& state) noexcept;
} // namespace omega::gameplay
