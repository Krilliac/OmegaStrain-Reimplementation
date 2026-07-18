#include "omega/retail/tdx_texture_storage_decoder.h"

#include "omega/retail/container_descriptors.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <utility>

namespace omega::retail
{
namespace
{
constexpr std::uint64_t kHeaderBytes = 64;
constexpr std::uint64_t kBlockHeaderBytes = 32;
constexpr std::uint64_t kObjectBytes = 0x60;
constexpr std::uint64_t kPaletteSlotBytes = 0x400;
constexpr std::size_t kMaximumPrimaryPlanes = 4;
constexpr std::size_t kMaximumObjects = kMaximumPrimaryPlanes + 1;

struct HeaderLayout
{
    asset::TextureSampleEncoding sample_encoding = asset::TextureSampleEncoding::Indexed8;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint16_t bits_per_pixel = 0;
    std::uint16_t block_count = 0;
    std::uint16_t primary_count = 0;
    std::uint16_t secondary_count = 0;
    std::uint32_t block_stride = 0;
    std::uint32_t primary_descriptor_bytes = 0;
    std::uint32_t secondary_descriptor_bytes = 0;
    bool indexed = false;
    bool implicit_zero_suffix = false;
    std::uint64_t counted_end = kHeaderBytes;
};

struct PlaneLayout
{
    std::uint64_t object_offset = 0;
    std::uint64_t data_offset = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint16_t transfer_code = 0;
    asset::TextureTransferElementEncoding encoding =
        asset::TextureTransferElementEncoding::Packed8;
    std::uint64_t byte_count = 0;
};

struct PaletteLayout
{
    std::uint64_t object_offset = 0;
    std::uint64_t data_offset = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t entry_count = 0;
    std::uint64_t byte_count = 0;
};

struct BlockLayout
{
    std::array<PlaneLayout, kMaximumPrimaryPlanes> planes{};
    std::size_t plane_count = 0;
    std::optional<PaletteLayout> palette;
    bool uses_implicit_zero_suffix = false;
};

struct DecodeUsage
{
    std::uint64_t decoded_items = 0;
    std::uint64_t logical_output_bytes = 0;
};

[[nodiscard]] asset::DecodeError Error(const asset::DecodeErrorCode code, std::string message,
    const std::optional<std::uint64_t> byte_offset = std::nullopt)
{
    return asset::DecodeError{
        .code = code,
        .byte_offset = byte_offset,
        .message = std::move(message),
    };
}

[[nodiscard]] std::uint16_t ReadU16(
    const std::span<const std::byte> bytes, const std::size_t offset) noexcept
{
    return std::to_integer<std::uint16_t>(bytes[offset]) |
           (std::to_integer<std::uint16_t>(bytes[offset + 1]) << 8U);
}

[[nodiscard]] std::uint32_t ReadU32(
    const std::span<const std::byte> bytes, const std::size_t offset) noexcept
{
    return std::to_integer<std::uint32_t>(bytes[offset]) |
           (std::to_integer<std::uint32_t>(bytes[offset + 1]) << 8U) |
           (std::to_integer<std::uint32_t>(bytes[offset + 2]) << 16U) |
           (std::to_integer<std::uint32_t>(bytes[offset + 3]) << 24U);
}

[[nodiscard]] bool Add(
    const std::uint64_t left, const std::uint64_t right, std::uint64_t& result) noexcept
{
    if (right > std::numeric_limits<std::uint64_t>::max() - left)
        return false;
    result = left + right;
    return true;
}

[[nodiscard]] bool Multiply(
    const std::uint64_t left, const std::uint64_t right, std::uint64_t& result) noexcept
{
    if (left != 0 && right > std::numeric_limits<std::uint64_t>::max() / left)
        return false;
    result = left * right;
    return true;
}

[[nodiscard]] bool ContainsRange(const std::span<const std::byte> bytes,
    const std::uint64_t offset, const std::uint64_t size) noexcept
{
    std::uint64_t end = 0;
    return Add(offset, size, end) && end <= bytes.size();
}

[[nodiscard]] bool RangesOverlap(const std::uint64_t left_offset, const std::uint64_t left_size,
    const std::uint64_t right_offset, const std::uint64_t right_size) noexcept
{
    std::uint64_t left_end = 0;
    std::uint64_t right_end = 0;
    if (!Add(left_offset, left_size, left_end) || !Add(right_offset, right_size, right_end))
        return true;
    return left_offset < right_end && right_offset < left_end;
}

[[nodiscard]] bool IsZero(const std::span<const std::byte> bytes) noexcept
{
    return std::ranges::all_of(
        bytes, [](const std::byte value) { return value == std::byte{0}; });
}

[[nodiscard]] std::uint64_t AbsoluteOffset(
    const std::uint64_t block_offset, const std::uint64_t local_offset) noexcept
{
    return block_offset + local_offset;
}

[[nodiscard]] asset::DecodeResult<std::uint64_t> RectangleBytes(
    const std::uint16_t transfer_code, const std::uint32_t width,
    const std::uint32_t height, const std::uint64_t byte_offset)
{
    if (width == 0 || height == 0)
        return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
            "TDX storage rectangle dimensions must be nonzero", byte_offset));
    std::uint64_t elements = 0;
    if (!Multiply(width, height, elements))
        return std::unexpected(Error(asset::DecodeErrorCode::Overflow,
            "TDX storage rectangle area overflows", byte_offset));
    std::uint64_t bytes = 0;
    switch (transfer_code)
    {
    case 0x00:
        if (!Multiply(elements, 4, bytes))
            return std::unexpected(Error(asset::DecodeErrorCode::Overflow,
                "TDX packed-32 rectangle size overflows", byte_offset));
        break;
    case 0x01:
        if (!Multiply(elements, 3, bytes))
            return std::unexpected(Error(asset::DecodeErrorCode::Overflow,
                "TDX packed-24 rectangle size overflows", byte_offset));
        break;
    case 0x13:
        bytes = elements;
        break;
    case 0x14:
        if (!Add(elements, 1, bytes))
            return std::unexpected(Error(asset::DecodeErrorCode::Overflow,
                "TDX packed-4 rectangle size overflows", byte_offset));
        bytes /= 2;
        break;
    default:
        return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
            "TDX storage rectangle transfer encoding is not supported", byte_offset));
    }
    return bytes;
}

[[nodiscard]] asset::TextureTransferElementEncoding TransferEncoding(
    const std::uint16_t transfer_code) noexcept
{
    switch (transfer_code)
    {
    case 0x00:
        return asset::TextureTransferElementEncoding::Packed32;
    case 0x01:
        return asset::TextureTransferElementEncoding::Packed24;
    case 0x14:
        return asset::TextureTransferElementEncoding::Packed4;
    default:
        return asset::TextureTransferElementEncoding::Packed8;
    }
}

[[nodiscard]] bool IsAllowedPrimaryTransfer(
    const std::uint16_t bits_per_pixel, const std::uint16_t transfer_code) noexcept
{
    switch (bits_per_pixel)
    {
    case 4:
        return transfer_code == 0x00 || transfer_code == 0x14;
    case 8:
        return transfer_code == 0x00 || transfer_code == 0x13;
    case 24:
        return transfer_code == 0x01;
    case 32:
        return transfer_code == 0x00;
    default:
        return false;
    }
}

[[nodiscard]] bool IsImplicitZeroFamily(const HeaderLayout& header,
    const std::uint64_t missing, const std::uint64_t primary_base,
    const std::uint64_t secondary_base, const PlaneLayout& plane,
    const std::optional<PaletteLayout>& palette) noexcept
{
    struct Family
    {
        std::uint16_t bits_per_pixel;
        std::uint32_t sample_width;
        std::uint32_t sample_height;
        std::uint64_t missing;
        std::uint32_t block_stride;
        std::uint32_t primary_base;
        std::uint32_t secondary_base;
        std::uint32_t primary_object_offset;
        std::uint32_t primary_data_offset;
        std::uint16_t transfer_code;
        std::uint32_t transfer_width;
        std::uint32_t transfer_height;
        std::uint32_t palette_object_offset;
        std::uint32_t palette_data_offset;
    };
    constexpr std::array<Family, 9> families{{
        {4, 64, 64, 32, 3360, 160, 32, 192, 1312, 0x00, 32, 16, 64, 288},
        {4, 64, 64, 64, 3360, 160, 32, 192, 1312, 0x00, 32, 16, 64, 288},
        {4, 32, 32, 32, 1824, 160, 32, 192, 1312, 0x00, 16, 8, 64, 288},
        {4, 128, 128, 32, 9504, 160, 32, 192, 1312, 0x00, 64, 32, 64, 288},
        {4, 128, 128, 64, 9504, 160, 32, 192, 1312, 0x00, 64, 32, 64, 288},
        {4, 32, 16, 16, 1568, 160, 32, 192, 1312, 0x14, 32, 16, 64, 288},
        {4, 128, 64, 32, 5408, 160, 32, 192, 1312, 0x14, 128, 64, 64, 288},
        {8, 16, 16, 256, 1568, 160, 32, 192, 1312, 0x00, 8, 8, 64, 288},
        {32, 16, 16, 16, 1184, 32, 32, 64, 160, 0x00, 16, 16, 0, 0},
    }};
    const std::uint64_t palette_object_offset = palette ? palette->object_offset : 0;
    const std::uint64_t palette_data_offset = palette ? palette->data_offset : 0;
    return std::ranges::any_of(families, [&](const Family& family) {
        return family.bits_per_pixel == header.bits_per_pixel &&
               family.sample_width == header.width && family.sample_height == header.height &&
               family.missing == missing && family.block_stride == header.block_stride &&
               family.primary_base == primary_base && family.secondary_base == secondary_base &&
               family.primary_object_offset == plane.object_offset &&
               family.primary_data_offset == plane.data_offset &&
               family.transfer_code == plane.transfer_code &&
               family.transfer_width == plane.width && family.transfer_height == plane.height &&
               family.palette_object_offset == palette_object_offset &&
               family.palette_data_offset == palette_data_offset;
    });
}

[[nodiscard]] asset::DecodeResult<HeaderLayout> ParseHeader(
    const std::span<const std::byte> bytes, const asset::DecodeLimits limits)
{
    auto descriptor = InspectTdxContainer(bytes, limits);
    if (!descriptor)
        return std::unexpected(descriptor.error());

    HeaderLayout layout{
        .width = descriptor->width,
        .height = descriptor->height,
        .bits_per_pixel = descriptor->bits_per_pixel,
        .block_count = ReadU16(bytes, 0x22),
        .primary_count = ReadU16(bytes, 0x24),
        .secondary_count = ReadU16(bytes, 0x26),
        .block_stride = ReadU32(bytes, 0x38),
        .primary_descriptor_bytes = ReadU16(bytes, 0x34),
        .secondary_descriptor_bytes = ReadU16(bytes, 0x36),
    };
    switch (layout.bits_per_pixel)
    {
    case 4:
        layout.sample_encoding = asset::TextureSampleEncoding::Indexed4;
        layout.indexed = true;
        break;
    case 8:
        layout.sample_encoding = asset::TextureSampleEncoding::Indexed8;
        layout.indexed = true;
        break;
    case 24:
        layout.sample_encoding = asset::TextureSampleEncoding::Packed24;
        break;
    case 32:
        layout.sample_encoding = asset::TextureSampleEncoding::Packed32;
        break;
    default:
        return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
            "TDX sample encoding is not supported", 8));
    }

    if (layout.block_count == 0 || layout.primary_count == 0 ||
        layout.primary_count > kMaximumPrimaryPlanes || layout.block_stride == 0)
        return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
            "TDX block or primary-plane count is zero or unsupported", 0x22));
    const std::uint16_t expected_secondary_count =
        static_cast<std::uint16_t>(layout.indexed ? 1U : 0U);
    if (layout.secondary_count != expected_secondary_count)
        return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
            "TDX palette count contradicts its sample encoding", 0x26));

    std::uint64_t blocks_bytes = 0;
    if (!Multiply(layout.block_count, layout.block_stride, blocks_bytes) ||
        !Add(kHeaderBytes, blocks_bytes, layout.counted_end))
        return std::unexpected(Error(
            asset::DecodeErrorCode::Overflow, "TDX counted block extent overflows", 0x38));

    if (layout.counted_end <= bytes.size())
    {
        const auto tail = bytes.subspan(static_cast<std::size_t>(layout.counted_end));
        if (!IsZero(tail))
            return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
                "TDX counted block region has an unobserved nonzero tail", layout.counted_end));
        return layout;
    }

    const std::uint64_t missing = layout.counted_end - bytes.size();
    if (layout.block_count != 1 || layout.primary_count != 1 || missing == 0 ||
        missing > 256U || missing % 16U != 0)
        return std::unexpected(Error(asset::DecodeErrorCode::Truncated,
            "TDX counted block region extends past the input", bytes.size()));
    layout.implicit_zero_suffix = true;
    return layout;
}

[[nodiscard]] asset::DecodeResult<void> AddObject(const std::uint64_t object_offset,
    const std::uint64_t block_offset, std::array<std::uint64_t, kMaximumObjects>& objects,
    std::size_t& object_count)
{
    if (object_offset < kBlockHeaderBytes)
        return std::unexpected(Error(asset::DecodeErrorCode::InvalidReference,
            "TDX object overlaps the block pointer header",
            AbsoluteOffset(block_offset, object_offset)));
    for (std::size_t index = 0; index < object_count; ++index)
    {
        if (objects[index] == object_offset)
            return std::unexpected(Error(asset::DecodeErrorCode::DuplicateReference,
                "TDX object table contains a duplicate reference",
                AbsoluteOffset(block_offset, object_offset)));
        if (RangesOverlap(objects[index], kObjectBytes, object_offset, kObjectBytes))
            return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
                "TDX object records overlap", AbsoluteOffset(block_offset, object_offset)));
    }
    objects[object_count++] = object_offset;
    return {};
}

[[nodiscard]] asset::DecodeResult<BlockLayout> ParseBlock(
    const std::span<const std::byte> block, const std::uint64_t block_offset,
    const HeaderLayout& header)
{
    if (block.size() < kBlockHeaderBytes)
        return std::unexpected(Error(asset::DecodeErrorCode::Truncated,
            "TDX block is shorter than its pointer header", block_offset + block.size()));

    const std::uint64_t primary_base = ReadU32(block, 0x18);
    const std::uint64_t secondary_base = ReadU32(block, 0x1C);
    std::array<std::uint64_t, kMaximumObjects> objects{};
    std::size_t object_count = 0;
    BlockLayout layout{.plane_count = header.primary_count};

    std::uint64_t primary_data_base = 0;
    std::uint64_t indexed_extra = header.indexed ? kPaletteSlotBytes : 0;
    if (!Add(primary_base, header.primary_descriptor_bytes, primary_data_base) ||
        !Add(primary_data_base, indexed_extra, primary_data_base))
        return std::unexpected(Error(asset::DecodeErrorCode::Overflow,
            "TDX primary data base overflows", block_offset + 0x18));

    for (std::size_t index = 0; index < layout.plane_count; ++index)
    {
        const std::uint64_t pointer_offset = index * sizeof(std::uint32_t);
        if (!ContainsRange(block, pointer_offset, sizeof(std::uint32_t)))
            return std::unexpected(Error(asset::DecodeErrorCode::Truncated,
                "TDX primary object table is truncated",
                AbsoluteOffset(block_offset, pointer_offset)));
        std::uint64_t object_offset = 0;
        if (!Add(primary_base, ReadU32(block, static_cast<std::size_t>(pointer_offset)),
                object_offset))
            return std::unexpected(Error(asset::DecodeErrorCode::Overflow,
                "TDX primary object reference overflows",
                AbsoluteOffset(block_offset, pointer_offset)));
        if (!ContainsRange(block, object_offset, kObjectBytes))
            return std::unexpected(Error(asset::DecodeErrorCode::InvalidReference,
                "TDX primary object reference is outside the available block",
                AbsoluteOffset(block_offset, pointer_offset)));
        auto object_result = AddObject(object_offset, block_offset, objects, object_count);
        if (!object_result)
            return std::unexpected(object_result.error());
        if (index != 0 && object_offset <= layout.planes[index - 1].object_offset)
            return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
                "TDX primary object order is outside the observed family",
                AbsoluteOffset(block_offset, object_offset)));

        const std::uint16_t transfer_code = static_cast<std::uint16_t>(
            (ReadU32(block, static_cast<std::size_t>(object_offset + 0x04)) >> 24U) & 0x3FU);
        if (!IsAllowedPrimaryTransfer(header.bits_per_pixel, transfer_code))
            return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
                "TDX primary transfer encoding contradicts the sample encoding",
                AbsoluteOffset(block_offset, object_offset + 0x04)));
        const std::uint32_t width =
            ReadU32(block, static_cast<std::size_t>(object_offset + 0x20));
        const std::uint32_t height =
            ReadU32(block, static_cast<std::size_t>(object_offset + 0x24));
        auto byte_count = RectangleBytes(transfer_code, width, height,
            AbsoluteOffset(block_offset, object_offset + 0x20));
        if (!byte_count)
            return std::unexpected(byte_count.error());
        std::uint64_t data_offset = 0;
        if (!Add(primary_data_base,
                ReadU32(block, static_cast<std::size_t>(object_offset + 0x54)), data_offset))
            return std::unexpected(Error(asset::DecodeErrorCode::Overflow,
                "TDX primary data reference overflows",
                AbsoluteOffset(block_offset, object_offset + 0x54)));
        if (data_offset > header.block_stride || data_offset > block.size())
            return std::unexpected(Error(asset::DecodeErrorCode::InvalidReference,
                "TDX primary data reference is outside the available block",
                AbsoluteOffset(block_offset, object_offset + 0x54)));

        layout.planes[index] = PlaneLayout{
            .object_offset = object_offset,
            .data_offset = data_offset,
            .width = width,
            .height = height,
            .transfer_code = transfer_code,
            .encoding = TransferEncoding(transfer_code),
            .byte_count = *byte_count,
        };
    }

    if (header.secondary_count != 0)
    {
        constexpr std::uint64_t pointer_offset = 0x14;
        if (!ContainsRange(block, pointer_offset, sizeof(std::uint32_t)))
            return std::unexpected(Error(asset::DecodeErrorCode::Truncated,
                "TDX palette object table is truncated", block_offset + pointer_offset));
        std::uint64_t object_offset = 0;
        if (!Add(secondary_base, ReadU32(block, pointer_offset), object_offset))
            return std::unexpected(Error(asset::DecodeErrorCode::Overflow,
                "TDX palette object reference overflows", block_offset + pointer_offset));
        if (!ContainsRange(block, object_offset, kObjectBytes))
            return std::unexpected(Error(asset::DecodeErrorCode::InvalidReference,
                "TDX palette object reference is outside the available block",
                block_offset + pointer_offset));
        auto object_result = AddObject(object_offset, block_offset, objects, object_count);
        if (!object_result)
            return std::unexpected(object_result.error());

        const std::uint16_t transfer_code = static_cast<std::uint16_t>(
            (ReadU32(block, static_cast<std::size_t>(object_offset + 0x04)) >> 24U) & 0x3FU);
        if (transfer_code != 0)
            return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
                "TDX palette does not use the observed four-byte entry storage",
                AbsoluteOffset(block_offset, object_offset + 0x04)));
        const std::uint32_t width =
            ReadU32(block, static_cast<std::size_t>(object_offset + 0x20));
        const std::uint32_t height =
            ReadU32(block, static_cast<std::size_t>(object_offset + 0x24));
        const std::uint32_t expected_width = header.bits_per_pixel == 4 ? 8U : 16U;
        const std::uint32_t expected_height = header.bits_per_pixel == 4 ? 2U : 16U;
        if (width != expected_width || height != expected_height)
            return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
                "TDX palette rectangle is outside the observed family",
                AbsoluteOffset(block_offset, object_offset + 0x20)));
        auto byte_count = RectangleBytes(transfer_code, width, height,
            AbsoluteOffset(block_offset, object_offset + 0x20));
        if (!byte_count)
            return std::unexpected(byte_count.error());
        const std::uint32_t entry_count = header.bits_per_pixel == 4 ? 16U : 256U;
        if (*byte_count != static_cast<std::uint64_t>(entry_count) * 4U)
            return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
                "TDX palette entry count contradicts its rectangle",
                AbsoluteOffset(block_offset, object_offset + 0x20)));

        std::uint64_t secondary_data_base = 0;
        std::uint64_t data_offset = 0;
        if (!Add(secondary_base, header.primary_descriptor_bytes, secondary_data_base) ||
            !Add(secondary_data_base, header.secondary_descriptor_bytes, secondary_data_base) ||
            !Add(secondary_data_base,
                ReadU32(block, static_cast<std::size_t>(object_offset + 0x54)), data_offset))
            return std::unexpected(Error(asset::DecodeErrorCode::Overflow,
                "TDX palette data reference overflows",
                AbsoluteOffset(block_offset, object_offset + 0x54)));
        if (!ContainsRange(block, data_offset, *byte_count))
            return std::unexpected(Error(asset::DecodeErrorCode::InvalidReference,
                "TDX palette data reference is outside the available block",
                AbsoluteOffset(block_offset, object_offset + 0x54)));
        layout.palette = PaletteLayout{
            .object_offset = object_offset,
            .data_offset = data_offset,
            .width = width,
            .height = height,
            .entry_count = entry_count,
            .byte_count = *byte_count,
        };
    }

    for (std::size_t index = 1; index < layout.plane_count; ++index)
    {
        const PlaneLayout& previous = layout.planes[index - 1];
        const PlaneLayout& plane = layout.planes[index];
        if (plane.data_offset == previous.data_offset)
            return std::unexpected(Error(asset::DecodeErrorCode::DuplicateReference,
                "TDX primary planes share a data reference",
                AbsoluteOffset(block_offset, plane.data_offset)));
        if (plane.data_offset < previous.data_offset)
            return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
                "TDX primary data order is outside the observed family",
                AbsoluteOffset(block_offset, plane.data_offset)));
    }

    for (std::size_t index = 0; index < layout.plane_count; ++index)
    {
        const PlaneLayout& plane = layout.planes[index];
        std::uint64_t plane_end = 0;
        if (!Add(plane.data_offset, plane.byte_count, plane_end))
            return std::unexpected(Error(asset::DecodeErrorCode::Overflow,
                "TDX primary plane extent overflows",
                AbsoluteOffset(block_offset, plane.data_offset)));
        const std::uint64_t expected_end = index + 1 < layout.plane_count
            ? layout.planes[index + 1].data_offset
            : header.block_stride;
        if (plane_end != expected_end)
            return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
                plane_end > expected_end ? "TDX primary storage planes overlap"
                                         : "TDX primary storage planes contain an unobserved gap",
                AbsoluteOffset(block_offset, plane.data_offset)));
        if (plane_end > block.size())
        {
            if (!header.implicit_zero_suffix || layout.plane_count != 1 || index != 0 ||
                plane_end != header.block_stride || plane.data_offset > block.size() ||
                !IsImplicitZeroFamily(header,
                    header.counted_end - (block_offset + block.size()), primary_base,
                    secondary_base, plane, layout.palette))
                return std::unexpected(Error(asset::DecodeErrorCode::Truncated,
                    "TDX primary storage plane extends past the input",
                    block_offset + block.size()));
            layout.uses_implicit_zero_suffix = true;
        }
    }

    if (header.indexed)
    {
        if (!layout.palette)
            return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
                "TDX indexed block has no palette", block_offset));
        std::uint64_t palette_slot_end = 0;
        if (!Add(layout.palette->data_offset, kPaletteSlotBytes, palette_slot_end) ||
            palette_slot_end != layout.planes[0].data_offset)
            return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
                "TDX palette does not occupy the observed storage slot",
                AbsoluteOffset(block_offset, layout.palette->data_offset)));
        const std::uint64_t palette_data_end =
            layout.palette->data_offset + layout.palette->byte_count;
        const auto gap = block.subspan(static_cast<std::size_t>(palette_data_end),
            static_cast<std::size_t>(palette_slot_end - palette_data_end));
        if (!IsZero(gap))
            return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
                "TDX palette slot padding is nonzero",
                AbsoluteOffset(block_offset, palette_data_end)));
    }
    else if (layout.palette)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
            "TDX direct-storage block unexpectedly contains a palette", block_offset));
    }

    for (std::size_t object_index = 0; object_index < object_count; ++object_index)
    {
        for (std::size_t plane_index = 0; plane_index < layout.plane_count; ++plane_index)
        {
            const PlaneLayout& plane = layout.planes[plane_index];
            if (RangesOverlap(
                    objects[object_index], kObjectBytes, plane.data_offset, plane.byte_count))
                return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
                    "TDX primary data overlaps an object record",
                    AbsoluteOffset(block_offset, plane.data_offset)));
        }
        if (layout.palette &&
            RangesOverlap(objects[object_index], kObjectBytes, layout.palette->data_offset,
                kPaletteSlotBytes))
            return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
                "TDX palette storage overlaps an object record",
                AbsoluteOffset(block_offset, layout.palette->data_offset)));
    }

    if (header.implicit_zero_suffix != layout.uses_implicit_zero_suffix)
        return std::unexpected(Error(asset::DecodeErrorCode::Truncated,
            "TDX implicit-zero family does not end within the final primary plane",
            block_offset + block.size()));
    return layout;
}

[[nodiscard]] asset::DecodeResult<void> Accumulate(std::uint64_t& total,
    const std::uint64_t amount, const std::uint64_t limit, const char* overflow_message,
    const char* limit_message)
{
    std::uint64_t next = 0;
    if (!Add(total, amount, next))
        return std::unexpected(Error(asset::DecodeErrorCode::Overflow, overflow_message));
    if (next > limit)
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded, limit_message));
    total = next;
    return {};
}

[[nodiscard]] asset::DecodeResult<DecodeUsage> CheckBudgets(
    const std::span<const std::byte> bytes, const HeaderLayout& header,
    const asset::DecodeLimits limits)
{
    std::uint64_t items = 1;
    std::uint64_t output_bytes = sizeof(asset::TextureStorageIR);
    if (items > limits.maximum_items || output_bytes > limits.maximum_output_bytes)
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
            "TDX decoded texture root exceeds decoder limits"));

    for (std::uint16_t block_index = 0; block_index < header.block_count; ++block_index)
    {
        const std::uint64_t block_offset =
            kHeaderBytes + static_cast<std::uint64_t>(block_index) * header.block_stride;
        const std::uint64_t available = std::min<std::uint64_t>(
            header.block_stride, static_cast<std::uint64_t>(bytes.size()) - block_offset);
        const auto block = bytes.subspan(
            static_cast<std::size_t>(block_offset), static_cast<std::size_t>(available));
        auto layout = ParseBlock(block, block_offset, header);
        if (!layout)
            return std::unexpected(layout.error());

        auto item_result = Accumulate(items, 1, limits.maximum_items,
            "TDX decoded item count overflows", "TDX decoded items exceed decoder limit");
        if (!item_result)
            return std::unexpected(item_result.error());
        auto output_result = Accumulate(output_bytes, sizeof(asset::TextureStorageBlockIR),
            limits.maximum_output_bytes, "TDX decoded output size overflows",
            "TDX decoded texture exceeds decoder output limit");
        if (!output_result)
            return std::unexpected(output_result.error());

        for (std::size_t plane_index = 0; plane_index < layout->plane_count; ++plane_index)
        {
            item_result = Accumulate(items, 1, limits.maximum_items,
                "TDX decoded item count overflows", "TDX decoded items exceed decoder limit");
            if (!item_result)
                return std::unexpected(item_result.error());
            output_result = Accumulate(output_bytes, sizeof(asset::TextureStoragePlaneIR),
                limits.maximum_output_bytes, "TDX decoded output size overflows",
                "TDX decoded texture exceeds decoder output limit");
            if (!output_result)
                return std::unexpected(output_result.error());
            output_result = Accumulate(output_bytes, layout->planes[plane_index].byte_count,
                limits.maximum_output_bytes, "TDX decoded output size overflows",
                "TDX decoded texture exceeds decoder output limit");
            if (!output_result)
                return std::unexpected(output_result.error());
        }
        if (layout->palette)
        {
            item_result = Accumulate(items, 1, limits.maximum_items,
                "TDX decoded item count overflows", "TDX decoded items exceed decoder limit");
            if (!item_result)
                return std::unexpected(item_result.error());
            item_result = Accumulate(items, layout->palette->entry_count, limits.maximum_items,
                "TDX decoded item count overflows", "TDX decoded items exceed decoder limit");
            if (!item_result)
                return std::unexpected(item_result.error());
            std::uint64_t palette_bytes = 0;
            if (!Multiply(layout->palette->entry_count,
                    sizeof(std::array<std::byte, 4>), palette_bytes))
                return std::unexpected(Error(
                    asset::DecodeErrorCode::Overflow, "TDX decoded palette size overflows"));
            output_result = Accumulate(output_bytes, palette_bytes,
                limits.maximum_output_bytes, "TDX decoded output size overflows",
                "TDX decoded texture exceeds decoder output limit");
            if (!output_result)
                return std::unexpected(output_result.error());
        }
    }
    return DecodeUsage{
        .decoded_items = items,
        .logical_output_bytes = output_bytes,
    };
}
} // namespace

asset::DecodeResult<DecodedTdxTextureStorage> DecodeTdxTextureStorageMeasured(
    const std::span<const std::byte> bytes, const asset::DecodeLimits limits)
{
    auto header = ParseHeader(bytes, limits);
    if (!header)
        return std::unexpected(header.error());
    auto budget_result = CheckBudgets(bytes, *header, limits);
    if (!budget_result)
        return std::unexpected(budget_result.error());

    DecodedTdxTextureStorage decoded{
        .storage = asset::TextureStorageIR{
            .width = header->width,
            .height = header->height,
            .sample_encoding = header->sample_encoding,
        },
        .decoded_items = budget_result->decoded_items,
        .logical_output_bytes = budget_result->logical_output_bytes,
    };
    decoded.storage.blocks.reserve(header->block_count);
    for (std::uint16_t block_index = 0; block_index < header->block_count; ++block_index)
    {
        const std::uint64_t block_offset =
            kHeaderBytes + static_cast<std::uint64_t>(block_index) * header->block_stride;
        const std::uint64_t available = std::min<std::uint64_t>(
            header->block_stride, static_cast<std::uint64_t>(bytes.size()) - block_offset);
        const auto block = bytes.subspan(
            static_cast<std::size_t>(block_offset), static_cast<std::size_t>(available));
        auto layout = ParseBlock(block, block_offset, *header);
        if (!layout)
            return std::unexpected(layout.error());

        asset::TextureStorageBlockIR output_block;
        output_block.planes.reserve(layout->plane_count);
        for (std::size_t plane_index = 0; plane_index < layout->plane_count; ++plane_index)
        {
            const PlaneLayout& plane = layout->planes[plane_index];
            if (plane.byte_count > std::numeric_limits<std::size_t>::max())
                return std::unexpected(Error(
                    asset::DecodeErrorCode::Overflow, "TDX owned primary plane size overflows"));
            asset::TextureStoragePlaneIR output_plane{
                .width = plane.width,
                .height = plane.height,
                .element_encoding = plane.encoding,
                .bytes = std::vector<std::byte>(
                    static_cast<std::size_t>(plane.byte_count), std::byte{0}),
            };
            const std::uint64_t physical_bytes = plane.data_offset < block.size()
                ? std::min<std::uint64_t>(plane.byte_count, block.size() - plane.data_offset)
                : 0;
            for (std::uint64_t index = 0; index < physical_bytes; ++index)
                output_plane.bytes[static_cast<std::size_t>(index)] =
                    block[static_cast<std::size_t>(plane.data_offset + index)];
            output_block.planes.push_back(std::move(output_plane));
        }
        if (layout->palette)
        {
            asset::TexturePaletteStorageIR palette{
                .width = layout->palette->width,
                .height = layout->palette->height,
            };
            palette.entries.resize(layout->palette->entry_count);
            for (std::uint32_t entry = 0; entry < layout->palette->entry_count; ++entry)
            {
                for (std::size_t channel = 0; channel < 4; ++channel)
                    palette.entries[entry][channel] = block[static_cast<std::size_t>(
                        layout->palette->data_offset + static_cast<std::uint64_t>(entry) * 4U +
                        channel)];
            }
            output_block.palette = std::move(palette);
        }
        decoded.storage.blocks.push_back(std::move(output_block));
    }
    return decoded;
}

asset::DecodeResult<asset::TextureStorageIR> DecodeTdxTextureStorage(
    const std::span<const std::byte> bytes, const asset::DecodeLimits limits)
{
    auto decoded = DecodeTdxTextureStorageMeasured(bytes, limits);
    if (!decoded)
        return std::unexpected(decoded.error());
    return std::move(decoded->storage);
}
} // namespace omega::retail
