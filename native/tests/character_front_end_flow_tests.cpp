#include "front_end.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
using omega::app::FrontEndCapabilities;
using omega::app::FrontEndCharacterSlot;
using omega::app::FrontEndCharacterStartupModel;
using omega::app::FrontEndCommand;
using omega::app::FrontEndCommandType;
using omega::app::FrontEndInputEdges;
using omega::app::FrontEndLabel;
using omega::app::FrontEndMainRow;
using omega::app::FrontEndMode;
using omega::app::FrontEndModelError;
using omega::app::FrontEndProfileSlot;
using omega::app::FrontEndReduction;
using omega::app::FrontEndState;
using omega::profiles::CharacterId;
using omega::profiles::CharacterMetadata;
using omega::profiles::CharacterSummary;
using omega::runtime::DebugImage;

int failures = 0;

void Check(const bool condition, const std::string_view message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

[[nodiscard]] CharacterSummary Summary(const std::uint16_t ordinal,
                                       std::string name)
{
    std::array<std::uint8_t, 16U> bytes{};
    bytes[14] = static_cast<std::uint8_t>((ordinal >> 8U) & 0xffU);
    bytes[15] = static_cast<std::uint8_t>(ordinal & 0xffU);
    return CharacterSummary{
        .id = CharacterId::FromBytes(bytes),
        .metadata =
            CharacterMetadata{
                .display_name = std::move(name),
                .created_unix_milliseconds = ordinal,
                .modified_unix_milliseconds = ordinal,
            },
        .metadata_revision = ordinal,
    };
}

[[nodiscard]] std::string LabelText(const FrontEndLabel &label)
{
    return std::string(label.cells.data(), label.length);
}

[[nodiscard]] bool IsOpaqueRgbaCard(const DebugImage &image)
{
    const std::size_t expected_bytes =
        static_cast<std::size_t>(omega::app::kFrontEndImageWidth) *
        static_cast<std::size_t>(omega::app::kFrontEndImageHeight) * 4U;
    if (image.width != omega::app::kFrontEndImageWidth ||
        image.height != omega::app::kFrontEndImageHeight ||
        image.rgba8_pixels.size() != expected_bytes)
    {
        return false;
    }

    for (std::size_t offset = 3U; offset < image.rgba8_pixels.size();
         offset += 4U)
    {
        if (image.rgba8_pixels[offset] != std::byte{255U})
            return false;
    }
    return true;
}

void CheckCharacterModelProjection()
{
    static_assert(noexcept(omega::app::MakeFrontEndCharacterStartupModel(
        std::declval<std::span<const CharacterSummary>>())));
    static_assert(omega::app::kFrontEndMaximumCharacters == 1'024U);
    static_assert(omega::app::kFrontEndVisibleCharacters == 3U);

    const std::array sorted{
        Summary(1U, "alpha"),
        Summary(2U, "Beta"),
        Summary(3U, "ABCDEFGHIJKLMNOPQRSTUVWXY"),
        Summary(4U, "not visible"),
    };
    const auto projected =
        omega::app::MakeFrontEndCharacterStartupModel(sorted);
    Check(projected.has_value(),
          "a strictly ID-sorted character catalog projects successfully");
    if (projected)
    {
        Check(projected->total_characters == 4U &&
                  projected->visible_characters == 3U,
              "character projection preserves the total and bounds the visible rows");
        Check(projected->characters[0].id == sorted[0].id &&
                  projected->characters[1].id == sorted[1].id &&
                  projected->characters[2].id == sorted[2].id,
              "character projection preserves the first three sorted identities");
        Check(LabelText(projected->characters[0].label) == "ALPHA" &&
                  LabelText(projected->characters[1].label) == "BETA" &&
                  LabelText(projected->characters[2].label) ==
                      "ABCDEFGHIJKLMNOPQRSTUVWX" &&
                  projected->characters[2].label.truncated,
              "character projection applies the fixed label policy without borrowing input");
    }

    const std::array unsorted{
        Summary(2U, "SECOND"),
        Summary(1U, "FIRST"),
    };
    const auto rejected_unsorted =
        omega::app::MakeFrontEndCharacterStartupModel(unsorted);
    Check(!rejected_unsorted &&
              rejected_unsorted.error() ==
                  FrontEndModelError::UnsortedCharacters,
          "an unsorted character catalog fails with the fixed character error");

    std::vector<CharacterSummary> over_limit;
    over_limit.reserve(omega::app::kFrontEndMaximumCharacters + 1U);
    for (std::size_t index = 0U;
         index <= omega::app::kFrontEndMaximumCharacters; ++index)
    {
        over_limit.push_back(
            Summary(static_cast<std::uint16_t>(index + 1U), "CHARACTER"));
    }
    const auto rejected_over_limit =
        omega::app::MakeFrontEndCharacterStartupModel(over_limit);
    Check(!rejected_over_limit &&
              rejected_over_limit.error() ==
                  FrontEndModelError::TooManyCharacters,
          "a character catalog above the fixed projection limit fails before materialization");
}

void CheckProfileToCharacterRouting()
{
    constexpr FrontEndState profiles_second{
        .mode = FrontEndMode::Profiles,
        .selected_main_row = FrontEndMainRow::Profiles,
        .selected_profile_slot = FrontEndProfileSlot::Second,
        .selected_character_slot = FrontEndCharacterSlot::First,
    };
    constexpr FrontEndInputEdges primary{.primary_pressed = true};

    const FrontEndReduction without_character_support =
        omega::app::ReduceFrontEnd(profiles_second, primary, 2U);
    Check(without_character_support ==
              FrontEndReduction{
                  .state = FrontEndState{
                      .mode = FrontEndMode::Main,
                      .selected_main_row = FrontEndMainRow::Profiles,
                      .selected_profile_slot = FrontEndProfileSlot::First,
                      .selected_character_slot = FrontEndCharacterSlot::First,
                  },
                  .command = FrontEndCommand{
                      .type = FrontEndCommandType::SetActiveProfile,
                      .profile_slot = FrontEndProfileSlot::Second,
                  },
              },
          "profile selection returns to Main when character selection is unsupported");

    constexpr FrontEndCapabilities character_support{
        .supports_character_selection = true,
    };
    const FrontEndReduction with_character_support =
        omega::app::ReduceFrontEnd(profiles_second, primary, 2U,
                                   character_support);
    Check(with_character_support ==
              FrontEndReduction{
                  .state = FrontEndState{
                      .mode = FrontEndMode::Characters,
                      .selected_main_row = FrontEndMainRow::Profiles,
                      .selected_profile_slot = FrontEndProfileSlot::First,
                      .selected_character_slot = FrontEndCharacterSlot::First,
                  },
                  .command = FrontEndCommand{
                      .type = FrontEndCommandType::SetActiveProfile,
                      .profile_slot = FrontEndProfileSlot::Second,
                  },
              },
          "profile selection routes to Characters only with explicit support");
}

void CheckCharacterCreationAndSelection()
{
    constexpr FrontEndState characters{
        .mode = FrontEndMode::Characters,
        .selected_main_row = FrontEndMainRow::Profiles,
        .selected_profile_slot = FrontEndProfileSlot::First,
        .selected_character_slot = FrontEndCharacterSlot::First,
    };
    constexpr FrontEndInputEdges primary{.primary_pressed = true};
    constexpr FrontEndInputEdges previous{.previous_pressed = true};
    constexpr FrontEndInputEdges next{.next_pressed = true};
    constexpr FrontEndCapabilities character_creation{
        .supports_character_selection = true,
        .can_create_first_character = true,
    };

    const FrontEndReduction create = omega::app::ReduceFrontEnd(
        characters, primary, 1U, character_creation, true, 0U);
    Check(create ==
              FrontEndReduction{
                  .state = characters,
                  .command = FrontEndCommand{
                      .type = FrontEndCommandType::CreateFirstCharacter,
                      .profile_slot = FrontEndProfileSlot::First,
                      .character_slot = FrontEndCharacterSlot::First,
                  },
              },
          "an empty supported Characters surface publishes creation and stays open");
    Check(omega::app::ReduceFrontEnd(create.state, {}, 1U,
                                     character_creation, true, 0U)
                  .command == FrontEndCommand{},
          "character creation is published only for a routed press edge");

    constexpr FrontEndCapabilities character_unavailable{
        .supports_character_selection = true,
    };
    Check(omega::app::ReduceFrontEnd(characters, primary, 1U,
                                     character_unavailable, true, 0U) ==
              FrontEndReduction{.state = characters},
          "an empty non-creatable Characters surface leaves Primary inert and relies on Cancel to return");

    constexpr FrontEndCapabilities character_selection{
        .supports_character_selection = true,
    };
    const FrontEndReduction second = omega::app::ReduceFrontEnd(
        characters, next, 1U, character_selection, true, 3U);
    const FrontEndReduction third = omega::app::ReduceFrontEnd(
        second.state, next, 1U, character_selection, true, 3U);
    const FrontEndReduction clamped = omega::app::ReduceFrontEnd(
        third.state, next, 1U, character_selection, true, 3U);
    const FrontEndReduction back_to_second = omega::app::ReduceFrontEnd(
        third.state, previous, 1U, character_selection, true, 3U);
    Check(second.state.selected_character_slot ==
                  FrontEndCharacterSlot::Second &&
              third.state.selected_character_slot ==
                  FrontEndCharacterSlot::Third &&
              clamped.state.selected_character_slot ==
                  FrontEndCharacterSlot::Third &&
              back_to_second.state.selected_character_slot ==
                  FrontEndCharacterSlot::Second,
          "populated character navigation advances, retreats, and clamps to visible rows");

    const FrontEndReduction selected = omega::app::ReduceFrontEnd(
        second.state, primary, 1U, character_selection, true, 3U);
    Check(selected ==
              FrontEndReduction{
                  .state = FrontEndState{
                      .mode = FrontEndMode::Main,
                      .selected_main_row = FrontEndMainRow::StartDiagnostic,
                      .selected_profile_slot = FrontEndProfileSlot::First,
                      .selected_character_slot = FrontEndCharacterSlot::First,
                  },
                  .command = FrontEndCommand{
                      .type = FrontEndCommandType::SetActiveCharacter,
                      .profile_slot = FrontEndProfileSlot::First,
                      .character_slot = FrontEndCharacterSlot::Second,
                  },
              },
          "selecting a populated character publishes its slot and returns to Main");
}

void CheckDiagnosticGate()
{
    constexpr FrontEndCapabilities gated_start{
        .can_start_diagnostic_campaign = true,
        .requires_active_profile_for_diagnostic_play = true,
        .requires_active_character_for_diagnostic_play = true,
    };
    constexpr FrontEndState main_start{
        .mode = FrontEndMode::Main,
        .selected_main_row = FrontEndMainRow::StartDiagnostic,
        .selected_profile_slot = FrontEndProfileSlot::First,
        .selected_character_slot = FrontEndCharacterSlot::First,
    };
    constexpr FrontEndInputEdges primary{.primary_pressed = true};

    const std::array denied{
        omega::app::ReduceFrontEnd(main_start, primary, 1U, gated_start,
                                   false, 1U, false),
        omega::app::ReduceFrontEnd(main_start, primary, 1U, gated_start,
                                   true, 1U, false),
        omega::app::ReduceFrontEnd(main_start, primary, 1U, gated_start,
                                   false, 1U, true),
    };
    Check(denied[0] == FrontEndReduction{.state = main_start} &&
              denied[1] == FrontEndReduction{.state = main_start} &&
              denied[2] == FrontEndReduction{.state = main_start},
          "diagnostic start remains inert until both confirmations are present");

    const FrontEndReduction allowed = omega::app::ReduceFrontEnd(
        main_start, primary, 1U, gated_start, true, 1U, true);
    Check(allowed ==
              FrontEndReduction{
                  .state = FrontEndState{
                      .mode = FrontEndMode::DiagnosticPlay,
                      .selected_main_row = FrontEndMainRow::StartDiagnostic,
                      .selected_profile_slot = FrontEndProfileSlot::First,
                      .selected_character_slot = FrontEndCharacterSlot::First,
                  },
                  .command = FrontEndCommand{
                      .type = FrontEndCommandType::StartDiagnosticCampaign,
                      .profile_slot = FrontEndProfileSlot::First,
                  },
              } &&
              omega::app::FrontEndAllowsSimulation(allowed.state, gated_start,
                                                    true, true),
          "both confirmations authorize the typed diagnostic start and simulation");
    Check(!omega::app::FrontEndAllowsSimulation(allowed.state, gated_start,
                                                true, false) &&
              !omega::app::FrontEndAllowsSimulation(allowed.state, gated_start,
                                                    false, true),
          "losing either confirmation closes an entered diagnostic state");
}

void CheckCancelAndInvalidStateContainment()
{
    constexpr FrontEndCapabilities character_support{
        .supports_character_selection = true,
        .can_create_first_character = true,
    };
    constexpr FrontEndState characters_second{
        .mode = FrontEndMode::Characters,
        .selected_main_row = FrontEndMainRow::Profiles,
        .selected_profile_slot = FrontEndProfileSlot::Second,
        .selected_character_slot = FrontEndCharacterSlot::Second,
    };
    constexpr FrontEndInputEdges cancel_and_primary{
        .primary_pressed = true,
        .cancel_pressed = true,
    };
    Check(omega::app::ReduceFrontEnd(characters_second, cancel_and_primary, 2U,
                                     character_support, true, 2U) ==
              FrontEndReduction{
                  .state = FrontEndState{
                      .mode = FrontEndMode::Main,
                      .selected_main_row = FrontEndMainRow::Profiles,
                      .selected_profile_slot = FrontEndProfileSlot::First,
                      .selected_character_slot = FrontEndCharacterSlot::First,
                  },
              },
          "cancel outranks character selection and returns to the Profiles row without a command");

    constexpr FrontEndState main_profiles{
        .mode = FrontEndMode::Main,
        .selected_main_row = FrontEndMainRow::Profiles,
        .selected_profile_slot = FrontEndProfileSlot::First,
        .selected_character_slot = FrontEndCharacterSlot::First,
    };
    Check(omega::app::ReduceFrontEnd(
              main_profiles, FrontEndInputEdges{.cancel_pressed = true}, 2U,
              character_support, true, 2U) ==
              FrontEndReduction{.state = main_profiles},
          "cancel is inert on Main");

    const FrontEndState invalid_character_slot{
        .mode = FrontEndMode::Characters,
        .selected_main_row = FrontEndMainRow::Profiles,
        .selected_profile_slot = FrontEndProfileSlot::First,
        .selected_character_slot = static_cast<FrontEndCharacterSlot>(0xffU),
    };
    Check(omega::app::ReduceFrontEnd(
              invalid_character_slot,
              FrontEndInputEdges{.primary_pressed = true}, 1U,
              character_support, true, 3U) ==
              FrontEndReduction{.state = omega::app::InitialFrontEndState()},
          "an invalid character-slot enum fails closed before command publication");

    const FrontEndState invalid_mode{
        .mode = static_cast<FrontEndMode>(0xffU),
        .selected_main_row = FrontEndMainRow::Profiles,
        .selected_profile_slot = FrontEndProfileSlot::First,
        .selected_character_slot = FrontEndCharacterSlot::First,
    };
    Check(omega::app::ReduceFrontEnd(
              invalid_mode, FrontEndInputEdges{.primary_pressed = true}, 1U,
              character_support, true, 3U) ==
              FrontEndReduction{.state = omega::app::InitialFrontEndState()},
          "an invalid front-end state fails closed before character routing");

    constexpr FrontEndState stale_third_slot{
        .mode = FrontEndMode::Characters,
        .selected_main_row = FrontEndMainRow::Profiles,
        .selected_profile_slot = FrontEndProfileSlot::First,
        .selected_character_slot = FrontEndCharacterSlot::Third,
    };
    Check(omega::app::ReduceFrontEnd(
              stale_third_slot, FrontEndInputEdges{.primary_pressed = true},
              1U, character_support, true, 1U) ==
              FrontEndReduction{
                  .state = FrontEndState{
                      .mode = FrontEndMode::Characters,
                      .selected_main_row = FrontEndMainRow::Profiles,
                      .selected_profile_slot = FrontEndProfileSlot::First,
                      .selected_character_slot = FrontEndCharacterSlot::First,
                  },
              },
          "a stale character slot outside the visible count resets without selecting another row");
}

void CheckCharacterImageContract()
{
    const std::array summaries{
        Summary(1U, "ALPHA"),
        Summary(2U, "BETA"),
        Summary(3U, "GAMMA"),
        Summary(4U, "DELTA"),
    };
    const auto projected =
        omega::app::MakeFrontEndCharacterStartupModel(summaries);
    Check(projected.has_value(),
          "the character image fixture projects successfully");
    if (!projected)
        return;

    const DebugImage first =
        omega::app::BuildProjectFrontEndCharactersImage(*projected);
    const DebugImage second =
        omega::app::BuildProjectFrontEndCharactersImage(*projected);
    Check(IsOpaqueRgbaCard(first) && IsOpaqueRgbaCard(second),
          "character cards are tightly packed opaque RGBA8 images");
    Check(first.rgba8_pixels == second.rgba8_pixels &&
              first.rgba8_pixels.data() != second.rgba8_pixels.data(),
          "repeated character-card builds are byte-deterministic and independently owned");

    constexpr FrontEndCapabilities creation{
        .supports_character_selection = true,
        .can_create_first_character = true,
    };
    const DebugImage empty =
        omega::app::BuildProjectFrontEndCharactersImage({}, creation);
    const DebugImage empty_again =
        omega::app::BuildProjectFrontEndCharactersImage({}, creation);
    Check(IsOpaqueRgbaCard(empty) &&
              empty.rgba8_pixels == empty_again.rgba8_pixels &&
              empty.rgba8_pixels != first.rgba8_pixels,
          "the empty creatable character card is opaque, deterministic, and distinct from a populated card");
}
} // namespace

int main()
{
    CheckCharacterModelProjection();
    CheckProfileToCharacterRouting();
    CheckCharacterCreationAndSelection();
    CheckDiagnosticGate();
    CheckCancelAndInvalidStateContainment();
    CheckCharacterImageContract();

    if (failures != 0)
    {
        std::cerr << failures << " character front-end flow test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "character front-end flow tests passed\n";
    return EXIT_SUCCESS;
}
