#pragma once

#include "omega/asset/level_ir.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace omega::runtime
{
struct ManifestDebugImage
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::byte> rgba8_pixels;

    [[nodiscard]] std::span<const std::byte> pixels() const noexcept
    {
        return rgba8_pixels;
    }
};

// [any worker thread; reentrant] Produces a synthetic manifest-coverage view. Tile positions are
// not world coordinates and must never be presented as reconstructed retail geometry.
[[nodiscard]] std::expected<ManifestDebugImage, std::string> BuildManifestDebugImage(
    const asset::LevelManifestIR& manifest);
} // namespace omega::runtime
