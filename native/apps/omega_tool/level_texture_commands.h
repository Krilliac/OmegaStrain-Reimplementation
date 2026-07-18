#pragma once

#include <filesystem>

namespace omega::tool
{
// Verifies the native level-texture path over an owner-supplied retail tree and emits one
// privacy-safe, fixed-schema aggregate JSON object. No source identity is included in the report.
[[nodiscard]] int LevelTextureStoreVerifyTree(const std::filesystem::path& root);
} // namespace omega::tool
