#include "omega/runtime/runtime_settings.h"

#include <array>
#include <chrono>
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

[[nodiscard]] bool WriteTextFile(
    const std::filesystem::path& path, const std::string_view text)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    return output.good();
}

void CheckProfileError(
    const std::expected<std::optional<omega::runtime::ContentLaunchProfile>,
        omega::runtime::ContentLaunchProfileError>& result,
    const omega::runtime::ContentLaunchProfileErrorCode code,
    const std::string_view message,
    const std::string_view context)
{
    Check(!result && result.error().code == code && result.error().message == message, context);
}

void CheckNoPrivateFragments(
    const std::string_view diagnostic, const std::string_view context)
{
    constexpr std::array forbidden{
        std::string_view("PrivateUser"),
        std::string_view("SecretVault"),
        std::string_view("privateuser"),
        std::string_view("secretvault"),
        std::string_view("raw-secret"),
        std::string_view("C:/Users/"),
    };
    for (const std::string_view fragment : forbidden)
        Check(diagnostic.find(fragment) == std::string_view::npos, context);
}

template<typename T>
void CheckInputFreeError(const std::expected<T, std::string>& result,
    const std::string_view expected, const std::string_view context)
{
    Check(!result, context);
    if (result)
        return;
    Check(result.error() == expected, context);
    CheckNoPrivateFragments(result.error(), context);
}
} // namespace

int RuntimeSettingsFailureCount()
{
    auto empty = omega::runtime::ParseConfigText("");
    Check(empty.has_value(), "an empty runtime configuration parses");
    if (empty)
    {
        auto defaults = omega::runtime::ResolveRuntimeSettings(*empty);
        Check(defaults &&
                  defaults->minimum_log_severity == omega::runtime::LogSeverity::Info &&
                  defaults->log_ring_capacity == omega::runtime::kDefaultLogRingCapacity &&
                  defaults->jobs.worker_count == omega::runtime::DefaultJobWorkerCount() &&
                  defaults->jobs.max_pending_jobs ==
                      omega::runtime::kDefaultMaxPendingJobs &&
                  defaults->frame.simulation_step ==
                      omega::runtime::kDefaultSimulationStep &&
                  defaults->frame.max_steps_per_frame ==
                      omega::runtime::kDefaultMaxStepsPerFrame &&
                   defaults->frame.max_frame_delta ==
                       omega::runtime::kDefaultMaxFrameDelta &&
                   defaults->max_input_events_per_frame ==
                       omega::runtime::kDefaultMaxInputEventsPerFrame &&
                   !defaults->gamepad_enabled,
            "the empty store resolves to explicit synthetic-shell defaults");
    }

    auto configured = omega::runtime::ParseConfigText(
        "log.minimum_severity = debug\n"
        "log.ring_capacity = 17\n"
        "jobs.worker_count = 2\n"
        "jobs.max_pending_jobs = 33\n"
        "frame.simulation_step_ns = 1000000\n"
        "frame.max_steps_per_frame = 3\n"
        "frame.max_delta_ns = 5000000\n"
        "input.max_events_per_frame = 41\n"
        "input.gamepad_enabled = true\n");
    Check(configured.has_value(), "a complete runtime configuration parses");
    if (configured)
    {
        auto resolved = omega::runtime::ResolveRuntimeSettings(*configured);
        Check(resolved && resolved->minimum_log_severity == omega::runtime::LogSeverity::Debug &&
                  resolved->log_ring_capacity == 17U && resolved->jobs.worker_count == 2U &&
                  resolved->jobs.max_pending_jobs == 33U &&
                  resolved->frame.simulation_step == std::chrono::milliseconds{1} &&
                  resolved->frame.max_steps_per_frame == 3U &&
                  resolved->frame.max_frame_delta == std::chrono::milliseconds{5} &&
                  resolved->max_input_events_per_frame == 41U &&
                  resolved->gamepad_enabled,
            "known keys resolve without clamping or hidden defaults");
    }

    auto unknown = omega::runtime::ParseConfigText(
        "privateuser.secretvault = PrivateUser-SecretVault-raw-secret\n");
    Check(unknown.has_value(), "unknown private setting fixture parses");
    if (unknown)
    {
        CheckInputFreeError(omega::runtime::ResolveRuntimeSettings(*unknown),
            "unknown runtime setting",
            "unknown-setting diagnostics omit the key and value");
    }
    auto bad_severity = omega::runtime::ParseConfigText(
        "log.minimum_severity = PrivateUser-SecretVault-raw-secret-severity\n");
    Check(bad_severity.has_value(), "private log severity fixture parses");
    if (bad_severity)
    {
        CheckInputFreeError(omega::runtime::ResolveRuntimeSettings(*bad_severity),
            "log.minimum_severity must be one of trace, debug, info, warning, or error",
            "unsupported log severities are rejected without echoing the raw value");
    }
    auto bad_workers = omega::runtime::ParseConfigText("jobs.worker_count = 0\n");
    Check(bad_workers && !omega::runtime::ResolveRuntimeSettings(*bad_workers),
        "service-owned worker bounds are enforced");
    auto bad_delta = omega::runtime::ParseConfigText(
        "frame.simulation_step_ns = 2000000\nframe.max_delta_ns = 1000000\n");
    Check(bad_delta && !omega::runtime::ResolveRuntimeSettings(*bad_delta),
        "the frame delta clamp cannot be shorter than one simulation step");
    auto bad_integer = omega::runtime::ParseConfigText(
        "input.max_events_per_frame = C:/Users/"
        "PrivateUser/SecretVault/raw-secret\n");
    Check(bad_integer.has_value(), "private integer setting fixture parses");
    if (bad_integer)
    {
        CheckInputFreeError(omega::runtime::ResolveRuntimeSettings(*bad_integer),
            "input.max_events_per_frame: config integer value must be a canonical base-10 int64",
            "typed setting diagnostics retain strict parsing without raw values");
    }
    auto bad_gamepad = omega::runtime::ParseConfigText(
        "input.gamepad_enabled = PrivateUser-SecretVault-raw-secret\n");
    Check(bad_gamepad.has_value(), "private gamepad setting fixture parses");
    if (bad_gamepad)
    {
        CheckInputFreeError(omega::runtime::ResolveRuntimeSettings(*bad_gamepad),
            "input.gamepad_enabled: config boolean value must be exactly 'true' or 'false'",
            "gamepad opt-in rejects non-boolean values without echoing private input");
    }

    using omega::runtime::ContentLaunchProfileErrorCode;
    Check(omega::runtime::ContentLaunchProfileErrorCodeName(
              ContentLaunchProfileErrorCode::MissingDataRoot) == "missing-data-root" &&
              omega::runtime::ContentLaunchProfileErrorCodeName(
                  ContentLaunchProfileErrorCode::InvalidDataRoot) == "invalid-data-root" &&
              omega::runtime::ContentLaunchProfileErrorCodeName(
                  ContentLaunchProfileErrorCode::InvalidLevelCode) == "invalid-level-code" &&
              omega::runtime::ContentLaunchProfileErrorCodeName(
                  ContentLaunchProfileErrorCode::InvalidOptions) == "invalid-options" &&
              omega::runtime::ContentLaunchProfileErrorCodeName(
                  static_cast<ContentLaunchProfileErrorCode>(255U)) == "unknown",
        "content launch profile error names are stable and unknown values fail closed");

    omega::runtime::LaunchOptions no_content_options;
    if (empty)
    {
        auto no_content = omega::runtime::ResolveContentLaunchProfile(no_content_options, *empty);
        Check(no_content && !no_content->has_value(),
            "an empty effective store and empty direct options select no content");
    }

    auto configured_content = omega::runtime::ParseConfigText(
        "content.data_root = configured/content\n"
        "content.level_code = minsk2\n");
    Check(configured_content && omega::runtime::ResolveRuntimeSettings(*configured_content),
        "content launch keys are known strict runtime settings");
    if (configured_content)
    {
        auto resolved_content = omega::runtime::ResolveContentLaunchProfile(
            no_content_options, *configured_content);
        Check(resolved_content && resolved_content->has_value() &&
                  (*resolved_content)->data_root ==
                      std::filesystem::path("configured/content") &&
                  (*resolved_content)->level_code == std::optional<std::string>{"MINSK2"},
            "configured content resolves a native path and uppercase level code");

        omega::runtime::LaunchOptions direct_content;
        direct_content.data_root = std::filesystem::path("direct/content");
        direct_content.level_code = "level7";
        auto direct_resolved = omega::runtime::ResolveContentLaunchProfile(
            direct_content, *configured_content);
        Check(direct_resolved && direct_resolved->has_value() &&
                  (*direct_resolved)->data_root == std::filesystem::path("direct/content") &&
                  (*direct_resolved)->level_code == std::optional<std::string>{"LEVEL7"},
            "a valid direct root and level atomically override configured content");

        direct_content.level_code.reset();
        auto root_only = omega::runtime::ResolveContentLaunchProfile(
            direct_content, *configured_content);
        Check(root_only && root_only->has_value() &&
                  (*root_only)->data_root == std::filesystem::path("direct/content") &&
                  !(*root_only)->level_code,
            "a direct root never inherits the configured level code");
    }

    auto missing_content_root = omega::runtime::ParseConfigText(
        "content.level_code = MINSK\n");
    Check(missing_content_root.has_value(), "the missing-root fixture parses");
    if (missing_content_root)
    {
        auto result = omega::runtime::ResolveContentLaunchProfile(
            no_content_options, *missing_content_root);
        CheckProfileError(result, ContentLaunchProfileErrorCode::MissingDataRoot,
            "content.data_root is required when content.level_code is set",
            "a configured level without a root returns the fixed missing-root error");

        omega::runtime::LaunchOptions valid_direct;
        valid_direct.data_root = std::filesystem::path("direct/content");
        result = omega::runtime::ResolveContentLaunchProfile(valid_direct, *missing_content_root);
        CheckProfileError(result, ContentLaunchProfileErrorCode::MissingDataRoot,
            "content.data_root is required when content.level_code is set",
            "malformed effective configuration remains fatal when direct CLI would win");
    }

    auto empty_content_root = omega::runtime::ParseConfigText(
        "content.data_root =\n");
    Check(empty_content_root.has_value(), "the empty-root fixture parses");
    if (empty_content_root)
    {
        auto result = omega::runtime::ResolveContentLaunchProfile(
            no_content_options, *empty_content_root);
        CheckProfileError(result, ContentLaunchProfileErrorCode::InvalidDataRoot,
            "content.data_root must be a non-empty valid path",
            "an empty configured root returns the fixed invalid-root error");
    }

    auto invalid_content_level = omega::runtime::ParseConfigText(
        "content.data_root = secret-configured-root\n"
        "content.level_code = secret-invalid-level\n");
    Check(invalid_content_level.has_value(), "the invalid-level fixture parses");
    if (invalid_content_level)
    {
        omega::runtime::LaunchOptions valid_direct;
        valid_direct.data_root = std::filesystem::path("secret-direct-root");
        valid_direct.level_code = "VALID1";
        auto result = omega::runtime::ResolveContentLaunchProfile(
            valid_direct, *invalid_content_level);
        CheckProfileError(result, ContentLaunchProfileErrorCode::InvalidLevelCode,
            "content.level_code must contain 1 to 32 ASCII alphanumeric characters",
            "invalid configured level bytes remain fatal when valid direct CLI would win");
        Check(!result && result.error().message.find("secret") == std::string::npos,
            "content profile diagnostics never echo configured or direct values");
    }

    auto long_content_level = omega::runtime::ParseConfigText(
        "content.data_root = configured/content\n"
        "content.level_code = ABCDEFGHIJKLMNOPQRSTUVWXYZ1234567\n");
    Check(long_content_level.has_value(), "the overlong-level fixture parses");
    if (long_content_level)
    {
        auto result = omega::runtime::ResolveContentLaunchProfile(
            no_content_options, *long_content_level);
        CheckProfileError(result, ContentLaunchProfileErrorCode::InvalidLevelCode,
            "content.level_code must contain 1 to 32 ASCII alphanumeric characters",
            "a configured level longer than 32 bytes is rejected");
    }

    auto boundary_content_levels = omega::runtime::ParseConfigText(
        "content.data_root = configured/content\n"
        "content.level_code = a\n");
    Check(boundary_content_levels.has_value(), "the one-byte-level fixture parses");
    if (boundary_content_levels)
    {
        auto result = omega::runtime::ResolveContentLaunchProfile(
            no_content_options, *boundary_content_levels);
        Check(result && result->has_value() &&
                  (*result)->level_code == std::optional<std::string>{"A"},
            "a one-byte configured level is accepted and normalized");
    }

    boundary_content_levels = omega::runtime::ParseConfigText(
        "content.data_root = configured/content\n"
        "content.level_code = abcdefghijklmnopqrstuvwxyz123456\n");
    Check(boundary_content_levels.has_value(), "the 32-byte-level fixture parses");
    if (boundary_content_levels)
    {
        auto result = omega::runtime::ResolveContentLaunchProfile(
            no_content_options, *boundary_content_levels);
        Check(result && result->has_value() &&
                  (*result)->level_code ==
                      std::optional<std::string>{"ABCDEFGHIJKLMNOPQRSTUVWXYZ123456"},
            "a 32-byte configured level is accepted and normalized");
    }

    auto empty_content_level = omega::runtime::ParseConfigText(
        "content.data_root = configured/content\n"
        "content.level_code =\n");
    Check(empty_content_level.has_value(), "the empty-level fixture parses");
    if (empty_content_level)
    {
        auto result = omega::runtime::ResolveContentLaunchProfile(
            no_content_options, *empty_content_level);
        CheckProfileError(result, ContentLaunchProfileErrorCode::InvalidLevelCode,
            "content.level_code must contain 1 to 32 ASCII alphanumeric characters",
            "an empty configured level is rejected rather than treated as absent");
    }

    if (empty)
    {
        omega::runtime::LaunchOptions missing_direct_root;
        missing_direct_root.level_code = "MINSK";
        auto result = omega::runtime::ResolveContentLaunchProfile(missing_direct_root, *empty);
        CheckProfileError(result, ContentLaunchProfileErrorCode::InvalidOptions,
            "direct content launch options are inconsistent",
            "an impossible direct level-without-root representation fails defensively");

        omega::runtime::LaunchOptions empty_direct_root;
        empty_direct_root.data_root = std::filesystem::path{};
        result = omega::runtime::ResolveContentLaunchProfile(empty_direct_root, *empty);
        CheckProfileError(result, ContentLaunchProfileErrorCode::InvalidOptions,
            "direct content launch options are inconsistent",
            "an impossible empty direct root representation fails defensively");

        omega::runtime::LaunchOptions invalid_direct_level;
        invalid_direct_level.data_root = std::filesystem::path("direct/content");
        invalid_direct_level.level_code = "BAD-LEVEL";
        result = omega::runtime::ResolveContentLaunchProfile(invalid_direct_level, *empty);
        CheckProfileError(result, ContentLaunchProfileErrorCode::InvalidOptions,
            "direct content launch options are inconsistent",
            "an impossible invalid direct level representation fails defensively");
    }

    omega::runtime::LaunchOptions override_only;
    override_only.config_overrides.push_back(
        {.key = "jobs.worker_count", .value = "1"});
    auto overridden = omega::runtime::LoadRuntimeConfig(override_only);
    Check(overridden && overridden->RequireInt64("jobs.worker_count") == 1,
        "command-line overrides apply to the empty default store");

    omega::runtime::LaunchOptions malformed_private_override;
    malformed_private_override.config_overrides.push_back(
        {.key = "PrivateUser.SecretVault", .value = "raw-secret-value"});
    CheckInputFreeError(omega::runtime::LoadRuntimeConfig(malformed_private_override),
        "--set override: config override: config key contains a byte outside [a-z0-9_.]",
        "malformed --set diagnostics omit the key and value");

    omega::runtime::LaunchOptions private_integer_override;
    private_integer_override.config_overrides.push_back(
        {.key = "jobs.worker_count",
            .value = "C:/Users/"
                     "PrivateUser/SecretVault/raw-secret-worker-count"});
    auto private_integer_override_config =
        omega::runtime::LoadRuntimeConfig(private_integer_override);
    Check(private_integer_override_config.has_value(),
        "a printable private --set integer value reaches typed validation");
    if (private_integer_override_config)
    {
        CheckInputFreeError(
            omega::runtime::ResolveRuntimeSettings(*private_integer_override_config),
            "jobs.worker_count: config integer value must be a canonical base-10 int64",
            "invalid integer --set diagnostics omit the raw value");
    }

    omega::runtime::LaunchOptions private_unknown_override;
    private_unknown_override.config_overrides.push_back(
        {.key = "privateuser.secretvault", .value = "PrivateUser-SecretVault-raw-secret"});
    auto private_unknown_override_config =
        omega::runtime::LoadRuntimeConfig(private_unknown_override);
    Check(private_unknown_override_config.has_value(),
        "a valid private-shaped unknown --set key reaches settings validation");
    if (private_unknown_override_config)
    {
        CheckInputFreeError(
            omega::runtime::ResolveRuntimeSettings(*private_unknown_override_config),
            "unknown runtime setting",
            "unknown --set diagnostics omit the key and value");
    }

    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path() /
                      ("omega-runtime-settings-tests-PrivateUser-SecretVault-" +
                          std::to_string(suffix));
    std::error_code file_error;
    std::filesystem::create_directories(root, file_error);
    Check(!file_error, "temporary runtime-settings directory is created");
    const auto config_path = root / "openomega.cfg";
    Check(WriteTextFile(config_path,
              "jobs.worker_count = 2\n"
              "log.minimum_severity = warning\n"
              "content.data_root = file/content\n"
              "content.level_code = file1\n"),
        "runtime-settings fixture is written");

    omega::runtime::LaunchOptions file_options;
    file_options.config_path = config_path;
    file_options.config_overrides.push_back(
        {.key = "jobs.worker_count", .value = "3"});
    file_options.config_overrides.push_back(
        {.key = "content.data_root", .value = "override/content"});
    file_options.config_overrides.push_back(
        {.key = "content.level_code", .value = "override2"});
    auto file_config = omega::runtime::LoadRuntimeConfig(file_options);
    Check(file_config && file_config->RequireInt64("jobs.worker_count") == 3 &&
              file_config->RequireString("log.minimum_severity") == "warning",
        "command-line values override a bounded configuration file");
    if (file_config)
    {
        auto file_profile = omega::runtime::ResolveContentLaunchProfile(
            file_options, *file_config);
        Check(file_profile && file_profile->has_value() &&
                  (*file_profile)->data_root == std::filesystem::path("override/content") &&
                  (*file_profile)->level_code == std::optional<std::string>{"OVERRIDE2"},
            "effective --set content values override the selected explicit file");

        file_options.data_root = std::filesystem::path("direct/content");
        file_options.level_code = "direct3";
        file_profile = omega::runtime::ResolveContentLaunchProfile(file_options, *file_config);
        Check(file_profile && file_profile->has_value() &&
                  (*file_profile)->data_root == std::filesystem::path("direct/content") &&
                  (*file_profile)->level_code == std::optional<std::string>{"DIRECT3"},
            "direct content atomically wins over effective --set and explicit file values");

        file_options.level_code.reset();
        file_profile = omega::runtime::ResolveContentLaunchProfile(file_options, *file_config);
        Check(file_profile && file_profile->has_value() &&
                  (*file_profile)->data_root == std::filesystem::path("direct/content") &&
                  !(*file_profile)->level_code,
            "a direct root-only choice does not inherit an effective --set level");
    }

    const auto private_integer_config_path = root / "PrivateUser-SecretVault-integer.cfg";
    Check(WriteTextFile(private_integer_config_path,
              "jobs.worker_count = C:/Users/"
              "PrivateUser/SecretVault/raw-secret-file-value\n"),
        "private integer file-profile fixture is written");
    omega::runtime::LaunchOptions private_integer_file_options;
    private_integer_file_options.config_path = private_integer_config_path;
    auto private_integer_file_config =
        omega::runtime::LoadRuntimeConfig(private_integer_file_options);
    Check(private_integer_file_config.has_value(),
        "a printable private file value reaches typed validation");
    if (private_integer_file_config)
    {
        CheckInputFreeError(
            omega::runtime::ResolveRuntimeSettings(*private_integer_file_config),
            "jobs.worker_count: config integer value must be a canonical base-10 int64",
            "invalid integer file diagnostics omit source path, key, and value");
    }

    const auto private_unknown_config_path = root / "PrivateUser-SecretVault-unknown.cfg";
    Check(WriteTextFile(private_unknown_config_path,
              "privateuser.secretvault = PrivateUser-SecretVault-raw-secret-file-value\n"),
        "private unknown-key file-profile fixture is written");
    omega::runtime::LaunchOptions private_unknown_file_options;
    private_unknown_file_options.config_path = private_unknown_config_path;
    auto private_unknown_file_config =
        omega::runtime::LoadRuntimeConfig(private_unknown_file_options);
    Check(private_unknown_file_config.has_value(),
        "a valid private-shaped unknown file key reaches settings validation");
    if (private_unknown_file_config)
    {
        CheckInputFreeError(
            omega::runtime::ResolveRuntimeSettings(*private_unknown_file_config),
            "unknown runtime setting",
            "unknown file-setting diagnostics omit source path, key, and value");
    }

    omega::runtime::LaunchOptions invalid_override_options;
    invalid_override_options.config_path = config_path;
    invalid_override_options.data_root = std::filesystem::path("direct/content");
    invalid_override_options.level_code = "DIRECT1";
    invalid_override_options.config_overrides.push_back(
        {.key = "content.level_code", .value = "invalid-level"});
    auto invalid_override_config = omega::runtime::LoadRuntimeConfig(invalid_override_options);
    Check(invalid_override_config.has_value(), "an opaque invalid-level override is stored");
    if (invalid_override_config)
    {
        auto result = omega::runtime::ResolveContentLaunchProfile(
            invalid_override_options, *invalid_override_config);
        CheckProfileError(result, ContentLaunchProfileErrorCode::InvalidLevelCode,
            "content.level_code must contain 1 to 32 ASCII alphanumeric characters",
            "invalid effective --set content remains fatal before valid direct CLI precedence");
    }

    const auto missing_config_path = root / "missing.cfg";
    file_options.config_path = missing_config_path;
    const auto missing_config = omega::runtime::LoadRuntimeConfig(file_options);
    Check(!missing_config,
        "a requested missing runtime configuration is fatal");
    if (!missing_config)
    {
        Check(missing_config.error() ==
                  "runtime configuration explicit profile: unable to open config file",
            "an explicit runtime configuration failure uses the fixed categorical label");
        Check(missing_config.error().find(missing_config_path.string()) == std::string::npos &&
                  missing_config.error().find("PrivateUser") == std::string::npos &&
                  missing_config.error().find("SecretVault") == std::string::npos,
            "an explicit runtime configuration failure omits private path identity");
    }
    std::filesystem::remove_all(root, file_error);
    Check(!file_error, "temporary runtime-settings directory is removed");
    return failures;
}
