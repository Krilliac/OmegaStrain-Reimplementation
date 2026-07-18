#include "omega/runtime/launch_options.h"

#include <initializer_list>
#include <iostream>
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
} // namespace

int LaunchOptionsFailureCount()
{
    auto defaults = Parse({});
    Check(defaults && defaults->frame_limit == -1 && !defaults->data_root &&
              !defaults->level_code && !defaults->config_path &&
              defaults->config_overrides.empty() && !defaults->probe_only &&
              !defaults->show_help,
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
    Check(!Parse({"--frames=999999999999999999999"}),
        "overflowing frame limits are rejected");
    Check(!Parse({"--frames=1", "--frames=2"}),
        "duplicate frame limits are rejected");
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
    Check(!Parse({"--unknown"}), "unknown options are rejected instead of ignored");

    auto help = Parse({"--help"});
    Check(help && help->show_help, "the standalone help option is accepted");
    Check(!Parse({"--help", "--frames=0"}),
        "help cannot mask invalid or side-effecting option combinations");
    return failures;
}
