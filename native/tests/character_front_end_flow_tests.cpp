#include "front_end.h"

#include <algorithm>
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

void CheckCreateAgentRoute()
{
    constexpr FrontEndInputEdges primary{.primary_pressed = true};
    constexpr FrontEndCapabilities create_owner{
        .can_create_first_profile = true,
        .supports_character_selection = true,
    };
    const FrontEndReduction owner_created = omega::app::ReduceFrontEnd(
        omega::app::InitialFrontEndState(), primary, 0U, create_owner);
    Check(owner_created == FrontEndReduction{
                              .state = FrontEndState{
                                  .mode = FrontEndMode::AgentCreation,
                                  .selected_main_row = FrontEndMainRow::CreateAgent,
                              },
                              .command = FrontEndCommand{
                                  .type = FrontEndCommandType::CreateProfileOwner,
                              },
                          },
          "the first Create Agent confirmation creates only its internal owner and enters creation");

    constexpr FrontEndCapabilities agent_creation{
        .supports_character_selection = true,
        .can_create_first_character = true,
    };
    const FrontEndReduction owner_confirmed = omega::app::ReduceFrontEnd(
        owner_created.state, primary, 1U, agent_creation, false, 0U);
    Check(owner_confirmed == FrontEndReduction{
                                .state = owner_created.state,
                                .command = FrontEndCommand{
                                    .type = FrontEndCommandType::ConfirmProfileOwner,
                                },
                            },
          "a second explicit confirmation activates the newly durable owner before agent creation");

    const FrontEndReduction agent_created = omega::app::ReduceFrontEnd(
        owner_confirmed.state, primary, 1U, agent_creation, true, 0U);
    Check(agent_created == FrontEndReduction{
                                .state = FrontEndState{
                                    .mode = FrontEndMode::AgentSelection,
                                    .selected_main_row = FrontEndMainRow::CreateAgent,
                                },
                                .command = FrontEndCommand{
                                    .type = FrontEndCommandType::CreateAgent,
                                },
                            },
          "creation confirmation publishes CreateAgent before the created-agent selection state");

    constexpr FrontEndCapabilities select_agent{
        .supports_character_selection = true,
    };
    const FrontEndReduction agent_confirmed = omega::app::ReduceFrontEnd(
        agent_created.state, primary, 1U, select_agent, true, 1U, false);
    Check(agent_confirmed == FrontEndReduction{
                                  .state = FrontEndState{
                                      .mode = FrontEndMode::BriefingRoom,
                                      .selected_main_row = FrontEndMainRow::CreateAgent,
                                  },
                                  .command = FrontEndCommand{
                                      .type = FrontEndCommandType::ConfirmAgent,
                                  },
                              },
          "the created agent reaches Briefing Room only with a durable confirmation command");
}

void CheckLoadAgentRoute()
{
    constexpr FrontEndInputEdges primary{.primary_pressed = true};
    constexpr FrontEndInputEdges previous{.previous_pressed = true};
    constexpr FrontEndInputEdges next{.next_pressed = true};
    constexpr FrontEndCapabilities support{
        .supports_character_selection = true,
    };
    constexpr FrontEndState title_load{
        .mode = FrontEndMode::Title,
        .selected_main_row = FrontEndMainRow::LoadAgent,
    };
    const FrontEndReduction entered = omega::app::ReduceFrontEnd(
        title_load, primary, 1U, support, false, 0U);
    Check(entered == FrontEndReduction{
                         .state = FrontEndState{
                             .mode = FrontEndMode::AgentSelection,
                             .selected_main_row = FrontEndMainRow::LoadAgent,
                         },
                         .command = FrontEndCommand{
                             .type = FrontEndCommandType::ConfirmProfileOwner,
                         },
                     },
          "Load Agent confirms its internal owner while projecting existing-agent selection");

    const FrontEndReduction second = omega::app::ReduceFrontEnd(
        entered.state, next, 1U, support, true, 3U);
    const FrontEndReduction third = omega::app::ReduceFrontEnd(
        second.state, next, 1U, support, true, 3U);
    const FrontEndReduction clamped = omega::app::ReduceFrontEnd(
        third.state, next, 1U, support, true, 3U);
    const FrontEndReduction back_to_second = omega::app::ReduceFrontEnd(
        third.state, previous, 1U, support, true, 3U);
    Check(second.state.selected_character_slot == FrontEndCharacterSlot::Second &&
              third.state.selected_character_slot == FrontEndCharacterSlot::Third &&
              clamped.state.selected_character_slot == FrontEndCharacterSlot::Third &&
              back_to_second.state.selected_character_slot == FrontEndCharacterSlot::Second,
          "existing-agent navigation advances, retreats, and clamps to bounded visible rows");

    const FrontEndReduction selected = omega::app::ReduceFrontEnd(
        second.state, primary, 1U, support, true, 3U);
    Check(selected == FrontEndReduction{
                          .state = FrontEndState{
                              .mode = FrontEndMode::BriefingRoom,
                              .selected_main_row = FrontEndMainRow::LoadAgent,
                              .selected_character_slot = FrontEndCharacterSlot::Second,
                          },
                          .command = FrontEndCommand{
                              .type = FrontEndCommandType::ConfirmAgent,
                              .character_slot = FrontEndCharacterSlot::Second,
                          },
                      },
          "confirming an existing agent publishes its bounded slot before Briefing Room");

    Check(omega::app::ReduceFrontEnd(title_load, primary, 0U, support) ==
              FrontEndReduction{.state = title_load},
          "Load Agent is inert without an owner catalog");
    Check(omega::app::ReduceFrontEnd(entered.state, primary, 1U, support, true, 0U) ==
              FrontEndReduction{.state = entered.state},
          "existing-agent selection is inert when the active owner has no agents");
}

void CheckBriefingAndGameplayRoute()
{
    constexpr FrontEndInputEdges primary{.primary_pressed = true};
    constexpr FrontEndCapabilities gated_start{
        .can_start_diagnostic_campaign = true,
        .requires_active_profile_for_diagnostic_play = true,
        .supports_character_selection = true,
        .requires_active_character_for_diagnostic_play = true,
    };
    constexpr FrontEndState briefing{
        .mode = FrontEndMode::BriefingRoom,
        .selected_main_row = FrontEndMainRow::LoadAgent,
    };
    const FrontEndReduction started = omega::app::ReduceFrontEnd(
        briefing, primary, 1U, gated_start, true, 1U, true);
    Check(started == FrontEndReduction{
                         .state = FrontEndState{
                             .mode = FrontEndMode::Gameplay,
                             .selected_main_row = FrontEndMainRow::LoadAgent,
                         },
                         .command = FrontEndCommand{
                             .type = FrontEndCommandType::StartCampaign,
                         },
                     } &&
              omega::app::FrontEndAllowsSimulation(started.state, gated_start, true, true),
          "Briefing confirmation publishes StartCampaign before entering Gameplay");

    const std::array gate_loss{
        omega::app::ReduceFrontEnd(briefing, {}, 1U, gated_start, false, 1U, true),
        omega::app::ReduceFrontEnd(briefing, {}, 1U, gated_start, true, 1U, false),
        omega::app::ReduceFrontEnd(started.state, {}, 1U, gated_start, false, 1U, true),
        omega::app::ReduceFrontEnd(started.state, {}, 1U, gated_start, true, 1U, false),
    };
    Check(std::ranges::all_of(gate_loss, [](const FrontEndReduction reduced) {
              return reduced == FrontEndReduction{.state = omega::app::InitialFrontEndState()};
          }),
          "Briefing Room and Gameplay fail closed when either confirmed identity is lost");
}

void CheckCancelAndInvalidStateContainment()
{
    constexpr FrontEndCapabilities support{
        .can_start_diagnostic_campaign = true,
        .requires_active_profile_for_diagnostic_play = true,
        .supports_character_selection = true,
        .requires_active_character_for_diagnostic_play = true,
    };
    constexpr FrontEndInputEdges cancel_and_primary{
        .primary_pressed = true,
        .cancel_pressed = true,
    };
    constexpr FrontEndState selection{
        .mode = FrontEndMode::AgentSelection,
        .selected_main_row = FrontEndMainRow::LoadAgent,
        .selected_character_slot = FrontEndCharacterSlot::Second,
    };
    Check(omega::app::ReduceFrontEnd(selection, cancel_and_primary, 1U, support, true, 2U) ==
              FrontEndReduction{.state = FrontEndState{
                                    .mode = FrontEndMode::Title,
                                    .selected_main_row = FrontEndMainRow::LoadAgent,
                                }},
          "cancel outranks agent confirmation and returns to the matching Title route");

    constexpr FrontEndState briefing{
        .mode = FrontEndMode::BriefingRoom,
        .selected_main_row = FrontEndMainRow::LoadAgent,
    };
    Check(omega::app::ReduceFrontEnd(
              briefing, FrontEndInputEdges{.cancel_pressed = true}, 1U, support, true, 1U, true) ==
              FrontEndReduction{.state = FrontEndState{
                                    .mode = FrontEndMode::AgentSelection,
                                    .selected_main_row = FrontEndMainRow::LoadAgent,
                                }},
          "Briefing Room cancel returns to the existing-agent selection route");

    const FrontEndState invalid_character_slot{
        .mode = FrontEndMode::AgentSelection,
        .selected_main_row = FrontEndMainRow::LoadAgent,
        .selected_character_slot = static_cast<FrontEndCharacterSlot>(0xffU),
    };
    Check(omega::app::ReduceFrontEnd(
              invalid_character_slot, FrontEndInputEdges{.primary_pressed = true},
              1U, support, true, 3U) ==
              FrontEndReduction{.state = omega::app::InitialFrontEndState()},
          "an invalid character slot fails closed before command publication");

    constexpr FrontEndState stale_third_slot{
        .mode = FrontEndMode::AgentSelection,
        .selected_main_row = FrontEndMainRow::LoadAgent,
        .selected_character_slot = FrontEndCharacterSlot::Third,
    };
    Check(omega::app::ReduceFrontEnd(
              stale_third_slot, FrontEndInputEdges{.primary_pressed = true},
              1U, support, true, 1U) ==
              FrontEndReduction{.state = FrontEndState{
                                    .mode = FrontEndMode::AgentSelection,
                                    .selected_main_row = FrontEndMainRow::LoadAgent,
                                }},
          "a stale agent position resets without confirming a different agent");
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
    CheckCreateAgentRoute();
    CheckLoadAgentRoute();
    CheckBriefingAndGameplayRoute();
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
