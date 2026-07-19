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

// Frozen only after an independent byte-level calculation of the project-owned
// image layout. BuildProjectDiagnosticMenuImage() is also hashed and reported
// at runtime so a future intentional layout change has an actionable value.
constexpr std::uint64_t kExpectedDiagnosticMenuFnv1a64 =
    UINT64_C(0xdaf00c60d17f05b5);

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
        break;
    default:
        return initial;
    }
    switch (state.selected_row)
    {
    case DiagnosticMenuRow::StartDiagnosticPlay:
    case DiagnosticMenuRow::ReservedProjectOne:
    case DiagnosticMenuRow::ReservedProjectTwo:
        break;
    default:
        return initial;
    }

    if (input.primary_pressed)
    {
        if (state.mode == DiagnosticMenuMode::DiagnosticPlay)
            return initial;
        if (state.selected_row == DiagnosticMenuRow::StartDiagnosticPlay)
            state.mode = DiagnosticMenuMode::DiagnosticPlay;
        return state;
    }

    if (state.mode == DiagnosticMenuMode::DiagnosticPlay ||
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
        case DiagnosticMenuRow::ReservedProjectOne:
            state.selected_row = DiagnosticMenuRow::StartDiagnosticPlay;
            break;
        case DiagnosticMenuRow::ReservedProjectTwo:
            state.selected_row = DiagnosticMenuRow::ReservedProjectOne;
            break;
        }
    }
    else
    {
        switch (state.selected_row)
        {
        case DiagnosticMenuRow::StartDiagnosticPlay:
            state.selected_row = DiagnosticMenuRow::ReservedProjectOne;
            break;
        case DiagnosticMenuRow::ReservedProjectOne:
            state.selected_row = DiagnosticMenuRow::ReservedProjectTwo;
            break;
        case DiagnosticMenuRow::ReservedProjectTwo:
            break;
        }
    }
    return state;
}
} // namespace

int main()
{
    using omega::app::BuildProjectDiagnosticMenuImage;
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
    };
    constexpr std::array rows{
        DiagnosticMenuRow::StartDiagnosticPlay,
        DiagnosticMenuRow::ReservedProjectOne,
        DiagnosticMenuRow::ReservedProjectTwo,
    };
    std::size_t exhaustive_cases = 0U;
    for (const DiagnosticMenuMode mode : modes)
    {
        for (const DiagnosticMenuRow row : rows)
        {
            for (std::uint32_t mask = 0U; mask < 8U; ++mask)
            {
                const DiagnosticMenuState source{
                    .mode = mode,
                    .selected_row = row,
                };
                const DiagnosticMenuInputEdges input = InputFromMask(mask);
                Check(UpdateDiagnosticMenu(source, input) ==
                          ReferenceUpdate(source, input),
                    "the exhaustive reducer result matches the independent oracle");
                ++exhaustive_cases;
            }
        }
    }
    Check(exhaustive_cases == 48U,
        "the exhaustive reducer matrix covers two modes, three rows, and eight edge masks");

    constexpr std::array invalid_states{
        DiagnosticMenuState{
            .mode = static_cast<DiagnosticMenuMode>(2U),
            .selected_row = DiagnosticMenuRow::StartDiagnosticPlay,
        },
        DiagnosticMenuState{
            .mode = static_cast<DiagnosticMenuMode>(255U),
            .selected_row = DiagnosticMenuRow::ReservedProjectTwo,
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
            .mode = static_cast<DiagnosticMenuMode>(2U),
            .selected_row = static_cast<DiagnosticMenuRow>(3U),
        },
    };
    std::size_t invalid_cases = 0U;
    for (const DiagnosticMenuState invalid : invalid_states)
    {
        for (std::uint32_t mask = 0U; mask < 8U; ++mask)
        {
            Check(UpdateDiagnosticMenu(invalid, InputFromMask(mask)) ==
                      InitialDiagnosticMenuState(),
                "invalid state resets before and consumes every edge combination");
            ++invalid_cases;
        }
    }
    Check(invalid_cases == 40U,
        "invalid mode, row, and combined states consume all eight edge masks");

    const DiagnosticMenuState first_row = InitialDiagnosticMenuState();
    const DiagnosticMenuState last_row{
        .mode = DiagnosticMenuMode::MainMenu,
        .selected_row = DiagnosticMenuRow::ReservedProjectTwo,
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
                  .selected_row = DiagnosticMenuRow::ReservedProjectOne,
              },
              DiagnosticMenuInputEdges{
                  .primary_pressed = true,
                  .previous_pressed = true,
                  .next_pressed = true,
              }) ==
              DiagnosticMenuState{
                  .mode = DiagnosticMenuMode::MainMenu,
                  .selected_row = DiagnosticMenuRow::ReservedProjectOne,
              },
        "primary consumes navigation while reserved project rows remain inert");

    Check(omega::app::kDiagnosticMenuToggleAction == 6U &&
              omega::app::kDiagnosticMenuPrimaryAction == 6U &&
              omega::app::kDiagnosticMenuPreviousAction == 2U &&
              omega::app::kDiagnosticMenuNextAction == 3U &&
              omega::app::kDiagnosticMenuRowCount == 3U &&
              omega::app::kDiagnosticMenuImageWidth == 128U &&
              omega::app::kDiagnosticMenuImageHeight == 72U,
        "the project action identifiers, row count, and image dimensions remain exact");

    const omega::runtime::DebugImage first = BuildProjectDiagnosticMenuImage();
    const omega::runtime::DebugImage second = BuildProjectDiagnosticMenuImage();
    constexpr std::size_t kExpectedBytes = 128U * 72U * 4U;
    const bool first_shape_is_safe = first.width == 128U && first.height == 72U &&
                                     first.rgba8_pixels.size() == kExpectedBytes &&
                                     first.pixels().size() == kExpectedBytes;
    Check(first_shape_is_safe,
        "the menu is one fully owned tightly packed 128x72 RGBA8 image");
    if (!first_shape_is_safe)
        return EXIT_FAILURE;
    Check(second.width == first.width && second.height == first.height &&
              second.rgba8_pixels == first.rgba8_pixels,
        "two independent menu builds are byte-identical");

    bool all_alpha_opaque = first.rgba8_pixels.size() == kExpectedBytes;
    std::size_t background_pixels = 0U;
    std::size_t cyan_pixels = 0U;
    std::size_t slate_pixels = 0U;
    std::size_t amber_pixels = 0U;
    std::size_t unknown_pixels = 0U;
    for (std::size_t offset = 0U; offset + 3U < first.rgba8_pixels.size();
         offset += 4U)
    {
        const Color pixel{first.rgba8_pixels[offset],
            first.rgba8_pixels[offset + 1U], first.rgba8_pixels[offset + 2U],
            first.rgba8_pixels[offset + 3U]};
        all_alpha_opaque =
            all_alpha_opaque && pixel[3] == std::byte{255U};
        if (pixel == kBackgroundColor)
            ++background_pixels;
        else if (pixel == kCyanColor)
            ++cyan_pixels;
        else if (pixel == kSlateColor)
            ++slate_pixels;
        else if (pixel == kAmberColor)
            ++amber_pixels;
        else
            ++unknown_pixels;
    }
    Check(all_alpha_opaque, "every menu pixel has fully opaque alpha");
    Check(background_pixels == 3'928U && cyan_pixels == 1'020U &&
              slate_pixels == 3'788U && amber_pixels == 480U &&
              unknown_pixels == 0U,
        "the four project-authored colors have their exact independent pixel counts");

    Check(PixelAt(first, 4U, 4U) == kBackgroundColor &&
              PixelAt(first, 2U, 2U) == kBackgroundColor,
        "interior pixels outside the card geometry retain the background");
    Check(PixelAt(first, 0U, 0U) == kCyanColor &&
              PixelAt(first, 1U, 35U) == kCyanColor &&
              PixelAt(first, 126U, 35U) == kCyanColor &&
              PixelAt(first, 64U, 70U) == kCyanColor &&
              PixelAt(first, 2U, 2U) != kCyanColor,
        "the cyan frame occupies exactly two pixels on every edge");
    Check(PixelAt(first, 7U, 7U) == kSlateColor &&
              PixelAt(first, 20U, 30U) == kSlateColor &&
              PixelAt(first, 20U, 45U) == kSlateColor &&
              PixelAt(first, 20U, 60U) == kSlateColor,
        "the title panel and all three geometric rows use slate");
    Check(PixelAt(first, 40U, 12U) == kAmberColor &&
              PixelAt(first, 115U, 15U) == kAmberColor,
        "the header status bar uses the exact amber half-open rectangle");
    Check(PixelAt(first, 8U, 8U) == kCyanColor &&
              PixelAt(first, 16U, 8U) == kCyanColor &&
              PixelAt(first, 24U, 8U) == kCyanColor &&
              PixelAt(first, 28U, 8U) == kCyanColor &&
              PixelAt(first, 26U, 16U) == kCyanColor &&
              PixelAt(first, 10U, 10U) == kSlateColor &&
              PixelAt(first, 24U, 16U) == kSlateColor,
        "representative filled and empty cells preserve the project-authored DEV glyph masks");

    const std::uint64_t digest = Fnv1a64(first.pixels());
    std::cout << "diagnostic menu FNV-1a-64: 0x" << std::hex
              << std::setfill('0') << std::setw(16) << digest << std::dec << '\n';
    Check(digest == kExpectedDiagnosticMenuFnv1a64,
        "the complete deterministic RGBA8 image matches the independently frozen digest");

    if (failures != 0)
    {
        std::cerr << failures << " diagnostic menu test(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "diagnostic menu tests passed\n";
    return EXIT_SUCCESS;
}
