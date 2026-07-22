#include "omega/retail/frontend_tdx_decoder.h"

#include "omega/retail/container_descriptors.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace omega::retail
{
namespace
{
constexpr std::uint64_t kHeaderBytes = 64;
constexpr std::uint64_t kTransferControlPrefixBytes = 0x20;
constexpr std::uint64_t kTransferPacketBytes = 0x60;
constexpr std::uint64_t kGsLocalMemoryBytes = 4ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kGsLocalMemoryWords = kGsLocalMemoryBytes / 4U;
constexpr std::uint64_t kGsLocalMemoryNibbles = kGsLocalMemoryBytes * 2U;

constexpr std::uint32_t kSecondaryBase = 0x20;
constexpr std::uint32_t kPaletteObjectOffset = 0x40;
constexpr std::uint32_t kPrimaryBase = 0xA0;
constexpr std::uint32_t kPrimaryObjectOffset = 0xC0;
constexpr std::uint32_t kPaletteDataOffset = 0x120;
constexpr std::uint32_t kPrimaryDataOffset = 0x520;

struct ParsedPacket
{
    TdxGsUploadRectangle rectangle;
    std::uint32_t data_reference = 0;
};

[[nodiscard]] asset::DecodeError Error(
    const asset::DecodeErrorCode code, std::string message,
    const std::optional<std::uint64_t> byte_offset = std::nullopt)
{
    return asset::DecodeError{
        .code = code,
        .byte_offset = byte_offset,
        .message = std::move(message),
    };
}

[[nodiscard]] std::uint16_t ReadU16(const std::span<const std::byte> bytes,
                                    const std::size_t offset) noexcept
{
    return std::to_integer<std::uint16_t>(bytes[offset]) |
           (std::to_integer<std::uint16_t>(bytes[offset + 1]) << 8U);
}

[[nodiscard]] std::uint32_t ReadU32(const std::span<const std::byte> bytes,
                                    const std::size_t offset) noexcept
{
    return std::to_integer<std::uint32_t>(bytes[offset]) |
           (std::to_integer<std::uint32_t>(bytes[offset + 1]) << 8U) |
           (std::to_integer<std::uint32_t>(bytes[offset + 2]) << 16U) |
           (std::to_integer<std::uint32_t>(bytes[offset + 3]) << 24U);
}

[[nodiscard]] std::uint64_t ReadU64(const std::span<const std::byte> bytes,
                                    const std::size_t offset) noexcept
{
    return static_cast<std::uint64_t>(ReadU32(bytes, offset)) |
           (static_cast<std::uint64_t>(ReadU32(bytes, offset + 4U)) << 32U);
}

[[nodiscard]] bool Add(const std::uint64_t left, const std::uint64_t right,
                       std::uint64_t& result) noexcept
{
    if (right > std::numeric_limits<std::uint64_t>::max() - left)
        return false;
    result = left + right;
    return true;
}

[[nodiscard]] bool Multiply(const std::uint64_t left, const std::uint64_t right,
                            std::uint64_t& result) noexcept
{
    if (left != 0 && right > std::numeric_limits<std::uint64_t>::max() / left)
        return false;
    result = left * right;
    return true;
}

[[nodiscard]] bool IsZero(const std::span<const std::byte> bytes) noexcept
{
    return std::ranges::all_of(bytes, [](const std::byte value) { return value == std::byte{0}; });
}

[[nodiscard]] constexpr std::uint64_t Bit(const std::uint32_t value, const unsigned bit) noexcept
{
    return (value >> bit) & 1U;
}

[[nodiscard]] asset::DecodeResult<void> ValidateTransferControlPrefix(
    const std::span<const std::byte> block, const std::uint64_t block_file_offset,
    const std::uint32_t prefix_offset)
{
    if (prefix_offset > block.size() ||
        kTransferControlPrefixBytes > block.size() - prefix_offset)
        return std::unexpected(Error(asset::DecodeErrorCode::InvalidReference,
                                     "frontend TDX transfer control prefix is outside its block",
                                     block_file_offset + prefix_offset));

    const auto prefix = block.subspan(prefix_offset, kTransferControlPrefixBytes);

    // The active pointer skips a DMA CNT tag followed by a four-loop packed
    // A+D GIF tag. Keep every reserved field closed rather than accepting an
    // arbitrary nonzero prefix from an unproven transfer-chain variant.
    constexpr std::uint32_t kDmaCntTag = (1U << 28U) | 6U;
    constexpr std::uint64_t kPackedAdGifTag = (1ULL << 60U) | 4U;
    constexpr std::uint64_t kAdRegisterDescriptor = 0x0EULL;
    if (ReadU32(prefix, 0x00) != kDmaCntTag || ReadU32(prefix, 0x04) != 0U ||
        ReadU64(prefix, 0x08) != 0U || ReadU64(prefix, 0x10) != kPackedAdGifTag ||
        ReadU64(prefix, 0x18) != kAdRegisterDescriptor)
        return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
                                     "frontend TDX transfer control prefix is malformed",
                                     block_file_offset + prefix_offset));
    return {};
}

[[nodiscard]] asset::DecodeResult<ParsedPacket> ParsePsmct32UploadPacket(
    const std::span<const std::byte> block, const std::uint64_t block_file_offset,
    const std::uint32_t object_offset, const std::uint16_t expected_buffer_width,
    const std::uint16_t expected_width, const std::uint16_t expected_height,
    const std::uint64_t expected_payload_bytes)
{
    const std::uint64_t packet_file_offset = block_file_offset + object_offset;
    if (object_offset > block.size() || kTransferPacketBytes > block.size() - object_offset)
        return std::unexpected(Error(asset::DecodeErrorCode::InvalidReference,
                                     "frontend TDX transfer packet is outside its block",
                                     packet_file_offset));

    const auto packet = block.subspan(object_offset, kTransferPacketBytes);
    if (ReadU32(packet, 0x00) != 0)
        return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
                                     "frontend TDX transfer source fields are unsupported",
                                     packet_file_offset));

    constexpr std::uint32_t kBitbltDestinationMask = 0x3F3F3FFFU;
    const std::uint32_t destination = ReadU32(packet, 0x04);
    if ((destination & ~kBitbltDestinationMask) != 0)
        return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
                                     "frontend TDX BITBLTBUF reserved bits are nonzero",
                                     packet_file_offset + 4U));
    const std::uint16_t destination_base = static_cast<std::uint16_t>(destination & 0x3FFFU);
    const std::uint16_t destination_width =
        static_cast<std::uint16_t>((destination >> 16U) & 0x3FU);
    const std::uint8_t destination_format = static_cast<std::uint8_t>((destination >> 24U) & 0x3FU);
    if (destination_format != static_cast<std::uint8_t>(TdxGsPixelStorageFormat::Psmct32))
        return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
                                     "frontend TDX upload is not PSMCT32",
                                     packet_file_offset + 4U));
    if (destination_width != expected_buffer_width)
        return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
                                     "frontend TDX upload buffer width contradicts its header",
                                     packet_file_offset + 6U));
    if (ReadU64(packet, 0x08) != 0x50U)
        return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
                                     "frontend TDX BITBLTBUF register identifier is invalid",
                                     packet_file_offset + 8U));

    if (ReadU64(packet, 0x10) != 0)
        return std::unexpected(
            Error(asset::DecodeErrorCode::UnsupportedVariant,
                  "frontend TDX transfer positioning or direction is unsupported",
                  packet_file_offset + 0x10U));
    if (ReadU64(packet, 0x18) != 0x51U)
        return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
                                     "frontend TDX TRXPOS register identifier is invalid",
                                     packet_file_offset + 0x18U));

    if (ReadU32(packet, 0x20) != expected_width || ReadU32(packet, 0x24) != expected_height)
        return std::unexpected(
            Error(asset::DecodeErrorCode::UnsupportedVariant,
                  "frontend TDX upload rectangle contradicts its display relationship",
                  packet_file_offset + 0x20U));
    if (ReadU64(packet, 0x28) != 0x52U)
        return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
                                     "frontend TDX TRXREG register identifier is invalid",
                                     packet_file_offset + 0x28U));

    if (ReadU64(packet, 0x30) != 0)
        return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
                                     "frontend TDX transfer direction is not host-to-local",
                                     packet_file_offset + 0x30U));
    if (ReadU64(packet, 0x38) != 0x53U)
        return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
                                     "frontend TDX TRXDIR register identifier is invalid",
                                     packet_file_offset + 0x38U));

    if (expected_payload_bytes == 0 || expected_payload_bytes % 16U != 0)
        return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
                                     "frontend TDX upload payload is not qword-aligned",
                                     packet_file_offset));
    const std::uint64_t qword_count64 = expected_payload_bytes / 16U;
    if (qword_count64 > 0xFFFFU)
        return std::unexpected(Error(asset::DecodeErrorCode::Overflow,
                                     "frontend TDX upload qword count overflows its packet",
                                     packet_file_offset));
    const std::uint32_t qword_count = static_cast<std::uint32_t>(qword_count64);
    const std::uint32_t gif_low = ReadU32(packet, 0x40);
    if ((gif_low & 0x7FFFU) != qword_count || (gif_low & 0xFFFF0000U) != 0 ||
        ReadU32(packet, 0x44) != 0x08000000U || ReadU64(packet, 0x48) != 0)
        return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
                                     "frontend TDX GIF IMAGE tag is invalid",
                                     packet_file_offset + 0x40U));
    if (ReadU32(packet, 0x50) != (0x30000000U | qword_count) || ReadU64(packet, 0x58) != 0)
        return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
                                     "frontend TDX DMA REF tag is invalid",
                                     packet_file_offset + 0x50U));

    return ParsedPacket{
        .rectangle =
            {
                .destination_base_pointer = destination_base,
                .destination_buffer_width = destination_width,
                .destination_x = 0,
                .destination_y = 0,
                .width = expected_width,
                .height = expected_height,
            },
        .data_reference = ReadU32(packet, 0x54),
    };
}

[[nodiscard]] asset::DecodeResult<void> ReplayPsmct32Upload(const std::span<const std::byte> source,
                                                            const TdxGsUploadRectangle& rectangle,
                                                            std::vector<std::byte>& gs_memory)
{
    std::uint64_t expected_bytes = 0;
    if (!Multiply(rectangle.width, rectangle.height, expected_bytes) ||
        !Multiply(expected_bytes, 4U, expected_bytes))
        return std::unexpected(
            Error(asset::DecodeErrorCode::Overflow, "frontend TDX upload size overflows"));
    if (source.size() != expected_bytes)
        return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
                                     "frontend TDX upload payload length is inconsistent"));

    std::size_t source_offset = 0;
    for (std::uint32_t y = 0; y < rectangle.height; ++y)
    {
        for (std::uint32_t x = 0; x < rectangle.width; ++x)
        {
            const auto word_address = GsPsmct32WordAddress(
                rectangle.destination_base_pointer, rectangle.destination_buffer_width,
                rectangle.destination_x + x, rectangle.destination_y + y);
            if (!word_address || *word_address >= kGsLocalMemoryWords)
                return std::unexpected(
                    Error(asset::DecodeErrorCode::InvalidReference,
                          "frontend TDX upload visits an invalid GS local-memory address"));
            const std::size_t destination_offset = static_cast<std::size_t>(*word_address) * 4U;
            if (destination_offset > gs_memory.size() || 4U > gs_memory.size() - destination_offset)
                return std::unexpected(Error(asset::DecodeErrorCode::InvalidReference,
                                             "frontend TDX upload exceeds GS local memory"));
            std::copy_n(source.begin() + static_cast<std::ptrdiff_t>(source_offset), 4,
                        gs_memory.begin() + static_cast<std::ptrdiff_t>(destination_offset));
            source_offset += 4U;
        }
    }
    return {};
}

[[nodiscard]] std::uint16_t PaletteSourceOrdinal(const asset::IndexedImageEncoding encoding,
                                                 const std::uint16_t logical_index) noexcept
{
    if (encoding == asset::IndexedImageEncoding::Indexed4)
        return logical_index;
    return static_cast<std::uint16_t>((logical_index & 0xE7U) | ((logical_index & 0x08U) << 1U) |
                                      ((logical_index & 0x10U) >> 1U));
}

[[nodiscard]] asset::DecodeResult<void> ExpandIndices(const FrontEndTdxGsUploadPlan& plan,
                                                      asset::IndexedImageIR& image,
                                                      const std::vector<std::byte>& gs_memory)
{
    std::size_t output_offset = 0;
    for (std::uint32_t y = 0; y < image.height; ++y)
    {
        for (std::uint32_t x = 0; x < image.width; ++x)
        {
            if (image.source_encoding == asset::IndexedImageEncoding::Indexed8)
            {
                const auto address =
                    GsPsmt8ByteAddress(plan.texture_base_pointer, plan.texture_buffer_width, x, y);
                if (!address || *address >= gs_memory.size())
                    return std::unexpected(
                        Error(asset::DecodeErrorCode::InvalidReference,
                              "frontend TDX indexed-8 sample address is invalid"));
                image.indices[output_offset] = std::to_integer<std::uint8_t>(gs_memory[*address]);
            }
            else
            {
                const auto nibble_address = GsPsmt4NibbleAddress(plan.texture_base_pointer,
                                                                 plan.texture_buffer_width, x, y);
                if (!nibble_address || *nibble_address >= kGsLocalMemoryNibbles)
                    return std::unexpected(
                        Error(asset::DecodeErrorCode::InvalidReference,
                              "frontend TDX indexed-4 sample address is invalid"));
                const std::size_t byte_address = static_cast<std::size_t>(*nibble_address >> 1U);
                if (byte_address >= gs_memory.size())
                    return std::unexpected(
                        Error(asset::DecodeErrorCode::InvalidReference,
                              "frontend TDX indexed-4 sample exceeds GS local memory"));
                const std::uint8_t packed = std::to_integer<std::uint8_t>(gs_memory[byte_address]);
                image.indices[output_offset] = static_cast<std::uint8_t>(
                    ((*nibble_address & 1U) == 0 ? packed : packed >> 4U) & 0x0FU);
            }
            ++output_offset;
        }
    }
    return {};
}

[[nodiscard]] asset::DecodeResult<void> ExpandPalette(const FrontEndTdxGsUploadPlan& plan,
                                                      asset::IndexedImageIR& image,
                                                      const std::vector<std::byte>& gs_memory)
{
    const std::uint16_t palette_width = plan.palette_upload.width;
    for (std::uint16_t logical_index = 0; logical_index < image.palette.size(); ++logical_index)
    {
        const std::uint16_t source_ordinal =
            PaletteSourceOrdinal(image.source_encoding, logical_index);
        const std::uint32_t source_x = source_ordinal % palette_width;
        const std::uint32_t source_y = source_ordinal / palette_width;
        const auto word_address =
            GsPsmct32WordAddress(plan.palette_upload.destination_base_pointer,
                                 plan.palette_upload.destination_buffer_width, source_x, source_y);
        if (!word_address || *word_address >= kGsLocalMemoryWords)
            return std::unexpected(Error(asset::DecodeErrorCode::InvalidReference,
                                         "frontend TDX palette address is invalid"));
        const std::size_t byte_address = static_cast<std::size_t>(*word_address) * 4U;
        if (byte_address > gs_memory.size() || 4U > gs_memory.size() - byte_address)
            return std::unexpected(Error(asset::DecodeErrorCode::InvalidReference,
                                         "frontend TDX palette exceeds GS local memory"));
        image.palette[logical_index] = asset::RawGsRgba8{
            .red = std::to_integer<std::uint8_t>(gs_memory[byte_address]),
            .green = std::to_integer<std::uint8_t>(gs_memory[byte_address + 1U]),
            .blue = std::to_integer<std::uint8_t>(gs_memory[byte_address + 2U]),
            .alpha = std::to_integer<std::uint8_t>(gs_memory[byte_address + 3U]),
        };
    }
    return {};
}
} // namespace

std::optional<std::uint32_t> GsPsmct32WordAddress(const std::uint16_t base_pointer,
                                                  const std::uint16_t buffer_width,
                                                  const std::uint32_t x,
                                                  const std::uint32_t y) noexcept
{
    if (base_pointer > 0x3FFFU || buffer_width == 0 || buffer_width > 0x3FU)
        return std::nullopt;
    const std::uint32_t local_x = x & 63U;
    const std::uint32_t local_y = y & 31U;
    const std::uint64_t within =
        Bit(local_x, 0) | (Bit(local_y, 0) << 1U) | (Bit(local_x, 1) << 2U) |
        (Bit(local_x, 2) << 3U) | (Bit(local_y, 1) << 4U) | (Bit(local_y, 2) << 5U) |
        (Bit(local_x, 3) << 6U) | (Bit(local_y, 3) << 7U) | (Bit(local_x, 4) << 8U) |
        (Bit(local_y, 4) << 9U) | (Bit(local_x, 5) << 10U);
    const std::uint64_t page_index = static_cast<std::uint64_t>(y / 32U) * buffer_width + x / 64U;
    const std::uint64_t address =
        static_cast<std::uint64_t>(base_pointer) * 64U + page_index * 2048U + within;
    return static_cast<std::uint32_t>(address & (kGsLocalMemoryWords - 1U));
}

std::optional<std::uint32_t> GsPsmt8ByteAddress(const std::uint16_t base_pointer,
                                                const std::uint16_t buffer_width,
                                                const std::uint32_t x,
                                                const std::uint32_t y) noexcept
{
    if (base_pointer > 0x3FFFU || buffer_width < 2U || buffer_width > 0x3FU ||
        (buffer_width & 1U) != 0)
        return std::nullopt;
    const std::uint32_t local_x = x & 127U;
    const std::uint32_t local_y = y & 63U;
    const std::uint64_t mixed = Bit(local_x, 2) ^ Bit(local_y, 1) ^ Bit(local_y, 2);
    const std::uint64_t within = Bit(local_y, 1) | (Bit(local_x, 3) << 1U) |
                                 (Bit(local_x, 0) << 2U) | (Bit(local_y, 0) << 3U) |
                                 (Bit(local_x, 1) << 4U) | (mixed << 5U) | (Bit(local_y, 2) << 6U) |
                                 (Bit(local_y, 3) << 7U) | (Bit(local_x, 4) << 8U) |
                                 (Bit(local_y, 4) << 9U) | (Bit(local_x, 5) << 10U) |
                                 (Bit(local_y, 5) << 11U) | (Bit(local_x, 6) << 12U);
    const std::uint64_t pages_per_row = buffer_width >> 1U;
    const std::uint64_t page_index = static_cast<std::uint64_t>(y / 64U) * pages_per_row + x / 128U;
    const std::uint64_t address =
        static_cast<std::uint64_t>(base_pointer) * 256U + page_index * 8192U + within;
    return static_cast<std::uint32_t>(address & (kGsLocalMemoryBytes - 1U));
}

std::optional<std::uint32_t> GsPsmt4NibbleAddress(const std::uint16_t base_pointer,
                                                  const std::uint16_t buffer_width,
                                                  const std::uint32_t x,
                                                  const std::uint32_t y) noexcept
{
    if (base_pointer > 0x3FFFU || buffer_width < 2U || buffer_width > 0x3FU ||
        (buffer_width & 1U) != 0)
        return std::nullopt;
    const std::uint32_t local_x = x & 127U;
    const std::uint32_t local_y = y & 127U;
    const std::uint64_t mixed = Bit(local_x, 2) ^ Bit(local_y, 1) ^ Bit(local_y, 2);
    const std::uint64_t within =
        Bit(local_y, 1) | (Bit(local_x, 3) << 1U) | (Bit(local_x, 4) << 2U) |
        (Bit(local_x, 0) << 3U) | (Bit(local_y, 0) << 4U) | (Bit(local_x, 1) << 5U) |
        (mixed << 6U) | (Bit(local_y, 2) << 7U) | (Bit(local_y, 3) << 8U) |
        (Bit(local_y, 4) << 9U) | (Bit(local_x, 5) << 10U) | (Bit(local_y, 5) << 11U) |
        (Bit(local_x, 6) << 12U) | (Bit(local_y, 6) << 13U);
    const std::uint64_t pages_per_row = buffer_width >> 1U;
    const std::uint64_t page_index =
        static_cast<std::uint64_t>(y / 128U) * pages_per_row + x / 128U;
    const std::uint64_t address =
        static_cast<std::uint64_t>(base_pointer) * 512U + page_index * 16384U + within;
    return static_cast<std::uint32_t>(address & (kGsLocalMemoryNibbles - 1U));
}

asset::DecodeResult<DecodedFrontEndTdx> DecodeFrontEndTdx(const std::span<const std::byte> bytes,
                                                          const asset::DecodeLimits limits)
{
    auto descriptor = InspectTdxContainer(bytes, limits);
    if (!descriptor)
        return std::unexpected(descriptor.error());

    const bool indexed8 = descriptor->bits_per_pixel == 8 &&
                          descriptor->observed_storage_format_code ==
                              static_cast<std::uint16_t>(TdxGsPixelStorageFormat::Psmt8);
    const bool indexed4 = descriptor->bits_per_pixel == 4 &&
                          descriptor->observed_storage_format_code ==
                              static_cast<std::uint16_t>(TdxGsPixelStorageFormat::Psmt4);
    if (!indexed8 && !indexed4)
        return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
                                     "frontend TDX is not an indexed-8 or indexed-4 texture", 8));
    if (descriptor->observed_flags != 5U && descriptor->observed_flags != 7U)
        return std::unexpected(
            Error(asset::DecodeErrorCode::UnsupportedVariant,
                  "frontend TDX header flags are outside the canonical frontend family", 2));
    if (descriptor->width < 32U || descriptor->width > 512U || descriptor->height < 32U ||
        descriptor->height > 512U)
        return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
                                     "frontend TDX display dimensions are outside "
                                     "the canonical frontend family",
                                     4));
    if (descriptor->block_count != 1U || descriptor->primary_plane_count != 1U ||
        descriptor->palette_plane_count != 1U)
        return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
                                     "frontend TDX requires exactly one block, "
                                     "primary plane, and palette plane",
                                     0x22));
    if (!descriptor->storage_word_matches_area_bit_formula)
        return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
                                     "frontend TDX storage size contradicts its display dimensions",
                                     0x0E));
    if (descriptor->counted_blocks_extent.relation != ObservedExtentRelation::Exact)
        return std::unexpected(
            Error(asset::DecodeErrorCode::Malformed,
                  "frontend TDX counted block extent must exactly match its input", 0x38));

    const std::uint16_t expected_texture_width =
        std::max<std::uint16_t>(2U, descriptor->width / 64U);
    if (descriptor->observed_width_unit_word != expected_texture_width ||
        (expected_texture_width & 1U) != 0)
        return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
                                     "frontend TDX texture buffer width is unsupported", 0x0C));

    const std::uint16_t palette_width = indexed8 ? 16U : 8U;
    const std::uint16_t palette_height = indexed8 ? 16U : 2U;
    if (ReadU16(bytes, 0x10) != palette_width || ReadU16(bytes, 0x12) != palette_height ||
        ReadU16(bytes, 0x14) != 32U || ReadU16(bytes, 0x16) != 0U || ReadU16(bytes, 0x18) != 1U ||
        ReadU16(bytes, 0x1A) != 4U)
        return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
                                     "frontend TDX palette header is unsupported", 0x10));
    if (ReadU16(bytes, 0x34) != 128U || ReadU16(bytes, 0x36) != 128U)
        return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
                                     "frontend TDX descriptor extents are unsupported", 0x34));

    const std::uint16_t upload_width = descriptor->width / 2U;
    const std::uint16_t upload_height =
        static_cast<std::uint16_t>(descriptor->height / (indexed8 ? 2U : 4U));
    std::uint64_t primary_payload_bytes = 0;
    if (!Multiply(upload_width, upload_height, primary_payload_bytes) ||
        !Multiply(primary_payload_bytes, 4U, primary_payload_bytes))
        return std::unexpected(Error(asset::DecodeErrorCode::Overflow,
                                     "frontend TDX primary upload size overflows", 4));
    const std::uint64_t palette_payload_bytes =
        static_cast<std::uint64_t>(palette_width) * palette_height * 4U;
    std::uint64_t expected_stride = 0;
    if (!Add(kPrimaryDataOffset, primary_payload_bytes, expected_stride) ||
        descriptor->block_stride != expected_stride)
        return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
                                     "frontend TDX block length contradicts its upload rectangles",
                                     0x38));

    const auto block = bytes.subspan(kHeaderBytes, descriptor->block_stride);
    if (ReadU32(block, 0x00) != 0x20U || ReadU32(block, 0x14) != 0x20U ||
        ReadU32(block, 0x18) != kPrimaryBase || ReadU32(block, 0x1C) != kSecondaryBase)
        return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
                                     "frontend TDX block pointer layout is unsupported",
                                     kHeaderBytes));
    if (!IsZero(block.subspan(4U, 0x10U)))
        return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
                                     "frontend TDX inactive block pointer slot is nonzero",
                                     kHeaderBytes + 4U));

    auto secondary_prefix =
        ValidateTransferControlPrefix(block, kHeaderBytes, kSecondaryBase);
    if (!secondary_prefix)
        return std::unexpected(secondary_prefix.error());
    auto primary_prefix = ValidateTransferControlPrefix(block, kHeaderBytes, kPrimaryBase);
    if (!primary_prefix)
        return std::unexpected(primary_prefix.error());

    auto primary_packet = ParsePsmct32UploadPacket(block, kHeaderBytes, kPrimaryObjectOffset,
                                                   expected_texture_width / 2U, upload_width,
                                                   upload_height, primary_payload_bytes);
    if (!primary_packet)
        return std::unexpected(primary_packet.error());
    auto palette_packet =
        ParsePsmct32UploadPacket(block, kHeaderBytes, kPaletteObjectOffset, 1U, palette_width,
                                 palette_height, palette_payload_bytes);
    if (!palette_packet)
        return std::unexpected(palette_packet.error());
    if (primary_packet->data_reference != 0U || palette_packet->data_reference != 0U)
        return std::unexpected(Error(asset::DecodeErrorCode::InvalidReference,
                                     "frontend TDX relocatable data reference is unsupported",
                                     kHeaderBytes + 0x54U));

    const std::uint64_t palette_data_end = kPaletteDataOffset + palette_payload_bytes;
    if (palette_data_end > kPrimaryDataOffset ||
        !IsZero(block.subspan(static_cast<std::size_t>(palette_data_end),
                              static_cast<std::size_t>(kPrimaryDataOffset - palette_data_end))))
        return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
                                     "frontend TDX palette slot padding is nonzero",
                                     kHeaderBytes + palette_data_end));

    std::uint64_t pixel_count = 0;
    if (!Multiply(descriptor->width, descriptor->height, pixel_count))
        return std::unexpected(
            Error(asset::DecodeErrorCode::Overflow, "frontend TDX pixel count overflows", 4));
    const std::uint64_t palette_count = indexed8 ? 256U : 16U;
    const std::uint64_t primary_word_count = primary_payload_bytes / 4U;
    const std::uint64_t palette_word_count = palette_payload_bytes / 4U;
    std::uint64_t decoded_items = 1U;
    for (const std::uint64_t amount :
         {primary_word_count, palette_word_count, pixel_count, palette_count})
    {
        if (!Add(decoded_items, amount, decoded_items))
            return std::unexpected(
                Error(asset::DecodeErrorCode::Overflow, "frontend TDX operation budget overflows"));
    }
    std::uint64_t logical_output_bytes = sizeof(DecodedFrontEndTdx);
    if (!Add(logical_output_bytes, pixel_count, logical_output_bytes) ||
        !Add(logical_output_bytes, palette_count * sizeof(asset::RawGsRgba8), logical_output_bytes))
        return std::unexpected(
            Error(asset::DecodeErrorCode::Overflow, "frontend TDX logical output size overflows"));
    if (decoded_items > limits.maximum_items)
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
                                     "frontend TDX operations exceed the decoder limit"));
    if (logical_output_bytes > limits.maximum_output_bytes)
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
                                     "frontend TDX owned output exceeds the decoder limit"));
    if (kGsLocalMemoryBytes > limits.maximum_scratch_bytes)
        return std::unexpected(
            Error(asset::DecodeErrorCode::LimitExceeded,
                  "frontend TDX GS local-memory scratch exceeds the decoder limit"));

    FrontEndTdxGsUploadPlan plan{
        .format_version = descriptor->format_version,
        .header_flags = descriptor->observed_flags,
        .sampling_format =
            indexed8 ? TdxGsPixelStorageFormat::Psmt8 : TdxGsPixelStorageFormat::Psmt4,
        .texture_base_pointer = primary_packet->rectangle.destination_base_pointer,
        .texture_buffer_width = expected_texture_width,
        .palette_storage_format = TdxGsPixelStorageFormat::Psmct32,
        .palette_storage_mode = 0,
        .palette_start = 0,
        .texture_alpha_enabled = true,
        .palette_load_enabled = ReadU16(bytes, 0x10) != 0,
        .primary_upload = primary_packet->rectangle,
        .palette_upload = palette_packet->rectangle,
    };
    asset::IndexedImageIR image{
        .width = descriptor->width,
        .height = descriptor->height,
        .source_encoding = indexed8 ? asset::IndexedImageEncoding::Indexed8
                                    : asset::IndexedImageEncoding::Indexed4,
        .indices = std::vector<std::uint8_t>(static_cast<std::size_t>(pixel_count)),
        .palette = std::vector<asset::RawGsRgba8>(static_cast<std::size_t>(palette_count)),
    };
    std::vector<std::byte> gs_memory(static_cast<std::size_t>(kGsLocalMemoryBytes), std::byte{0});

    const auto primary_source =
        block.subspan(kPrimaryDataOffset, static_cast<std::size_t>(primary_payload_bytes));
    auto replay_result = ReplayPsmct32Upload(primary_source, plan.primary_upload, gs_memory);
    if (!replay_result)
        return std::unexpected(replay_result.error());
    auto index_result = ExpandIndices(plan, image, gs_memory);
    if (!index_result)
        return std::unexpected(index_result.error());

    const auto palette_source =
        block.subspan(kPaletteDataOffset, static_cast<std::size_t>(palette_payload_bytes));
    replay_result = ReplayPsmct32Upload(palette_source, plan.palette_upload, gs_memory);
    if (!replay_result)
        return std::unexpected(replay_result.error());
    auto palette_result = ExpandPalette(plan, image, gs_memory);
    if (!palette_result)
        return std::unexpected(palette_result.error());

    return DecodedFrontEndTdx{
        .upload_plan = std::move(plan),
        .image = std::move(image),
        .decoded_items = decoded_items,
        .logical_output_bytes = logical_output_bytes,
        .peak_scratch_bytes = kGsLocalMemoryBytes,
    };
}
} // namespace omega::retail
