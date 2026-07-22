#pragma once

#include "omega/asset/decode.h"
#include "omega/asset/indexed_image_ir.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace omega::retail
{
enum class TdxGsPixelStorageFormat : std::uint8_t
{
    Psmct32 = 0x00,
    Psmt8 = 0x13,
    Psmt4 = 0x14,
};

struct TdxGsUploadRectangle
{
    std::uint16_t destination_base_pointer = 0;
    std::uint16_t destination_buffer_width = 0;
    std::uint16_t destination_x = 0;
    std::uint16_t destination_y = 0;
    std::uint16_t width = 0;
    std::uint16_t height = 0;

    bool operator==(const TdxGsUploadRectangle&) const = default;
};

// Normalized values needed to reproduce the bounded frontend transfer-0 display
// transform. No serialized pointer, source offset, borrowed payload, or
// renderer/GPU object crosses this boundary.
struct FrontEndTdxGsUploadPlan
{
    std::uint16_t format_version = 0;
    std::uint16_t header_flags = 0;
    TdxGsPixelStorageFormat sampling_format = TdxGsPixelStorageFormat::Psmt8;
    std::uint16_t texture_base_pointer = 0;
    std::uint16_t texture_buffer_width = 0;
    TdxGsPixelStorageFormat palette_storage_format = TdxGsPixelStorageFormat::Psmct32;
    std::uint8_t palette_storage_mode = 0;
    std::uint8_t palette_start = 0;
    bool texture_alpha_enabled = false;
    bool palette_load_enabled = false;
    TdxGsUploadRectangle primary_upload;
    TdxGsUploadRectangle palette_upload;

    bool operator==(const FrontEndTdxGsUploadPlan&) const = default;
};

struct DecodedFrontEndTdx
{
    FrontEndTdxGsUploadPlan upload_plan;
    asset::IndexedImageIR image;
    std::uint64_t decoded_items = 0;
    std::uint64_t logical_output_bytes = 0;
    std::uint64_t peak_scratch_bytes = 0;

    bool operator==(const DecodedFrontEndTdx&) const = default;
};

// Independently derived GS local-memory address formulas. The result is wrapped to the 4 MiB GS
// address space. Zero buffer widths are invalid; indexed formats also reject odd buffer widths.
[[nodiscard]] std::optional<std::uint32_t> GsPsmct32WordAddress(std::uint16_t base_pointer,
                                                                std::uint16_t buffer_width,
                                                                std::uint32_t x,
                                                                std::uint32_t y) noexcept;
[[nodiscard]] std::optional<std::uint32_t> GsPsmt8ByteAddress(std::uint16_t base_pointer,
                                                              std::uint16_t buffer_width,
                                                              std::uint32_t x,
                                                              std::uint32_t y) noexcept;
[[nodiscard]] std::optional<std::uint32_t> GsPsmt4NibbleAddress(std::uint16_t base_pointer,
                                                                std::uint16_t buffer_width,
                                                                std::uint32_t x,
                                                                std::uint32_t y) noexcept;

[[nodiscard]] constexpr float GsAlphaCoefficient(const std::uint8_t raw_alpha) noexcept
{
    return static_cast<float>(raw_alpha) / 128.0F;
}

[[nodiscard]] constexpr std::uint8_t GsAlphaToRgba8(const std::uint8_t raw_alpha) noexcept
{
    if (raw_alpha >= 128U)
        return 255U;
    return static_cast<std::uint8_t>((static_cast<std::uint32_t>(raw_alpha) * 255U + 64U) / 128U);
}

// [any worker thread; reentrant] Strictly decodes the observed one-block
// frontend transfer-0 TDX family. Unsupported packet layouts fail closed. The
// returned image and plan own all state.
[[nodiscard]] asset::DecodeResult<DecodedFrontEndTdx> DecodeFrontEndTdx(
    std::span<const std::byte> bytes, asset::DecodeLimits limits = {});
} // namespace omega::retail
