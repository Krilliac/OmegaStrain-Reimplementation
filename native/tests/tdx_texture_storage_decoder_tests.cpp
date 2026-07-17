#include "omega/retail/tdx_texture_storage_decoder.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

namespace
{
struct PlaneSpec
{
    std::uint16_t transfer_code = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint8_t seed = 0;
};

struct FixtureSpec
{
    std::uint16_t bits_per_pixel = 8;
    std::uint16_t width = 16;
    std::uint16_t height = 16;
    std::vector<std::vector<PlaneSpec>> blocks;
};

void WriteU16(
    std::vector<std::byte>& bytes, const std::size_t offset, const std::uint16_t value)
{
    bytes[offset] = static_cast<std::byte>(value & 0xFFU);
    bytes[offset + 1] = static_cast<std::byte>((value >> 8U) & 0xFFU);
}

void WriteU32(
    std::vector<std::byte>& bytes, const std::size_t offset, const std::uint32_t value)
{
    for (unsigned shift = 0; shift < 32; shift += 8)
        bytes[offset + shift / 8U] = static_cast<std::byte>((value >> shift) & 0xFFU);
}

std::uint32_t ReadU32(const std::vector<std::byte>& bytes, const std::size_t offset)
{
    return std::to_integer<std::uint32_t>(bytes[offset]) |
           (std::to_integer<std::uint32_t>(bytes[offset + 1]) << 8U) |
           (std::to_integer<std::uint32_t>(bytes[offset + 2]) << 16U) |
           (std::to_integer<std::uint32_t>(bytes[offset + 3]) << 24U);
}

std::uint64_t PlaneBytes(const PlaneSpec& plane)
{
    const std::uint64_t elements =
        static_cast<std::uint64_t>(plane.width) * plane.height;
    switch (plane.transfer_code)
    {
    case 0x00:
        return elements * 4U;
    case 0x01:
        return elements * 3U;
    case 0x13:
        return elements;
    default:
        return (elements + 1U) / 2U;
    }
}

std::uint16_t HeaderFormat(const std::uint16_t bits_per_pixel)
{
    switch (bits_per_pixel)
    {
    case 4:
        return 0x14;
    case 8:
        return 0x13;
    case 24:
        return 0x01;
    default:
        return 0x00;
    }
}

std::vector<std::byte> MakeTdx(const FixtureSpec& spec)
{
    const bool indexed = spec.bits_per_pixel == 4 || spec.bits_per_pixel == 8;
    const std::size_t primary_count = spec.blocks.front().size();
    const std::uint32_t descriptor_bytes = static_cast<std::uint32_t>(primary_count * 128U);
    const std::uint64_t primary_base = indexed ? 0xA0U : 0x20U;
    constexpr std::uint64_t secondary_base = 0x20U;
    const std::uint64_t primary_start =
        primary_base + descriptor_bytes + (indexed ? 0x400U : 0U);
    std::uint64_t primary_bytes = 0;
    for (const PlaneSpec& plane : spec.blocks.front())
        primary_bytes += PlaneBytes(plane);
    const std::uint32_t stride = static_cast<std::uint32_t>(primary_start + primary_bytes);

    std::vector<std::byte> bytes(64, std::byte{0});
    WriteU16(bytes, 0x00, 5);
    WriteU16(bytes, 0x02, static_cast<std::uint16_t>(indexed ? 1U : 0U));
    WriteU16(bytes, 0x04, spec.width);
    WriteU16(bytes, 0x06, spec.height);
    WriteU16(bytes, 0x08, spec.bits_per_pixel);
    WriteU16(bytes, 0x0A, HeaderFormat(spec.bits_per_pixel));
    const std::uint16_t minimum_width_units =
        static_cast<std::uint16_t>(spec.bits_per_pixel <= 8 ? 2U : 1U);
    WriteU16(bytes, 0x0C,
        std::max(minimum_width_units, static_cast<std::uint16_t>(spec.width / 64U)));
    const std::uint64_t header_area_bytes = static_cast<std::uint64_t>(spec.width) *
        spec.height * spec.bits_per_pixel / 8U;
    WriteU16(bytes, 0x0E, static_cast<std::uint16_t>(header_area_bytes / 256U));
    WriteU16(bytes, 0x10,
        spec.bits_per_pixel == 4 ? 8U : spec.bits_per_pixel == 8 ? 16U : 0U);
    WriteU16(bytes, 0x12,
        spec.bits_per_pixel == 4 ? 2U : spec.bits_per_pixel == 8 ? 16U : 0U);
    WriteU16(bytes, 0x14, static_cast<std::uint16_t>(indexed ? 32U : 0U));
    WriteU16(bytes, 0x18, static_cast<std::uint16_t>(indexed ? 1U : 0U));
    WriteU16(bytes, 0x1A, static_cast<std::uint16_t>(indexed ? 4U : 0U));
    WriteU16(bytes, 0x22, static_cast<std::uint16_t>(spec.blocks.size()));
    WriteU16(bytes, 0x24, static_cast<std::uint16_t>(primary_count));
    WriteU16(bytes, 0x26, static_cast<std::uint16_t>(indexed ? 1U : 0U));
    WriteU16(bytes, 0x34, static_cast<std::uint16_t>(descriptor_bytes));
    WriteU16(bytes, 0x36, static_cast<std::uint16_t>(indexed ? 128U : 0U));
    WriteU32(bytes, 0x38, stride);

    for (const auto& planes : spec.blocks)
    {
        std::vector<std::byte> block(stride, std::byte{0});
        WriteU32(block, 0x18, static_cast<std::uint32_t>(primary_base));
        WriteU32(block, 0x1C, static_cast<std::uint32_t>(secondary_base));
        std::uint64_t data_cursor = primary_start;
        for (std::size_t index = 0; index < planes.size(); ++index)
        {
            const std::uint32_t relative_object =
                static_cast<std::uint32_t>(0x20U + index * 128U);
            WriteU32(block, index * 4U, relative_object);
            const std::size_t object =
                static_cast<std::size_t>(primary_base + relative_object);
            WriteU32(block, object + 0x04,
                static_cast<std::uint32_t>(planes[index].transfer_code) << 24U);
            WriteU32(block, object + 0x20, planes[index].width);
            WriteU32(block, object + 0x24, planes[index].height);
            WriteU32(block, object + 0x40,
                static_cast<std::uint32_t>(PlaneBytes(planes[index]) / 4U));
            const std::uint64_t primary_data_base =
                primary_base + descriptor_bytes + (indexed ? 0x400U : 0U);
            WriteU32(block, object + 0x54,
                static_cast<std::uint32_t>(data_cursor - primary_data_base));
            const std::uint64_t byte_count = PlaneBytes(planes[index]);
            for (std::uint64_t byte = 0; byte < byte_count; ++byte)
                block[static_cast<std::size_t>(data_cursor + byte)] = static_cast<std::byte>(
                    static_cast<std::uint8_t>(planes[index].seed + byte));
            data_cursor += byte_count;
        }

        if (indexed)
        {
            constexpr std::uint32_t relative_object = 0x20U;
            WriteU32(block, 0x14, relative_object);
            const std::size_t object =
                static_cast<std::size_t>(secondary_base + relative_object);
            WriteU32(block, object + 0x04, 0);
            const std::uint32_t palette_width = spec.bits_per_pixel == 4 ? 8U : 16U;
            const std::uint32_t palette_height = spec.bits_per_pixel == 4 ? 2U : 16U;
            WriteU32(block, object + 0x20, palette_width);
            WriteU32(block, object + 0x24, palette_height);
            WriteU32(block, object + 0x40, palette_width * palette_height);
            WriteU32(block, object + 0x54, 0);
            const std::size_t palette_target =
                static_cast<std::size_t>(secondary_base + descriptor_bytes + 128U);
            const std::uint32_t entries = spec.bits_per_pixel == 4 ? 16U : 256U;
            for (std::uint32_t entry = 0; entry < entries; ++entry)
            {
                block[palette_target + entry * 4U] = static_cast<std::byte>(entry & 0xFFU);
                block[palette_target + entry * 4U + 1U] =
                    static_cast<std::byte>(planes.front().seed);
                block[palette_target + entry * 4U + 2U] =
                    static_cast<std::byte>((entry ^ planes.front().seed) & 0xFFU);
                block[palette_target + entry * 4U + 3U] = static_cast<std::byte>(
                    entry % 3U == 0 ? 0U : entry % 3U == 1 ? 128U : 255U);
            }
        }
        bytes.insert(bytes.end(), block.begin(), block.end());
    }
    return bytes;
}

int failures = 0;

void Check(const bool condition, const std::string_view message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

template <typename Value>
void CheckError(const omega::asset::DecodeResult<Value>& result,
    const omega::asset::DecodeErrorCode code, const std::string_view message)
{
    Check(!result && result.error().code == code, message);
}

FixtureSpec SinglePlane(const std::uint16_t bits_per_pixel, const std::uint16_t width,
    const std::uint16_t height, const std::uint16_t transfer_code, const std::uint8_t seed)
{
    return FixtureSpec{
        .bits_per_pixel = bits_per_pixel,
        .width = width,
        .height = height,
        .blocks = {{PlaneSpec{transfer_code, width, height, seed}}},
    };
}
} // namespace

int TdxTextureStorageDecoderFailureCount()
{
    auto indexed4_bytes = MakeTdx(SinglePlane(4, 16, 16, 0x14, 0x21));
    auto indexed4 = omega::retail::DecodeTdxTextureStorage(indexed4_bytes);
    Check(indexed4 && indexed4->sample_encoding ==
                          omega::asset::TextureSampleEncoding::Indexed4 &&
              indexed4->blocks.size() == 1 && indexed4->blocks[0].planes.size() == 1 &&
              indexed4->blocks[0].planes[0].element_encoding ==
                  omega::asset::TextureTransferElementEncoding::Packed4 &&
              indexed4->blocks[0].planes[0].bytes.size() == 128,
        "TDX indexed4 storage decodes without unpacking texels");
    Check(indexed4 && indexed4->blocks[0].palette &&
              indexed4->blocks[0].palette->entries.size() == 16 &&
              indexed4->blocks[0].palette->entries[1][3] == std::byte{128} &&
              indexed4->blocks[0].palette->entries[2][3] == std::byte{255},
        "TDX indexed4 palette preserves all four source channel bytes");

    auto ownership_bytes = indexed4_bytes;
    auto owned_indexed4 = omega::retail::DecodeTdxTextureStorage(ownership_bytes);
    std::fill(ownership_bytes.begin(), ownership_bytes.end(), std::byte{0});
    Check(owned_indexed4 && owned_indexed4->blocks[0].planes[0].bytes[0] == std::byte{0x21} &&
              owned_indexed4->blocks[0].palette->entries[1][3] == std::byte{128},
        "TDX decoded storage owns plane and palette bytes after source replacement");

    const auto indexed8_bytes = MakeTdx(SinglePlane(8, 16, 16, 0x13, 0x31));
    auto indexed8 = omega::retail::DecodeTdxTextureStorage(indexed8_bytes);
    Check(indexed8 && indexed8->sample_encoding ==
                          omega::asset::TextureSampleEncoding::Indexed8 &&
              indexed8->blocks[0].planes[0].element_encoding ==
                  omega::asset::TextureTransferElementEncoding::Packed8 &&
              indexed8->blocks[0].palette &&
              indexed8->blocks[0].palette->entries.size() == 256,
        "TDX indexed8 storage retains its encoded plane and 256 source palette entries");

    const auto direct24_bytes = MakeTdx(SinglePlane(24, 16, 16, 0x01, 0x41));
    auto direct24 = omega::retail::DecodeTdxTextureStorage(direct24_bytes);
    Check(direct24 && direct24->sample_encoding ==
                          omega::asset::TextureSampleEncoding::Packed24 &&
              direct24->blocks[0].planes[0].element_encoding ==
                  omega::asset::TextureTransferElementEncoding::Packed24 &&
              !direct24->blocks[0].palette &&
              direct24->blocks[0].planes[0].bytes.size() == 16U * 16U * 3U,
        "TDX packed24 storage decodes without inventing a channel interpretation");

    const auto direct32_bytes = MakeTdx(SinglePlane(32, 16, 16, 0x00, 0x51));
    auto direct32 = omega::retail::DecodeTdxTextureStorage(direct32_bytes);
    Check(direct32 && direct32->sample_encoding ==
                          omega::asset::TextureSampleEncoding::Packed32 &&
              direct32->blocks[0].planes[0].element_encoding ==
                  omega::asset::TextureTransferElementEncoding::Packed32 &&
              !direct32->blocks[0].palette &&
              direct32->blocks[0].planes[0].bytes[3] == std::byte{0x54},
        "TDX packed32 source bytes survive without alpha scaling or channel conversion");

    const auto transfer32_bytes = MakeTdx(FixtureSpec{
        .bits_per_pixel = 8,
        .width = 16,
        .height = 16,
        .blocks = {{PlaneSpec{0x00, 8, 8, 0x61}}},
    });
    auto transfer32 = omega::retail::DecodeTdxTextureStorage(transfer32_bytes);
    Check(transfer32 && transfer32->sample_encoding ==
                            omega::asset::TextureSampleEncoding::Indexed8 &&
              transfer32->blocks[0].planes[0].element_encoding ==
                  omega::asset::TextureTransferElementEncoding::Packed32,
        "TDX sample encoding remains distinct from transfer element encoding");

    const FixtureSpec multi_spec{
        .bits_per_pixel = 8,
        .width = 16,
        .height = 16,
        .blocks = {
            {PlaneSpec{0x13, 16, 8, 0x71}, PlaneSpec{0x13, 8, 8, 0x81}},
            {PlaneSpec{0x13, 16, 8, 0x91}, PlaneSpec{0x13, 8, 8, 0xA1}},
        },
    };
    const auto multi_bytes = MakeTdx(multi_spec);
    auto multi = omega::retail::DecodeTdxTextureStorage(multi_bytes);
    Check(multi && multi->blocks.size() == 2 && multi->blocks[0].planes.size() == 2 &&
              multi->blocks[0].planes[0].bytes[0] == std::byte{0x71} &&
              multi->blocks[0].planes[1].bytes[0] == std::byte{0x81} &&
              multi->blocks[1].planes[0].bytes[0] == std::byte{0x91} &&
              multi->blocks[1].planes[1].bytes[0] == std::byte{0xA1},
        "TDX block and plane source order is preserved without assigning frame or mip meaning");

    auto zero_tail_bytes = indexed8_bytes;
    zero_tail_bytes.resize(zero_tail_bytes.size() + 16U, std::byte{0});
    auto zero_tail = omega::retail::DecodeTdxTextureStorage(zero_tail_bytes);
    Check(zero_tail && indexed8 && *zero_tail == *indexed8,
        "TDX all-zero counted-region tail is validated and omitted from canonical storage");

    struct ImplicitCase
    {
        std::uint16_t bits_per_pixel;
        std::uint16_t width;
        std::uint16_t height;
        std::uint64_t missing;
        std::uint16_t transfer_code;
        std::uint32_t transfer_width;
        std::uint32_t transfer_height;
    };
    constexpr std::array<ImplicitCase, 9> implicit_cases{{
        {4, 64, 64, 32, 0x00, 32, 16},
        {4, 64, 64, 64, 0x00, 32, 16},
        {4, 32, 32, 32, 0x00, 16, 8},
        {4, 128, 128, 32, 0x00, 64, 32},
        {4, 128, 128, 64, 0x00, 64, 32},
        {4, 32, 16, 16, 0x14, 32, 16},
        {4, 128, 64, 32, 0x14, 128, 64},
        {8, 16, 16, 256, 0x00, 8, 8},
        {32, 16, 16, 16, 0x00, 16, 16},
    }};
    bool all_implicit_twins_match = true;
    for (const ImplicitCase& item : implicit_cases)
    {
        auto complete = MakeTdx(FixtureSpec{
            .bits_per_pixel = item.bits_per_pixel,
            .width = item.width,
            .height = item.height,
            .blocks = {{PlaneSpec{item.transfer_code, item.transfer_width,
                item.transfer_height, 0xB1}}},
        });
        std::fill(complete.end() - static_cast<std::ptrdiff_t>(item.missing), complete.end(),
            std::byte{0});
        auto complete_result = omega::retail::DecodeTdxTextureStorage(complete);
        auto implicit = complete;
        implicit.resize(implicit.size() - static_cast<std::size_t>(item.missing));
        auto implicit_result = omega::retail::DecodeTdxTextureStorage(implicit);
        all_implicit_twins_match = all_implicit_twins_match && complete_result &&
            implicit_result && *complete_result == *implicit_result;
    }
    Check(all_implicit_twins_match,
        "every proven implicit-zero suffix family canonicalizes identically to its complete twin");

    auto unobserved_transfer = MakeTdx(SinglePlane(4, 64, 64, 0x14, 0xB2));
    std::fill(unobserved_transfer.end() - 32, unobserved_transfer.end(), std::byte{0});
    unobserved_transfer.resize(unobserved_transfer.size() - 32U);
    CheckError(omega::retail::DecodeTdxTextureStorage(unobserved_transfer),
        omega::asset::DecodeErrorCode::Truncated,
        "TDX implicit-zero reconstruction rejects an unproven transfer rectangle signature");

    auto unobserved_missing = direct32_bytes;
    unobserved_missing.resize(unobserved_missing.size() - 32U);
    CheckError(omega::retail::DecodeTdxTextureStorage(unobserved_missing),
        omega::asset::DecodeErrorCode::Truncated,
        "TDX implicit-zero reconstruction rejects a missing length outside the exact allowlist");

    auto shifted_layout = direct32_bytes;
    constexpr std::size_t direct32_primary_object = 64U + 64U;
    WriteU32(shifted_layout, direct32_primary_object + 0x54,
        ReadU32(shifted_layout, direct32_primary_object + 0x54) + 16U);
    WriteU32(shifted_layout, 0x38, ReadU32(shifted_layout, 0x38) + 16U);
    shifted_layout.resize(shifted_layout.size() + 16U, std::byte{0});
    shifted_layout.resize(shifted_layout.size() - 16U);
    CheckError(omega::retail::DecodeTdxTextureStorage(shifted_layout),
        omega::asset::DecodeErrorCode::Truncated,
        "TDX implicit-zero reconstruction rejects a shifted storage layout");

    auto unaligned_missing = direct32_bytes;
    unaligned_missing.resize(unaligned_missing.size() - 8U);
    CheckError(omega::retail::DecodeTdxTextureStorage(unaligned_missing),
        omega::asset::DecodeErrorCode::Malformed,
        "TDX implicit-zero reconstruction rejects a non-16-byte shortage");

    auto incomplete_palette = MakeTdx(SinglePlane(8, 16, 16, 0x13, 0xC1));
    std::fill(incomplete_palette.end() - 256, incomplete_palette.end(), std::byte{0});
    incomplete_palette.resize(incomplete_palette.size() - 256U);
    constexpr std::size_t indexed8_palette_object = 64U + 64U;
    WriteU32(incomplete_palette, indexed8_palette_object + 0x54, 1024U);
    CheckError(omega::retail::DecodeTdxTextureStorage(incomplete_palette),
        omega::asset::DecodeErrorCode::InvalidReference,
        "TDX implicit-zero reconstruction still requires a complete bounded palette");

    bool all_short_headers_rejected = true;
    for (std::size_t size = 0; size < 64U; ++size)
    {
        const auto result = omega::retail::DecodeTdxTextureStorage(
            std::span<const std::byte>(direct32_bytes.data(), size));
        all_short_headers_rejected = all_short_headers_rejected && !result &&
            result.error().code == omega::asset::DecodeErrorCode::Truncated;
    }
    Check(all_short_headers_rejected, "every TDX storage header truncation is rejected safely");

    auto bad_reference = direct32_bytes;
    WriteU32(bad_reference, 64, 0xFFFFFFF0U);
    CheckError(omega::retail::DecodeTdxTextureStorage(bad_reference),
        omega::asset::DecodeErrorCode::InvalidReference,
        "TDX rejects an object pointer outside its block");

    auto duplicate_object = multi_bytes;
    WriteU32(duplicate_object, 68, ReadU32(duplicate_object, 64));
    CheckError(omega::retail::DecodeTdxTextureStorage(duplicate_object),
        omega::asset::DecodeErrorCode::DuplicateReference,
        "TDX rejects duplicate primary object references");

    auto duplicate_target = multi_bytes;
    constexpr std::size_t indexed_first_object = 64U + 192U;
    constexpr std::size_t indexed_second_object = 64U + 320U;
    WriteU32(duplicate_target, indexed_second_object + 0x54,
        ReadU32(duplicate_target, indexed_first_object + 0x54));
    CheckError(omega::retail::DecodeTdxTextureStorage(duplicate_target),
        omega::asset::DecodeErrorCode::DuplicateReference,
        "TDX rejects duplicate primary data references");

    auto bad_code = indexed4_bytes;
    WriteU32(bad_code, indexed_first_object + 0x04, 0x13U << 24U);
    CheckError(omega::retail::DecodeTdxTextureStorage(bad_code),
        omega::asset::DecodeErrorCode::UnsupportedVariant,
        "TDX rejects a primary transfer encoding that contradicts the sample encoding");

    auto zero_rectangle = direct32_bytes;
    WriteU32(zero_rectangle, direct32_primary_object + 0x20, 0);
    CheckError(omega::retail::DecodeTdxTextureStorage(zero_rectangle),
        omega::asset::DecodeErrorCode::Malformed,
        "TDX rejects a zero-sized primary storage rectangle");

    auto bad_palette_code = indexed4_bytes;
    constexpr std::size_t palette_object = 64U + 64U;
    WriteU32(bad_palette_code, palette_object + 0x04, 0x01U << 24U);
    CheckError(omega::retail::DecodeTdxTextureStorage(bad_palette_code),
        omega::asset::DecodeErrorCode::UnsupportedVariant,
        "TDX rejects a palette outside the observed four-byte entry storage");

    auto dirty_palette_gap = indexed4_bytes;
    const std::size_t palette_target = 64U + 288U;
    dirty_palette_gap[palette_target + 64U] = std::byte{1};
    CheckError(omega::retail::DecodeTdxTextureStorage(dirty_palette_gap),
        omega::asset::DecodeErrorCode::Malformed,
        "TDX rejects nonzero indexed4 palette-slot padding");

    auto bad_count = direct32_bytes;
    WriteU16(bad_count, 0x24, 0);
    WriteU16(bad_count, 0x34, 0);
    CheckError(omega::retail::DecodeTdxTextureStorage(bad_count),
        omega::asset::DecodeErrorCode::UnsupportedVariant,
        "TDX rejects a primary-plane count outside the observed family");

    auto gap = MakeTdx(FixtureSpec{
        .bits_per_pixel = 8,
        .width = 16,
        .height = 16,
        .blocks = {{PlaneSpec{0x13, 16, 8, 0x11}, PlaneSpec{0x13, 8, 8, 0x22}}},
    });
    const std::uint32_t old_stride = ReadU32(gap, 0x38);
    WriteU32(gap, indexed_second_object + 0x54,
        ReadU32(gap, indexed_second_object + 0x54) + 16U);
    WriteU32(gap, 0x38, old_stride + 16U);
    gap.resize(gap.size() + 16U, std::byte{0});
    CheckError(omega::retail::DecodeTdxTextureStorage(gap),
        omega::asset::DecodeErrorCode::Malformed,
        "TDX rejects a gap between primary storage rectangles");

    auto overlap = MakeTdx(FixtureSpec{
        .bits_per_pixel = 8,
        .width = 16,
        .height = 16,
        .blocks = {{PlaneSpec{0x13, 16, 8, 0x11}, PlaneSpec{0x13, 8, 8, 0x22}}},
    });
    const std::uint32_t overlap_stride = ReadU32(overlap, 0x38);
    WriteU32(overlap, indexed_second_object + 0x54,
        ReadU32(overlap, indexed_second_object + 0x54) - 16U);
    WriteU32(overlap, 0x38, overlap_stride - 16U);
    overlap.resize(overlap.size() - 16U);
    CheckError(omega::retail::DecodeTdxTextureStorage(overlap),
        omega::asset::DecodeErrorCode::Malformed,
        "TDX rejects overlapping primary storage rectangles");

    auto dirty_tail = indexed8_bytes;
    dirty_tail.resize(dirty_tail.size() + 16U, std::byte{0});
    dirty_tail.back() = std::byte{1};
    CheckError(omega::retail::DecodeTdxTextureStorage(dirty_tail),
        omega::asset::DecodeErrorCode::UnsupportedVariant,
        "TDX keeps an unobserved nonzero counted-region tail unsupported");

    auto multi_implicit = MakeTdx(FixtureSpec{
        .bits_per_pixel = 32,
        .width = 16,
        .height = 16,
        .blocks = {{PlaneSpec{0x00, 8, 16, 0x31}, PlaneSpec{0x00, 8, 16, 0x41}}},
    });
    multi_implicit.resize(multi_implicit.size() - 16U);
    CheckError(omega::retail::DecodeTdxTextureStorage(multi_implicit),
        omega::asset::DecodeErrorCode::Truncated,
        "TDX implicit-zero reconstruction is limited to one primary plane");

    auto multiblock_implicit = MakeTdx(FixtureSpec{
        .bits_per_pixel = 32,
        .width = 16,
        .height = 16,
        .blocks = {
            {PlaneSpec{0x00, 16, 16, 0x31}},
            {PlaneSpec{0x00, 16, 16, 0x41}},
        },
    });
    multiblock_implicit.resize(multiblock_implicit.size() - 16U);
    CheckError(omega::retail::DecodeTdxTextureStorage(multiblock_implicit),
        omega::asset::DecodeErrorCode::Truncated,
        "TDX implicit-zero reconstruction is limited to one counted block");

    const auto budget_bytes = MakeTdx(SinglePlane(4, 16, 16, 0x14, 0x55));
    auto limits = omega::asset::DecodeLimits{};
    limits.maximum_input_bytes = budget_bytes.size();
    Check(omega::retail::DecodeTdxTextureStorage(budget_bytes, limits).has_value(),
        "TDX exact input-byte budget succeeds");
    limits.maximum_input_bytes = budget_bytes.size() - 1U;
    CheckError(omega::retail::DecodeTdxTextureStorage(budget_bytes, limits),
        omega::asset::DecodeErrorCode::LimitExceeded,
        "TDX one-below input-byte budget fails");

    constexpr std::uint64_t indexed4_items = 1U + 1U + 1U + 1U + 16U;
    limits = omega::asset::DecodeLimits{};
    limits.maximum_items = indexed4_items;
    Check(omega::retail::DecodeTdxTextureStorage(budget_bytes, limits).has_value(),
        "TDX exact logical-item budget succeeds");
    limits.maximum_items = indexed4_items - 1U;
    CheckError(omega::retail::DecodeTdxTextureStorage(budget_bytes, limits),
        omega::asset::DecodeErrorCode::LimitExceeded,
        "TDX one-below logical-item budget fails");

    const std::uint64_t indexed4_output_bytes = sizeof(omega::asset::TextureStorageIR) +
        sizeof(omega::asset::TextureStorageBlockIR) +
        sizeof(omega::asset::TextureStoragePlaneIR) + 128U +
        16U * sizeof(std::array<std::byte, 4>);
    limits = omega::asset::DecodeLimits{};
    limits.maximum_output_bytes = indexed4_output_bytes;
    Check(omega::retail::DecodeTdxTextureStorage(budget_bytes, limits).has_value(),
        "TDX exact logical-output budget succeeds");
    limits.maximum_output_bytes = indexed4_output_bytes - 1U;
    CheckError(omega::retail::DecodeTdxTextureStorage(budget_bytes, limits),
        omega::asset::DecodeErrorCode::LimitExceeded,
        "TDX one-below logical-output budget fails");

    limits = omega::asset::DecodeLimits{};
    limits.maximum_scratch_bytes = 0;
    limits.maximum_nesting_depth = 0;
    Check(omega::retail::DecodeTdxTextureStorage(budget_bytes, limits).has_value(),
        "TDX flat two-pass decode succeeds with zero dynamic scratch and depth zero");

    return failures;
}
