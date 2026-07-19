#include "omega/runtime/launch_options.h"
#include "omega/runtime/run_capture_session.h"

#include <array>
#include <initializer_list>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

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

std::expected<omega::runtime::LaunchOptions, std::string> Parse(
    const std::initializer_list<std::string_view> arguments)
{
    return omega::runtime::ParseLaunchOptions(
        std::vector<std::string_view>(arguments));
}

void CheckError(const std::expected<omega::runtime::LaunchOptions, std::string>& result,
    const std::string_view expected, const std::string_view message)
{
    Check(!result && result.error() == expected, message);
}
} // namespace

int LaunchOptionsFailureCount()
{
    auto defaults = Parse({});
    Check(defaults && defaults->frame_limit == -1 && !defaults->data_root &&
              !defaults->level_code && !defaults->config_path &&
              defaults->config_overrides.empty() && !defaults->capture_run &&
              !defaults->probe_only && !defaults->show_help,
        "empty launch arguments preserve the interactive content-free shell");

    auto content = Parse({"--data-root=D:/Owned Game", "--level=minsk", "--probe-only"});
    Check(content && content->frame_limit == -1 && content->data_root &&
              content->data_root->generic_string() == "D:/Owned Game" &&
              content->level_code == "MINSK" && content->probe_only,
        "content, level, and probe options compose without filesystem access");
    auto rendered = Parse(
        {"--data-root=D:/Owned Game", "--level=minsk", "--frames=120"});
    Check(rendered && rendered->frame_limit == 120 && !rendered->probe_only,
        "content-backed rendering accepts an explicit frame limit");
    Check(rendered && !rendered->capture_run,
        "ordinary finite rendering does not silently enable run capture");
    Check(Parse({"--data-root=D:/Owned Game"}).has_value(),
        "a data root may be validated without selecting a level");

    auto configured = Parse({"--config=D:/OpenOmega/runtime.cfg", "--set=log.minimum_severity=debug",
        "--set=frame.max_steps_per_frame=4"});
    Check(configured && configured->config_path &&
              configured->config_path->generic_string() == "D:/OpenOmega/runtime.cfg" &&
              configured->config_overrides.size() == 2U &&
              configured->config_overrides[0].key == "log.minimum_severity" &&
              configured->config_overrides[0].value == "debug" &&
              configured->config_overrides[1].key == "frame.max_steps_per_frame" &&
              configured->config_overrides[1].value == "4",
        "configuration file and bounded command-line overrides preserve source order");
    auto empty_value = Parse({"--set=debug.label="});
    Check(empty_value && empty_value->config_overrides.size() == 1U &&
              empty_value->config_overrides[0].value.empty(),
        "configuration overrides preserve legal empty values");

    Check(!Parse({"--frames=-1"}), "negative frame limits are rejected");
    CheckError(Parse({"--capture-run", "--frames=-1"}),
        "--frames requires a non-negative integer",
        "invalid frame syntax retains precedence over capture range validation");
    Check(!Parse({"--frames=999999999999999999999"}),
        "overflowing frame limits are rejected");
    Check(!Parse({"--frames=1", "--frames=2"}),
        "duplicate frame limits are rejected");

    const std::string maximum_frames = "--frames=" +
        std::to_string(omega::runtime::kMaximumRunCaptureSessionFrames);
    const std::string above_maximum_frames = "--frames=" +
        std::to_string(omega::runtime::kMaximumRunCaptureSessionFrames + 1U);
    const std::array maximum_capture_arguments{
        std::string_view(maximum_frames), std::string_view("--capture-run")};
    auto maximum_capture = omega::runtime::ParseLaunchOptions(maximum_capture_arguments);
    Check(maximum_capture && maximum_capture->capture_run &&
              maximum_capture->frame_limit == static_cast<int>(
                  omega::runtime::kMaximumRunCaptureSessionFrames),
        "capture accepts the exact shared session maximum");

    auto capture_first = Parse({"--capture-run", "--frames=1"});
    Check(capture_first && capture_first->capture_run && capture_first->frame_limit == 1,
        "capture accepts its minimum frame count before the frame option");
    auto frames_first = Parse({"--frames=7", "--capture-run"});
    Check(frames_first && frames_first->capture_run && frames_first->frame_limit == 7,
        "capture accepts the frame option before the capture flag");

    CheckError(Parse({"--capture-run", "--capture-run", "--frames=1"}),
        "--capture-run may be specified only once",
        "duplicate capture flags have a stable once-only error");
    CheckError(Parse({"--capture-run"}), "--capture-run requires --frames",
        "capture requires an explicit frame option");
    const std::string capture_range_error =
        "--capture-run requires --frames in the range 1.." +
        std::to_string(omega::runtime::kMaximumRunCaptureSessionFrames);
    CheckError(Parse({"--frames=0", "--capture-run"}), capture_range_error,
        "capture rejects an explicitly empty run");
    const std::array above_maximum_capture_arguments{
        std::string_view("--capture-run"), std::string_view(above_maximum_frames)};
    CheckError(omega::runtime::ParseLaunchOptions(above_maximum_capture_arguments),
        capture_range_error, "capture rejects a frame count above the shared session maximum");
    CheckError(Parse({"--capture-run=true", "--frames=1"}),
        "unknown option: --capture-run=true",
        "capture is an exact boolean token and does not accept an attached value");

    auto ordinary_zero = Parse({"--frames=0"});
    Check(ordinary_zero && ordinary_zero->frame_limit == 0 && !ordinary_zero->capture_run,
        "ordinary zero-frame startup retains its existing behavior");
    const std::array above_maximum_ordinary_arguments{
        std::string_view(above_maximum_frames)};
    auto above_maximum_ordinary =
        omega::runtime::ParseLaunchOptions(above_maximum_ordinary_arguments);
    Check(above_maximum_ordinary && !above_maximum_ordinary->capture_run &&
              above_maximum_ordinary->frame_limit == static_cast<int>(
                  omega::runtime::kMaximumRunCaptureSessionFrames + 1U),
        "ordinary finite runs retain frame counts above the capture maximum");
    Check(!Parse({"--data-root="}), "empty data-root values are rejected");
    Check(!Parse({"--data-root=A", "--data-root=B"}),
        "duplicate data roots are rejected");
    Check(!Parse({"--config="}), "empty config paths are rejected");
    Check(!Parse({"--config=A", "--config=B"}), "duplicate config paths are rejected");
    Check(!Parse({"--set=missing_separator"}), "configuration overrides require an equals sign");
    Check(!Parse({"--set==missing_key"}), "configuration overrides require a key");
    Check(!Parse({"--set=jobs.worker_count=1", "--set=jobs.worker_count=2"}),
        "duplicate configuration override keys are rejected");
    Check(!Parse({"--set=jobs.worker_count=1", "--set=jobs.worker_count =2"}),
        "duplicate configuration override keys are compared after config-style trimming");
    Check(!Parse({"--level=MINSK"}), "level selection requires an explicit data root");
    Check(!Parse({"--data-root=A", "--level=../MINSK"}),
        "unsafe level components are rejected by the launch boundary");
    Check(!Parse({"--probe-only"}), "headless probing requires a data root");
    Check(!Parse({"--probe-only", "--probe-only", "--data-root=A"}),
        "duplicate probe flags are rejected");
    Check(!Parse({"--data-root=A", "--probe-only", "--frames=1"}),
        "headless probing cannot silently discard a frame limit");
    CheckError(Parse({"--capture-run", "--data-root=A", "--probe-only", "--frames=1"}),
        "--probe-only cannot be combined with --frames",
        "capture cannot bypass the existing probe and frame exclusion");
    Check(!Parse({"--unknown"}), "unknown options are rejected instead of ignored");

    auto help = Parse({"--help"});
    Check(help && help->show_help && !help->capture_run,
        "the standalone long help option is accepted without capture");
    auto short_help = Parse({"-h"});
    Check(short_help && short_help->show_help && !short_help->capture_run,
        "the standalone short help option is accepted without capture");
    Check(!Parse({"--help", "--frames=0"}),
        "help cannot mask invalid or side-effecting option combinations");
    CheckError(Parse({"--help", "--capture-run"}),
        "--help cannot be combined with other options",
        "standalone help validation precedes capture dependency validation");
    CheckError(Parse({"--capture-run", "-h", "--frames=1"}),
        "--help cannot be combined with other options",
        "capture cannot be combined with short help in any argument order");
    CheckError(Parse({"--help", "-h"}), "help may be requested only once",
        "long and short help aliases share the existing once-only contract");

    const std::string_view usage = omega::runtime::LaunchUsage();
    Check(usage == "usage: openomega [-h|--help]\n"
                   "       openomega [--config=PATH] [--set=KEY=VALUE ...] "
                   "[--frames=N [--capture-run]] "
                   "[--data-root=PATH [--level=CODE] [--probe-only]]\n",
        "usage exactly documents standalone help and the capture frame dependency");
    return failures;
}
