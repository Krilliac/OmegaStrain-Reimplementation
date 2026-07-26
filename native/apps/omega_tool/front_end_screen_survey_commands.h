#pragma once

#include <filesystem>

namespace omega::tool
{
// Reports, for one owner-supplied retail data root, which of the screen keys
// this repository already declares can be loaded, plus an aggregate-only survey
// of the Title screen's visible buttons.
//
// The output is a single fixed-schema JSON line on stdout containing counts,
// booleans, the repository's own error-code names, and the button identifiers
// this repository already publishes. It contains no source path, no member
// name, no payload byte, no digest, and no owner-authored text, so the owner can
// read the whole line before sending it and can see that nothing proprietary is
// in it.
//
// Returns 0 when the root opened and the survey ran, 1 otherwise. A screen that
// fails to load is a recorded observation, not a command failure: reporting
// which screens do not load is the point.
[[nodiscard]] int FrontEndScreenSurvey(const std::filesystem::path& root);
} // namespace omega::tool
