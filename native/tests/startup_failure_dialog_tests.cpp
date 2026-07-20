#include "startup_failure_dialog.h"

#include <SDL3/SDL_init.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>

namespace
{
int failures = 0;

void Check(const bool condition, const std::string_view message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

[[nodiscard]] std::string Text(const omega::app::StartupFailureDialogText& text)
{
    return std::string(text.bytes.data(), text.size);
}

[[nodiscard]] std::string ExpectedText(
    const std::string_view stage,
    const std::string_view category,
    const std::string_view detail)
{
    return "OpenOmega could not reach the main menu.\n\nStage: " + std::string(stage) +
           "\nCode: " + std::string(category) + "\nDetail: " + std::string(detail) +
           "\n\nCheck the private OpenOmega configuration, game-data root, or launch "
           "arguments, then try again.";
}

[[nodiscard]] omega::app::StartupFailureDialogText Build(
    const omega::app::StartupFailureStage stage,
    const std::string_view category,
    const std::string_view detail)
{
    return omega::app::BuildStartupFailureDialogText(
        {.stage = stage, .category = category, .detail = detail});
}

void TestStageLabelsAndFallbacks()
{
    struct StageCase
    {
        omega::app::StartupFailureStage stage;
        std::string_view label;
    };
    constexpr std::array cases{
        StageCase{omega::app::StartupFailureStage::RuntimeConfiguration,
                  "runtime configuration"},
        StageCase{omega::app::StartupFailureStage::RuntimeSettings,
                  "runtime settings"},
        StageCase{omega::app::StartupFailureStage::ContentLaunchProfile,
                  "content launch profile"},
        StageCase{omega::app::StartupFailureStage::ContentStartup,
                  "content startup"},
    };

    for (const StageCase& test : cases)
    {
        Check(Text(Build(test.stage, "code", "detail")) ==
                  ExpectedText(test.label, "code", "detail"),
              "stage label and complete template remain exact");
    }

    Check(Text(Build(static_cast<omega::app::StartupFailureStage>(0xffU),
                   "code", "detail")) == ExpectedText("startup", "code", "detail"),
          "unknown stage uses the fixed startup fallback");
    Check(Text(Build(omega::app::StartupFailureStage::ContentStartup, "", "")) ==
              ExpectedText("content startup", "unknown",
                           "No additional detail is available."),
          "empty category and detail use fixed fallbacks");
    Check(Text(Build(omega::app::StartupFailureStage::ContentStartup,
                   " \t\r\n", " \t\r\n")) ==
              ExpectedText("content startup", "unknown",
                           "No additional detail is available."),
          "whitespace-only category and detail use fixed fallbacks");
}

void TestProjection()
{
    Check(Text(Build(omega::app::StartupFailureStage::RuntimeSettings,
                   "  alpha\t \r\nbeta  ", " one\r\ntwo\t three ")) ==
              ExpectedText("runtime settings", "alpha beta", "one two three"),
          "projected whitespace is collapsed and trimmed");
    Check(Text(Build(omega::app::StartupFailureStage::RuntimeSettings,
                   "!~", "detail")) ==
              ExpectedText("runtime settings", "!~", "detail"),
          "printable ASCII endpoints are preserved");

    const std::array hostile{
        'A', '\0', static_cast<char>(0x01), static_cast<char>(0x7f),
        static_cast<char>(0x80), static_cast<char>(0xff), 'Z'};
    Check(Text(Build(omega::app::StartupFailureStage::RuntimeSettings,
                   std::string_view(hostile.data(), hostile.size()), "detail")) ==
              ExpectedText("runtime settings", "A?????Z", "detail"),
          "NUL, controls, DEL, and high bytes are replaced");
}

void TestLimitsAndCapacity()
{
    const std::string category_at_limit(
        omega::app::kStartupFailureDialogCategoryLimit, 'C');
    const std::string category_over_limit(
        omega::app::kStartupFailureDialogCategoryLimit + 1U, 'C');
    const std::string category_truncated(
        omega::app::kStartupFailureDialogCategoryLimit - 3U, 'C');
    const std::string detail_at_limit(
        omega::app::kStartupFailureDialogDetailLimit, 'D');
    const std::string detail_over_limit(
        omega::app::kStartupFailureDialogDetailLimit + 1U, 'D');
    const std::string detail_truncated(
        omega::app::kStartupFailureDialogDetailLimit - 3U, 'D');

    Check(Text(Build(omega::app::StartupFailureStage::RuntimeConfiguration,
                   category_at_limit, "detail")) ==
              ExpectedText("runtime configuration", category_at_limit, "detail"),
          "category at the limit is not truncated");
    Check(Text(Build(omega::app::StartupFailureStage::RuntimeConfiguration,
                   category_over_limit, "detail")) ==
              ExpectedText("runtime configuration", category_truncated + "...", "detail"),
          "category over the limit uses the frozen ellipsis rule");
    Check(Text(Build(omega::app::StartupFailureStage::RuntimeConfiguration,
                   category_at_limit + " \t\r\n", "detail")) ==
              ExpectedText("runtime configuration", category_at_limit, "detail"),
          "trailing projected whitespace does not cause truncation");
    Check(Text(Build(omega::app::StartupFailureStage::ContentStartup,
                   "code", detail_at_limit)) ==
              ExpectedText("content startup", "code", detail_at_limit),
          "detail at the limit is not truncated");
    Check(Text(Build(omega::app::StartupFailureStage::ContentStartup,
                   "code", detail_over_limit)) ==
              ExpectedText("content startup", "code", detail_truncated + "..."),
          "detail over the limit uses the frozen ellipsis rule");

    const auto maximum = Build(omega::app::StartupFailureStage::ContentLaunchProfile,
                               category_at_limit, detail_at_limit);
    Check(maximum.size == omega::app::kStartupFailureDialogMaximumTextSize,
          "maximum projected dialog text has the frozen size");
    Check(maximum.size < maximum.bytes.size() && maximum.bytes[maximum.size] == '\0',
          "maximum projected dialog text is NUL terminated in bounds");
    Check(maximum.size != 0U && maximum.bytes[maximum.size - 1U] == '.',
          "dialog text has no trailing newline");
    for (std::size_t index = 0U; index < maximum.size; ++index)
    {
        Check(static_cast<unsigned char>(maximum.bytes[index]) <= 0x7fU,
              "dialog text contains only ASCII bytes");
    }
}

void TestOwnershipAndIndependence()
{
    std::string category = "owned-category";
    std::string detail = "owned-detail";
    const auto first = Build(omega::app::StartupFailureStage::ContentStartup,
                             category, detail);
    const std::string expected = Text(first);
    category.assign("changed-category");
    detail.assign("changed-detail");
    Check(Text(first) == expected, "built dialog text owns its projected bytes");

    omega::app::StartupFailureDialogText after_source_destruction;
    {
        const std::string scoped_category = "scoped-category";
        const std::string scoped_detail = "scoped-detail";
        after_source_destruction = Build(
            omega::app::StartupFailureStage::ContentStartup,
            scoped_category, scoped_detail);
    }
    Check(Text(after_source_destruction) ==
              ExpectedText("content startup", "scoped-category", "scoped-detail"),
          "built dialog text survives source destruction");

    const auto second = Build(omega::app::StartupFailureStage::RuntimeSettings,
                              "second", "independent");
    Check(Text(first) == expected, "independent builds share no mutable state");
    Check(Text(second) == ExpectedText("runtime settings", "second", "independent"),
          "independent build returns its own exact text");
}

void TestPolicyAndSuppressedShow()
{
    using omega::app::StartupFailureDialogOutcome;
    using omega::app::StartupFailureDialogPolicy;

    Check(omega::app::ResolveStartupFailureDialogPolicy(nullptr) ==
              StartupFailureDialogPolicy::Allow,
          "missing suppression value allows presentation");
    constexpr std::array allowed_values{"", "0", "true", "TRUE", " 1", "1 ", "01"};
    for (const char* const value : allowed_values)
    {
        Check(omega::app::ResolveStartupFailureDialogPolicy(value) ==
                  StartupFailureDialogPolicy::Allow,
              "only exact one suppresses presentation");
    }
    Check(omega::app::ResolveStartupFailureDialogPolicy("1") ==
              StartupFailureDialogPolicy::Suppress,
          "exact one suppresses presentation");

    const omega::app::StartupFailureDialogRequest request{
        .stage = omega::app::StartupFailureStage::ContentStartup,
        .category = "code",
        .detail = "detail",
    };
    Check(SDL_WasInit(0) == 0U, "SDL begins uninitialized in the dialog unit test");
    Check(omega::app::TryShowStartupFailureDialog(
              request, StartupFailureDialogPolicy::Suppress) ==
              StartupFailureDialogOutcome::Suppressed,
          "suppressed show returns without presentation");
    Check(omega::app::TryShowStartupFailureDialog(
              request, static_cast<StartupFailureDialogPolicy>(0xffU)) ==
              StartupFailureDialogOutcome::Suppressed,
          "invalid policy fails closed without presentation");
    Check(SDL_WasInit(0) == 0U,
          "suppressed and invalid-policy paths do not initialize SDL");
}
} // namespace

int main()
{
    using omega::app::StartupFailureDialogOutcome;
    using omega::app::StartupFailureDialogPolicy;
    using omega::app::StartupFailureDialogRequest;
    using omega::app::StartupFailureStage;

    static_assert(sizeof(StartupFailureStage) == sizeof(std::uint8_t));
    static_assert(sizeof(StartupFailureDialogPolicy) == sizeof(std::uint8_t));
    static_assert(sizeof(StartupFailureDialogOutcome) == sizeof(std::uint8_t));
    static_assert(static_cast<std::uint8_t>(StartupFailureStage::RuntimeConfiguration) == 0U);
    static_assert(static_cast<std::uint8_t>(StartupFailureStage::RuntimeSettings) == 1U);
    static_assert(static_cast<std::uint8_t>(StartupFailureStage::ContentLaunchProfile) == 2U);
    static_assert(static_cast<std::uint8_t>(StartupFailureStage::ContentStartup) == 3U);
    static_assert(static_cast<std::uint8_t>(StartupFailureDialogPolicy::Allow) == 0U);
    static_assert(static_cast<std::uint8_t>(StartupFailureDialogPolicy::Suppress) == 1U);
    static_assert(static_cast<std::uint8_t>(StartupFailureDialogOutcome::Suppressed) == 0U);
    static_assert(static_cast<std::uint8_t>(StartupFailureDialogOutcome::Presented) == 1U);
    static_assert(static_cast<std::uint8_t>(StartupFailureDialogOutcome::Unavailable) == 2U);

    constexpr StartupFailureDialogRequest request{
        .stage = StartupFailureStage::RuntimeConfiguration,
        .category = "code",
        .detail = "detail",
    };
    static_assert(noexcept(omega::app::BuildStartupFailureDialogText(request)));
    static_assert(noexcept(omega::app::ResolveStartupFailureDialogPolicy(nullptr)));
    static_assert(noexcept(omega::app::ReadStartupFailureDialogPolicyFromEnvironment()));
    static_assert(noexcept(omega::app::TryShowStartupFailureDialog(
        request, StartupFailureDialogPolicy::Suppress)));

    static_assert(std::string_view(omega::app::kStartupFailureDialogTitle) ==
                  "OpenOmega startup error");
    static_assert(std::string_view(
                      omega::app::kStartupFailureDialogDisableEnvironmentVariable) ==
                  "OPENOMEGA_DISABLE_STARTUP_DIALOG");
    static_assert(omega::app::kStartupFailureDialogCategoryLimit == 48U);
    static_assert(omega::app::kStartupFailureDialogDetailLimit == 384U);
    static_assert(omega::app::kStartupFailureDialogMaximumTextSize == 616U);
    static_assert(omega::app::kStartupFailureDialogTextCapacity == 640U);

    TestStageLabelsAndFallbacks();
    TestProjection();
    TestLimitsAndCapacity();
    TestOwnershipAndIndependence();
    TestPolicyAndSuppressedShow();

    if (failures != 0)
    {
        std::cerr << failures << " startup failure dialog test(s) failed\n";
        return 1;
    }
    std::cout << "startup failure dialog tests passed\n";
    return 0;
}
