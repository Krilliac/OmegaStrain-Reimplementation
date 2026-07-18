#pragma once

#include <filesystem>

namespace omega::tool
{
[[nodiscard]] int PopVerifyTree(const std::filesystem::path& root);
[[nodiscard]] int LevelManifestVerifyTree(const std::filesystem::path& root);
[[nodiscard]] int LevelSpatialVerifyTree(const std::filesystem::path& root);
[[nodiscard]] int LevelMaterialCatalogsVerifyTree(const std::filesystem::path& root);
} // namespace omega::tool
