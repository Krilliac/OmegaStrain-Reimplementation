#pragma once

#include "omega/asset/texture_storage_ir.h"
#include "omega/runtime/debug_image.h"

#include <cstdint>
#include <expected>
#include <string_view>

namespace omega::runtime
{
// Synthetic diagnostic budgets, not retail texture or renderer limits.
struct TextureStorageTopologyDebugImageLimits
{
    std::uint64_t maximum_blocks = 4096U;
    std::uint64_t maximum_planes = 262144U;
    std::uint64_t maximum_palette_entries = 1048576U;
    std::uint64_t maximum_output_bytes = 64ULL * 1024ULL * 1024ULL;
};

enum class TextureStorageTopologyDebugImageErrorCode
{
    InvalidTextureDimensions,
    InvalidSampleEncoding,
    EmptyBlockSet,
    BlockLimitExceeded,
    EmptyPlaneSet,
    PlaneMarkerCapacityExceeded,
    PlaneLimitExceeded,
    InvalidPlaneDimensions,
    InvalidTransferElementEncoding,
    PlaneByteSizeOverflow,
    PlaneByteSizeMismatch,
    InvalidPaletteDimensions,
    PaletteEntryCountMismatch,
    PaletteEntryLimitExceeded,
    ImageDimensionOverflow,
    ImageByteSizeOverflow,
    OutputByteLimitExceeded,
    AllocationFailed,
};

[[nodiscard]] constexpr std::string_view TextureStorageTopologyDebugImageErrorCodeName(
    const TextureStorageTopologyDebugImageErrorCode code) noexcept
{
    switch (code)
    {
    case TextureStorageTopologyDebugImageErrorCode::InvalidTextureDimensions:
        return "invalid-texture-dimensions";
    case TextureStorageTopologyDebugImageErrorCode::InvalidSampleEncoding:
        return "invalid-sample-encoding";
    case TextureStorageTopologyDebugImageErrorCode::EmptyBlockSet:
        return "empty-block-set";
    case TextureStorageTopologyDebugImageErrorCode::BlockLimitExceeded:
        return "block-limit-exceeded";
    case TextureStorageTopologyDebugImageErrorCode::EmptyPlaneSet:
        return "empty-plane-set";
    case TextureStorageTopologyDebugImageErrorCode::PlaneMarkerCapacityExceeded:
        return "plane-marker-capacity-exceeded";
    case TextureStorageTopologyDebugImageErrorCode::PlaneLimitExceeded:
        return "plane-limit-exceeded";
    case TextureStorageTopologyDebugImageErrorCode::InvalidPlaneDimensions:
        return "invalid-plane-dimensions";
    case TextureStorageTopologyDebugImageErrorCode::InvalidTransferElementEncoding:
        return "invalid-transfer-element-encoding";
    case TextureStorageTopologyDebugImageErrorCode::PlaneByteSizeOverflow:
        return "plane-byte-size-overflow";
    case TextureStorageTopologyDebugImageErrorCode::PlaneByteSizeMismatch:
        return "plane-byte-size-mismatch";
    case TextureStorageTopologyDebugImageErrorCode::InvalidPaletteDimensions:
        return "invalid-palette-dimensions";
    case TextureStorageTopologyDebugImageErrorCode::PaletteEntryCountMismatch:
        return "palette-entry-count-mismatch";
    case TextureStorageTopologyDebugImageErrorCode::PaletteEntryLimitExceeded:
        return "palette-entry-limit-exceeded";
    case TextureStorageTopologyDebugImageErrorCode::ImageDimensionOverflow:
        return "image-dimension-overflow";
    case TextureStorageTopologyDebugImageErrorCode::ImageByteSizeOverflow:
        return "image-byte-size-overflow";
    case TextureStorageTopologyDebugImageErrorCode::OutputByteLimitExceeded:
        return "output-byte-limit-exceeded";
    case TextureStorageTopologyDebugImageErrorCode::AllocationFailed:
        return "allocation-failed";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view TextureStorageTopologyDebugImageErrorMessage(
    const TextureStorageTopologyDebugImageErrorCode code) noexcept
{
    switch (code)
    {
    case TextureStorageTopologyDebugImageErrorCode::InvalidTextureDimensions:
        return "texture storage topology image requires nonzero texture dimensions";
    case TextureStorageTopologyDebugImageErrorCode::InvalidSampleEncoding:
        return "texture storage topology image sample encoding is invalid";
    case TextureStorageTopologyDebugImageErrorCode::EmptyBlockSet:
        return "texture storage topology image requires at least one block";
    case TextureStorageTopologyDebugImageErrorCode::BlockLimitExceeded:
        return "texture storage topology image exceeds the block limit";
    case TextureStorageTopologyDebugImageErrorCode::EmptyPlaneSet:
        return "texture storage topology image requires every block to contain a plane";
    case TextureStorageTopologyDebugImageErrorCode::PlaneMarkerCapacityExceeded:
        return "texture storage topology image exceeds the per-block plane-marker capacity";
    case TextureStorageTopologyDebugImageErrorCode::PlaneLimitExceeded:
        return "texture storage topology image exceeds the plane limit";
    case TextureStorageTopologyDebugImageErrorCode::InvalidPlaneDimensions:
        return "texture storage topology image requires nonzero plane dimensions";
    case TextureStorageTopologyDebugImageErrorCode::InvalidTransferElementEncoding:
        return "texture storage topology image transfer-element encoding is invalid";
    case TextureStorageTopologyDebugImageErrorCode::PlaneByteSizeOverflow:
        return "texture storage topology image plane byte size overflows";
    case TextureStorageTopologyDebugImageErrorCode::PlaneByteSizeMismatch:
        return "texture storage topology image plane byte size does not match its rectangle";
    case TextureStorageTopologyDebugImageErrorCode::InvalidPaletteDimensions:
        return "texture storage topology image requires nonzero palette dimensions";
    case TextureStorageTopologyDebugImageErrorCode::PaletteEntryCountMismatch:
        return "texture storage topology image palette entry count does not match its rectangle";
    case TextureStorageTopologyDebugImageErrorCode::PaletteEntryLimitExceeded:
        return "texture storage topology image exceeds the palette-entry limit";
    case TextureStorageTopologyDebugImageErrorCode::ImageDimensionOverflow:
        return "texture storage topology image dimensions overflow";
    case TextureStorageTopologyDebugImageErrorCode::ImageByteSizeOverflow:
        return "texture storage topology image byte size overflows";
    case TextureStorageTopologyDebugImageErrorCode::OutputByteLimitExceeded:
        return "texture storage topology image exceeds the output-byte limit";
    case TextureStorageTopologyDebugImageErrorCode::AllocationFailed:
        return "texture storage topology image allocation failed";
    }
    return "texture storage topology image error is unknown";
}

struct TextureStorageTopologyDebugImageError
{
    TextureStorageTopologyDebugImageErrorCode code =
        TextureStorageTopologyDebugImageErrorCode::InvalidTextureDimensions;
    // Fixed category text only; it contains no payload, resource, or source identity.
    std::string_view message = TextureStorageTopologyDebugImageErrorMessage(code);
};

// [any thread; reentrant] Produces an owned source-order topology contact sheet. The image
// describes only canonical block, plane, encoding, and palette presence; payload bytes and
// unassigned retail/display semantics do not affect its pixels. The input is borrowed only for
// the duration of the call, and the result performs no I/O or shared-state mutation.
[[nodiscard]] std::expected<DebugImage, TextureStorageTopologyDebugImageError>
    BuildTextureStorageTopologyDebugImage(const asset::TextureStorageIR& storage,
        const TextureStorageTopologyDebugImageLimits& limits = {});
} // namespace omega::runtime
