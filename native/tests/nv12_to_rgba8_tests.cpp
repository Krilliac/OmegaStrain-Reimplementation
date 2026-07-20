#include "omega/media/nv12_to_rgba8.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <span>
#include <string_view>

namespace
{
int failures = 0;

void Check(const bool condition, const std::string_view message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

[[nodiscard]] std::array<std::byte, 4> Pixel(const omega::media::Rgba8VideoFrame& frame,
                                             const std::size_t index)
{
    const std::size_t offset = index * 4U;
    return {
        frame.pixels[offset + 0U],
        frame.pixels[offset + 1U],
        frame.pixels[offset + 2U],
        frame.pixels[offset + 3U],
    };
}
} // namespace

int main()
{
    constexpr std::array<std::byte, 4> opaque_red{std::byte{255}, std::byte{0}, std::byte{0},
                                                  std::byte{255}};
    constexpr std::array<std::byte, 4> opaque_blue{std::byte{0}, std::byte{0}, std::byte{255},
                                                   std::byte{255}};

    // Two 2x2 chroma cells in a 4x2 visible frame, with independent row padding.
    constexpr std::array<std::byte, 28> padded_nv12{
        std::byte{81},  std::byte{81},  std::byte{41}, std::byte{41}, std::byte{1},  std::byte{2},
        std::byte{3},   std::byte{4},   std::byte{81}, std::byte{81}, std::byte{41}, std::byte{41},
        std::byte{5},   std::byte{6},   std::byte{7},  std::byte{8},  std::byte{90}, std::byte{240},
        std::byte{240}, std::byte{110}, std::byte{9},  std::byte{10}, std::byte{11}, std::byte{12},
        std::byte{13},  std::byte{14},  std::byte{15}, std::byte{16},
    };
    auto converted = omega::media::ConvertNv12ToRgba8(omega::media::Nv12FrameView{
        .width = 4U,
        .height = 2U,
        .luma_stride = 8U,
        .chroma_stride = 12U,
        .bytes = padded_nv12,
    });
    Check(converted && converted->width == 4U && converted->height == 2U &&
              converted->pixels.size() == 32U,
          "padded NV12 storage converts to one tight RGBA8 frame");
    if (converted)
    {
        Check(Pixel(*converted, 0U) == opaque_red && Pixel(*converted, 1U) == opaque_red &&
                  Pixel(*converted, 2U) == opaque_blue && Pixel(*converted, 3U) == opaque_blue &&
                  Pixel(*converted, 4U) == opaque_red && Pixel(*converted, 7U) == opaque_blue,
              "limited-range BT.601 conversion preserves canonical red/blue chroma cells");
    }

    constexpr std::array<std::byte, 6> black_nv12{
        std::byte{16}, std::byte{16}, std::byte{16}, std::byte{16}, std::byte{128}, std::byte{128},
    };
    auto black = omega::media::ConvertNv12ToRgba8(omega::media::Nv12FrameView{
        .width = 2U,
        .height = 2U,
        .luma_stride = 2U,
        .chroma_stride = 2U,
        .bytes = black_nv12,
    });
    Check(black && Pixel(*black, 0U) ==
                       std::array{std::byte{0}, std::byte{0}, std::byte{0}, std::byte{255}},
          "studio-range black maps to opaque zero RGB");

    constexpr std::array<std::byte, 6> white_nv12{
        std::byte{235}, std::byte{235}, std::byte{235},
        std::byte{235}, std::byte{128}, std::byte{128},
    };
    auto white = omega::media::ConvertNv12ToRgba8(omega::media::Nv12FrameView{
        .width = 2U,
        .height = 2U,
        .luma_stride = 2U,
        .chroma_stride = 2U,
        .bytes = white_nv12,
    });
    Check(white && Pixel(*white, 0U) ==
                       std::array{std::byte{255}, std::byte{255}, std::byte{255}, std::byte{255}},
          "studio-range white saturates to opaque 255 RGB");

    const auto Rejects = [](const omega::media::Nv12FrameView frame,
                            const std::uint64_t limit =
                                omega::media::kMaximumNv12Rgba8OutputBytes) {
        return !omega::media::ConvertNv12ToRgba8(frame, limit);
    };
    Check(Rejects({}), "zero dimensions are rejected");
    Check(Rejects({.width = 3U,
                   .height = 2U,
                   .luma_stride = 3U,
                   .chroma_stride = 4U,
                   .bytes = black_nv12}),
          "odd dimensions are rejected");
    Check(Rejects({.width = 2U,
                   .height = 2U,
                   .luma_stride = 1U,
                   .chroma_stride = 2U,
                   .bytes = black_nv12}),
          "undersized luma stride is rejected");
    Check(Rejects({.width = 2U,
                   .height = 2U,
                   .luma_stride = 2U,
                   .chroma_stride = 2U,
                   .bytes = std::span<const std::byte>(black_nv12).first(5U)}),
          "truncated storage is rejected");
    Check(Rejects({.width = 2U,
                   .height = 2U,
                   .luma_stride = 2U,
                   .chroma_stride = 2U,
                   .bytes = black_nv12},
                  15U),
          "caller output limit is enforced before allocation");
    Check(Rejects({.width = omega::media::kMaximumNv12FrameWidth + 2U,
                   .height = 2U,
                   .luma_stride = omega::media::kMaximumNv12FrameWidth + 2U,
                   .chroma_stride = omega::media::kMaximumNv12FrameWidth + 2U,
                   .bytes = black_nv12},
                  std::numeric_limits<std::uint64_t>::max()),
          "decoder dimension ceiling cannot be raised by a caller limit");

    if (failures != 0)
    {
        std::cerr << failures << " NV12 conversion test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "NV12 conversion tests passed\n";
    return EXIT_SUCCESS;
}
