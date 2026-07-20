#include "omega/runtime/packed24_transfer_debug_image.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <stdexcept>
#include <vector>

namespace omega::runtime
{
namespace
{
struct ImagePlan
{
    std::uint64_t source_bytes = 0U;
    std::uint64_t output_bytes = 0U;
};

[[nodiscard]] constexpr Packed24TransferDebugImageError Error(
    const Packed24TransferDebugImageErrorCode code) noexcept
{
    return Packed24TransferDebugImageError{
        .code = code,
        .message = Packed24TransferDebugImageErrorMessage(code),
    };
}

[[nodiscard]] constexpr bool Multiply(
    const std::uint64_t left, const std::uint64_t right,
    std::uint64_t& output) noexcept
{
    if (left != 0U && right > std::numeric_limits<std::uint64_t>::max() / left)
        return false;
    output = left * right;
    return true;
}

[[nodiscard]] constexpr bool IsValidSampleEncoding(
    const asset::TextureSampleEncoding encoding) noexcept
{
    switch (encoding)
    {
    case asset::TextureSampleEncoding::Indexed4:
    case asset::TextureSampleEncoding::Indexed8:
    case asset::TextureSampleEncoding::Packed24:
    case asset::TextureSampleEncoding::Packed32:
        return true;
    }
    return false;
}

[[nodiscard]] constexpr bool IsValidTransferElementEncoding(
    const asset::TextureTransferElementEncoding encoding) noexcept
{
    switch (encoding)
    {
    case asset::TextureTransferElementEncoding::Packed4:
    case asset::TextureTransferElementEncoding::Packed8:
    case asset::TextureTransferElementEncoding::Packed24:
    case asset::TextureTransferElementEncoding::Packed32:
        return true;
    }
    return false;
}

[[nodiscard]] std::expected<ImagePlan, Packed24TransferDebugImageError> Preflight(
    const asset::TextureStorageIR& storage,
    const Packed24TransferDebugImageLimits& limits) noexcept
{
    if (storage.width == 0U || storage.height == 0U)
    {
        return std::unexpected(
            Error(Packed24TransferDebugImageErrorCode::InvalidTextureDimensions));
    }
    if (!IsValidSampleEncoding(storage.sample_encoding))
    {
        return std::unexpected(
            Error(Packed24TransferDebugImageErrorCode::InvalidSampleEncoding));
    }
    if (storage.sample_encoding != asset::TextureSampleEncoding::Packed24)
    {
        return std::unexpected(
            Error(Packed24TransferDebugImageErrorCode::UnsupportedSampleEncoding));
    }
    if (storage.blocks.size() != 1U)
    {
        return std::unexpected(
            Error(Packed24TransferDebugImageErrorCode::BlockCountMismatch));
    }

    const asset::TextureStorageBlockIR& block = storage.blocks.front();
    if (block.planes.size() != 1U)
    {
        return std::unexpected(
            Error(Packed24TransferDebugImageErrorCode::PlaneCountMismatch));
    }
    if (block.palette)
    {
        return std::unexpected(
            Error(Packed24TransferDebugImageErrorCode::UnexpectedPalette));
    }

    const asset::TextureStoragePlaneIR& plane = block.planes.front();
    if (plane.width == 0U || plane.height == 0U)
    {
        return std::unexpected(
            Error(Packed24TransferDebugImageErrorCode::InvalidPlaneDimensions));
    }
    if (!IsValidTransferElementEncoding(plane.element_encoding))
    {
        return std::unexpected(Error(
            Packed24TransferDebugImageErrorCode::InvalidTransferElementEncoding));
    }
    if (plane.element_encoding != asset::TextureTransferElementEncoding::Packed24)
    {
        return std::unexpected(Error(
            Packed24TransferDebugImageErrorCode::UnsupportedTransferElementEncoding));
    }
    if (plane.width != storage.width || plane.height != storage.height)
    {
        return std::unexpected(Error(
            Packed24TransferDebugImageErrorCode::TexturePlaneDimensionMismatch));
    }

    std::uint64_t area = 0U;
    std::uint64_t source_bytes = 0U;
    std::uint64_t output_bytes = 0U;
    if (!Multiply(storage.width, storage.height, area) ||
        !Multiply(area, 3U, source_bytes))
    {
        return std::unexpected(
            Error(Packed24TransferDebugImageErrorCode::SourceByteSizeOverflow));
    }
    if (!Multiply(area, 4U, output_bytes))
    {
        return std::unexpected(
            Error(Packed24TransferDebugImageErrorCode::OutputByteSizeOverflow));
    }
    if (source_bytes != static_cast<std::uint64_t>(plane.bytes.size()))
    {
        return std::unexpected(
            Error(Packed24TransferDebugImageErrorCode::SourceByteSizeMismatch));
    }
    if (source_bytes > limits.maximum_source_bytes)
    {
        return std::unexpected(
            Error(Packed24TransferDebugImageErrorCode::SourceByteLimitExceeded));
    }
    if (output_bytes > limits.maximum_output_bytes ||
        output_bytes > std::numeric_limits<std::size_t>::max())
    {
        return std::unexpected(
            Error(Packed24TransferDebugImageErrorCode::OutputByteLimitExceeded));
    }

    return ImagePlan{
        .source_bytes = source_bytes,
        .output_bytes = output_bytes,
    };
}
} // namespace

std::expected<DebugImage, Packed24TransferDebugImageError>
BuildPacked24TransferDebugImage(const asset::TextureStorageIR& storage,
    const Packed24TransferDebugImageLimits& limits)
{
    const auto planned = Preflight(storage, limits);
    if (!planned)
        return std::unexpected(planned.error());

    try
    {
        DebugImage image{
            .width = storage.width,
            .height = storage.height,
            .rgba8_pixels =
                std::vector<std::byte>(static_cast<std::size_t>(planned->output_bytes)),
        };
        const asset::TextureStoragePlaneIR& plane = storage.blocks.front().planes.front();
        for (std::size_t source = 0U, output = 0U;
             source < static_cast<std::size_t>(planned->source_bytes);
             source += 3U, output += 4U)
        {
            image.rgba8_pixels[output] = plane.bytes[source];
            image.rgba8_pixels[output + 1U] = plane.bytes[source + 1U];
            image.rgba8_pixels[output + 2U] = plane.bytes[source + 2U];
            image.rgba8_pixels[output + 3U] = std::byte{0xff};
        }
        return image;
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected(
            Error(Packed24TransferDebugImageErrorCode::AllocationFailed));
    }
    catch (const std::length_error&)
    {
        return std::unexpected(
            Error(Packed24TransferDebugImageErrorCode::AllocationFailed));
    }
}
} // namespace omega::runtime
