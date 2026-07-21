#include "startup_failure_dialog.h"

#include "omega/runtime/runtime_settings.h"

#include <SDL3/SDL_init.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
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

[[nodiscard]] bool WriteTextFile(
    const std::filesystem::path& path, const std::string_view text)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    return output.good();
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
        StageCase{omega::app::StartupFailureStage::NativePersistence,
                  "native persistence"},
        StageCase{omega::app::StartupFailureStage::ApplicationStartup,
                  "application startup"},
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

template<typename T>
void CheckRuntimePrivacyProjection(
    const std::expected<T, std::string>& result,
    const omega::app::StartupFailureStage stage,
    const std::string_view stage_label,
    const std::string_view category,
    const std::string_view expected_detail,
    const std::filesystem::path& private_path,
    const std::string_view context)
{
    Check(!result, context);
    if (result)
        return;

    Check(result.error() == expected_detail, context);
    const std::string dialog = Text(Build(stage, category, result.error()));
    Check(dialog == ExpectedText(stage_label, category, expected_detail),
        "fixed runtime detail reaches the dialog projection unchanged");
    Check(dialog.find(private_path.string()) == std::string::npos,
        "runtime privacy dialogs omit the complete source path");
    constexpr std::array forbidden{
        std::string_view("PrivateUser"),
        std::string_view("SecretVault"),
        std::string_view("privateuser"),
        std::string_view("secretvault"),
        std::string_view("raw-secret"),
        std::string_view("C:/Users/"),
    };
    for (const std::string_view fragment : forbidden)
        Check(dialog.find(fragment) == std::string::npos,
            "runtime privacy dialogs omit raw config fragments");
}

void TestRuntimeConfigurationPrivacyProjection()
{
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path() /
                      ("openomega-dialog-PrivateUser-SecretVault-" +
                          std::to_string(suffix));
    std::error_code file_error;
    std::filesystem::remove_all(root, file_error);
    file_error.clear();
    std::filesystem::create_directories(root, file_error);
    Check(!file_error, "the dialog privacy fixture root is created");

    const auto missing_explicit = root / "missing-explicit.cfg";
    omega::runtime::LaunchOptions explicit_options;
    explicit_options.config_path = missing_explicit;
    CheckRuntimePrivacyProjection(
        omega::runtime::LoadRuntimeConfig(explicit_options),
        omega::app::StartupFailureStage::RuntimeConfiguration,
        "runtime configuration", "runtime-configuration",
        "runtime configuration explicit profile: unable to open config file",
        missing_explicit,
        "an explicit config error is available for dialog projection");

    const auto nonregular_default = root / "default-profile.cfg";
    std::filesystem::create_directory(nonregular_default, file_error);
    Check(!file_error, "the non-regular dialog privacy fixture is created");
    CheckRuntimePrivacyProjection(
        omega::runtime::LoadRuntimeConfig({}, nonregular_default),
        omega::app::StartupFailureStage::RuntimeConfiguration,
        "runtime configuration", "runtime-configuration",
        "runtime configuration default profile: config path is not a regular file",
        nonregular_default,
        "a default config error is available for dialog projection");

    const auto malformed_profile = root / "PrivateUser-SecretVault-malformed.cfg";
    Check(WriteTextFile(malformed_profile,
              "PrivateUser.SecretVault = C:/Users/PrivateUser/SecretVault/raw-secret\n"),
        "the malformed private dialog profile is written");
    omega::runtime::LaunchOptions malformed_options;
    malformed_options.config_path = malformed_profile;
    CheckRuntimePrivacyProjection(
        omega::runtime::LoadRuntimeConfig(malformed_options),
        omega::app::StartupFailureStage::RuntimeConfiguration,
        "runtime configuration", "runtime-configuration",
        "runtime configuration explicit profile: config line 1: config key contains a byte outside [a-z0-9_.]",
        malformed_profile,
        "malformed file key and value are absent from the dialog");

    const auto duplicate_profile = root / "PrivateUser-SecretVault-duplicate.cfg";
    Check(WriteTextFile(duplicate_profile,
              "privateuser.secretvault = PrivateUser-first-secret\n"
              "privateuser.secretvault = SecretVault-second-secret\n"),
        "the duplicate private dialog profile is written");
    omega::runtime::LaunchOptions duplicate_options;
    duplicate_options.config_path = duplicate_profile;
    CheckRuntimePrivacyProjection(
        omega::runtime::LoadRuntimeConfig(duplicate_options),
        omega::app::StartupFailureStage::RuntimeConfiguration,
        "runtime configuration", "runtime-configuration",
        "runtime configuration explicit profile: config line 2 duplicates an earlier key",
        duplicate_profile,
        "duplicate file key and values are absent from the dialog");

    const auto integer_profile = root / "PrivateUser-SecretVault-integer.cfg";
    Check(WriteTextFile(integer_profile,
              "jobs.worker_count = C:/Users/PrivateUser/SecretVault/raw-secret-integer\n"),
        "the private integer dialog profile is written");
    omega::runtime::LaunchOptions integer_options;
    integer_options.config_path = integer_profile;
    auto integer_config = omega::runtime::LoadRuntimeConfig(integer_options);
    Check(integer_config.has_value(), "the private integer dialog profile loads");
    if (integer_config)
    {
        CheckRuntimePrivacyProjection(
            omega::runtime::ResolveRuntimeSettings(*integer_config),
            omega::app::StartupFailureStage::RuntimeSettings,
            "runtime settings", "runtime-settings",
            "jobs.worker_count: config integer value must be a canonical base-10 int64",
            integer_profile,
            "invalid file integer values are absent from the dialog");
    }

    const auto severity_profile = root / "PrivateUser-SecretVault-severity.cfg";
    Check(WriteTextFile(severity_profile,
              "log.minimum_severity = PrivateUser-SecretVault-raw-secret-severity\n"),
        "the private unsupported-value dialog profile is written");
    omega::runtime::LaunchOptions severity_options;
    severity_options.config_path = severity_profile;
    auto severity_config = omega::runtime::LoadRuntimeConfig(severity_options);
    Check(severity_config.has_value(), "the private unsupported-value dialog profile loads");
    if (severity_config)
    {
        CheckRuntimePrivacyProjection(
            omega::runtime::ResolveRuntimeSettings(*severity_config),
            omega::app::StartupFailureStage::RuntimeSettings,
            "runtime settings", "runtime-settings",
            "log.minimum_severity must be one of trace, debug, info, warning, or error",
            severity_profile,
            "unsupported file values are absent from the dialog");
    }

    const auto unknown_profile = root / "PrivateUser-SecretVault-unknown.cfg";
    Check(WriteTextFile(unknown_profile,
              "privateuser.secretvault = PrivateUser-SecretVault-raw-secret-unknown\n"),
        "the private unknown-setting dialog profile is written");
    omega::runtime::LaunchOptions unknown_options;
    unknown_options.config_path = unknown_profile;
    auto unknown_config = omega::runtime::LoadRuntimeConfig(unknown_options);
    Check(unknown_config.has_value(), "the private unknown-setting dialog profile loads");
    if (unknown_config)
    {
        CheckRuntimePrivacyProjection(
            omega::runtime::ResolveRuntimeSettings(*unknown_config),
            omega::app::StartupFailureStage::RuntimeSettings,
            "runtime settings", "runtime-settings",
            "unknown runtime setting",
            unknown_profile,
            "unknown file key and value are absent from the dialog");
    }

    const auto boolean_profile = root / "PrivateUser-SecretVault-boolean.cfg";
    Check(WriteTextFile(boolean_profile,
              "privateuser.flag = C:/Users/PrivateUser/SecretVault/raw-secret-boolean\n"),
        "the private boolean dialog profile is written");
    omega::runtime::LaunchOptions boolean_options;
    boolean_options.config_path = boolean_profile;
    auto boolean_config = omega::runtime::LoadRuntimeConfig(boolean_options);
    Check(boolean_config.has_value(), "the private boolean dialog profile loads");
    if (boolean_config)
    {
        CheckRuntimePrivacyProjection(
            boolean_config->GetBool("privateuser.flag"),
            omega::app::StartupFailureStage::RuntimeSettings,
            "runtime settings", "runtime-settings",
            "config boolean value must be exactly 'true' or 'false'",
            boolean_profile,
            "invalid file boolean values are absent from the dialog projection");
    }

    omega::runtime::LaunchOptions malformed_override;
    malformed_override.config_overrides.push_back(
        {.key = "PrivateUser.SecretVault", .value = "raw-secret-override"});
    CheckRuntimePrivacyProjection(
        omega::runtime::LoadRuntimeConfig(malformed_override),
        omega::app::StartupFailureStage::RuntimeConfiguration,
        "runtime configuration", "runtime-configuration",
        "--set override: config override: config key contains a byte outside [a-z0-9_.]",
        root,
        "malformed --set key and value are absent from the dialog");

    omega::runtime::LaunchOptions boolean_override;
    boolean_override.config_overrides.push_back(
        {.key = "privateuser.flag",
            .value = "C:/Users/PrivateUser/SecretVault/raw-secret-boolean-override"});
    auto boolean_override_config = omega::runtime::LoadRuntimeConfig(boolean_override);
    Check(boolean_override_config.has_value(), "the private boolean --set dialog fixture loads");
    if (boolean_override_config)
    {
        CheckRuntimePrivacyProjection(
            boolean_override_config->GetBool("privateuser.flag"),
            omega::app::StartupFailureStage::RuntimeSettings,
            "runtime settings", "runtime-settings",
            "config boolean value must be exactly 'true' or 'false'",
            root,
            "invalid --set boolean values are absent from the dialog projection");
    }

    omega::runtime::LaunchOptions integer_override;
    integer_override.config_overrides.push_back(
        {.key = "jobs.worker_count",
            .value = "C:/Users/PrivateUser/SecretVault/raw-secret-override"});
    auto integer_override_config = omega::runtime::LoadRuntimeConfig(integer_override);
    Check(integer_override_config.has_value(), "the private integer --set dialog fixture loads");
    if (integer_override_config)
    {
        CheckRuntimePrivacyProjection(
            omega::runtime::ResolveRuntimeSettings(*integer_override_config),
            omega::app::StartupFailureStage::RuntimeSettings,
            "runtime settings", "runtime-settings",
            "jobs.worker_count: config integer value must be a canonical base-10 int64",
            root,
            "invalid --set integer values are absent from the dialog");
    }

    omega::runtime::LaunchOptions severity_override;
    severity_override.config_overrides.push_back(
        {.key = "log.minimum_severity",
            .value = "PrivateUser-SecretVault-raw-secret-severity-override"});
    auto severity_override_config = omega::runtime::LoadRuntimeConfig(severity_override);
    Check(severity_override_config.has_value(),
        "the private unsupported-value --set dialog fixture loads");
    if (severity_override_config)
    {
        CheckRuntimePrivacyProjection(
            omega::runtime::ResolveRuntimeSettings(*severity_override_config),
            omega::app::StartupFailureStage::RuntimeSettings,
            "runtime settings", "runtime-settings",
            "log.minimum_severity must be one of trace, debug, info, warning, or error",
            root,
            "unsupported --set values are absent from the dialog");
    }

    omega::runtime::LaunchOptions unknown_override;
    unknown_override.config_overrides.push_back(
        {.key = "privateuser.secretvault", .value = "PrivateUser-SecretVault-raw-secret"});
    auto unknown_override_config = omega::runtime::LoadRuntimeConfig(unknown_override);
    Check(unknown_override_config.has_value(), "the private unknown --set dialog fixture loads");
    if (unknown_override_config)
    {
        CheckRuntimePrivacyProjection(
            omega::runtime::ResolveRuntimeSettings(*unknown_override_config),
            omega::app::StartupFailureStage::RuntimeSettings,
            "runtime settings", "runtime-settings",
            "unknown runtime setting",
            root,
            "unknown --set key and value are absent from the dialog");
    }

    std::filesystem::remove_all(root, file_error);
    Check(!file_error, "the dialog privacy fixture root is removed");
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
    static_assert(static_cast<std::uint8_t>(StartupFailureStage::NativePersistence) == 4U);
    static_assert(static_cast<std::uint8_t>(StartupFailureStage::ApplicationStartup) == 5U);
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
    TestRuntimeConfigurationPrivacyProjection();
    TestPolicyAndSuppressedShow();

    if (failures != 0)
    {
        std::cerr << failures << " startup failure dialog test(s) failed\n";
        return 1;
    }
    std::cout << "startup failure dialog tests passed\n";
    return 0;
}
