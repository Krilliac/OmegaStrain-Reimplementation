#pragma once

#include "omega/profiles/character_catalog.h"
#include "omega/profiles/profile_catalog.h"
#include "omega/runtime/content_startup.h"
#include "omega/runtime/texture_storage_topology_debug_image.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>

namespace omega::app
{
// Project-owned logical actions used by the synthetic native front-end. They
// are not retail input identifiers or control-layout claims. Actions 2 and 3
// remain shared with diagnostic locomotion; action 6 is primary/confirm and
// action 7 is the distinct cancel edge.
inline constexpr std::uint32_t kFrontEndPrimaryAction = 6U;
inline constexpr std::uint32_t kFrontEndPreviousAction = 2U;
inline constexpr std::uint32_t kFrontEndNextAction = 3U;
inline constexpr std::uint32_t kFrontEndCancelAction = 7U;

inline constexpr std::size_t kFrontEndMaximumProfiles = 1'024U;
inline constexpr std::size_t kFrontEndVisibleProfiles = 3U;
inline constexpr std::size_t kFrontEndMaximumCharacters = 1'024U;
inline constexpr std::size_t kFrontEndVisibleCharacters = 3U;
inline constexpr std::size_t kFrontEndLabelCells = 24U;
inline constexpr std::size_t kFrontEndMainRowCount = 4U;
inline constexpr std::uint32_t kFrontEndImageWidth = 128U;
inline constexpr std::uint32_t kFrontEndImageHeight = 72U;
// Project-owned first-profile metadata. It is intentionally independent of
// retail save naming and is the only display name published by the bounded
// first-profile command.
inline constexpr std::string_view kFrontEndFirstProfileDisplayName = "PROFILE 1";
// Project-owned bootstrap character. It supplies a deterministic native path
// through creation and selection without asserting a retail archetype, model,
// loadout, appearance field, or save-slot meaning.
inline constexpr std::string_view kFrontEndFirstCharacterDisplayName = "DIAGNOSTIC CHARACTER";

enum class FrontEndMode : std::uint8_t
{
    Main = 0U,
    Profiles = 1U,
    DiagnosticPlay = 2U,
    Controls = 3U,
    AssetTopology = 4U,
    Characters = 5U,
};

enum class FrontEndMainRow : std::uint8_t
{
    StartDiagnostic = 0U,
    Profiles = 1U,
    Controls = 2U,
    AssetTopology = 3U,
};

// Startup-model positions only. These are project-owned presentation slots,
// not retail save slots, memory-card positions, or persistent identifiers.
enum class FrontEndProfileSlot : std::uint8_t
{
    First = 0U,
    Second = 1U,
    Third = 2U,
};

// Project-owned presentation positions, not retail character slots.
enum class FrontEndCharacterSlot : std::uint8_t
{
    First = 0U,
    Second = 1U,
    Third = 2U,
};

// Fixed project-font cells. Unused cells remain NUL-filled; length is
// authoritative and never exceeds the array extent. Projection uppercases ASCII
// a-z, preserves supported project-font ASCII, maps each other Unicode scalar
// to one '?', and never truncates within a scalar.
struct FrontEndLabel
{
    std::array<char, kFrontEndLabelCells> cells{};
    std::uint8_t length = 0U;
    bool truncated = false;

    friend constexpr bool operator==(const FrontEndLabel &, const FrontEndLabel &) noexcept = default;
};

struct FrontEndProfile
{
    // Present for every visible profile produced by MakeFrontEndStartupModel.
    // Optional keeps the zero model inert and default-constructible without
    // inventing a default ProfileId.
    std::optional<profiles::ProfileId> id;
    FrontEndLabel label{};

    friend constexpr bool operator==(const FrontEndProfile &, const FrontEndProfile &) noexcept = default;
};

// Owned bounded model initialized from the ID-sorted native catalog before SDL
// startup. The app may replace the exact zero model with its one-profile model
// after the durable first-profile create. It contains no borrowed string,
// catalog, database, or renderer lifetime and defines no implicit active
// profile.
struct FrontEndStartupModel
{
    std::uint16_t total_profiles = 0U;
    std::uint8_t visible_profiles = 0U;
    std::array<FrontEndProfile, kFrontEndVisibleProfiles> profiles{};

    friend constexpr bool operator==(const FrontEndStartupModel &, const FrontEndStartupModel &) noexcept = default;
};

struct FrontEndCharacter
{
    std::optional<profiles::CharacterId> id;
    FrontEndLabel label{};

    friend constexpr bool operator==(const FrontEndCharacter &, const FrontEndCharacter &) noexcept = default;
};

// Owned, profile-scoped character snapshot. The active ProfileId remains an
// application/persistence identity and is intentionally not duplicated here.
struct FrontEndCharacterStartupModel
{
    std::uint16_t total_characters = 0U;
    std::uint8_t visible_characters = 0U;
    std::array<FrontEndCharacter, kFrontEndVisibleCharacters> characters{};

    friend constexpr bool operator==(const FrontEndCharacterStartupModel &,
                                     const FrontEndCharacterStartupModel &) noexcept = default;
};

enum class FrontEndModelError : std::uint8_t
{
    TooManyProfiles = 0U,
    UnsortedProfiles = 1U,
    TooManyCharacters = 2U,
    UnsortedCharacters = 3U,
};

[[nodiscard]] constexpr std::string_view FrontEndModelErrorMessage(const FrontEndModelError error) noexcept
{
    switch (error)
    {
    case FrontEndModelError::TooManyProfiles:
        return "front-end profile snapshot exceeds the fixed profile limit";
    case FrontEndModelError::UnsortedProfiles:
        return "front-end profile snapshot is not strictly ID-sorted";
    case FrontEndModelError::TooManyCharacters:
        return "front-end character snapshot exceeds the fixed character limit";
    case FrontEndModelError::UnsortedCharacters:
        return "front-end character snapshot is not strictly ID-sorted";
    }
    return "front-end profile snapshot exceeds the fixed profile limit";
}

// [persistence/game thread, before SDL] Copies at most 1024 strictly ascending
// ProfileSummary records into a fixed front-end snapshot. It performs no
// allocation, I/O, catalog access, or mutation and never echoes input data
// through its fixed error taxonomy.
[[nodiscard]] std::expected<FrontEndStartupModel, FrontEndModelError> MakeFrontEndStartupModel(
    std::span<const profiles::ProfileSummary> summaries) noexcept;

// [persistence/game thread] Copies one already validated, ID-sorted character
// catalog result into a fixed app-layer snapshot. It performs no I/O or
// mutation.
[[nodiscard]] std::expected<FrontEndCharacterStartupModel, FrontEndModelError> MakeFrontEndCharacterStartupModel(
    std::span<const profiles::CharacterSummary> summaries) noexcept;

// [any thread; reentrant] Resolves the bounded startup-model position that
// currently holds an already-confirmed active profile. The durable confirmation
// remains the only authorization source; this helper never confirms, selects,
// or invents one. It fails closed for an absent identifier, for an identifier
// the model does not hold, for an unpopulated position, and for every position
// outside the current visible/total counts, so a stale identifier can never
// resolve to a position the model no longer presents. The result is a
// project-owned presentation position, not a retail save slot or persistent
// identifier. No allocation, I/O, catalog access, or persistence work occurs.
[[nodiscard]] constexpr std::optional<FrontEndProfileSlot> FrontEndConfirmedProfileSlot(
    const FrontEndStartupModel &profiles, const std::optional<profiles::ProfileId> &confirmed_profile_id) noexcept
{
    if (!confirmed_profile_id.has_value())
        return std::nullopt;

    std::size_t bounded_slots = profiles.profiles.size();
    if (static_cast<std::size_t>(profiles.visible_profiles) < bounded_slots)
        bounded_slots = static_cast<std::size_t>(profiles.visible_profiles);
    if (static_cast<std::size_t>(profiles.total_profiles) < bounded_slots)
        bounded_slots = static_cast<std::size_t>(profiles.total_profiles);

    for (std::size_t slot = 0U; slot < bounded_slots; ++slot)
    {
        const std::optional<profiles::ProfileId> &candidate = profiles.profiles[slot].id;
        if (candidate.has_value() && *candidate == *confirmed_profile_id)
            return static_cast<FrontEndProfileSlot>(slot);
    }
    return std::nullopt;
}

// [any thread; reentrant] True when an already-confirmed identifier still
// resolves against the caller-owned bounded model. This is the only supported
// live input to the explicit diagnostic-play gate below, so the gate can never
// be satisfied by a position the model does not currently present.
[[nodiscard]] constexpr bool FrontEndHasConfirmedActiveProfile(
    const FrontEndStartupModel &profiles, const std::optional<profiles::ProfileId> &confirmed_profile_id) noexcept
{
    return FrontEndConfirmedProfileSlot(profiles, confirmed_profile_id).has_value();
}

// [any thread; reentrant] Resolves an explicitly confirmed session character
// against the current profile-scoped bounded model. No persistence work occurs.
[[nodiscard]] constexpr std::optional<FrontEndCharacterSlot> FrontEndConfirmedCharacterSlot(
    const FrontEndCharacterStartupModel &characters,
    const std::optional<profiles::CharacterId> &confirmed_character_id) noexcept
{
    if (!confirmed_character_id.has_value())
        return std::nullopt;

    std::size_t bounded_slots = characters.characters.size();
    if (static_cast<std::size_t>(characters.visible_characters) < bounded_slots)
        bounded_slots = static_cast<std::size_t>(characters.visible_characters);
    if (static_cast<std::size_t>(characters.total_characters) < bounded_slots)
        bounded_slots = static_cast<std::size_t>(characters.total_characters);

    for (std::size_t slot = 0U; slot < bounded_slots; ++slot)
    {
        const auto &candidate = characters.characters[slot].id;
        if (candidate.has_value() && *candidate == *confirmed_character_id)
            return static_cast<FrontEndCharacterSlot>(slot);
    }
    return std::nullopt;
}

[[nodiscard]] constexpr bool FrontEndHasConfirmedActiveCharacter(
    const FrontEndCharacterStartupModel &characters,
    const std::optional<profiles::CharacterId> &confirmed_character_id) noexcept
{
    return FrontEndConfirmedCharacterSlot(characters, confirmed_character_id).has_value();
}

// Small owned app-layer value. It has no service, platform, renderer, database,
// or retail-data lifetime. The default remains the safe DiagnosticPlay state
// used by legacy nonmodal replay.
struct FrontEndState
{
    FrontEndMode mode = FrontEndMode::DiagnosticPlay;
    FrontEndMainRow selected_main_row = FrontEndMainRow::StartDiagnostic;
    FrontEndProfileSlot selected_profile_slot = FrontEndProfileSlot::First;
    FrontEndCharacterSlot selected_character_slot = FrontEndCharacterSlot::First;

    friend constexpr bool operator==(const FrontEndState &, const FrontEndState &) noexcept = default;
};

struct FrontEndInputEdges
{
    bool primary_pressed = false;
    bool previous_pressed = false;
    bool next_pressed = false;
    bool cancel_pressed = false;

    friend constexpr bool operator==(const FrontEndInputEdges &, const FrontEndInputEdges &) noexcept = default;
};

enum class FrontEndCommandType : std::uint8_t
{
    None = 0U,
    SetActiveProfile = 1U,
    CreateFirstProfile = 2U,
    StartDiagnosticCampaign = 3U,
    CreateFirstCharacter = 4U,
    SetActiveCharacter = 5U,
};

// Independent capability inputs default closed. Persistence explicitly
// advertises the commands that the current session can satisfy before the pure
// reducer may publish their projected states. Diagnostic play is available
// only when start support is open and its optional confirmation gate is
// satisfied.
struct FrontEndCapabilities
{
    bool can_create_first_profile = false;
    bool can_start_diagnostic_campaign = false;
    bool requires_active_profile_for_diagnostic_play = false;
    bool supports_character_selection = false;
    bool can_create_first_character = false;
    bool requires_active_character_for_diagnostic_play = false;

    friend constexpr bool operator==(const FrontEndCapabilities &, const FrontEndCapabilities &) noexcept = default;
};

// [any thread; reentrant] True when the explicit diagnostic-play confirmation
// gate is satisfied. This predicate does not advertise start support.
// Live callers must derive `active_profile_is_confirmed` with
// FrontEndHasConfirmedActiveProfile. The bounded replay adapter may instead
// supply its private identity-free mirror, which begins closed and opens only
// after a replayed SetActiveProfile command. This predicate deliberately holds
// no identity of its own and therefore cannot be satisfied by a second,
// separately mutable live selection. No allocation, I/O, or catalog access
// occurs.
[[nodiscard]] constexpr bool FrontEndSatisfiesDiagnosticPlayGate(
    const FrontEndCapabilities capabilities, const bool active_profile_is_confirmed,
    const bool active_character_is_confirmed = false) noexcept
{
    const bool profile_gate = !capabilities.requires_active_profile_for_diagnostic_play || active_profile_is_confirmed;
    const bool character_gate =
        !capabilities.requires_active_character_for_diagnostic_play || active_character_is_confirmed;
    return profile_gate && character_gate;
}

// [any thread; reentrant] Combines the two independent diagnostic inputs. A
// caller may explicitly enable a synthetic persistence-free start by opening
// support without requiring confirmation. No allocation, I/O, catalog access,
// or persistence mutation occurs.
[[nodiscard]] constexpr bool FrontEndAllowsDiagnosticPlay(const FrontEndCapabilities capabilities,
                                                          const bool active_profile_is_confirmed,
                                                          const bool active_character_is_confirmed = false) noexcept
{
    return capabilities.can_start_diagnostic_campaign &&
           FrontEndSatisfiesDiagnosticPlayGate(capabilities, active_profile_is_confirmed,
                                               active_character_is_confirmed);
}

// Fully owned reducer publication. SetActiveProfile carries only one of the
// three bounded startup-model positions. CreateFirstProfile and
// StartDiagnosticCampaign carry no caller data; the latter is a project-owned
// diagnostic start request, not a retail campaign, save-slot, or gameplay
// semantic claim. No command carries a catalog view or performs persistence
// work.
struct FrontEndCommand
{
    FrontEndCommandType type = FrontEndCommandType::None;
    FrontEndProfileSlot profile_slot = FrontEndProfileSlot::First;
    FrontEndCharacterSlot character_slot = FrontEndCharacterSlot::First;

    friend constexpr bool operator==(const FrontEndCommand &, const FrontEndCommand &) noexcept = default;
};

struct FrontEndReduction
{
    FrontEndState state{};
    FrontEndCommand command{};

    friend constexpr bool operator==(const FrontEndReduction &, const FrontEndReduction &) noexcept = default;
};

struct FrontEndView
{
    FrontEndMode mode = FrontEndMode::Main;
    FrontEndMainRow selected_main_row = FrontEndMainRow::StartDiagnostic;
    FrontEndProfileSlot selected_profile_slot = FrontEndProfileSlot::First;
    FrontEndCharacterSlot selected_character_slot = FrontEndCharacterSlot::First;
    runtime::ContentStartupStage content_stage = runtime::ContentStartupStage::NoContent;
    FrontEndStartupModel profiles{};
    FrontEndCharacterStartupModel characters{};
    // Presentation-only position of the already-confirmed active profile,
    // resolved from the confirmed identifier against `profiles`. It is empty
    // whenever nothing is confirmed or the model no longer holds the confirmed
    // identifier, so the view can never mark a row the model does not present.
    std::optional<FrontEndProfileSlot> active_profile_slot{};
    std::optional<FrontEndCharacterSlot> active_character_slot{};

    friend constexpr bool operator==(const FrontEndView &, const FrontEndView &) noexcept = default;
};

// [any thread; reentrant] Explicit startup state for the project-owned static
// front-end.
[[nodiscard]] constexpr FrontEndState InitialFrontEndState() noexcept
{
    return FrontEndState{
        .mode = FrontEndMode::Main,
        .selected_main_row = FrontEndMainRow::StartDiagnostic,
        .selected_profile_slot = FrontEndProfileSlot::First,
        .selected_character_slot = FrontEndCharacterSlot::First,
    };
}

// [any thread; reentrant] Selects the project-owned startup surface from an
// already-captured profile-count snapshot and an explicit persistence
// capability. Valid nonempty snapshots and explicitly creatable empty
// snapshots open Profiles at its first slot. Every malformed, out-of-bounds,
// or non-creatable empty snapshot fails closed to InitialFrontEndState. This
// pure planner performs no command publication, allocation, I/O, persistence,
// or identity work.
[[nodiscard]] constexpr FrontEndState PlanProjectFrontEndStartupState(
    const std::uint16_t total_profiles, const std::uint8_t visible_profiles,
    const FrontEndCapabilities capabilities = {}) noexcept
{
    const bool counts_are_bounded =
        total_profiles <= kFrontEndMaximumProfiles && visible_profiles <= kFrontEndVisibleProfiles;
    const bool visible_count_fits_total = visible_profiles <= total_profiles;
    const bool both_counts_are_zero = total_profiles == 0U && visible_profiles == 0U;
    const bool both_counts_are_nonzero = total_profiles != 0U && visible_profiles != 0U;
    if (!counts_are_bounded || !visible_count_fits_total || (!both_counts_are_zero && !both_counts_are_nonzero) ||
        (both_counts_are_zero && !capabilities.can_create_first_profile))
    {
        return InitialFrontEndState();
    }

    return FrontEndState{
        .mode = FrontEndMode::Profiles,
        .selected_main_row = FrontEndMainRow::Profiles,
        .selected_profile_slot = FrontEndProfileSlot::First,
        .selected_character_slot = FrontEndCharacterSlot::First,
    };
}

[[nodiscard]] constexpr bool IsValidFrontEndState(const FrontEndState state) noexcept
{
    const bool valid_mode = state.mode == FrontEndMode::Main || state.mode == FrontEndMode::Profiles ||
                            state.mode == FrontEndMode::DiagnosticPlay || state.mode == FrontEndMode::Controls ||
                            state.mode == FrontEndMode::AssetTopology || state.mode == FrontEndMode::Characters;
    const bool valid_row = state.selected_main_row == FrontEndMainRow::StartDiagnostic ||
                           state.selected_main_row == FrontEndMainRow::Profiles ||
                           state.selected_main_row == FrontEndMainRow::Controls ||
                           state.selected_main_row == FrontEndMainRow::AssetTopology;
    const bool valid_profile_slot = state.selected_profile_slot == FrontEndProfileSlot::First ||
                                    state.selected_profile_slot == FrontEndProfileSlot::Second ||
                                    state.selected_profile_slot == FrontEndProfileSlot::Third;
    const bool valid_character_slot = state.selected_character_slot == FrontEndCharacterSlot::First ||
                                      state.selected_character_slot == FrontEndCharacterSlot::Second ||
                                      state.selected_character_slot == FrontEndCharacterSlot::Third;
    return valid_mode && valid_row && valid_profile_slot && valid_character_slot;
}

// [any thread; reentrant] Consumes already-routed logical press edges and a
// caller-owned startup-model count. Invalid input state fails closed to the
// initial front-end before considering any edge. The count is clamped to the
// three project-owned slots. Cancel has priority over primary and navigation;
// it is inert on Main and returns every other mode to its corresponding Main
// row without publishing a command. Primary has priority over navigation. A
// selectable Profiles slot publishes one typed command and returns to its Main
// row. When the catalog is empty and the explicit capability is enabled,
// Primary publishes CreateFirstProfile and remains in Profiles; the default
// capability preserves the legacy empty return transition byte-for-byte.
// Primary on Main/StartDiagnostic publishes the project-only
// StartDiagnosticCampaign command with the projected DiagnosticPlay state only
// when its explicit capability is enabled; otherwise that row is inert.
// Out-of-range slots retain that legacy return transition. Empty
// selection/navigation is otherwise inert. Simultaneous navigation edges are
// neutral, and navigation is press-edge-only and clamps at both bounds.
//
// Start support and confirmation are independent inputs. Both must permit the
// transition. A closed support capability or an unsatisfied confirmation gate
// leaves Main/StartDiagnostic inert and publishes no command.
// Live callers must derive it with FrontEndHasConfirmedActiveProfile; the
// bounded replay adapter may use its private identity-free mirror, initialized
// closed and opened only by a replayed SetActiveProfile command. An already
// entered DiagnosticPlay state fails closed to the initial front end whenever
// either input closes it. No allocation, I/O, catalog access, or persistence
// mutation occurs.
[[nodiscard]] constexpr FrontEndReduction ReduceFrontEnd(FrontEndState state, const FrontEndInputEdges input,
                                                         const std::uint8_t visible_profile_slots,
                                                         const FrontEndCapabilities capabilities = {},
                                                         const bool active_profile_is_confirmed = false,
                                                         const std::uint8_t visible_character_slots = 0U,
                                                         const bool active_character_is_confirmed = false) noexcept
{
    if (!IsValidFrontEndState(state))
        return FrontEndReduction{.state = InitialFrontEndState()};

    const bool diagnostic_play_is_permitted =
        FrontEndAllowsDiagnosticPlay(capabilities, active_profile_is_confirmed, active_character_is_confirmed);
    if (!diagnostic_play_is_permitted && state.mode == FrontEndMode::DiagnosticPlay)
        return FrontEndReduction{.state = InitialFrontEndState()};
    if (state.mode == FrontEndMode::Characters &&
        (!capabilities.supports_character_selection || !active_profile_is_confirmed))
    {
        return FrontEndReduction{.state = InitialFrontEndState()};
    }

    if (input.cancel_pressed)
    {
        switch (state.mode)
        {
        case FrontEndMode::Main:
            return FrontEndReduction{.state = state};
        case FrontEndMode::Profiles:
            state.selected_main_row = FrontEndMainRow::Profiles;
            break;
        case FrontEndMode::Characters:
            state.selected_main_row = FrontEndMainRow::Profiles;
            break;
        case FrontEndMode::DiagnosticPlay:
            state.selected_main_row = FrontEndMainRow::StartDiagnostic;
            break;
        case FrontEndMode::Controls:
            state.selected_main_row = FrontEndMainRow::Controls;
            break;
        case FrontEndMode::AssetTopology:
            state.selected_main_row = FrontEndMainRow::AssetTopology;
            break;
        }
        state.mode = FrontEndMode::Main;
        state.selected_profile_slot = FrontEndProfileSlot::First;
        state.selected_character_slot = FrontEndCharacterSlot::First;
        return FrontEndReduction{.state = state};
    }

    constexpr std::uint8_t kMaximumSelectableProfiles = static_cast<std::uint8_t>(kFrontEndVisibleProfiles);
    const std::uint8_t selectable_profiles =
        visible_profile_slots < kMaximumSelectableProfiles ? visible_profile_slots : kMaximumSelectableProfiles;
    const bool profile_slot_is_selectable =
        state.mode != FrontEndMode::Profiles ||
        static_cast<std::uint8_t>(state.selected_profile_slot) < selectable_profiles;
    if (!profile_slot_is_selectable)
    {
        state.selected_profile_slot = FrontEndProfileSlot::First;
    }
    constexpr std::uint8_t kMaximumSelectableCharacters = static_cast<std::uint8_t>(kFrontEndVisibleCharacters);
    const std::uint8_t selectable_characters =
        visible_character_slots < kMaximumSelectableCharacters ? visible_character_slots : kMaximumSelectableCharacters;
    const bool character_slot_is_selectable =
        state.mode != FrontEndMode::Characters ||
        static_cast<std::uint8_t>(state.selected_character_slot) < selectable_characters;
    if (!character_slot_is_selectable)
        state.selected_character_slot = FrontEndCharacterSlot::First;

    if (input.primary_pressed)
    {
        switch (state.mode)
        {
        case FrontEndMode::Characters:
            if (selectable_characters == 0U && capabilities.can_create_first_character)
            {
                return FrontEndReduction{
                    .state = state,
                    .command =
                        FrontEndCommand{
                            .type = FrontEndCommandType::CreateFirstCharacter,
                            .profile_slot = FrontEndProfileSlot::First,
                            .character_slot = FrontEndCharacterSlot::First,
                        },
                };
            }
            if (!character_slot_is_selectable)
                return FrontEndReduction{.state = state};
            return FrontEndReduction{
                .state =
                    FrontEndState{
                        .mode = FrontEndMode::Main,
                        .selected_main_row = FrontEndMainRow::StartDiagnostic,
                        .selected_profile_slot = FrontEndProfileSlot::First,
                        .selected_character_slot = FrontEndCharacterSlot::First,
                    },
                .command =
                    FrontEndCommand{
                        .type = FrontEndCommandType::SetActiveCharacter,
                        .profile_slot = FrontEndProfileSlot::First,
                        .character_slot = state.selected_character_slot,
                    },
            };
        case FrontEndMode::Profiles:
            if (selectable_profiles == 0U && capabilities.can_create_first_profile)
            {
                return FrontEndReduction{
                    .state = state,
                    .command =
                        FrontEndCommand{
                            .type = FrontEndCommandType::CreateFirstProfile,
                            .profile_slot = FrontEndProfileSlot::First,
                        },
                };
            }
            if (!profile_slot_is_selectable)
            {
                return FrontEndReduction{.state = FrontEndState{
                                             .mode = FrontEndMode::Main,
                                             .selected_main_row = FrontEndMainRow::Profiles,
                                             .selected_profile_slot = FrontEndProfileSlot::First,
                                         }};
            }
            return FrontEndReduction{
                .state =
                    FrontEndState{
                        .mode =
                            capabilities.supports_character_selection ? FrontEndMode::Characters : FrontEndMode::Main,
                        .selected_main_row = FrontEndMainRow::Profiles,
                        .selected_profile_slot = FrontEndProfileSlot::First,
                        .selected_character_slot = FrontEndCharacterSlot::First,
                    },
                .command =
                    FrontEndCommand{
                        .type = FrontEndCommandType::SetActiveProfile,
                        .profile_slot = state.selected_profile_slot,
                    },
            };
        case FrontEndMode::DiagnosticPlay:
            return FrontEndReduction{.state = InitialFrontEndState()};
        case FrontEndMode::Controls:
            return FrontEndReduction{.state = FrontEndState{
                                         .mode = FrontEndMode::Main,
                                         .selected_main_row = FrontEndMainRow::Controls,
                                         .selected_profile_slot = FrontEndProfileSlot::First,
                                     }};
        case FrontEndMode::AssetTopology:
            return FrontEndReduction{.state = FrontEndState{
                                         .mode = FrontEndMode::Main,
                                         .selected_main_row = FrontEndMainRow::AssetTopology,
                                         .selected_profile_slot = FrontEndProfileSlot::First,
                                     }};
        case FrontEndMode::Main:
            break;
        }

        if (state.selected_main_row == FrontEndMainRow::StartDiagnostic && !diagnostic_play_is_permitted)
        {
            return FrontEndReduction{.state = state};
        }

        state.selected_profile_slot = FrontEndProfileSlot::First;
        state.selected_character_slot = FrontEndCharacterSlot::First;
        switch (state.selected_main_row)
        {
        case FrontEndMainRow::StartDiagnostic:
            state.mode = FrontEndMode::DiagnosticPlay;
            return FrontEndReduction{
                .state = state,
                .command =
                    FrontEndCommand{
                        .type = FrontEndCommandType::StartDiagnosticCampaign,
                        .profile_slot = FrontEndProfileSlot::First,
                    },
            };
        case FrontEndMainRow::Profiles:
            state.mode = FrontEndMode::Profiles;
            state.selected_profile_slot = FrontEndProfileSlot::First;
            break;
        case FrontEndMainRow::Controls:
            state.mode = FrontEndMode::Controls;
            break;
        case FrontEndMainRow::AssetTopology:
            state.mode = FrontEndMode::AssetTopology;
            break;
        }
        return FrontEndReduction{.state = state};
    }

    if (input.previous_pressed == input.next_pressed)
        return FrontEndReduction{.state = state};

    if (state.mode == FrontEndMode::Profiles)
    {
        if (selectable_profiles == 0U)
            return FrontEndReduction{.state = state};

        const std::uint8_t slot = static_cast<std::uint8_t>(state.selected_profile_slot);
        if (input.previous_pressed && slot > 0U)
        {
            state.selected_profile_slot = static_cast<FrontEndProfileSlot>(slot - 1U);
        }
        else if (input.next_pressed && slot + 1U < selectable_profiles)
        {
            state.selected_profile_slot = static_cast<FrontEndProfileSlot>(slot + 1U);
        }
        return FrontEndReduction{.state = state};
    }

    if (state.mode == FrontEndMode::Characters)
    {
        if (selectable_characters == 0U)
            return FrontEndReduction{.state = state};

        const std::uint8_t slot = static_cast<std::uint8_t>(state.selected_character_slot);
        if (input.previous_pressed && slot > 0U)
        {
            state.selected_character_slot = static_cast<FrontEndCharacterSlot>(slot - 1U);
        }
        else if (input.next_pressed && slot + 1U < selectable_characters)
        {
            state.selected_character_slot = static_cast<FrontEndCharacterSlot>(slot + 1U);
        }
        return FrontEndReduction{.state = state};
    }

    if (state.mode != FrontEndMode::Main)
        return FrontEndReduction{.state = state};

    const std::uint8_t row = static_cast<std::uint8_t>(state.selected_main_row);
    if (input.previous_pressed && row > 0U)
    {
        state.selected_main_row = static_cast<FrontEndMainRow>(row - 1U);
    }
    else if (input.next_pressed && row + 1U < kFrontEndMainRowCount)
    {
        state.selected_main_row = static_cast<FrontEndMainRow>(row + 1U);
    }
    return FrontEndReduction{.state = state};
}

// [any thread; reentrant] Simulation is enabled only by a fully valid
// DiagnosticPlay state with explicit start support and a satisfied optional
// confirmation gate.
[[nodiscard]] constexpr bool FrontEndAllowsSimulation(const FrontEndState state,
                                                      const FrontEndCapabilities capabilities = {},
                                                      const bool active_profile_is_confirmed = false,
                                                      const bool active_character_is_confirmed = false) noexcept
{
    return IsValidFrontEndState(state) && state.mode == FrontEndMode::DiagnosticPlay &&
           FrontEndAllowsDiagnosticPlay(capabilities, active_profile_is_confirmed, active_character_is_confirmed);
}

// [any thread; reentrant] Normalizes invalid state to InitialFrontEndState and
// copies only fixed values. The published active-profile position is resolved
// from the confirmed identifier against the same model the view carries, so the
// view holds no independently mutable second identity and an unresolvable
// identifier simply leaves the position empty. The returned view has no
// borrowed lifetime and performs no allocation or I/O.
[[nodiscard]] constexpr FrontEndView BuildFrontEndView(
    const FrontEndState state, const runtime::ContentStartupStage content_stage, const FrontEndStartupModel &profiles,
    const std::optional<profiles::ProfileId> &confirmed_profile_id, const FrontEndCharacterStartupModel &characters,
    const std::optional<profiles::CharacterId> &confirmed_character_id) noexcept
{
    const FrontEndState normalized = IsValidFrontEndState(state) ? state : InitialFrontEndState();
    const runtime::ContentStartupStage normalized_stage =
        content_stage == runtime::ContentStartupStage::DataMounted ||
                content_stage == runtime::ContentStartupStage::LevelContent
            ? content_stage
            : runtime::ContentStartupStage::NoContent;
    return FrontEndView{
        .mode = normalized.mode,
        .selected_main_row = normalized.selected_main_row,
        .selected_profile_slot = normalized.selected_profile_slot,
        .selected_character_slot = normalized.selected_character_slot,
        .content_stage = normalized_stage,
        .profiles = profiles,
        .characters = characters,
        .active_profile_slot = FrontEndConfirmedProfileSlot(profiles, confirmed_profile_id),
        .active_character_slot = FrontEndConfirmedCharacterSlot(characters, confirmed_character_id),
    };
}

// Source-compatible identity-free projection used by legacy synthetic replay
// and tests. The complete live overload above carries the character snapshot.
[[nodiscard]] constexpr FrontEndView BuildFrontEndView(
    const FrontEndState state, const runtime::ContentStartupStage content_stage, const FrontEndStartupModel &profiles,
    const std::optional<profiles::ProfileId> &confirmed_profile_id = std::nullopt) noexcept
{
    return BuildFrontEndView(state, content_stage, profiles, confirmed_profile_id, FrontEndCharacterStartupModel{},
                             std::nullopt);
}

// [any thread; reentrant] Fully owned project-generated opaque RGBA8 cards.
// These builders perform no I/O and consume no platform object, decoded retail
// asset, or emulator data.
[[nodiscard]] runtime::DebugImage BuildProjectFrontEndMainImage(runtime::ContentStartupStage content_stage,
                                                                std::uint16_t profile_count);
[[nodiscard]] runtime::DebugImage BuildProjectFrontEndProfilesImage(const FrontEndStartupModel &profiles,
                                                                    FrontEndCapabilities capabilities = {});
[[nodiscard]] runtime::DebugImage BuildProjectFrontEndCharactersImage(const FrontEndCharacterStartupModel &characters,
                                                                      FrontEndCapabilities capabilities = {});
[[nodiscard]] runtime::DebugImage BuildProjectFrontEndDiagnosticPlayImage();
[[nodiscard]] runtime::DebugImage BuildProjectFrontEndControlsImage();

// Builds the exact project-owned E-0066 three-block topology fixture and
// returns its fully owned metadata-only diagnostic image.
[[nodiscard]] std::expected<runtime::DebugImage, runtime::TextureStorageTopologyDebugImageError>
BuildProjectFrontEndAssetTopologyImage();

static_assert(std::is_trivially_copyable_v<FrontEndLabel>);
static_assert(std::is_standard_layout_v<FrontEndLabel>);
static_assert(std::is_trivially_copyable_v<FrontEndProfile>);
static_assert(std::is_standard_layout_v<FrontEndProfile>);
static_assert(std::is_trivially_copyable_v<FrontEndStartupModel>);
static_assert(std::is_standard_layout_v<FrontEndStartupModel>);
static_assert(std::is_trivially_copyable_v<FrontEndCharacter>);
static_assert(std::is_standard_layout_v<FrontEndCharacter>);
static_assert(std::is_trivially_copyable_v<FrontEndCharacterStartupModel>);
static_assert(std::is_standard_layout_v<FrontEndCharacterStartupModel>);
static_assert(std::is_trivially_copyable_v<FrontEndState>);
static_assert(std::is_standard_layout_v<FrontEndState>);
static_assert(std::is_trivially_copyable_v<FrontEndInputEdges>);
static_assert(std::is_standard_layout_v<FrontEndInputEdges>);
static_assert(std::is_trivially_copyable_v<FrontEndCapabilities>);
static_assert(std::is_standard_layout_v<FrontEndCapabilities>);
static_assert(std::is_trivially_copyable_v<FrontEndCommand>);
static_assert(std::is_standard_layout_v<FrontEndCommand>);
static_assert(std::is_trivially_copyable_v<FrontEndReduction>);
static_assert(std::is_standard_layout_v<FrontEndReduction>);
static_assert(std::is_trivially_copyable_v<FrontEndView>);
static_assert(std::is_standard_layout_v<FrontEndView>);
static_assert(sizeof(FrontEndMode) == 1U);
static_assert(sizeof(FrontEndMainRow) == 1U);
static_assert(sizeof(FrontEndProfileSlot) == 1U);
static_assert(sizeof(FrontEndCharacterSlot) == 1U);
static_assert(sizeof(FrontEndCommandType) == 1U);
static_assert(sizeof(FrontEndCapabilities) == 6U);
static_assert(sizeof(FrontEndModelError) == 1U);
} // namespace omega::app
