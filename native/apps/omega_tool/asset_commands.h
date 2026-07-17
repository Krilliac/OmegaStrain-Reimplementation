#pragma once

#include <filesystem>

namespace omega::tool
{
[[nodiscard]] int AssetMetadataVerifyTree(const std::filesystem::path& root);
} // namespace omega::tool
