#pragma once

#include "omega/asset/decode.h"
#include "omega/asset/texture_storage_ir.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace omega::retail
{
struct DecodedTdxTextureStorage
{
    asset::TextureStorageIR storage;
    std::uint64_t decoded_items = 0;
    std::uint64_t logical_output_bytes = 0;
};

// [any worker thread; reentrant] Converts the independently documented TDX storage rectangles
// into canonical owned storage. It performs no palette permutation, texel unpacking/swizzle,
// channel conversion, alpha scaling, or renderer/GPU upload.
[[nodiscard]] asset::DecodeResult<asset::TextureStorageIR> DecodeTdxTextureStorage(
    std::span<const std::byte> bytes, asset::DecodeLimits limits = {});

// [any worker thread; reentrant] Performs the same decode while publishing exact operation-budget
// usage for a caller composing multiple textures under one shared DecodeLimits context. The usage
// values describe the complete standalone texture result, including its root value.
[[nodiscard]] asset::DecodeResult<DecodedTdxTextureStorage>
DecodeTdxTextureStorageMeasured(
    std::span<const std::byte> bytes, asset::DecodeLimits limits = {});
} // namespace omega::retail
