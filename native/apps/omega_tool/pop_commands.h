#pragma once

#include <filesystem>

namespace omega::tool
{
[[nodiscard]] int PopVerifyTree(const std::filesystem::path& root);
} // namespace omega::tool
