#include "omega/runtime/launch_options.h"

#include "omega/runtime/run_capture_session.h"

#include <algorithm>
#include <charconv>
#include <limits>
#include <system_error>
#include <utility>

namespace omega::runtime
{
namespace
{
constexpr std::string_view kFramesPrefix = "--frames=";
constexpr std::string_view kDataRootPrefix = "--data-root=";
constexpr std::string_view kLevelPrefix = "--level=";
constexpr std::string_view kConfigPrefix = "--config=";
constexpr std::string_view kSetPrefix = "--set=";
static_assert(kMaximumRunCaptureSessionFrames <=
              static_cast<std::size_t>(std::numeric_limits<int>::max()));

[[nodiscard]] std::string CaptureRunFrameRangeError()
{
    return "--capture-run requires --frames in the range 1.." +
           std::to_string(kMaximumRunCaptureSessionFrames);
}

[[nodiscard]] std::string_view TrimConfigBlanks(std::string_view value) noexcept
{
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t'))
        value.remove_prefix(1U);
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t'))
        value.remove_suffix(1U);
    return value;
}

[[nodiscard]] bool IsSafeLevelCode(const std::string_view value) noexcept
{
    if (value.empty() || value.size() > 32U)
        return false;
    for (const unsigned char character : value)
    {
        const bool upper = character >= static_cast<unsigned char>('A') &&
                           character <= static_cast<unsigned char>('Z');
        const bool lower = character >= static_cast<unsigned char>('a') &&
                           character <= static_cast<unsigned char>('z');
        const bool digit = character >= static_cast<unsigned char>('0') &&
                           character <= static_cast<unsigned char>('9');
        if (!upper && !lower && !digit)
            return false;
    }
    return true;
}
} // namespace

std::expected<LaunchOptions, std::string> ParseLaunchOptions(
    const std::span<const std::string_view> arguments)
{
    LaunchOptions result;
    bool saw_frames = false;
    bool saw_data_root = false;
    bool saw_level = false;
    bool saw_config = false;
    bool saw_capture_run = false;
    bool saw_probe_only = false;
    bool saw_help = false;

    for (const std::string_view argument : arguments)
    {
        if (argument.starts_with(kFramesPrefix))
        {
            if (saw_frames)
                return std::unexpected("--frames may be specified only once");
            saw_frames = true;
            const std::string_view value = argument.substr(kFramesPrefix.size());
            int parsed = -1;
            const auto conversion = std::from_chars(
                value.data(), value.data() + value.size(), parsed);
            if (value.empty() || conversion.ec != std::errc{} ||
                conversion.ptr != value.data() + value.size() || parsed < 0)
                return std::unexpected("--frames requires a non-negative integer");
            result.frame_limit = parsed;
            continue;
        }
        if (argument.starts_with(kDataRootPrefix))
        {
            if (saw_data_root)
                return std::unexpected("--data-root may be specified only once");
            saw_data_root = true;
            const std::string_view value = argument.substr(kDataRootPrefix.size());
            if (value.empty())
                return std::unexpected("--data-root requires a path");
            result.data_root = std::filesystem::path(value);
            continue;
        }
        if (argument.starts_with(kLevelPrefix))
        {
            if (saw_level)
                return std::unexpected("--level may be specified only once");
            saw_level = true;
            const std::string_view value = argument.substr(kLevelPrefix.size());
            if (!IsSafeLevelCode(value))
                return std::unexpected(
                    "--level requires 1 to 32 ASCII letters or digits");
            std::string normalized;
            normalized.reserve(value.size());
            for (const unsigned char character : value)
            {
                const bool lower = character >= static_cast<unsigned char>('a') &&
                                   character <= static_cast<unsigned char>('z');
                normalized.push_back(
                    static_cast<char>(lower ? character - ('a' - 'A') : character));
            }
            result.level_code = std::move(normalized);
            continue;
        }
        if (argument.starts_with(kConfigPrefix))
        {
            if (saw_config)
                return std::unexpected("--config may be specified only once");
            saw_config = true;
            const std::string_view value = argument.substr(kConfigPrefix.size());
            if (value.empty())
                return std::unexpected("--config requires a path");
            result.config_path = std::filesystem::path(value);
            continue;
        }
        if (argument.starts_with(kSetPrefix))
        {
            if (result.config_overrides.size() >= kMaxLaunchConfigOverrides)
                return std::unexpected("too many --set overrides");
            const std::string_view assignment = argument.substr(kSetPrefix.size());
            const std::size_t separator = assignment.find('=');
            if (separator == std::string_view::npos)
                return std::unexpected("--set requires KEY=VALUE");

            const std::string_view key = TrimConfigBlanks(assignment.substr(0U, separator));
            const std::string_view value = TrimConfigBlanks(assignment.substr(separator + 1U));
            if (key.empty())
                return std::unexpected("--set requires KEY=VALUE");

            LaunchConfigOverride override{
                .key = std::string(key),
                .value = std::string(value),
            };
            const auto duplicate = std::ranges::find(
                result.config_overrides, override.key, &LaunchConfigOverride::key);
            if (duplicate != result.config_overrides.end())
                return std::unexpected("--set key may be specified only once: " + override.key);
            result.config_overrides.push_back(std::move(override));
            continue;
        }
        if (argument == "--probe-only")
        {
            if (saw_probe_only)
                return std::unexpected("--probe-only may be specified only once");
            saw_probe_only = true;
            result.probe_only = true;
            continue;
        }
        if (argument == "--capture-run")
        {
            if (saw_capture_run)
                return std::unexpected("--capture-run may be specified only once");
            saw_capture_run = true;
            result.capture_run = true;
            continue;
        }
        if (argument == "--help" || argument == "-h")
        {
            if (saw_help)
                return std::unexpected("help may be requested only once");
            saw_help = true;
            result.show_help = true;
            continue;
        }
        return std::unexpected("unknown option: " + std::string(argument));
    }

    if (result.level_code && !result.data_root)
        return std::unexpected("--level requires --data-root");
    if (result.probe_only && !result.data_root)
        return std::unexpected("--probe-only requires --data-root");
    if (result.probe_only && saw_frames)
        return std::unexpected("--probe-only cannot be combined with --frames");
    if (result.show_help && arguments.size() != 1U)
        return std::unexpected("--help cannot be combined with other options");
    if (result.capture_run && !saw_frames)
        return std::unexpected("--capture-run requires --frames");
    if (result.capture_run &&
        (result.frame_limit == 0 ||
            static_cast<std::size_t>(result.frame_limit) >
                kMaximumRunCaptureSessionFrames))
        return std::unexpected(CaptureRunFrameRangeError());
    return result;
}

std::string_view LaunchUsage() noexcept
{
    return "usage: openomega [-h|--help]\n"
           "       openomega [--config=PATH] [--set=KEY=VALUE ...] "
           "[--frames=N [--capture-run]] "
           "[--data-root=PATH [--level=CODE] [--probe-only]]\n";
}
} // namespace omega::runtime
