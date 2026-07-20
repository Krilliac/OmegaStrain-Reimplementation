#pragma once

#include "omega/asset/texture_storage_ir.h"
#include "omega/runtime/debug_image.h"

#include <cstdint>
#include <expected>
#include <string_view>

namespace omega::runtime
{
struct Packed24TransferDebugImageLimits
{
    // Synthetic diagnostic budgets, not retail, GPU, or user-configurable limits.
    std::uint64_t maximum_source_bytes = 48ULL * 1024ULL * 1024ULL;
    std::uint64_t maximum_output_bytes = 64ULL * 1024ULL * 1024ULL;
};

enum class Packed24TransferDebugImageErrorCode
{
    InvalidTextureDimensions,
    InvalidSampleEncoding,
    UnsupportedSampleEncoding,
    BlockCountMismatch,
    PlaneCountMismatch,
    UnexpectedPalette,
    InvalidPlaneDimensions,
    InvalidTransferElementEncoding,
    UnsupportedTransferElementEncoding,
    TexturePlaneDimensionMismatch,
    SourceByteSizeOverflow,
    OutputByteSizeOverflow,
    SourceByteSizeMismatch,
    SourceByteLimitExceeded,
    OutputByteLimitExceeded,
    AllocationFailed,
};

[[nodiscard]] constexpr std::string_view Packed24TransferDebugImageErrorCodeName(
    const Packed24TransferDebugImageErrorCode code) noexcept
{
    switch (code)
    {
    case Packed24TransferDebugImageErrorCode::InvalidTextureDimensions:
        return "invalid-texture-dimensions";
    case Packed24TransferDebugImageErrorCode::InvalidSampleEncoding:
        return "invalid-sample-encoding";
    case Packed24TransferDebugImageErrorCode::UnsupportedSampleEncoding:
        return "unsupported-sample-encoding";
    case Packed24TransferDebugImageErrorCode::BlockCountMismatch:
        return "block-count-mismatch";
    case Packed24TransferDebugImageErrorCode::PlaneCountMismatch:
        return "plane-count-mismatch";
    case Packed24TransferDebugImageErrorCode::UnexpectedPalette:
        return "unexpected-palette";
    case Packed24TransferDebugImageErrorCode::InvalidPlaneDimensions:
        return "invalid-plane-dimensions";
    case Packed24TransferDebugImageErrorCode::InvalidTransferElementEncoding:
        return "invalid-transfer-element-encoding";
    case Packed24TransferDebugImageErrorCode::UnsupportedTransferElementEncoding:
        return "unsupported-transfer-element-encoding";
    case Packed24TransferDebugImageErrorCode::TexturePlaneDimensionMismatch:
        return "texture-plane-dimension-mismatch";
    case Packed24TransferDebugImageErrorCode::SourceByteSizeOverflow:
        return "source-byte-size-overflow";
    case Packed24TransferDebugImageErrorCode::OutputByteSizeOverflow:
        return "output-byte-size-overflow";
    case Packed24TransferDebugImageErrorCode::SourceByteSizeMismatch:
        return "source-byte-size-mismatch";
    case Packed24TransferDebugImageErrorCode::SourceByteLimitExceeded:
        return "source-byte-limit-exceeded";
    case Packed24TransferDebugImageErrorCode::OutputByteLimitExceeded:
        return "output-byte-limit-exceeded";
    case Packed24TransferDebugImageErrorCode::AllocationFailed:
        return "allocation-failed";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view Packed24TransferDebugImageErrorMessage(
    const Packed24TransferDebugImageErrorCode code) noexcept
{
    switch (code)
    {
    case Packed24TransferDebugImageErrorCode::InvalidTextureDimensions:
        return "packed-24 transfer debug image requires nonzero texture dimensions";
    case Packed24TransferDebugImageErrorCode::InvalidSampleEncoding:
        return "packed-24 transfer debug image sample encoding is invalid";
    case Packed24TransferDebugImageErrorCode::UnsupportedSampleEncoding:
        return "packed-24 transfer debug image requires packed-24 sample encoding";
    case Packed24TransferDebugImageErrorCode::BlockCountMismatch:
        return "packed-24 transfer debug image requires exactly one block";
    case Packed24TransferDebugImageErrorCode::PlaneCountMismatch:
        return "packed-24 transfer debug image requires exactly one plane";
    case Packed24TransferDebugImageErrorCode::UnexpectedPalette:
        return "packed-24 transfer debug image does not accept a palette";
    case Packed24TransferDebugImageErrorCode::InvalidPlaneDimensions:
        return "packed-24 transfer debug image requires nonzero plane dimensions";
    case Packed24TransferDebugImageErrorCode::InvalidTransferElementEncoding:
        return "packed-24 transfer debug image transfer-element encoding is invalid";
    case Packed24TransferDebugImageErrorCode::UnsupportedTransferElementEncoding:
        return "packed-24 transfer debug image requires packed-24 transfer-element encoding";
    case Packed24TransferDebugImageErrorCode::TexturePlaneDimensionMismatch:
        return "packed-24 transfer debug image texture and plane dimensions do not match";
    case Packed24TransferDebugImageErrorCode::SourceByteSizeOverflow:
        return "packed-24 transfer debug image source byte size overflows";
    case Packed24TransferDebugImageErrorCode::OutputByteSizeOverflow:
        return "packed-24 transfer debug image output byte size overflows";
    case Packed24TransferDebugImageErrorCode::SourceByteSizeMismatch:
        return "packed-24 transfer debug image source byte size does not match the transfer rectangle";
    case Packed24TransferDebugImageErrorCode::SourceByteLimitExceeded:
        return "packed-24 transfer debug image exceeds the source-byte limit";
    case Packed24TransferDebugImageErrorCode::OutputByteLimitExceeded:
        return "packed-24 transfer debug image exceeds the output-byte limit";
    case Packed24TransferDebugImageErrorCode::AllocationFailed:
        return "packed-24 transfer debug image allocation failed";
    }
    return "packed-24 transfer debug image error is unknown";
}

struct Packed24TransferDebugImageError
{
    Packed24TransferDebugImageErrorCode code;
    // Fixed category-only text; it contains no dimensions, payload, offset, or source identity.
    std::string_view message;
};

// [any worker thread; reentrant] Statelessly borrows one strict Packed24 transfer shape for this
// call, returns independently owned source-slot diagnostic bytes, and performs no I/O, platform,
// GPU, service, or shared-state work.
[[nodiscard]] std::expected<DebugImage, Packed24TransferDebugImageError>
BuildPacked24TransferDebugImage(const asset::TextureStorageIR& storage,
    const Packed24TransferDebugImageLimits& limits = {});
} // namespace omega::runtime
