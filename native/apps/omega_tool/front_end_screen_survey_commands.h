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
// individual screen load failure is part of the report. Returns 1 when the root
// itself could not be opened.
[[nodiscard]] int FrontEndScreenSurvey(const std::filesystem::path& root);
} // namespace omega::tool
