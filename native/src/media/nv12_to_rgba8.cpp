#include "omega/media/nv12_to_rgba8.h"

#include <algorithm>
#include <limits>
#include <new>

namespace omega::media
{
namespace
{
[[nodiscard]] bool Multiply(const std::uint64_t left, const std::uint64_t right,
                            std::uint64_t& result) noexcept
{
    if (left != 0U && right > std::numeric_limits<std::uint64_t>::max() / left)
        return false;
    result = left * right;
    return true;
}

[[nodiscard]] bool Add(const std::uint64_t left, const std::uint64_t right,
                       std::uint64_t& result) noexcept
{
    if (right > std::numeric_limits<std::uint64_t>::max() - left)
        return false;
    result = left + right;
    return true;
}

[[nodiscard]] constexpr std::byte ClampChannel(const std::int32_t value) noexcept
{
    return static_cast<std::byte>(std::clamp(value, 0, 255));
}
} // namespace

std::expected<Rgba8VideoFrame, std::string> ConvertNv12ToRgba8(
    const Nv12FrameView frame, const std::uint64_t maximum_output_bytes)
{
    if (frame.width == 0U || frame.height == 0U)
        return std::unexpected("NV12 frame dimensions must be nonzero");
    if ((frame.width & 1U) != 0U || (frame.height & 1U) != 0U)
        return std::unexpected("NV12 frame dimensions must be even");
    if (frame.width > kMaximumNv12FrameWidth || frame.height > kMaximumNv12FrameHeight)
        return std::unexpected("NV12 frame dimensions exceed the decoder ceiling");
    if (frame.luma_stride < frame.width || frame.chroma_stride < frame.width)
        return std::unexpected("NV12 frame stride is smaller than its visible width");

    std::uint64_t luma_bytes = 0U;
    std::uint64_t chroma_bytes = 0U;
    std::uint64_t required_input_bytes = 0U;
    if (!Multiply(frame.luma_stride, frame.height, luma_bytes) ||
        !Multiply(frame.chroma_stride, frame.height / 2U, chroma_bytes) ||
        !Add(luma_bytes, chroma_bytes, required_input_bytes) ||
        required_input_bytes > frame.bytes.size())
    {
        return std::unexpected("NV12 frame storage is truncated or overflows");
    }

    std::uint64_t pixel_count = 0U;
    std::uint64_t output_bytes = 0U;
    if (!Multiply(frame.width, frame.height, pixel_count) ||
        !Multiply(pixel_count, 4U, output_bytes) || output_bytes > maximum_output_bytes ||
        output_bytes > kMaximumNv12Rgba8OutputBytes ||
        output_bytes > std::numeric_limits<std::size_t>::max())
    {
        return std::unexpected("NV12 RGBA8 output exceeds the byte limit");
    }

    try
    {
        Rgba8VideoFrame result{
            .width = frame.width,
            .height = frame.height,
            .pixels = std::vector<std::byte>(static_cast<std::size_t>(output_bytes)),
        };
        const std::size_t chroma_base = static_cast<std::size_t>(luma_bytes);
        for (std::uint32_t y = 0U; y < frame.height; ++y)
        {
            const std::size_t luma_row = static_cast<std::size_t>(y) * frame.luma_stride;
            const std::size_t chroma_row =
                chroma_base + static_cast<std::size_t>(y / 2U) * frame.chroma_stride;
            for (std::uint32_t x = 0U; x < frame.width; ++x)
            {
                const std::int32_t luma = std::to_integer<std::uint8_t>(frame.bytes[luma_row + x]);
                const std::size_t chroma = chroma_row + (x & ~1U);
                const std::int32_t u = std::to_integer<std::uint8_t>(frame.bytes[chroma]) - 128;
                const std::int32_t v =
                    std::to_integer<std::uint8_t>(frame.bytes[chroma + 1U]) - 128;
                const std::int32_t c = std::max(0, luma - 16);
                const std::int32_t red = (298 * c + 409 * v + 128) >> 8;
                const std::int32_t green = (298 * c - 100 * u - 208 * v + 128) >> 8;
                const std::int32_t blue = (298 * c + 516 * u + 128) >> 8;

                const std::size_t output = (static_cast<std::size_t>(y) * frame.width + x) * 4U;
                result.pixels[output + 0U] = ClampChannel(red);
                result.pixels[output + 1U] = ClampChannel(green);
                result.pixels[output + 2U] = ClampChannel(blue);
                result.pixels[output + 3U] = std::byte{255};
            }
        }
        return result;
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected("NV12 RGBA8 output allocation failed");
    }
    catch (...)
    {
        return std::unexpected("NV12 RGBA8 conversion failed unexpectedly");
    }
}
} // namespace omega::media
