#include "diagnostic_menu.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <span>
#include <string_view>
#include <type_traits>
#include <vector>

namespace
{
using Color = std::array<std::byte, 4U>;

constexpr Color kBackgroundColor{
    std::byte{8U}, std::byte{12U}, std::byte{24U}, std::byte{255U}};
constexpr Color kCyanColor{
    std::byte{112U}, std::byte{220U}, std::byte{255U}, std::byte{255U}};
constexpr Color kSlateColor{
    std::byte{28U}, std::byte{38U}, std::byte{58U}, std::byte{255U}};
constexpr Color kAmberColor{
    std::byte{255U}, std::byte{196U}, std::byte{64U}, std::byte{255U}};

// Frozen only after independent byte-level calculations from the project-owned
// card contract. Both builders are hashed and reported at runtime so a future
// intentional layout change has an actionable value.
constexpr std::uint64_t kExpectedNoContentMenuFnv1a64 =
    UINT64_C(0x8e8e3f7fff4f971a);
constexpr std::uint64_t kExpectedDataMountedMenuFnv1a64 =
    UINT64_C(0x517ad52bbf1fbe61);
constexpr std::uint64_t kExpectedLevelContentMenuFnv1a64 =
    UINT64_C(0x08405186aa105db1);
constexpr std::uint64_t kExpectedDiagnosticControlsFnv1a64 =
    UINT64_C(0xcfa7cc57696aae0a);
constexpr std::uint64_t kExpectedDiagnosticAssetTopologyFnv1a64 =
    UINT64_C(0xb56c8db088c5a9fe);

[[nodiscard]] Color PixelAt(const omega::runtime::DebugImage& image,
    const std::uint32_t x, const std::uint32_t y)
{
    const std::size_t offset =
        (static_cast<std::size_t>(y) * image.width + x) * 4U;
    return {image.rgba8_pixels[offset], image.rgba8_pixels[offset + 1U],
        image.rgba8_pixels[offset + 2U], image.rgba8_pixels[offset + 3U]};
}

[[nodiscard]] std::uint64_t Fnv1a64(
    const std::span<const std::byte> bytes) noexcept
{
    constexpr std::uint64_t kOffsetBasis = UINT64_C(14695981039346656037);
    constexpr std::uint64_t kPrime = UINT64_C(1099511628211);
    std::uint64_t hash = kOffsetBasis;
    for (const std::byte value : bytes)
    {
        hash ^= std::to_integer<std::uint8_t>(value);
        hash *= kPrime;
    }
    return hash;
}

struct ColorHistogram
{
    std::size_t background = 0U;
    std::size_t cyan = 0U;
    std::size_t slate = 0U;
    std::size_t amber = 0U;
    std::size_t unknown = 0U;
    bool all_alpha_opaque = true;

    friend constexpr bool operator==(
        const ColorHistogram&, const ColorHistogram&) noexcept = default;
};

[[nodiscard]] ColorHistogram CountColors(
    const omega::runtime::DebugImage& image) noexcept
{
    ColorHistogram result;
    for (std::size_t offset = 0U; offset + 3U < image.rgba8_pixels.size();
         offset += 4U)
    {
        const Color pixel{image.rgba8_pixels[offset],
            image.rgba8_pixels[offset + 1U], image.rgba8_pixels[offset + 2U],
            image.rgba8_pixels[offset + 3U]};
        result.all_alpha_opaque =
            result.all_alpha_opaque && pixel[3] == std::byte{255U};
        if (pixel == kBackgroundColor)
            ++result.background;
        else if (pixel == kCyanColor)
            ++result.cyan;
        else if (pixel == kSlateColor)
            ++result.slate;
        else if (pixel == kAmberColor)
            ++result.amber;
        else
            ++result.unknown;
    }
    return result;
}

[[nodiscard]] constexpr omega::app::DiagnosticMenuInputEdges InputFromMask(
    const std::uint32_t mask) noexcept
{
    return omega::app::DiagnosticMenuInputEdges{
        .primary_pressed = (mask & 0b001U) != 0U,
        .previous_pressed = (mask & 0b010U) != 0U,
        .next_pressed = (mask & 0b100U) != 0U,
    };
}

// Independent explicit oracle for the complete small reducer domain. It deliberately does not
// call InitialDiagnosticMenuState() or share the production reducer's navigation arithmetic.
[[nodiscard]] constexpr omega::app::DiagnosticMenuState ReferenceUpdate(
    omega::app::DiagnosticMenuState state,
    const omega::app::DiagnosticMenuInputEdges input) noexcept
{
    using omega::app::DiagnosticMenuMode;
    using omega::app::DiagnosticMenuRow;
    using omega::app::DiagnosticMenuState;

    constexpr DiagnosticMenuState initial{
        .mode = DiagnosticMenuMode::MainMenu,
        .selected_row = DiagnosticMenuRow::StartDiagnosticPlay,
    };

    switch (state.mode)
    {
    case DiagnosticMenuMode::MainMenu:
    case DiagnosticMenuMode::DiagnosticPlay:
    case DiagnosticMenuMode::Controls:
    case DiagnosticMenuMode::AssetTopology:
        break;
    default:
        return initial;
    }
    switch (state.selected_row)
    {
    case DiagnosticMenuRow::StartDiagnosticPlay:
    case DiagnosticMenuRow::ShowControls:
    case DiagnosticMenuRow::ShowAssetTopology:
        break;
    default:
        return initial;
    }

    if (input.primary_pressed)
    {
        if (state.mode == DiagnosticMenuMode::DiagnosticPlay)
            return initial;
        if (state.mode == DiagnosticMenuMode::Controls)
        {
            return DiagnosticMenuState{
                .mode = DiagnosticMenuMode::MainMenu,
                .selected_row = DiagnosticMenuRow::ShowControls,
            };
        }
        if (state.mode == DiagnosticMenuMode::AssetTopology)
        {
            return DiagnosticMenuState{
                .mode = DiagnosticMenuMode::MainMenu,
                .selected_row = DiagnosticMenuRow::ShowAssetTopology,
            };
        }
        if (state.selected_row == DiagnosticMenuRow::StartDiagnosticPlay)
            state.mode = DiagnosticMenuMode::DiagnosticPlay;
        else if (state.selected_row == DiagnosticMenuRow::ShowControls)
            state.mode = DiagnosticMenuMode::Controls;
        else if (state.selected_row == DiagnosticMenuRow::ShowAssetTopology)
            state.mode = DiagnosticMenuMode::AssetTopology;
        return state;
    }

    if (state.mode != DiagnosticMenuMode::MainMenu ||
        input.previous_pressed == input.next_pressed)
    {
        return state;
    }

    if (input.previous_pressed)
    {
        switch (state.selected_row)
        {
        case DiagnosticMenuRow::StartDiagnosticPlay:
            break;
        case DiagnosticMenuRow::ShowControls:
            state.selected_row = DiagnosticMenuRow::StartDiagnosticPlay;
            break;
        case DiagnosticMenuRow::ShowAssetTopology:
            state.selected_row = DiagnosticMenuRow::ShowControls;
            break;
        }
    }
    else
    {
        switch (state.selected_row)
        {
        case DiagnosticMenuRow::StartDiagnosticPlay:
            state.selected_row = DiagnosticMenuRow::ShowControls;
            break;
        case DiagnosticMenuRow::ShowControls:
            state.selected_row = DiagnosticMenuRow::ShowAssetTopology;
            break;
        case DiagnosticMenuRow::ShowAssetTopology:
            break;
        }
    }
    return state;
}
} // namespace

int main()
{
    using omega::app::BuildProjectDiagnosticControlsImage;
    using omega::app::BuildProjectDiagnosticAssetTopologyImage;
    using omega::app::BuildProjectDiagnosticMenuImage;
    using omega::app::DiagnosticMenuAllowsSimulation;
    using omega::app::DiagnosticMenuInputEdges;
    using omega::app::DiagnosticMenuMode;
    using omega::app::DiagnosticMenuRow;
    using omega::app::DiagnosticMenuState;
    using omega::app::InitialDiagnosticMenuState;
    using omega::app::UpdateDiagnosticMenu;

    static_assert(omega::app::kDiagnosticMenuToggleAction == 6U);
    static_assert(omega::app::kDiagnosticMenuPrimaryAction ==
                  omega::app::kDiagnosticMenuToggleAction);
    static_assert(omega::app::kDiagnosticMenuPreviousAction == 2U);
    static_assert(omega::app::kDiagnosticMenuNextAction == 3U);
    static_assert(omega::app::kDiagnosticMenuRowCount == 3U);
    static_assert(omega::app::kDiagnosticMenuImageWidth == 128U);
    static_assert(omega::app::kDiagnosticMenuImageHeight == 72U);
    static_assert(sizeof(DiagnosticMenuMode) == 1U);
    static_assert(sizeof(DiagnosticMenuRow) == 1U);
    static_assert(static_cast<std::uint8_t>(DiagnosticMenuMode::Controls) == 2U);
    static_assert(static_cast<std::uint8_t>(DiagnosticMenuMode::AssetTopology) == 3U);
    static_assert(static_cast<std::uint8_t>(DiagnosticMenuRow::ShowControls) == 1U);
    static_assert(static_cast<std::uint8_t>(DiagnosticMenuRow::ShowAssetTopology) == 2U);
    static_assert(std::is_trivially_copyable_v<DiagnosticMenuMode>);
    static_assert(std::is_standard_layout_v<DiagnosticMenuMode>);
    static_assert(std::is_trivially_copyable_v<DiagnosticMenuRow>);
    static_assert(std::is_standard_layout_v<DiagnosticMenuRow>);
    static_assert(std::is_trivially_copyable_v<DiagnosticMenuState>);
    static_assert(std::is_standard_layout_v<DiagnosticMenuState>);
    static_assert(std::is_trivially_copyable_v<DiagnosticMenuInputEdges>);
    static_assert(std::is_standard_layout_v<DiagnosticMenuInputEdges>);
    static_assert(std::is_same_v<
        decltype(UpdateDiagnosticMenu(
            DiagnosticMenuState{}, DiagnosticMenuInputEdges{})),
        DiagnosticMenuState>);
    static_assert(noexcept(InitialDiagnosticMenuState()));
    static_assert(noexcept(DiagnosticMenuAllowsSimulation(DiagnosticMenuState{})));
    static_assert(noexcept(UpdateDiagnosticMenu(
        DiagnosticMenuState{}, DiagnosticMenuInputEdges{})));
    static_assert(DiagnosticMenuState{} ==
                  DiagnosticMenuState{
                      .mode = DiagnosticMenuMode::DiagnosticPlay,
                      .selected_row = DiagnosticMenuRow::StartDiagnosticPlay,
                  });
    static_assert(InitialDiagnosticMenuState() ==
                  DiagnosticMenuState{
                      .mode = DiagnosticMenuMode::MainMenu,
                      .selected_row = DiagnosticMenuRow::StartDiagnosticPlay,
                  });
    static_assert(UpdateDiagnosticMenu(
                      DiagnosticMenuState{}, DiagnosticMenuInputEdges{}) ==
                  DiagnosticMenuState{});
    static_assert(UpdateDiagnosticMenu(
                      DiagnosticMenuState{},
                      DiagnosticMenuInputEdges{.primary_pressed = true}) ==
                  InitialDiagnosticMenuState());
    static_assert(UpdateDiagnosticMenu(
                      InitialDiagnosticMenuState(),
                      DiagnosticMenuInputEdges{.primary_pressed = true}) ==
                  DiagnosticMenuState{});
    static_assert(UpdateDiagnosticMenu(
                      DiagnosticMenuState{
                          .mode = DiagnosticMenuMode::MainMenu,
                          .selected_row = DiagnosticMenuRow::ShowControls,
                      },
                      DiagnosticMenuInputEdges{.primary_pressed = true}) ==
                  DiagnosticMenuState{
                      .mode = DiagnosticMenuMode::Controls,
                      .selected_row = DiagnosticMenuRow::ShowControls,
                  });
    static_assert(UpdateDiagnosticMenu(
                      DiagnosticMenuState{
                          .mode = DiagnosticMenuMode::Controls,
                          .selected_row = DiagnosticMenuRow::ShowAssetTopology,
                      },
                      DiagnosticMenuInputEdges{.primary_pressed = true}) ==
                  DiagnosticMenuState{
                      .mode = DiagnosticMenuMode::MainMenu,
                      .selected_row = DiagnosticMenuRow::ShowControls,
                  });
    static_assert(UpdateDiagnosticMenu(
                      DiagnosticMenuState{
                          .mode = DiagnosticMenuMode::MainMenu,
                          .selected_row = DiagnosticMenuRow::ShowAssetTopology,
                      },
                      DiagnosticMenuInputEdges{.primary_pressed = true}) ==
                  DiagnosticMenuState{
                      .mode = DiagnosticMenuMode::AssetTopology,
                      .selected_row = DiagnosticMenuRow::ShowAssetTopology,
                  });
    static_assert(UpdateDiagnosticMenu(
                      DiagnosticMenuState{
                          .mode = DiagnosticMenuMode::AssetTopology,
                          .selected_row = DiagnosticMenuRow::StartDiagnosticPlay,
                      },
                      DiagnosticMenuInputEdges{.primary_pressed = true}) ==
                  DiagnosticMenuState{
                      .mode = DiagnosticMenuMode::MainMenu,
                      .selected_row = DiagnosticMenuRow::ShowAssetTopology,
                  });
    static_assert(DiagnosticMenuAllowsSimulation(DiagnosticMenuState{}));
    static_assert(!DiagnosticMenuAllowsSimulation(InitialDiagnosticMenuState()));
    static_assert(!DiagnosticMenuAllowsSimulation(DiagnosticMenuState{
        .mode = DiagnosticMenuMode::Controls,
        .selected_row = DiagnosticMenuRow::ShowControls,
    }));
    static_assert(!DiagnosticMenuAllowsSimulation(DiagnosticMenuState{
        .mode = DiagnosticMenuMode::AssetTopology,
        .selected_row = DiagnosticMenuRow::ShowAssetTopology,
    }));

    int failures = 0;
    const auto Check = [&failures](
                           const bool condition, const std::string_view message) {
        if (!condition)
        {
            std::cerr << "FAILED: " << message << '\n';
            ++failures;
        }
    };

    Check(DiagnosticMenuState{} ==
              DiagnosticMenuState{
                  .mode = DiagnosticMenuMode::DiagnosticPlay,
                  .selected_row = DiagnosticMenuRow::StartDiagnosticPlay,
              },
        "the safe default is diagnostic play with the first row selected");
    Check(InitialDiagnosticMenuState() ==
              DiagnosticMenuState{
                  .mode = DiagnosticMenuMode::MainMenu,
                  .selected_row = DiagnosticMenuRow::StartDiagnosticPlay,
              },
        "the explicit app startup state is the main menu on its first row");

    constexpr std::array modes{
        DiagnosticMenuMode::MainMenu,
        DiagnosticMenuMode::DiagnosticPlay,
        DiagnosticMenuMode::Controls,
        DiagnosticMenuMode::AssetTopology,
    };
    constexpr std::array rows{
        DiagnosticMenuRow::StartDiagnosticPlay,
        DiagnosticMenuRow::ShowControls,
        DiagnosticMenuRow::ShowAssetTopology,
    };
    std::size_t exhaustive_cases = 0U;
    std::size_t simulation_predicate_cases = 0U;
    for (const DiagnosticMenuMode mode : modes)
    {
        for (const DiagnosticMenuRow row : rows)
        {
            const DiagnosticMenuState source{
                .mode = mode,
                .selected_row = row,
            };
            Check(DiagnosticMenuAllowsSimulation(source) ==
                      (mode == DiagnosticMenuMode::DiagnosticPlay),
                "simulation is allowed by every valid diagnostic-play row only");
            ++simulation_predicate_cases;
            for (std::uint32_t mask = 0U; mask < 8U; ++mask)
            {
                const DiagnosticMenuInputEdges input = InputFromMask(mask);
                Check(UpdateDiagnosticMenu(source, input) ==
                          ReferenceUpdate(source, input),
                    "the exhaustive reducer result matches the independent oracle");
                ++exhaustive_cases;
            }
        }
    }
    Check(exhaustive_cases == 96U,
        "the exhaustive reducer matrix covers four modes, three rows, and eight edge masks");
    Check(simulation_predicate_cases == 12U,
        "the simulation predicate covers every valid mode and row combination");

    constexpr std::array invalid_states{
        DiagnosticMenuState{
            .mode = static_cast<DiagnosticMenuMode>(4U),
            .selected_row = DiagnosticMenuRow::StartDiagnosticPlay,
        },
        DiagnosticMenuState{
            .mode = static_cast<DiagnosticMenuMode>(255U),
            .selected_row = DiagnosticMenuRow::ShowAssetTopology,
        },
        DiagnosticMenuState{
            .mode = DiagnosticMenuMode::MainMenu,
            .selected_row = static_cast<DiagnosticMenuRow>(3U),
        },
        DiagnosticMenuState{
            .mode = DiagnosticMenuMode::DiagnosticPlay,
            .selected_row = static_cast<DiagnosticMenuRow>(255U),
        },
        DiagnosticMenuState{
            .mode = DiagnosticMenuMode::Controls,
            .selected_row = static_cast<DiagnosticMenuRow>(3U),
        },
        DiagnosticMenuState{
            .mode = DiagnosticMenuMode::AssetTopology,
            .selected_row = static_cast<DiagnosticMenuRow>(3U),
        },
        DiagnosticMenuState{
            .mode = static_cast<DiagnosticMenuMode>(4U),
            .selected_row = static_cast<DiagnosticMenuRow>(3U),
        },
    };
    std::size_t invalid_cases = 0U;
    for (const DiagnosticMenuState invalid : invalid_states)
    {
        Check(!DiagnosticMenuAllowsSimulation(invalid),
            "every invalid mode or row fails closed to modal simulation");
        for (std::uint32_t mask = 0U; mask < 8U; ++mask)
        {
            Check(UpdateDiagnosticMenu(invalid, InputFromMask(mask)) ==
                      InitialDiagnosticMenuState(),
                "invalid state resets before and consumes every edge combination");
            ++invalid_cases;
        }
    }
    Check(invalid_cases == 56U,
        "invalid mode, row, and combined states consume all eight edge masks");

    const DiagnosticMenuState first_row = InitialDiagnosticMenuState();
    const DiagnosticMenuState last_row{
        .mode = DiagnosticMenuMode::MainMenu,
        .selected_row = DiagnosticMenuRow::ShowAssetTopology,
    };
    Check(UpdateDiagnosticMenu(first_row,
              DiagnosticMenuInputEdges{.previous_pressed = true}) == first_row &&
              UpdateDiagnosticMenu(last_row,
                  DiagnosticMenuInputEdges{.next_pressed = true}) == last_row,
        "previous and next navigation clamp at their respective boundaries");
    Check(UpdateDiagnosticMenu(first_row,
              DiagnosticMenuInputEdges{
                  .previous_pressed = true,
                  .next_pressed = true,
              }) == first_row,
        "simultaneous previous and next edges are neutral");
    Check(UpdateDiagnosticMenu(
              DiagnosticMenuState{
                  .mode = DiagnosticMenuMode::MainMenu,
                  .selected_row = DiagnosticMenuRow::ShowControls,
              },
              DiagnosticMenuInputEdges{
                  .primary_pressed = true,
                  .previous_pressed = true,
                  .next_pressed = true,
              }) ==
              DiagnosticMenuState{
                  .mode = DiagnosticMenuMode::Controls,
                  .selected_row = DiagnosticMenuRow::ShowControls,
              },
        "primary has priority over navigation when entering the controls card");
    Check(UpdateDiagnosticMenu(
              DiagnosticMenuState{
                  .mode = DiagnosticMenuMode::Controls,
                  .selected_row = DiagnosticMenuRow::ShowAssetTopology,
              },
              DiagnosticMenuInputEdges{
                  .primary_pressed = true,
                  .previous_pressed = true,
                  .next_pressed = true,
              }) ==
              DiagnosticMenuState{
                  .mode = DiagnosticMenuMode::MainMenu,
                  .selected_row = DiagnosticMenuRow::ShowControls,
              },
        "primary returns every controls-card row to main-menu row one");
    Check(UpdateDiagnosticMenu(
              DiagnosticMenuState{
                  .mode = DiagnosticMenuMode::MainMenu,
                  .selected_row = DiagnosticMenuRow::ShowAssetTopology,
              },
              DiagnosticMenuInputEdges{.primary_pressed = true}) ==
              DiagnosticMenuState{
                  .mode = DiagnosticMenuMode::AssetTopology,
                  .selected_row = DiagnosticMenuRow::ShowAssetTopology,
              },
        "the final project row enters the asset-topology card on primary");
    Check(UpdateDiagnosticMenu(
              DiagnosticMenuState{
                  .mode = DiagnosticMenuMode::AssetTopology,
                  .selected_row = DiagnosticMenuRow::StartDiagnosticPlay,
              },
              DiagnosticMenuInputEdges{
                  .primary_pressed = true,
                  .previous_pressed = true,
                  .next_pressed = true,
              }) ==
              DiagnosticMenuState{
                  .mode = DiagnosticMenuMode::MainMenu,
                  .selected_row = DiagnosticMenuRow::ShowAssetTopology,
              },
        "primary returns every asset-topology row to main-menu row two");

    Check(omega::app::kDiagnosticMenuToggleAction == 6U &&
              omega::app::kDiagnosticMenuPrimaryAction == 6U &&
              omega::app::kDiagnosticMenuPreviousAction == 2U &&
              omega::app::kDiagnosticMenuNextAction == 3U &&
              omega::app::kDiagnosticMenuRowCount == 3U &&
              omega::app::kDiagnosticMenuImageWidth == 128U &&
              omega::app::kDiagnosticMenuImageHeight == 72U,
        "the project action identifiers, row count, and image dimensions remain exact");

    const omega::runtime::DebugImage no_content_first =
        BuildProjectDiagnosticMenuImage(
            omega::runtime::ContentStartupStage::NoContent);
    const omega::runtime::DebugImage no_content_second =
        BuildProjectDiagnosticMenuImage(
            omega::runtime::ContentStartupStage::NoContent);
    const omega::runtime::DebugImage data_mounted_first =
        BuildProjectDiagnosticMenuImage(
            omega::runtime::ContentStartupStage::DataMounted);
    const omega::runtime::DebugImage data_mounted_second =
        BuildProjectDiagnosticMenuImage(
            omega::runtime::ContentStartupStage::DataMounted);
    const omega::runtime::DebugImage level_content_first =
        BuildProjectDiagnosticMenuImage(
            omega::runtime::ContentStartupStage::LevelContent);
    const omega::runtime::DebugImage level_content_second =
        BuildProjectDiagnosticMenuImage(
            omega::runtime::ContentStartupStage::LevelContent);
    const omega::runtime::DebugImage invalid_stage =
        BuildProjectDiagnosticMenuImage(
            static_cast<omega::runtime::ContentStartupStage>(255U));
    const omega::runtime::DebugImage& first = no_content_first;
    constexpr std::size_t kExpectedBytes = 128U * 72U * 4U;
    const std::array<const omega::runtime::DebugImage*, 7U> all_menu_images{
        &no_content_first, &no_content_second, &data_mounted_first,
        &data_mounted_second, &level_content_first, &level_content_second,
        &invalid_stage,
    };
    bool every_menu_shape_is_safe = true;
    for (const omega::runtime::DebugImage* image : all_menu_images)
    {
        every_menu_shape_is_safe = every_menu_shape_is_safe && image->width == 128U &&
                                   image->height == 72U &&
                                   image->rgba8_pixels.size() == kExpectedBytes &&
                                   image->pixels().size() == kExpectedBytes;
    }
    Check(every_menu_shape_is_safe,
        "every content-stage menu is a fully owned tightly packed 128x72 RGBA8 image");
    if (!every_menu_shape_is_safe)
        return EXIT_FAILURE;
    Check(no_content_second.rgba8_pixels == no_content_first.rgba8_pixels &&
              data_mounted_second.rgba8_pixels == data_mounted_first.rgba8_pixels &&
              level_content_second.rgba8_pixels == level_content_first.rgba8_pixels &&
              invalid_stage.rgba8_pixels == no_content_first.rgba8_pixels,
        "each valid content-stage menu is deterministic and invalid enum values render NoContent");
    Check(no_content_second.rgba8_pixels.data() !=
                  no_content_first.rgba8_pixels.data() &&
              data_mounted_second.rgba8_pixels.data() !=
                  data_mounted_first.rgba8_pixels.data() &&
              level_content_second.rgba8_pixels.data() !=
                  level_content_first.rgba8_pixels.data() &&
              invalid_stage.rgba8_pixels.data() !=
                  no_content_first.rgba8_pixels.data() &&
              no_content_first.rgba8_pixels.data() !=
                  data_mounted_first.rgba8_pixels.data() &&
              data_mounted_first.rgba8_pixels.data() !=
                  level_content_first.rgba8_pixels.data(),
        "every content-stage build owns a distinct pixel buffer");

    const ColorHistogram no_content_histogram = CountColors(no_content_first);
    const ColorHistogram data_mounted_histogram = CountColors(data_mounted_first);
    const ColorHistogram level_content_histogram = CountColors(level_content_first);
    Check(no_content_histogram == ColorHistogram{
              .background = 3'593U,
              .cyan = 1'627U,
              .slate = 3'516U,
              .amber = 480U,
              .unknown = 0U,
              .all_alpha_opaque = true,
          },
        "the NoContent menu has its exact opaque four-color histogram");
    Check(data_mounted_histogram == ColorHistogram{
              .background = 3'600U,
              .cyan = 1'620U,
              .slate = 3'516U,
              .amber = 480U,
              .unknown = 0U,
              .all_alpha_opaque = true,
          },
        "the DataMounted menu has its exact opaque four-color histogram");
    Check(level_content_histogram == ColorHistogram{
              .background = 3'594U,
              .cyan = 1'626U,
              .slate = 3'516U,
              .amber = 480U,
              .unknown = 0U,
              .all_alpha_opaque = true,
          },
        "the LevelContent menu has its exact opaque four-color histogram");

    constexpr std::array probe_coordinates{
        std::array{4U, 4U}, std::array{0U, 0U}, std::array{8U, 8U},
        std::array{9U, 8U}, std::array{40U, 12U}, std::array{8U, 23U},
        std::array{24U, 22U}, std::array{62U, 22U}, std::array{9U, 30U},
        std::array{16U, 30U}, std::array{17U, 30U}, std::array{77U, 30U},
        std::array{16U, 45U}, std::array{17U, 45U}, std::array{68U, 60U},
        std::array{69U, 62U},
    };
    constexpr std::array probe_colors{
        kBackgroundColor, kCyanColor, kSlateColor, kCyanColor,
        kAmberColor, kCyanColor, kCyanColor, kCyanColor,
        kCyanColor, kSlateColor, kCyanColor, kCyanColor,
        kSlateColor, kCyanColor, kCyanColor, kCyanColor,
    };
    const std::array<const omega::runtime::DebugImage*, 3U> stage_menu_images{
        &no_content_first, &data_mounted_first, &level_content_first};
    bool probes_match = true;
    for (const omega::runtime::DebugImage* image : stage_menu_images)
    {
        for (std::size_t index = 0U; index < probe_coordinates.size(); ++index)
        {
            probes_match = probes_match &&
                           PixelAt(*image, probe_coordinates[index][0],
                               probe_coordinates[index][1]) == probe_colors[index];
        }
    }
    Check(probes_match,
        "sixteen shared probes cover the frame, labels, legend, and all menu rows in every stage");

    constexpr std::array content_boundary_coordinates{
        std::array{91U, 54U}, std::array{93U, 54U}, std::array{117U, 58U},
        std::array{119U, 58U}, std::array{95U, 61U}, std::array{96U, 61U},
        std::array{115U, 65U},
    };
    constexpr std::array content_boundary_colors{
        kBackgroundColor, kCyanColor, kCyanColor, kBackgroundColor,
        kBackgroundColor, kCyanColor, kBackgroundColor,
    };
    bool content_boundaries_match = true;
    for (const omega::runtime::DebugImage* image : stage_menu_images)
    {
        for (std::size_t index = 0U; index < content_boundary_coordinates.size();
             ++index)
        {
            content_boundaries_match = content_boundaries_match &&
                                       PixelAt(*image,
                                           content_boundary_coordinates[index][0],
                                           content_boundary_coordinates[index][1]) ==
                                           content_boundary_colors[index];
        }
    }
    Check(content_boundaries_match,
        "the CONTENT and stage labels retain exact shared boundary pixels");

    constexpr std::array stage_discriminator_coordinates{
        std::array{97U, 61U}, std::array{98U, 61U}, std::array{100U, 61U},
    };
    constexpr std::array stage_discriminator_colors{
        std::array{kBackgroundColor, kCyanColor, kBackgroundColor},
        std::array{kCyanColor, kBackgroundColor, kBackgroundColor},
        std::array{kBackgroundColor, kBackgroundColor, kCyanColor},
    };
    bool stage_discriminators_match = true;
    for (std::size_t stage = 0U; stage < stage_menu_images.size(); ++stage)
    {
        for (std::size_t index = 0U;
             index < stage_discriminator_coordinates.size(); ++index)
        {
            stage_discriminators_match = stage_discriminators_match &&
                                         PixelAt(*stage_menu_images[stage],
                                             stage_discriminator_coordinates[index][0],
                                             stage_discriminator_coordinates[index][1]) ==
                                             stage_discriminator_colors[stage][index];
        }
    }
    Check(stage_discriminators_match,
        "three exact discriminator pixels distinguish NONE, DATA, and LEVEL");

    const std::uint64_t no_content_digest = Fnv1a64(no_content_first.pixels());
    const std::uint64_t data_mounted_digest = Fnv1a64(data_mounted_first.pixels());
    const std::uint64_t level_content_digest = Fnv1a64(level_content_first.pixels());
    std::cout << "diagnostic menu NoContent/DataMounted/LevelContent FNV-1a-64: 0x"
              << std::hex << std::setfill('0') << std::setw(16) << no_content_digest
              << "/0x" << std::setw(16) << data_mounted_digest << "/0x"
              << std::setw(16) << level_content_digest << std::dec << '\n';
    Check(no_content_digest == kExpectedNoContentMenuFnv1a64 &&
              data_mounted_digest == kExpectedDataMountedMenuFnv1a64 &&
              level_content_digest == kExpectedLevelContentMenuFnv1a64,
        "all three deterministic content-stage menus match their independently frozen digests");

    const omega::runtime::DebugImage controls_first =
        BuildProjectDiagnosticControlsImage();
    const omega::runtime::DebugImage controls_second =
        BuildProjectDiagnosticControlsImage();
    const bool controls_shape_is_safe = controls_first.width == 128U &&
                                        controls_first.height == 72U &&
                                        controls_first.rgba8_pixels.size() == kExpectedBytes &&
                                        controls_first.pixels().size() == kExpectedBytes;
    Check(controls_shape_is_safe,
        "the controls card is one fully owned tightly packed 128x72 RGBA8 image");
    if (!controls_shape_is_safe)
        return EXIT_FAILURE;
    Check(controls_second.width == controls_first.width &&
              controls_second.height == controls_first.height &&
              controls_second.rgba8_pixels == controls_first.rgba8_pixels &&
              controls_second.rgba8_pixels.data() !=
                  controls_first.rgba8_pixels.data() &&
              controls_first.rgba8_pixels != first.rgba8_pixels,
        "independent controls-card builds own distinct byte-identical buffers separate from the menu card");

    bool controls_alpha_opaque =
        controls_first.rgba8_pixels.size() == kExpectedBytes;
    std::size_t controls_background_pixels = 0U;
    std::size_t controls_cyan_pixels = 0U;
    std::size_t controls_slate_pixels = 0U;
    std::size_t controls_amber_pixels = 0U;
    std::size_t controls_unknown_pixels = 0U;
    for (std::size_t offset = 0U;
         offset + 3U < controls_first.rgba8_pixels.size(); offset += 4U)
    {
        const Color pixel{controls_first.rgba8_pixels[offset],
            controls_first.rgba8_pixels[offset + 1U],
            controls_first.rgba8_pixels[offset + 2U],
            controls_first.rgba8_pixels[offset + 3U]};
        controls_alpha_opaque =
            controls_alpha_opaque && pixel[3] == std::byte{255U};
        if (pixel == kBackgroundColor)
            ++controls_background_pixels;
        else if (pixel == kCyanColor)
            ++controls_cyan_pixels;
        else if (pixel == kSlateColor)
            ++controls_slate_pixels;
        else if (pixel == kAmberColor)
            ++controls_amber_pixels;
        else
            ++controls_unknown_pixels;
    }
    Check(controls_alpha_opaque,
        "every controls-card pixel has fully opaque alpha");
    Check(controls_background_pixels == 2'104U &&
              controls_cyan_pixels == 1'452U &&
              controls_slate_pixels == 5'247U &&
              controls_amber_pixels == 413U && controls_unknown_pixels == 0U,
        "the controls card has the exact independently calculated four-color histogram");

    // These probes independently cover the frame, shared header, controls title, both panels,
    // their gap, and one lit pixel from each of the six frozen control labels.
    constexpr std::array controls_probe_coordinates{
        std::array{0U, 0U}, std::array{4U, 4U}, std::array{8U, 8U},
        std::array{9U, 8U}, std::array{40U, 12U}, std::array{43U, 11U},
        std::array{7U, 24U}, std::array{8U, 24U}, std::array{20U, 25U},
        std::array{20U, 32U}, std::array{13U, 39U}, std::array{12U, 46U},
        std::array{8U, 52U}, std::array{8U, 54U}, std::array{22U, 55U},
        std::array{12U, 62U},
    };
    constexpr std::array controls_probe_colors{
        kCyanColor, kBackgroundColor, kSlateColor, kCyanColor,
        kAmberColor, kCyanColor, kBackgroundColor, kSlateColor,
        kCyanColor, kCyanColor, kCyanColor, kCyanColor,
        kBackgroundColor, kSlateColor, kCyanColor, kCyanColor,
    };
    bool controls_probes_match = true;
    for (std::size_t index = 0U; index < controls_probe_coordinates.size();
         ++index)
    {
        controls_probes_match = controls_probes_match &&
                                PixelAt(controls_first,
                                    controls_probe_coordinates[index][0],
                                    controls_probe_coordinates[index][1]) ==
                                    controls_probe_colors[index];
    }
    Check(controls_probes_match,
        "sixteen independent probes cover every controls-card label and region");

    const std::uint64_t controls_digest = Fnv1a64(controls_first.pixels());
    std::cout << "diagnostic controls FNV-1a-64: 0x" << std::hex
              << std::setfill('0') << std::setw(16) << controls_digest
              << std::dec << '\n';
    Check(controls_digest == kExpectedDiagnosticControlsFnv1a64,
        "the complete controls-card RGBA8 image matches the independently frozen digest");

    auto asset_topology_first = BuildProjectDiagnosticAssetTopologyImage();
    auto asset_topology_second = BuildProjectDiagnosticAssetTopologyImage();
    const bool asset_topology_shape_is_safe =
        asset_topology_first && asset_topology_second &&
        asset_topology_first->width == 96U && asset_topology_first->height == 32U &&
        asset_topology_first->rgba8_pixels.size() == 12'288U &&
        asset_topology_first->pixels().size() == 12'288U &&
        asset_topology_second->width == asset_topology_first->width &&
        asset_topology_second->height == asset_topology_first->height &&
        asset_topology_second->rgba8_pixels == asset_topology_first->rgba8_pixels;
    Check(asset_topology_shape_is_safe,
        "the exact project-owned three-block fixture produces two frozen 96x32 topology images");
    if (asset_topology_shape_is_safe)
    {
        Check(asset_topology_first->rgba8_pixels.data() !=
                  asset_topology_second->rgba8_pixels.data() &&
                  asset_topology_first->rgba8_pixels != first.rgba8_pixels &&
                  asset_topology_first->rgba8_pixels != controls_first.rgba8_pixels,
            "each topology build deeply owns bytes separate from every other project card");

        std::size_t topology_background_pixels = 0U;
        std::size_t topology_cyan_pixels = 0U;
        std::size_t topology_slate_pixels = 0U;
        std::size_t topology_amber_pixels = 0U;
        std::size_t topology_unknown_pixels = 0U;
        for (std::size_t offset = 0U;
             offset + 3U < asset_topology_first->rgba8_pixels.size(); offset += 4U)
        {
            const Color pixel{asset_topology_first->rgba8_pixels[offset],
                asset_topology_first->rgba8_pixels[offset + 1U],
                asset_topology_first->rgba8_pixels[offset + 2U],
                asset_topology_first->rgba8_pixels[offset + 3U]};
            if (pixel == kBackgroundColor)
                ++topology_background_pixels;
            else if (pixel == kCyanColor)
                ++topology_cyan_pixels;
            else if (pixel == kSlateColor)
                ++topology_slate_pixels;
            else if (pixel == kAmberColor)
                ++topology_amber_pixels;
            else
                ++topology_unknown_pixels;
        }
        Check(topology_background_pixels == 2'667U &&
                  topology_slate_pixels == 372U &&
                  topology_cyan_pixels == 23U && topology_amber_pixels == 10U &&
                  topology_unknown_pixels == 0U,
            "the project topology card preserves the exact E-0066 four-color population");

        const std::uint64_t asset_topology_digest =
            Fnv1a64(asset_topology_first->pixels());
        std::cout << "diagnostic asset topology FNV-1a-64: 0x" << std::hex
                  << std::setfill('0') << std::setw(16) << asset_topology_digest
                  << std::dec << '\n';
        Check(asset_topology_digest == kExpectedDiagnosticAssetTopologyFnv1a64,
            "the project topology card matches the independently frozen E-0066 digest");

        const std::vector<std::byte> second_pixels_before_mutation =
            asset_topology_second->rgba8_pixels;
        asset_topology_first->rgba8_pixels.front() = std::byte{0U};
        Check(asset_topology_second->rgba8_pixels == second_pixels_before_mutation,
            "mutating one returned topology image cannot alias another owned build");
    }

    if (failures != 0)
    {
        std::cerr << failures << " diagnostic menu test(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "diagnostic menu tests passed\n";
    return EXIT_SUCCESS;
}
