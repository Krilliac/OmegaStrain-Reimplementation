#include "front_end.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
using omega::app::FrontEndInputEdges;
using omega::app::FrontEndCommand;
using omega::app::FrontEndCommandType;
using omega::app::FrontEndLabel;
using omega::app::FrontEndMainRow;
using omega::app::FrontEndMode;
using omega::app::FrontEndModelError;
using omega::app::FrontEndProfileSlot;
using omega::app::FrontEndReduction;
using omega::app::FrontEndStartupModel;
using omega::app::FrontEndState;
using omega::profiles::ProfileId;
using omega::profiles::ProfileMetadata;
using omega::profiles::ProfileSummary;
using omega::runtime::ContentStartupStage;
using omega::runtime::DebugImage;

using Color = std::array<std::byte, 4U>;

constexpr Color kBackgroundColor{std::byte{8U}, std::byte{12U}, std::byte{24U}, std::byte{255U}};
constexpr Color kCyanColor{std::byte{112U}, std::byte{220U}, std::byte{255U}, std::byte{255U}};
constexpr Color kSlateColor{std::byte{28U}, std::byte{38U}, std::byte{58U}, std::byte{255U}};
constexpr Color kAmberColor{std::byte{255U}, std::byte{196U}, std::byte{64U}, std::byte{255U}};

int failures = 0;

void Check(const bool condition, const std::string_view message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

[[nodiscard]] ProfileSummary Summary(const std::uint16_t ordinal, std::string name)
{
    std::array<std::uint8_t, 16U> bytes{};
    bytes[14] = static_cast<std::uint8_t>((ordinal >> 8U) & 0xffU);
    bytes[15] = static_cast<std::uint8_t>(ordinal & 0xffU);
    return ProfileSummary{
        .id = ProfileId::FromBytes(bytes),
        .metadata =
            ProfileMetadata{
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

[[nodiscard]] constexpr FrontEndInputEdges InputFromMask(const std::uint8_t mask) noexcept
{
    return FrontEndInputEdges{
        .primary_pressed = (mask & 1U) != 0U,
        .previous_pressed = (mask & 2U) != 0U,
        .next_pressed = (mask & 4U) != 0U,
        .cancel_pressed = (mask & 8U) != 0U,
    };
}

// Independent table-style oracle. It deliberately does not call a production
// validity helper or use production navigation arithmetic.
[[nodiscard]] constexpr FrontEndReduction ReferenceReduce(
    FrontEndState state, const FrontEndInputEdges input, const std::uint8_t visible_profile_slots) noexcept
{
    const auto mode_byte = static_cast<std::uint8_t>(state.mode);
    const auto row_byte = static_cast<std::uint8_t>(state.selected_main_row);
    const auto profile_slot_byte = static_cast<std::uint8_t>(state.selected_profile_slot);
    if (mode_byte > 4U || row_byte > 3U || profile_slot_byte > 2U)
    {
        return FrontEndReduction{.state = omega::app::InitialFrontEndState()};
    }

    if (input.cancel_pressed)
    {
        if (state.mode == FrontEndMode::Main)
            return FrontEndReduction{.state = state};
        constexpr std::array returned_rows{
            FrontEndMainRow::StartDiagnostic,
            FrontEndMainRow::Profiles,
            FrontEndMainRow::StartDiagnostic,
            FrontEndMainRow::Controls,
            FrontEndMainRow::AssetTopology,
        };
        state.mode = FrontEndMode::Main;
        state.selected_main_row = returned_rows[mode_byte];
        state.selected_profile_slot = FrontEndProfileSlot::First;
        return FrontEndReduction{.state = state};
    }

    const std::uint8_t selectable_profiles = visible_profile_slots > 3U ? 3U : visible_profile_slots;
    const bool profile_slot_is_selectable =
        state.mode != FrontEndMode::Profiles || profile_slot_byte < selectable_profiles;
    if (!profile_slot_is_selectable)
        state.selected_profile_slot = FrontEndProfileSlot::First;

    if (input.primary_pressed)
    {
        if (state.mode == FrontEndMode::Profiles)
        {
            if (!profile_slot_is_selectable)
            {
                return FrontEndReduction{.state = FrontEndState{
                                             .mode = FrontEndMode::Main,
                                             .selected_main_row = FrontEndMainRow::Profiles,
                                             .selected_profile_slot = FrontEndProfileSlot::First,
                                         }};
            }
            return FrontEndReduction{
                .state = FrontEndState{
                    .mode = FrontEndMode::Main,
                    .selected_main_row = FrontEndMainRow::Profiles,
                    .selected_profile_slot = FrontEndProfileSlot::First,
                },
                .command = FrontEndCommand{
                    .type = FrontEndCommandType::SetActiveProfile,
                    .profile_slot = state.selected_profile_slot,
                },
            };
        }

        constexpr std::array entered_modes{
            FrontEndMode::DiagnosticPlay,
            FrontEndMode::Profiles,
            FrontEndMode::Controls,
            FrontEndMode::AssetTopology,
        };
        if (state.mode == FrontEndMode::Main)
        {
            state.mode = entered_modes[row_byte];
            state.selected_profile_slot = FrontEndProfileSlot::First;
        }
        else
        {
            constexpr std::array returned_rows{
                FrontEndMainRow::StartDiagnostic,
                FrontEndMainRow::Profiles,
                FrontEndMainRow::StartDiagnostic,
                FrontEndMainRow::Controls,
                FrontEndMainRow::AssetTopology,
            };
            state.mode = FrontEndMode::Main;
            state.selected_main_row = returned_rows[mode_byte];
            state.selected_profile_slot = FrontEndProfileSlot::First;
        }
        return FrontEndReduction{.state = state};
    }

    if (input.previous_pressed == input.next_pressed)
    {
        return FrontEndReduction{.state = state};
    }

    if (state.mode == FrontEndMode::Profiles)
    {
        if (selectable_profiles == 0U)
            return FrontEndReduction{.state = state};
        constexpr std::array previous_slots{
            FrontEndProfileSlot::First,
            FrontEndProfileSlot::First,
            FrontEndProfileSlot::Second,
        };
        constexpr std::array next_slots{
            FrontEndProfileSlot::Second,
            FrontEndProfileSlot::Third,
            FrontEndProfileSlot::Third,
        };
        const std::size_t slot = static_cast<std::size_t>(state.selected_profile_slot);
        if (input.previous_pressed)
            state.selected_profile_slot = previous_slots[slot];
        else if (slot + 1U < selectable_profiles)
            state.selected_profile_slot = next_slots[slot];
        return FrontEndReduction{.state = state};
    }

    if (state.mode != FrontEndMode::Main)
        return FrontEndReduction{.state = state};

    if (input.previous_pressed)
    {
        constexpr std::array previous_rows{
            FrontEndMainRow::StartDiagnostic,
            FrontEndMainRow::StartDiagnostic,
            FrontEndMainRow::Profiles,
            FrontEndMainRow::Controls,
        };
        state.selected_main_row = previous_rows[row_byte];
    }
    else
    {
        constexpr std::array next_rows{
            FrontEndMainRow::Profiles,
            FrontEndMainRow::Controls,
            FrontEndMainRow::AssetTopology,
            FrontEndMainRow::AssetTopology,
        };
        state.selected_main_row = next_rows[row_byte];
    }
    return FrontEndReduction{.state = state};
}

[[nodiscard]] std::uint64_t Fnv1a64(const std::span<const std::byte> bytes) noexcept
{
    std::uint64_t result = 0xcbf29ce484222325ULL;
    for (const std::byte value : bytes)
    {
        result ^= std::to_integer<std::uint8_t>(value);
        result *= 0x100000001b3ULL;
    }
    return result;
}

[[nodiscard]] Color PixelAt(const DebugImage &image, const std::uint32_t x, const std::uint32_t y)
{
    const std::size_t offset = (static_cast<std::size_t>(y) * image.width + x) * 4U;
    return Color{
        image.rgba8_pixels[offset],
        image.rgba8_pixels[offset + 1U],
        image.rgba8_pixels[offset + 2U],
        image.rgba8_pixels[offset + 3U],
    };
}

[[nodiscard]] bool IsOpaqueCard(const DebugImage &image)
{
    if (image.width != omega::app::kFrontEndImageWidth || image.height != omega::app::kFrontEndImageHeight ||
        image.rgba8_pixels.size() != static_cast<std::size_t>(image.width) * image.height * 4U)
    {
        return false;
    }
    for (std::size_t offset = 3U; offset < image.rgba8_pixels.size(); offset += 4U)
    {
        if (image.rgba8_pixels[offset] != std::byte{255U})
            return false;
    }
    return true;
}

void CheckModelContract()
{
    static_assert(noexcept(omega::app::MakeFrontEndStartupModel(std::declval<std::span<const ProfileSummary>>())));
    static_assert(omega::app::kFrontEndMaximumProfiles == 1'024U);
    static_assert(omega::app::kFrontEndVisibleProfiles == 3U);
    static_assert(omega::app::kFrontEndLabelCells == 24U);
    static_assert(sizeof(FrontEndModelError) == 1U);
    static_assert(std::is_trivially_copyable_v<FrontEndStartupModel>);

    Check(omega::app::FrontEndModelErrorMessage(FrontEndModelError::TooManyProfiles) ==
                  "front-end profile snapshot exceeds the fixed profile limit" &&
              omega::app::FrontEndModelErrorMessage(FrontEndModelError::UnsortedProfiles) ==
                  "front-end profile snapshot is not strictly ID-sorted" &&
              omega::app::FrontEndModelErrorMessage(static_cast<FrontEndModelError>(0xffU)) ==
                  "front-end profile snapshot exceeds the fixed profile limit",
          "the bounded model errors use fixed complete messages and a fixed "
          "fallback");

    const auto empty = omega::app::MakeFrontEndStartupModel({});
    Check(empty && *empty == FrontEndStartupModel{},
          "an empty profile catalog produces the exact zero model without an "
          "implicit profile");

    std::vector<ProfileSummary> summaries;
    summaries.push_back(Summary(1U, "alpha Z9-._/?"));
    summaries.push_back(Summary(2U, std::string("Jos") + "\xc3\xa9" + " " + "\xf0\x9f\x98\x80"));
    summaries.push_back(Summary(3U, "ABCDEFGHIJKLMNOPQRSTUVWX"));
    summaries.push_back(Summary(4U, "ABCDEFGHIJKLMNOPQRSTUVWXY"));
    const std::array original_names{
        summaries[0].metadata.display_name,
        summaries[1].metadata.display_name,
        summaries[2].metadata.display_name,
        summaries[3].metadata.display_name,
    };
    const auto projected = omega::app::MakeFrontEndStartupModel(summaries);
    Check(projected && projected->total_profiles == 4U && projected->visible_profiles == 3U &&
              projected->profiles[0].id == summaries[0].id && projected->profiles[1].id == summaries[1].id &&
              projected->profiles[2].id == summaries[2].id &&
              LabelText(projected->profiles[0].label) == "ALPHA Z9-._/?" &&
              LabelText(projected->profiles[1].label) == "JOS? ?" &&
              LabelText(projected->profiles[2].label) == "ABCDEFGHIJKLMNOPQRSTUVWX" &&
              !projected->profiles[0].label.truncated && !projected->profiles[1].label.truncated &&
              !projected->profiles[2].label.truncated &&
              summaries[0].metadata.display_name == original_names[0] &&
              summaries[1].metadata.display_name == original_names[1] &&
              summaries[2].metadata.display_name == original_names[2] &&
              summaries[3].metadata.display_name == original_names[3],
          "projection uppercases supported ASCII, maps unsupported scalars once, "
          "preserves 24 cells, and never mutates metadata");
    if (projected)
    {
        const FrontEndStartupModel owned = *projected;
        summaries[0].metadata.display_name.assign("MUTATED");
        summaries.clear();
        Check(LabelText(owned.profiles[0].label) == "ALPHA Z9-._/?" &&
                  owned.profiles[0].id == ProfileId::FromBytes(
                                               {0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U,
                                                0U, 0U, 0U, 0U, 0U, 0U, 0U, 1U}) &&
                  owned.total_profiles == 4U && owned.visible_profiles == 3U,
              "the startup model owns fixed IDs and cells after source strings "
              "and summaries are destroyed");
    }

    std::vector<ProfileSummary> malformed;
    malformed.push_back(Summary(1U, std::string{"A"} + "\x80" + "B"));
    malformed.push_back(
        Summary(2U, std::string{"C"} + std::string("\xe2\x82", 2U)));
    malformed.push_back(Summary(3U, std::string{"D"} + "\xe2" + "X"));
    const auto malformed_result = omega::app::MakeFrontEndStartupModel(malformed);
    Check(malformed_result &&
              LabelText(malformed_result->profiles[0].label) == "A?B" &&
              LabelText(malformed_result->profiles[1].label) == "C??" &&
              LabelText(malformed_result->profiles[2].label) == "D?X" &&
              !malformed_result->profiles[0].label.truncated &&
              !malformed_result->profiles[1].label.truncated &&
              !malformed_result->profiles[2].label.truncated,
        "standalone continuation, incomplete scalar, and invalid continuation sequences fail closed one consumed byte at a time");

    std::vector<ProfileSummary> truncation;
    truncation.push_back(Summary(1U, "ABCDEFGHIJKLMNOPQRSTUVWXY"));
    const auto truncated = omega::app::MakeFrontEndStartupModel(truncation);
    Check(truncated && truncated->profiles[0].label.length == 24U &&
              LabelText(truncated->profiles[0].label) == "ABCDEFGHIJKLMNOPQRSTUVWX" &&
              truncated->profiles[0].label.truncated,
          "a 25-scalar name truncates to 24 complete fixed cells and marks the "
          "projection");

    std::vector<ProfileSummary> duplicate;
    duplicate.push_back(Summary(1U, "FIRST"));
    duplicate.push_back(Summary(1U, "DUPLICATE"));
    const auto duplicate_result = omega::app::MakeFrontEndStartupModel(duplicate);
    Check(!duplicate_result && duplicate_result.error() == FrontEndModelError::UnsortedProfiles,
          "duplicate profile IDs fail the strict ascending-order contract");
    std::ranges::reverse(duplicate);
    const auto descending_result = omega::app::MakeFrontEndStartupModel(duplicate);
    Check(!descending_result && descending_result.error() == FrontEndModelError::UnsortedProfiles,
          "nonascending profile IDs fail before projection");

    std::vector<ProfileSummary> maximum;
    maximum.reserve(omega::app::kFrontEndMaximumProfiles + 1U);
    for (std::uint16_t index = 0U; index < omega::app::kFrontEndMaximumProfiles; ++index)
    {
        maximum.push_back(Summary(index, "PROFILE"));
    }
    const auto maximum_result = omega::app::MakeFrontEndStartupModel(maximum);
    Check(maximum_result && maximum_result->total_profiles == 1'024U && maximum_result->visible_profiles == 3U,
          "the exact maximum catalog produces a bounded three-label snapshot");
    maximum.push_back(Summary(1'024U, "TOO MANY"));
    const auto over_limit = omega::app::MakeFrontEndStartupModel(maximum);
    Check(!over_limit && over_limit.error() == FrontEndModelError::TooManyProfiles,
          "the first over-limit catalog fails before any projection");
}

void CheckReducerAndViewContract()
{
    static_assert(omega::app::kFrontEndPrimaryAction == 6U);
    static_assert(omega::app::kFrontEndPreviousAction == 2U);
    static_assert(omega::app::kFrontEndNextAction == 3U);
    static_assert(omega::app::kFrontEndCancelAction == 7U);
    static_assert(omega::app::kFrontEndMainRowCount == 4U);
    static_assert(noexcept(omega::app::ReduceFrontEnd({}, {}, 0U)));
    static_assert(noexcept(omega::app::FrontEndAllowsSimulation({})));
    static_assert(noexcept(omega::app::BuildFrontEndView(
        std::declval<FrontEndState>(), ContentStartupStage::NoContent,
        std::declval<const FrontEndStartupModel &>())));

    Check(omega::app::InitialFrontEndState() ==
                  FrontEndState{
                      .mode = FrontEndMode::Main,
                      .selected_main_row = FrontEndMainRow::StartDiagnostic,
                      .selected_profile_slot = FrontEndProfileSlot::First,
                  } &&
              FrontEndState{} ==
                  FrontEndState{
                      .mode = FrontEndMode::DiagnosticPlay,
                      .selected_main_row = FrontEndMainRow::StartDiagnostic,
                      .selected_profile_slot = FrontEndProfileSlot::First,
                  },
          "explicit startup opens Main while the default remains legacy "
          "DiagnosticPlay");

    bool exhaustive_matches = true;
    bool exhaustive_gate_matches = true;
    constexpr std::array<std::uint8_t, 6U> profile_counts{0U, 1U, 2U, 3U, 4U, 0xffU};
    for (std::uint32_t mode = 0U; mode <= 0xffU; ++mode)
    {
        for (std::uint32_t row = 0U; row <= 0xffU; ++row)
        {
            const FrontEndState state{
                .mode = static_cast<FrontEndMode>(mode),
                .selected_main_row = static_cast<FrontEndMainRow>(row),
            };
            const bool oracle_allows = mode == 2U && row <= 3U;
            exhaustive_gate_matches =
                exhaustive_gate_matches && omega::app::FrontEndAllowsSimulation(state) == oracle_allows;
            for (const std::uint8_t profile_count : profile_counts)
            {
                for (std::uint8_t mask = 0U; mask < 16U; ++mask)
                {
                    exhaustive_matches = exhaustive_matches &&
                                         omega::app::ReduceFrontEnd(state, InputFromMask(mask), profile_count) ==
                                             ReferenceReduce(state, InputFromMask(mask), profile_count);
                }
            }
        }
    }
    Check(exhaustive_matches, "all 6291456 mode/row/count/edge combinations match "
                              "the independent reducer oracle");
    Check(exhaustive_gate_matches, "all 65536 byte-representable states allow "
                                   "simulation only in valid DiagnosticPlay");

    bool exhaustive_slot_matches = true;
    bool exhaustive_slot_gate_matches = true;
    for (std::uint32_t mode = 0U; mode <= 4U; ++mode)
    {
        for (std::uint32_t row = 0U; row <= 3U; ++row)
        {
            for (std::uint32_t profile_slot = 0U; profile_slot <= 0xffU; ++profile_slot)
            {
                const FrontEndState state{
                    .mode = static_cast<FrontEndMode>(mode),
                    .selected_main_row = static_cast<FrontEndMainRow>(row),
                    .selected_profile_slot = static_cast<FrontEndProfileSlot>(profile_slot),
                };
                const bool oracle_allows = mode == 2U && profile_slot <= 2U;
                exhaustive_slot_gate_matches = exhaustive_slot_gate_matches &&
                                               omega::app::FrontEndAllowsSimulation(state) == oracle_allows;
                for (const std::uint8_t profile_count : profile_counts)
                {
                    for (std::uint8_t mask = 0U; mask < 16U; ++mask)
                    {
                        exhaustive_slot_matches = exhaustive_slot_matches &&
                                                  omega::app::ReduceFrontEnd(
                                                      state, InputFromMask(mask), profile_count) ==
                                                      ReferenceReduce(state, InputFromMask(mask), profile_count);
                    }
                }
            }
        }
    }
    Check(exhaustive_slot_matches,
          "all 491520 valid-mode/row and byte-slot/count/edge combinations match the independent oracle");
    Check(exhaustive_slot_gate_matches,
          "every byte-representable profile slot gates simulation only when the complete state is valid");

    const FrontEndState profiles_second{
        .mode = FrontEndMode::Profiles,
        .selected_main_row = FrontEndMainRow::Profiles,
        .selected_profile_slot = FrontEndProfileSlot::Second,
    };
    Check(omega::app::ReduceFrontEnd(
              profiles_second,
              FrontEndInputEdges{.primary_pressed = true, .previous_pressed = true, .next_pressed = true}, 3U) ==
              FrontEndReduction{
                  .state = FrontEndState{
                      .mode = FrontEndMode::Main,
                      .selected_main_row = FrontEndMainRow::Profiles,
                      .selected_profile_slot = FrontEndProfileSlot::First,
                  },
                  .command = FrontEndCommand{
                      .type = FrontEndCommandType::SetActiveProfile,
                      .profile_slot = FrontEndProfileSlot::Second,
                  },
              },
          "profile activation has priority over simultaneous navigation and publishes the pre-navigation slot");

    Check(omega::app::ReduceFrontEnd(
              profiles_second,
              FrontEndInputEdges{
                  .primary_pressed = true,
                  .previous_pressed = true,
                  .next_pressed = true,
                  .cancel_pressed = true,
              },
              3U) ==
              FrontEndReduction{.state = FrontEndState{
                                    .mode = FrontEndMode::Main,
                                    .selected_main_row = FrontEndMainRow::Profiles,
                                    .selected_profile_slot = FrontEndProfileSlot::First,
                                }},
          "cancel has priority over activation and navigation and publishes no profile command");

    constexpr std::array cancel_origins{
        FrontEndState{.mode = FrontEndMode::DiagnosticPlay,
            .selected_main_row = FrontEndMainRow::AssetTopology,
            .selected_profile_slot = FrontEndProfileSlot::Third},
        FrontEndState{.mode = FrontEndMode::Profiles,
            .selected_main_row = FrontEndMainRow::StartDiagnostic,
            .selected_profile_slot = FrontEndProfileSlot::Second},
        FrontEndState{.mode = FrontEndMode::Controls,
            .selected_main_row = FrontEndMainRow::Profiles,
            .selected_profile_slot = FrontEndProfileSlot::Third},
        FrontEndState{.mode = FrontEndMode::AssetTopology,
            .selected_main_row = FrontEndMainRow::Controls,
            .selected_profile_slot = FrontEndProfileSlot::Second},
    };
    constexpr std::array cancel_rows{
        FrontEndMainRow::StartDiagnostic,
        FrontEndMainRow::Profiles,
        FrontEndMainRow::Controls,
        FrontEndMainRow::AssetTopology,
    };
    bool every_modal_cancel_returns_to_its_row = true;
    for (std::size_t index = 0U; index < cancel_origins.size(); ++index)
    {
        every_modal_cancel_returns_to_its_row =
            every_modal_cancel_returns_to_its_row &&
            omega::app::ReduceFrontEnd(cancel_origins[index],
                FrontEndInputEdges{.cancel_pressed = true}, 3U) ==
                FrontEndReduction{.state = FrontEndState{
                                      .mode = FrontEndMode::Main,
                                      .selected_main_row = cancel_rows[index],
                                      .selected_profile_slot = FrontEndProfileSlot::First,
                                  }};
    }
    Check(every_modal_cancel_returns_to_its_row,
        "cancel returns every modal mode to its corresponding Main row without a command");

    const FrontEndState main_controls{
        .mode = FrontEndMode::Main,
        .selected_main_row = FrontEndMainRow::Controls,
        .selected_profile_slot = FrontEndProfileSlot::Second,
    };
    Check(omega::app::ReduceFrontEnd(main_controls,
              FrontEndInputEdges{
                  .primary_pressed = true,
                  .previous_pressed = true,
                  .next_pressed = true,
                  .cancel_pressed = true,
              },
              3U) == FrontEndReduction{.state = main_controls},
        "Main cancel is inert and consumes simultaneous activation and navigation edges");

    bool empty_is_fail_closed = true;
    const FrontEndState empty_profiles{
        .mode = FrontEndMode::Profiles,
        .selected_main_row = FrontEndMainRow::Profiles,
        .selected_profile_slot = FrontEndProfileSlot::First,
    };
    for (std::uint8_t mask = 0U; mask < 16U; ++mask)
    {
        const FrontEndReduction reduced =
            omega::app::ReduceFrontEnd(empty_profiles, InputFromMask(mask), 0U);
        const FrontEndState expected_state = (mask & 9U) != 0U
                                                 ? FrontEndState{
                                                       .mode = FrontEndMode::Main,
                                                       .selected_main_row = FrontEndMainRow::Profiles,
                                                       .selected_profile_slot = FrontEndProfileSlot::First,
                                                   }
                                                 : empty_profiles;
        empty_is_fail_closed = empty_is_fail_closed && reduced.state == expected_state &&
                               reduced.command == FrontEndCommand{};
    }
    Check(empty_is_fail_closed,
          "all empty-profile action combinations publish no command; navigation is inert and primary or cancel returns");

    Check(omega::app::ReduceFrontEnd(
              FrontEndState{
                  .mode = FrontEndMode::Profiles,
                  .selected_main_row = FrontEndMainRow::Profiles,
                  .selected_profile_slot = FrontEndProfileSlot::Third,
              },
              FrontEndInputEdges{.next_pressed = true}, 0xffU)
                  .state.selected_profile_slot == FrontEndProfileSlot::Third,
          "an adversarial count above three cannot navigate beyond the third displayed slot");

    Check(omega::app::ReduceFrontEnd(
              FrontEndState{
                  .mode = FrontEndMode::Profiles,
                  .selected_main_row = FrontEndMainRow::Profiles,
                  .selected_profile_slot = FrontEndProfileSlot::Third,
              },
              FrontEndInputEdges{.primary_pressed = true}, 1U) ==
              FrontEndReduction{.state = FrontEndState{
                                    .mode = FrontEndMode::Main,
                                    .selected_main_row = FrontEndMainRow::Profiles,
                                    .selected_profile_slot = FrontEndProfileSlot::First,
                                }},
          "a stale highlighted slot outside the current count returns safely without selecting a different profile");

    FrontEndStartupModel model{
        .total_profiles = 4U,
        .visible_profiles = 3U,
    };
    model.profiles[0].label.cells[0] = 'A';
    model.profiles[0].label.length = 1U;
    const auto valid_view = omega::app::BuildFrontEndView(
        FrontEndState{
            .mode = FrontEndMode::Profiles,
            .selected_main_row = FrontEndMainRow::Profiles,
        },
        ContentStartupStage::LevelContent, model);
    Check(valid_view ==
              omega::app::FrontEndView{
                  .mode = FrontEndMode::Profiles,
                  .selected_main_row = FrontEndMainRow::Profiles,
                  .selected_profile_slot = FrontEndProfileSlot::First,
                  .content_stage = ContentStartupStage::LevelContent,
                  .profiles = model,
              },
          "the view copies valid state, content classification, and the complete "
          "fixed model");
    model.profiles[0].label.cells[0] = 'Z';
    Check(valid_view.profiles.profiles[0].label.cells[0] == 'A', "a front-end view owns its model copy");

    const auto invalid_view = omega::app::BuildFrontEndView(
        FrontEndState{
            .mode = static_cast<FrontEndMode>(0xffU),
            .selected_main_row = static_cast<FrontEndMainRow>(0xffU),
        },
        static_cast<ContentStartupStage>(0xffU), model);
    Check(invalid_view.mode == FrontEndMode::Main &&
              invalid_view.selected_main_row == FrontEndMainRow::StartDiagnostic &&
              invalid_view.content_stage == ContentStartupStage::NoContent && invalid_view.profiles == model,
          "an invalid view request fails closed without discarding its immutable "
          "profile snapshot");
}

void CheckRasterContract()
{
    FrontEndStartupModel profiles{
        .total_profiles = 5U,
        .visible_profiles = 3U,
    };
    constexpr std::array<std::string_view, 3U> labels{"ALPHA", "JOS?", "ABCDEFGHIJKLMNOPQRSTUVWX"};
    for (std::size_t index = 0U; index < labels.size(); ++index)
    {
        profiles.profiles[index].label.length = static_cast<std::uint8_t>(labels[index].size());
        std::ranges::copy(labels[index], profiles.profiles[index].label.cells.begin());
    }
    profiles.profiles[2].label.truncated = true;

    const DebugImage main_none = omega::app::BuildProjectFrontEndMainImage(ContentStartupStage::NoContent, 0U);
    const DebugImage main_data = omega::app::BuildProjectFrontEndMainImage(ContentStartupStage::DataMounted, 3U);
    const DebugImage main_level = omega::app::BuildProjectFrontEndMainImage(ContentStartupStage::LevelContent, 1'024U);
    const DebugImage empty_profiles = omega::app::BuildProjectFrontEndProfilesImage({});
    const DebugImage populated_profiles = omega::app::BuildProjectFrontEndProfilesImage(profiles);
    const DebugImage diagnostic = omega::app::BuildProjectFrontEndDiagnosticPlayImage();
    const DebugImage controls = omega::app::BuildProjectFrontEndControlsImage();

    const std::array<const DebugImage *, 7U> cards{&main_none,          &main_data,  &main_level, &empty_profiles,
                                                   &populated_profiles, &diagnostic, &controls};
    Check(std::ranges::all_of(cards, [](const DebugImage *image) { return IsOpaqueCard(*image); }),
          "all project front-end cards are tightly packed opaque 128x72 RGBA8 "
          "images");
    Check(PixelAt(main_none, 0U, 0U) == kCyanColor && PixelAt(main_none, 4U, 4U) == kBackgroundColor &&
              PixelAt(main_none, 8U, 8U) == kSlateColor && PixelAt(main_none, 40U, 12U) == kAmberColor &&
              PixelAt(main_none, 8U, 28U) == kCyanColor && PixelAt(main_none, 13U, 28U) == kSlateColor &&
              PixelAt(main_none, 8U, 58U) == kCyanColor && PixelAt(main_none, 13U, 58U) == kSlateColor,
          "main-card probes cover the opaque frame, header, and all four-row "
          "geometry");
    Check(PixelAt(main_none, 41U, 23U) == kCyanColor &&
              PixelAt(main_data, 41U, 22U) == kCyanColor &&
              PixelAt(main_level, 42U, 23U) == kBackgroundColor,
        "content-stage probes independently distinguish NONE, DATA, and LEVEL labels");
    Check(PixelAt(empty_profiles, 8U, 24U) == kSlateColor &&
              PixelAt(empty_profiles, 28U, 38U) == kCyanColor &&
              PixelAt(empty_profiles, 8U, 59U) == kSlateColor &&
              PixelAt(populated_profiles, 17U, 28U) == kCyanColor &&
              PixelAt(populated_profiles, 113U, 48U) == kCyanColor,
          "profile-card probes cover empty/populated panels and the scalar-safe "
          "truncation marker");
    Check(PixelAt(diagnostic, 8U, 34U) == kSlateColor &&
              PixelAt(diagnostic, 36U, 40U) == kCyanColor &&
              PixelAt(controls, 8U, 24U) == kSlateColor &&
              PixelAt(controls, 12U, 25U) == kCyanColor,
        "diagnostic and controls probes cover their distinct project-authored panels and labels");

    const std::array hashes{
        Fnv1a64(main_none.pixels()),      Fnv1a64(main_data.pixels()),          Fnv1a64(main_level.pixels()),
        Fnv1a64(empty_profiles.pixels()), Fnv1a64(populated_profiles.pixels()), Fnv1a64(diagnostic.pixels()),
        Fnv1a64(controls.pixels()),
    };
    std::cout << "front-end card FNV-1a-64:";
    for (const std::uint64_t hash : hashes)
    {
        std::cout << " 0x" << std::hex << std::setfill('0') << std::setw(16) << hash;
    }
    std::cout << std::dec << '\n';
    // Frozen only after a candidate run's seven exact 36,864-byte RGBA dumps
    // were checked by a separate Python FNV-1a implementation, SHA-256, four-
    // color histograms, and the independently selected semantic probes above.
    constexpr std::array<std::uint64_t, 7U> expected_hashes{
        0x177b53f8cad1acdeULL,
        0x50cbdb38858d5d32ULL,
        0xdd47380e14da12f2ULL,
        0xea15c2933b6cfffdULL,
        0xe26182d60b0bd82dULL,
        0x37f823d27a4cb3ceULL,
        0xcfa7cc57696aae0aULL,
    };
    Check(hashes == expected_hashes, "all complete project front-end cards match "
                                     "their independently frozen hashes");

    const auto topology_first = omega::app::BuildProjectFrontEndAssetTopologyImage();
    const auto topology_second = omega::app::BuildProjectFrontEndAssetTopologyImage();
    Check(topology_first && topology_second && topology_first->width == 96U && topology_first->height == 32U &&
              topology_first->rgba8_pixels.size() == 12'288U &&
              topology_first->rgba8_pixels == topology_second->rgba8_pixels &&
              topology_first->rgba8_pixels.data() != topology_second->rgba8_pixels.data() &&
              Fnv1a64(topology_first->pixels()) == 0xb56c8db088c5a9feULL,
          "the existing project topology card remains deterministic and "
          "independently owned");

    const DebugImage second_empty = omega::app::BuildProjectFrontEndProfilesImage({});
    Check(second_empty.rgba8_pixels == empty_profiles.rgba8_pixels &&
              second_empty.rgba8_pixels.data() != empty_profiles.rgba8_pixels.data(),
          "repeated profile-card builds own distinct byte-identical storage");
}
} // namespace

int main()
{
    CheckModelContract();
    CheckReducerAndViewContract();
    CheckRasterContract();

    if (failures != 0)
    {
        std::cerr << failures << " front-end test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "front-end tests passed\n";
    return EXIT_SUCCESS;
}
