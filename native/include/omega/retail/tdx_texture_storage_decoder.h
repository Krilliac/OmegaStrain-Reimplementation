#pragma once

#include "omega/asset/decode.h"
#include "omega/asset/texture_storage_ir.h"

#include <cstddef>
#include <span>

namespace omega::retail
{
// [any worker thread; reentrant] Converts the independently documented TDX storage rectangles
// into canonical owned storage. It performs no palette permutation, texel unpacking/swizzle,
// channel conversion, alpha scaling, or renderer/GPU upload.
[[nodiscard]] asset::DecodeResult<asset::TextureStorageIR> DecodeTdxTextureStorage(
    std::span<const std::byte> bytes, asset::DecodeLimits limits = {});
} // namespace omega::retail
