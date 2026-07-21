#include "front_end.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
using omega::app::FrontEndCapabilities;
using omega::app::FrontEndCommand;
using omega::app::FrontEndCommandType;
using omega::app::FrontEndInputEdges;
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
    FrontEndState state, const FrontEndInputEdges input, const std::uint8_t visible_profile_slots,
    const FrontEndCapabilities capabilities = {}) noexcept
{
    const auto mode_byte = static_cast<std::uint8_t>(state.mode);
    const auto row_byte = static_cast<std::uint8_t>(state.selected_main_row);
    const auto profile_slot_byte = static_cast<std::uint8_t>(state.selected_profile_slot);
    if (mode_byte > 4U || row_byte > 3U || profile_slot_byte > 2U)
    {
        return FrontEndReduction{.state = omega::app::InitialFrontEndState()};
    }
    if (state.mode == FrontEndMode::DiagnosticPlay &&
        !capabilities.can_start_diagnostic_campaign)
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
            if (selectable_profiles == 0U && capabilities.can_create_first_profile)
            {
                return FrontEndReduction{
                    .state = state,
                    .command = FrontEndCommand{
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
            if (state.selected_main_row == FrontEndMainRow::StartDiagnostic &&
                !capabilities.can_start_diagnostic_campaign)
            {
                return FrontEndReduction{.state = state};
            }
            state.mode = entered_modes[row_byte];
            state.selected_profile_slot = FrontEndProfileSlot::First;
            if (state.selected_main_row == FrontEndMainRow::StartDiagnostic)
            {
                return FrontEndReduction{
                    .state = state,
                    .command = FrontEndCommand{
                        .type = FrontEndCommandType::StartDiagnosticCampaign,
                        .profile_slot = FrontEndProfileSlot::First,
                    },
                };
            }
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

// Independent gate oracle. It states the confirmation-gate deviations as
// post-conditions over the table oracle above instead of calling any production
// helper, so a production gate regression cannot hide inside it.
[[nodiscard]] constexpr FrontEndReduction ReferenceReduceWithGate(
    const FrontEndState state, const FrontEndInputEdges input, const std::uint8_t visible_profile_slots,
    const FrontEndCapabilities capabilities, const bool active_profile_is_confirmed) noexcept
{
    const auto mode_byte = static_cast<std::uint8_t>(state.mode);
    const auto row_byte = static_cast<std::uint8_t>(state.selected_main_row);
    const auto profile_slot_byte = static_cast<std::uint8_t>(state.selected_profile_slot);
    const bool state_is_valid = mode_byte <= 4U && row_byte <= 3U && profile_slot_byte <= 2U;
    const bool gate_is_satisfied =
        !capabilities.requires_active_profile_for_diagnostic_play || active_profile_is_confirmed;
    if (state_is_valid && !gate_is_satisfied)
    {
        if (mode_byte == 2U)
            return FrontEndReduction{.state = omega::app::InitialFrontEndState()};
        if (mode_byte == 0U && row_byte == 0U && input.primary_pressed && !input.cancel_pressed)
            return FrontEndReduction{.state = state};
    }
    return ReferenceReduce(state, input, visible_profile_slots, capabilities);
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

[[nodiscard]] constexpr FrontEndState ReferenceProjectFrontEndStartupState(
    const std::uint16_t total_profiles, const std::uint8_t visible_profiles,
    const FrontEndCapabilities capabilities) noexcept
{
    constexpr FrontEndState kProfilesState{
        .mode = FrontEndMode::Profiles,
        .selected_main_row = FrontEndMainRow::Profiles,
        .selected_profile_slot = FrontEndProfileSlot::First,
    };
    const bool valid_counts = total_profiles <= omega::app::kFrontEndMaximumProfiles &&
                              visible_profiles <= omega::app::kFrontEndVisibleProfiles &&
                              visible_profiles <= total_profiles &&
                              ((total_profiles == 0U && visible_profiles == 0U) ||
                               (total_profiles != 0U && visible_profiles != 0U));
    if (!valid_counts || (total_profiles == 0U && !capabilities.can_create_first_profile))
        return omega::app::InitialFrontEndState();
    return kProfilesState;
}

[[nodiscard]] constexpr bool StartupPlannerMatchesVisibleBoundaries(
    const std::uint16_t total_profiles) noexcept
{
    constexpr std::array capabilities{false, true};
    for (std::uint16_t visible_value = 0U; visible_value <= 0xffU; ++visible_value)
    {
        const auto visible_profiles = static_cast<std::uint8_t>(visible_value);
        for (const bool can_create : capabilities)
        {
            const FrontEndCapabilities capability{.can_create_first_profile = can_create};
            if (omega::app::PlanProjectFrontEndStartupState(
                    total_profiles, visible_profiles, capability) !=
                ReferenceProjectFrontEndStartupState(total_profiles, visible_profiles, capability))
            {
                return false;
            }
        }
    }
    return true;
}

void CheckStartupStatePlannerContract()
{
    constexpr FrontEndState kInitialState{
        .mode = FrontEndMode::Main,
        .selected_main_row = FrontEndMainRow::StartDiagnostic,
        .selected_profile_slot = FrontEndProfileSlot::First,
    };
    constexpr FrontEndState kProfilesState{
        .mode = FrontEndMode::Profiles,
        .selected_main_row = FrontEndMainRow::Profiles,
        .selected_profile_slot = FrontEndProfileSlot::First,
    };
    constexpr FrontEndCapabilities kCreationEnabled{.can_create_first_profile = true};

    static_assert(noexcept(omega::app::PlanProjectFrontEndStartupState(0U, 0U, {})));
    static_assert(omega::app::PlanProjectFrontEndStartupState(0U, 0U, {}) == kInitialState);
    static_assert(omega::app::PlanProjectFrontEndStartupState(0U, 0U, kCreationEnabled) == kProfilesState);
    static_assert(omega::app::PlanProjectFrontEndStartupState(1U, 1U, {}) == kProfilesState);
    static_assert(omega::app::PlanProjectFrontEndStartupState(1'024U, 3U, kCreationEnabled) == kProfilesState);
    static_assert(omega::app::PlanProjectFrontEndStartupState(1U, 0U, kCreationEnabled) == kInitialState);
    static_assert(omega::app::PlanProjectFrontEndStartupState(0U, 1U, kCreationEnabled) == kInitialState);
    static_assert(omega::app::PlanProjectFrontEndStartupState(1'025U, 3U, kCreationEnabled) == kInitialState);
    static_assert(StartupPlannerMatchesVisibleBoundaries(0U));
    static_assert(StartupPlannerMatchesVisibleBoundaries(1U));
    static_assert(StartupPlannerMatchesVisibleBoundaries(3U));
    static_assert(StartupPlannerMatchesVisibleBoundaries(4U));
    static_assert(StartupPlannerMatchesVisibleBoundaries(1'024U));
    static_assert(StartupPlannerMatchesVisibleBoundaries(1'025U));
    static_assert(StartupPlannerMatchesVisibleBoundaries(65'535U));

    bool all_boundary_results_match = true;
    constexpr std::array<std::uint16_t, 7U> totals{0U, 1U, 3U, 4U, 1'024U, 1'025U, 65'535U};
    constexpr std::array capabilities_under_test{false, true};
    for (const std::uint16_t total_profiles : totals)
    {
        for (std::uint16_t visible_value = 0U; visible_value <= 0xffU; ++visible_value)
        {
            const auto visible_profiles = static_cast<std::uint8_t>(visible_value);
            for (const bool can_create : capabilities_under_test)
            {
                const FrontEndCapabilities capabilities{.can_create_first_profile = can_create};
                all_boundary_results_match =
                    all_boundary_results_match &&
                    omega::app::PlanProjectFrontEndStartupState(
                        total_profiles, visible_profiles, capabilities) ==
                        ReferenceProjectFrontEndStartupState(total_profiles, visible_profiles, capabilities);
            }
        }
    }
    Check(all_boundary_results_match,
          "all 3584 total/visible/capability boundary combinations return the exact fail-closed or Profiles state");

    std::uint16_t total_profiles = 4U;
    std::uint8_t visible_profiles = 3U;
    FrontEndCapabilities capabilities{.can_create_first_profile = true};
    const std::uint16_t original_total = total_profiles;
    const std::uint8_t original_visible = visible_profiles;
    const FrontEndCapabilities original_capabilities = capabilities;
    const FrontEndState planned = omega::app::PlanProjectFrontEndStartupState(
        total_profiles, visible_profiles, capabilities);
    Check(planned == kProfilesState && total_profiles == original_total &&
              visible_profiles == original_visible && capabilities == original_capabilities,
          "startup planning returns the exact Profiles state without mutating caller-owned inputs");
}

void CheckReducerAndViewContract()
{
    static_assert(omega::app::kFrontEndPrimaryAction == 6U);
    static_assert(omega::app::kFrontEndPreviousAction == 2U);
    static_assert(omega::app::kFrontEndNextAction == 3U);
    static_assert(omega::app::kFrontEndCancelAction == 7U);
    static_assert(omega::app::kFrontEndMainRowCount == 4U);
    static_assert(omega::app::kFrontEndFirstProfileDisplayName == "PROFILE 1");
    static_assert(static_cast<std::uint8_t>(FrontEndCommandType::None) == 0U);
    static_assert(static_cast<std::uint8_t>(FrontEndCommandType::SetActiveProfile) == 1U);
    static_assert(static_cast<std::uint8_t>(FrontEndCommandType::CreateFirstProfile) == 2U);
    static_assert(static_cast<std::uint8_t>(FrontEndCommandType::StartDiagnosticCampaign) == 3U);
    static_assert(static_cast<std::uint8_t>(FrontEndMode::BriefingRoom) == 6U);
    static_assert(std::is_trivially_copyable_v<FrontEndCapabilities>);
    static_assert(FrontEndCapabilities{} == FrontEndCapabilities{
        .can_create_first_profile = false,
        .can_start_diagnostic_campaign = false,
        .requires_active_profile_for_diagnostic_play = false,
    });
    static_assert(noexcept(omega::app::ReduceFrontEnd({}, {}, 0U)));
    static_assert(noexcept(omega::app::ReduceFrontEnd(
        {}, {}, 0U, FrontEndCapabilities{.can_create_first_profile = true})));
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

    bool exhaustive_disabled_matches = true;
    bool exhaustive_enabled_matches = true;
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
            constexpr FrontEndCapabilities start_enabled{
                .can_start_diagnostic_campaign = true,
            };
            exhaustive_gate_matches =
                exhaustive_gate_matches &&
                !omega::app::FrontEndAllowsSimulation(state) &&
                omega::app::FrontEndAllowsSimulation(state, start_enabled) == oracle_allows;
            for (const std::uint8_t profile_count : profile_counts)
            {
                for (std::uint8_t mask = 0U; mask < 16U; ++mask)
                {
                    exhaustive_disabled_matches =
                        exhaustive_disabled_matches &&
                        omega::app::ReduceFrontEnd(state, InputFromMask(mask), profile_count) ==
                            ReferenceReduce(state, InputFromMask(mask), profile_count);
                    constexpr FrontEndCapabilities enabled{.can_create_first_profile = true};
                    exhaustive_enabled_matches =
                        exhaustive_enabled_matches &&
                        omega::app::ReduceFrontEnd(state, InputFromMask(mask), profile_count, enabled) ==
                            ReferenceReduce(state, InputFromMask(mask), profile_count, enabled);
                }
            }
        }
    }
    Check(exhaustive_disabled_matches,
          "all 6291456 mode/row/count/edge combinations with the default-disabled "
          "capability match the independent reducer oracle");
    Check(exhaustive_enabled_matches,
          "all 6291456 mode/row/count/edge combinations with first-profile "
          "creation enabled match the independent reducer oracle");
    Check(exhaustive_gate_matches, "all 65536 byte-representable states allow "
                                   "simulation only in valid DiagnosticPlay");

    bool exhaustive_slot_disabled_matches = true;
    bool exhaustive_slot_enabled_matches = true;
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
                constexpr FrontEndCapabilities start_enabled{
                    .can_start_diagnostic_campaign = true,
                };
                exhaustive_slot_gate_matches = exhaustive_slot_gate_matches &&
                                               !omega::app::FrontEndAllowsSimulation(state) &&
                                               omega::app::FrontEndAllowsSimulation(
                                                   state, start_enabled) == oracle_allows;
                for (const std::uint8_t profile_count : profile_counts)
                {
                    for (std::uint8_t mask = 0U; mask < 16U; ++mask)
                    {
                        exhaustive_slot_disabled_matches =
                            exhaustive_slot_disabled_matches &&
                            omega::app::ReduceFrontEnd(state, InputFromMask(mask), profile_count) ==
                                ReferenceReduce(state, InputFromMask(mask), profile_count);
                        constexpr FrontEndCapabilities enabled{.can_create_first_profile = true};
                        exhaustive_slot_enabled_matches =
                            exhaustive_slot_enabled_matches &&
                            omega::app::ReduceFrontEnd(state, InputFromMask(mask), profile_count, enabled) ==
                                ReferenceReduce(state, InputFromMask(mask), profile_count, enabled);
                    }
                }
            }
        }
    }
    Check(exhaustive_slot_disabled_matches,
          "all 491520 valid-mode/row and byte-slot/count/edge combinations with "
          "the default-disabled capability match the independent oracle");
    Check(exhaustive_slot_enabled_matches,
          "all 491520 valid-mode/row and byte-slot/count/edge combinations with "
          "first-profile creation enabled match the independent oracle");
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
              omega::app::InitialFrontEndState(),
              FrontEndInputEdges{
                  .primary_pressed = true,
                  .previous_pressed = true,
                  .next_pressed = true,
              },
              3U,
              FrontEndCapabilities{
                  .can_create_first_profile = false,
                  .can_start_diagnostic_campaign = true,
              }) ==
              FrontEndReduction{
                  .state = FrontEndState{
                      .mode = FrontEndMode::DiagnosticPlay,
                      .selected_main_row = FrontEndMainRow::StartDiagnostic,
                      .selected_profile_slot = FrontEndProfileSlot::First,
                  },
                  .command = FrontEndCommand{
                      .type = FrontEndCommandType::StartDiagnosticCampaign,
                      .profile_slot = FrontEndProfileSlot::First,
                  },
              },
          "diagnostic activation has priority over navigation and publishes one project-owned start command");

    Check(omega::app::ReduceFrontEnd(
              omega::app::InitialFrontEndState(),
              FrontEndInputEdges{.primary_pressed = true}, 3U) ==
              FrontEndReduction{.state = omega::app::InitialFrontEndState()},
          "diagnostic activation is inert while the explicit session capability is closed");

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

    constexpr FrontEndCapabilities creation_enabled{.can_create_first_profile = true};
    bool creatable_empty_edges_are_exact = true;
    for (std::uint8_t mask = 0U; mask < 16U; ++mask)
    {
        const FrontEndReduction reduced =
            omega::app::ReduceFrontEnd(empty_profiles, InputFromMask(mask), 0U, creation_enabled);
        FrontEndReduction expected{.state = empty_profiles};
        if ((mask & 8U) != 0U)
        {
            expected.state = FrontEndState{
                .mode = FrontEndMode::Main,
                .selected_main_row = FrontEndMainRow::Profiles,
                .selected_profile_slot = FrontEndProfileSlot::First,
            };
        }
        else if ((mask & 1U) != 0U)
        {
            expected.command = FrontEndCommand{
                .type = FrontEndCommandType::CreateFirstProfile,
                .profile_slot = FrontEndProfileSlot::First,
            };
        }
        creatable_empty_edges_are_exact = creatable_empty_edges_are_exact && reduced == expected;
    }
    Check(creatable_empty_edges_are_exact,
          "empty creation is press-edge-only, remains in Profiles, outranks "
          "navigation, and remains subordinate to cancel");

    const FrontEndReduction create_edge = omega::app::ReduceFrontEnd(
        empty_profiles, FrontEndInputEdges{.primary_pressed = true}, 0U, creation_enabled);
    Check(create_edge.command.type == FrontEndCommandType::CreateFirstProfile &&
              omega::app::ReduceFrontEnd(create_edge.state, {}, 0U, creation_enabled).command == FrontEndCommand{},
          "a held primary level cannot republish creation without another routed press edge");

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

// The gate's only authorization input is an already-confirmed identifier
// resolved against the bounded model, so these checks never construct a second
// selection value: production has none to construct.
void CheckActiveProfileGateContract()
{
    const std::array summaries{Summary(1U, "ALPHA"), Summary(2U, "BETA"), Summary(3U, "GAMMA"),
                               Summary(4U, "DELTA")};
    const auto built = omega::app::MakeFrontEndStartupModel(summaries);
    Check(built.has_value(), "the gate fixture builds a four-profile bounded model");
    if (!built)
        return;
    const FrontEndStartupModel model = *built;

    const std::optional<ProfileId> first = summaries[0].id;
    const std::optional<ProfileId> second = summaries[1].id;
    const std::optional<ProfileId> third = summaries[2].id;
    const std::optional<ProfileId> beyond_visible = summaries[3].id;
    const std::optional<ProfileId> uncatalogued = Summary(9U, "OMEGA").id;

    Check(omega::app::FrontEndConfirmedProfileSlot(model, first) == FrontEndProfileSlot::First &&
              omega::app::FrontEndConfirmedProfileSlot(model, second) == FrontEndProfileSlot::Second &&
              omega::app::FrontEndConfirmedProfileSlot(model, third) == FrontEndProfileSlot::Third,
          "every visible position resolves the identifier the model actually holds");
    Check(!omega::app::FrontEndConfirmedProfileSlot(model, std::nullopt) &&
              !omega::app::FrontEndConfirmedProfileSlot(model, beyond_visible) &&
              !omega::app::FrontEndConfirmedProfileSlot(model, uncatalogued),
          "an absent, invisible, or uncatalogued identifier resolves to no position");

    FrontEndStartupModel shrunk = model;
    shrunk.visible_profiles = 1U;
    Check(omega::app::FrontEndConfirmedProfileSlot(shrunk, first) == FrontEndProfileSlot::First &&
              !omega::app::FrontEndConfirmedProfileSlot(shrunk, second) &&
              !omega::app::FrontEndConfirmedProfileSlot(shrunk, third),
          "a position outside the current visible count is stale and resolves to nothing");

    FrontEndStartupModel emptied = model;
    emptied.total_profiles = 0U;
    Check(!omega::app::FrontEndConfirmedProfileSlot(emptied, first) &&
              !omega::app::FrontEndConfirmedProfileSlot(emptied, second),
          "a zero total count leaves every retained position stale");

    FrontEndStartupModel unpopulated = model;
    unpopulated.profiles[1].id.reset();
    Check(!omega::app::FrontEndConfirmedProfileSlot(unpopulated, second) &&
              omega::app::FrontEndConfirmedProfileSlot(unpopulated, third) == FrontEndProfileSlot::Third,
          "an unpopulated position resolves nothing without disturbing its neighbours");

    FrontEndStartupModel adversarial = model;
    adversarial.visible_profiles = 0xffU;
    adversarial.total_profiles = 0xffffU;
    Check(omega::app::FrontEndConfirmedProfileSlot(adversarial, third) == FrontEndProfileSlot::Third &&
              !omega::app::FrontEndConfirmedProfileSlot(adversarial, beyond_visible),
          "adversarial counts cannot resolve past the three fixed positions");

    constexpr FrontEndCapabilities start_enabled{
        .can_start_diagnostic_campaign = true,
    };
    constexpr FrontEndCapabilities gate_enabled{
        .can_start_diagnostic_campaign = true,
        .requires_active_profile_for_diagnostic_play = true,
    };
    Check(omega::app::FrontEndSatisfiesDiagnosticPlayGate({}, false) &&
              omega::app::FrontEndSatisfiesDiagnosticPlayGate({}, true) &&
              !omega::app::FrontEndSatisfiesDiagnosticPlayGate(gate_enabled, false) &&
              omega::app::FrontEndSatisfiesDiagnosticPlayGate(gate_enabled, true),
          "the default capability leaves the gate open and the explicit capability closes it");
    Check(omega::app::FrontEndHasConfirmedActiveProfile(model, second) &&
              !omega::app::FrontEndHasConfirmedActiveProfile(model, beyond_visible) &&
              !omega::app::FrontEndHasConfirmedActiveProfile(model, std::nullopt),
          "the only gate input is a confirmed identifier the model still resolves");

    constexpr FrontEndState diagnostic_play{
        .mode = FrontEndMode::DiagnosticPlay,
        .selected_main_row = FrontEndMainRow::StartDiagnostic,
        .selected_profile_slot = FrontEndProfileSlot::First,
    };
    Check(!omega::app::FrontEndAllowsSimulation(diagnostic_play) &&
               omega::app::FrontEndAllowsSimulation(diagnostic_play, start_enabled, false) &&
               !omega::app::FrontEndAllowsSimulation(diagnostic_play, gate_enabled, false) &&
               omega::app::FrontEndAllowsSimulation(diagnostic_play, gate_enabled, true),
          "simulation requires explicit start support and then any enabled confirmation gate");

    bool unconfirmed_diagnostic_fails_closed = true;
    for (std::uint8_t mask = 0U; mask < 16U; ++mask)
    {
        unconfirmed_diagnostic_fails_closed =
            unconfirmed_diagnostic_fails_closed &&
            omega::app::ReduceFrontEnd(diagnostic_play, InputFromMask(mask), 3U, gate_enabled, false) ==
                FrontEndReduction{.state = omega::app::InitialFrontEndState()};
    }
    Check(unconfirmed_diagnostic_fails_closed,
          "an already-entered diagnostic play fails closed on every edge once its "
          "confirmation stops resolving");

    constexpr FrontEndState main_start_diagnostic{
        .mode = FrontEndMode::Main,
        .selected_main_row = FrontEndMainRow::StartDiagnostic,
        .selected_profile_slot = FrontEndProfileSlot::First,
    };
    Check(omega::app::ReduceFrontEnd(main_start_diagnostic, FrontEndInputEdges{.primary_pressed = true}, 3U,
              gate_enabled, false) ==
              FrontEndReduction{.state = main_start_diagnostic},
          "the supported but unauthorized diagnostic row is inert and publishes no command");
    Check(omega::app::ReduceFrontEnd(main_start_diagnostic, FrontEndInputEdges{.primary_pressed = true}, 3U,
              gate_enabled, true) ==
              FrontEndReduction{
                  .state = diagnostic_play,
                  .command = FrontEndCommand{
                      .type = FrontEndCommandType::StartDiagnosticCampaign,
                      .profile_slot = FrontEndProfileSlot::First,
                  },
              },
          "a confirmation publishes the typed diagnostic start and enters play");

    constexpr FrontEndState profiles_second{
        .mode = FrontEndMode::Profiles,
        .selected_main_row = FrontEndMainRow::Profiles,
        .selected_profile_slot = FrontEndProfileSlot::Second,
    };
    Check(omega::app::ReduceFrontEnd(profiles_second, FrontEndInputEdges{.primary_pressed = true}, 3U,
              gate_enabled, false)
                  .command == FrontEndCommand{
                                  .type = FrontEndCommandType::SetActiveProfile,
                                  .profile_slot = FrontEndProfileSlot::Second,
                              },
          "the closed gate never blocks the selection that can open it");

    constexpr FrontEndCapabilities creation_and_gate{
        .can_create_first_profile = true,
        .can_start_diagnostic_campaign = true,
        .requires_active_profile_for_diagnostic_play = true,
    };
    constexpr FrontEndState empty_profiles{
        .mode = FrontEndMode::Profiles,
        .selected_main_row = FrontEndMainRow::Profiles,
        .selected_profile_slot = FrontEndProfileSlot::First,
    };
    bool creation_never_activates = true;
    for (std::uint8_t mask = 0U; mask < 16U; ++mask)
    {
        const FrontEndReduction reduced =
            omega::app::ReduceFrontEnd(empty_profiles, InputFromMask(mask), 0U, creation_and_gate, false);
        creation_never_activates = creation_never_activates &&
                                   reduced.command.type != FrontEndCommandType::SetActiveProfile &&
                                   !omega::app::FrontEndAllowsSimulation(reduced.state, creation_and_gate, false);
    }
    Check(creation_never_activates,
          "creating the first profile publishes no activation and cannot open the "
          "gate by itself");

    const auto one_profile = omega::app::MakeFrontEndStartupModel(std::span{summaries}.first(1U));
    Check(one_profile && !omega::app::FrontEndHasConfirmedActiveProfile(*one_profile, std::nullopt) &&
              omega::app::FrontEndHasConfirmedActiveProfile(*one_profile, first),
          "the post-create model still requires an explicit confirmation before it "
          "can satisfy the gate");

    bool gated_matches_oracle = true;
    bool confirmed_gate_matches_open_start = true;
    constexpr std::array<std::uint8_t, 6U> profile_counts{0U, 1U, 2U, 3U, 4U, 0xffU};
    for (std::uint32_t mode = 0U; mode <= 5U; ++mode)
    {
        for (std::uint32_t row = 0U; row <= 4U; ++row)
        {
            for (std::uint32_t profile_slot = 0U; profile_slot <= 3U; ++profile_slot)
            {
                const FrontEndState state{
                    .mode = static_cast<FrontEndMode>(mode),
                    .selected_main_row = static_cast<FrontEndMainRow>(row),
                    .selected_profile_slot = static_cast<FrontEndProfileSlot>(profile_slot),
                };
                for (const std::uint8_t profile_count : profile_counts)
                {
                    for (std::uint8_t mask = 0U; mask < 16U; ++mask)
                    {
                        const FrontEndInputEdges input = InputFromMask(mask);
                        gated_matches_oracle =
                            gated_matches_oracle &&
                            omega::app::ReduceFrontEnd(state, input, profile_count, gate_enabled, false) ==
                                ReferenceReduceWithGate(state, input, profile_count, gate_enabled, false) &&
                            omega::app::ReduceFrontEnd(state, input, profile_count, creation_and_gate, false) ==
                                ReferenceReduceWithGate(state, input, profile_count, creation_and_gate, false);
                        confirmed_gate_matches_open_start =
                            confirmed_gate_matches_open_start &&
                            omega::app::ReduceFrontEnd(state, input, profile_count, gate_enabled, true) ==
                                omega::app::ReduceFrontEnd(
                                    state, input, profile_count, start_enabled) &&
                            omega::app::ReduceFrontEnd(state, input, profile_count, {}, true) ==
                                omega::app::ReduceFrontEnd(state, input, profile_count);
                    }
                }
            }
        }
    }
    Check(gated_matches_oracle,
          "all 11520 gated mode/row/slot/count/edge combinations match the "
          "independent gate oracle with and without first-profile creation");
    Check(confirmed_gate_matches_open_start,
          "a satisfied gate reproduces the explicit synthetic start path while an "
          "unconsulted confirmation cannot open closed start support");

    const auto active_view = omega::app::BuildFrontEndView(profiles_second, ContentStartupStage::NoContent, model,
                                                           second);
    Check(active_view.active_profile_slot == FrontEndProfileSlot::Second && active_view.profiles == model,
          "the view publishes the position its own model resolves for the confirmed "
          "identifier");
    Check(!omega::app::BuildFrontEndView(profiles_second, ContentStartupStage::NoContent, model, beyond_visible)
               .active_profile_slot &&
              !omega::app::BuildFrontEndView(profiles_second, ContentStartupStage::NoContent, shrunk, second)
                   .active_profile_slot &&
              !omega::app::BuildFrontEndView(profiles_second, ContentStartupStage::NoContent, model)
                   .active_profile_slot,
          "an unresolvable identifier and the default request both leave the active "
          "row unmarked");
    Check(omega::app::BuildFrontEndView(
              FrontEndState{
                  .mode = static_cast<FrontEndMode>(0xffU),
                  .selected_main_row = static_cast<FrontEndMainRow>(0xffU),
              },
              ContentStartupStage::NoContent, model, third)
                  .active_profile_slot == FrontEndProfileSlot::Third,
          "an invalid state fails closed without discarding the resolved active row");
}

void CheckRasterContract()
{
    FrontEndStartupModel one_profile{
        .total_profiles = 1U,
        .visible_profiles = 1U,
    };
    one_profile.profiles[0].label.length =
        static_cast<std::uint8_t>(omega::app::kFrontEndFirstProfileDisplayName.size());
    std::ranges::copy(omega::app::kFrontEndFirstProfileDisplayName,
                      one_profile.profiles[0].label.cells.begin());

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
    const DebugImage main_count_one =
        omega::app::BuildProjectFrontEndMainImage(ContentStartupStage::NoContent, 1U);
    const DebugImage main_data = omega::app::BuildProjectFrontEndMainImage(ContentStartupStage::DataMounted, 3U);
    const DebugImage main_level = omega::app::BuildProjectFrontEndMainImage(ContentStartupStage::LevelContent, 1'024U);
    const DebugImage empty_profiles = omega::app::BuildProjectFrontEndProfilesImage({});
    const DebugImage capability_disabled_empty = omega::app::BuildProjectFrontEndProfilesImage(
        {}, FrontEndCapabilities{.can_create_first_profile = false});
    const DebugImage creatable_empty = omega::app::BuildProjectFrontEndProfilesImage(
        {}, FrontEndCapabilities{.can_create_first_profile = true});
    const DebugImage one_profile_card = omega::app::BuildProjectFrontEndProfilesImage(one_profile);
    const DebugImage populated_profiles = omega::app::BuildProjectFrontEndProfilesImage(profiles);
    const DebugImage diagnostic = omega::app::BuildProjectFrontEndDiagnosticPlayImage();
    const DebugImage controls = omega::app::BuildProjectFrontEndControlsImage();

    const std::array<const DebugImage *, 11U> cards{
        &main_none,
        &main_count_one,
        &main_data,
        &main_level,
        &empty_profiles,
        &capability_disabled_empty,
        &creatable_empty,
        &one_profile_card,
        &populated_profiles,
        &diagnostic,
        &controls,
    };
    Check(std::ranges::all_of(cards, [](const DebugImage *image) { return IsOpaqueCard(*image); }),
          "all project front-end cards are tightly packed opaque 128x72 RGBA8 "
          "images");
    Check(PixelAt(main_none, 0U, 0U) == kCyanColor && PixelAt(main_none, 4U, 4U) == kBackgroundColor &&
              PixelAt(main_none, 8U, 8U) == kSlateColor && PixelAt(main_none, 40U, 12U) == kAmberColor &&
              PixelAt(main_none, 8U, 28U) == kCyanColor && PixelAt(main_none, 13U, 28U) == kSlateColor &&
              PixelAt(main_none, 8U, 58U) == kCyanColor && PixelAt(main_none, 13U, 58U) == kSlateColor,
          "main-card probes cover the opaque frame, header, and all four-row "
          "geometry");
    Check(PixelAt(main_none, 56U, 11U) == kCyanColor &&
              PixelAt(main_none, 57U, 11U) == kCyanColor &&
              PixelAt(main_none, 58U, 11U) == kAmberColor &&
              PixelAt(main_none, 16U, 29U) == kCyanColor &&
              PixelAt(main_none, 17U, 29U) == kSlateColor &&
              PixelAt(main_none, 18U, 29U) == kCyanColor &&
              PixelAt(main_none, 20U, 29U) == kCyanColor,
        "main-card semantic probes distinguish the BRIEFING ROOM title and MISSION SELECT first row");
    Check(PixelAt(main_none, 41U, 23U) == kCyanColor &&
              PixelAt(main_data, 41U, 22U) == kCyanColor &&
              PixelAt(main_level, 42U, 23U) == kBackgroundColor,
        "content-stage probes independently distinguish NONE, DATA, and LEVEL labels");
    Check(PixelAt(main_none, 104U, 22U) == kCyanColor &&
              PixelAt(main_count_one, 104U, 22U) == kBackgroundColor &&
              PixelAt(main_count_one, 105U, 22U) == kCyanColor,
          "the count-one probe independently distinguishes the fixed one-digit profile count");
    Check(PixelAt(empty_profiles, 8U, 24U) == kSlateColor &&
              PixelAt(empty_profiles, 28U, 38U) == kCyanColor &&
              PixelAt(empty_profiles, 8U, 59U) == kSlateColor &&
              PixelAt(creatable_empty, 17U, 32U) == kCyanColor &&
              PixelAt(creatable_empty, 16U, 42U) == kCyanColor &&
              PixelAt(one_profile_card, 16U, 28U) == kCyanColor &&
              PixelAt(populated_profiles, 17U, 28U) == kCyanColor &&
              PixelAt(populated_profiles, 113U, 48U) == kCyanColor,
          "profile-card probes cover legacy-empty, creatable-empty, one-profile, "
          "populated, and scalar-safe truncation paths");
    Check(capability_disabled_empty.rgba8_pixels == empty_profiles.rgba8_pixels,
          "an explicitly disabled creation capability preserves every legacy empty-card byte");
    Check(PixelAt(diagnostic, 8U, 34U) == kSlateColor &&
              PixelAt(diagnostic, 36U, 40U) == kCyanColor &&
              PixelAt(controls, 8U, 24U) == kSlateColor &&
              PixelAt(controls, 12U, 25U) == kCyanColor,
        "diagnostic and controls probes cover their distinct project-authored panels and labels");
    Check(PixelAt(diagnostic, 16U, 59U) == kCyanColor &&
              PixelAt(diagnostic, 17U, 59U) == kSlateColor &&
              PixelAt(diagnostic, 18U, 59U) == kCyanColor &&
              PixelAt(diagnostic, 87U, 59U) == kCyanColor &&
              PixelAt(controls, 17U, 25U) == kCyanColor &&
              PixelAt(controls, 18U, 25U) == kSlateColor &&
              PixelAt(controls, 14U, 46U) == kCyanColor &&
              PixelAt(controls, 18U, 55U) == kCyanColor &&
              PixelAt(controls, 13U, 62U) == kCyanColor &&
              PixelAt(controls, 14U, 62U) == kCyanColor,
        "keyboard-first card probes distinguish menu, quit, movement, select/fire, back, and target legends");

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
        0x270ea18399817f81ULL,
        0x7a315522b1ed32e5ULL,
        0x10d5b8366f1d4ba5ULL,
        0xea15c2933b6cfffdULL,
        0xe26182d60b0bd82dULL,
        0x35ba044580a8be52ULL,
        0x11fbb806ab0dd626ULL,
    };
    Check(hashes == expected_hashes, "all complete project front-end cards match "
                                     "their independently frozen hashes");

    const std::array new_hashes{
        Fnv1a64(main_count_one.pixels()),
        Fnv1a64(creatable_empty.pixels()),
        Fnv1a64(one_profile_card.pixels()),
    };
    std::cout << "first-profile card FNV-1a-64:";
    for (const std::uint64_t hash : new_hashes)
    {
        std::cout << " 0x" << std::hex << std::setfill('0') << std::setw(16) << hash;
    }
    std::cout << std::dec << '\n';
    constexpr std::array<std::uint64_t, 3U> expected_new_hashes{
        0x29eb2cd1d30e624dULL,
        0xca45b40c018f0de6ULL,
        0x6889eb81d787f146ULL,
    };
    Check(new_hashes == expected_new_hashes,
          "count-one, creatable-empty, and one-profile cards match their independently frozen hashes");

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

void CheckInputAliasProjection()
{
    constexpr FrontEndInputEdges direct{
        .primary_pressed = false,
        .previous_pressed = true,
        .next_pressed = false,
        .cancel_pressed = false,
    };
    Check(omega::app::ResolveFrontEndInputEdges(FrontEndMode::Main, direct,
              true, true, true, true) ==
              FrontEndInputEdges{
                  .primary_pressed = true,
                  .previous_pressed = true,
                  .next_pressed = true,
                  .cancel_pressed = true,
              },
        "modal input projection maps left/right and contextual fire/target aliases");
    Check(omega::app::ResolveFrontEndInputEdges(
              FrontEndMode::DiagnosticPlay, direct, true, true, true, true) ==
              FrontEndInputEdges{
                  .primary_pressed = false,
                  .previous_pressed = true,
                  .next_pressed = true,
                  .cancel_pressed = false,
              },
        "DiagnosticPlay input projection keeps fire and target out of menu edges");
    Check(omega::app::ResolveFrontEndInputEdges(FrontEndMode::DiagnosticPlay,
              FrontEndInputEdges{
                  .primary_pressed = true,
                  .cancel_pressed = true,
              },
              false, false, false, false) ==
              FrontEndInputEdges{
                  .primary_pressed = true,
                  .cancel_pressed = true,
              },
        "direct confirm and cancel edges remain available in DiagnosticPlay");
}
} // namespace

int main()
{
    CheckModelContract();
    CheckStartupStatePlannerContract();
    CheckReducerAndViewContract();
    CheckActiveProfileGateContract();
    CheckRasterContract();
    CheckInputAliasProjection();

    if (failures != 0)
    {
        std::cerr << failures << " front-end test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "front-end tests passed\n";
    return EXIT_SUCCESS;
}
