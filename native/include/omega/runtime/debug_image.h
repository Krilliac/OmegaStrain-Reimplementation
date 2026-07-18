#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace omega::runtime
{
// Fully owned renderer-neutral RGBA8 image. Platform backends may borrow pixels during upload,
// but no GPU object or platform type crosses this boundary.
struct DebugImage
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::byte> rgba8_pixels;

    [[nodiscard]] std::span<const std::byte> pixels() const noexcept
    {
        return rgba8_pixels;
    }
};
} // namespace omega::runtime
