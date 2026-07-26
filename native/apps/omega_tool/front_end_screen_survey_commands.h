#pragma once

#include <filesystem>

namespace omega::tool
{
// Loads only the front-end screen roles already declared by the current owned
// API and emits a bounded, aggregate-only report. The owner path, decoded
// payloads, diagnostic messages, and unknown widget identifiers are never
// serialized.
//
// Returns 0 when the root opened and all declared roles were surveyed. An
// individual screen load failure is part of the report. A root-open failure
// returns 1. This function can propagate an allocation failure, and omega_tool's
// top-level boundary also maps escaping allocation/internal failures to 1, so a
// process exit code of 1 is not exclusive evidence of a root-open failure.
[[nodiscard]] int FrontEndScreenSurvey(const std::filesystem::path& root);
} // namespace omega::tool
