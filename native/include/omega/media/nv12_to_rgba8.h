#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace omega::media
{
inline constexpr std::uint32_t kMaximumNv12FrameWidth = 1920U;
inline constexpr std::uint32_t kMaximumNv12FrameHeight = 1088U;
inline constexpr std::uint64_t kMaximumNv12Rgba8OutputBytes =
    static_cast<std::uint64_t>(kMaximumNv12FrameWidth) * kMaximumNv12FrameHeight * 4U;

// One top-down, limited-range BT.601 NV12 frame. The luma plane begins at byte zero and occupies
// luma_stride * height bytes. The interleaved UV plane follows immediately and occupies
// chroma_stride * (height / 2) bytes. Row padding is accepted but never read as pixels.
struct Nv12FrameView
{
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    std::uint32_t luma_stride = 0U;
    std::uint32_t chroma_stride = 0U;
    std::span<const std::byte> bytes;
};

struct Rgba8VideoFrame
{
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    std::vector<std::byte> pixels;
};

// [any worker thread; reentrant] Converts one bounded, top-down NV12 frame to tightly packed
// opaque RGBA8 using the integer limited-range BT.601 matrix. The result owns no source bytes.
[[nodiscard]] std::expected<Rgba8VideoFrame, std::string> ConvertNv12ToRgba8(
    Nv12FrameView frame, std::uint64_t maximum_output_bytes = kMaximumNv12Rgba8OutputBytes);
} // namespace omega::media
