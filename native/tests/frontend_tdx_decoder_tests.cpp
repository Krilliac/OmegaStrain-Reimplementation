#include "omega/retail/frontend_tdx_decoder.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace
{
constexpr std::size_t kHeaderBytes = 64;
constexpr std::size_t kSecondaryControlPrefix = kHeaderBytes + 0x20;
constexpr std::size_t kPrimaryControlPrefix = kHeaderBytes + 0xA0;
constexpr std::size_t kPrimaryObject = kHeaderBytes + 0xC0;
constexpr std::size_t kPaletteObject = kHeaderBytes + 0x40;
constexpr std::size_t kPaletteData = kHeaderBytes + 0x120;
constexpr std::size_t kPrimaryData = kHeaderBytes + 0x520;

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

void WriteU16(std::vector<std::byte>& bytes, const std::size_t offset, const std::uint16_t value)
{
    bytes[offset] = static_cast<std::byte>(value & 0xFFU);
    bytes[offset + 1U] = static_cast<std::byte>((value >> 8U) & 0xFFU);
}

void WriteU32(std::vector<std::byte>& bytes, const std::size_t offset, const std::uint32_t value)
{
    for (unsigned shift = 0; shift < 32U; shift += 8U)
        bytes[offset + shift / 8U] = static_cast<std::byte>((value >> shift) & 0xFFU);
}

void WriteU64(std::vector<std::byte>& bytes, const std::size_t offset, const std::uint64_t value)
{
    WriteU32(bytes, offset, static_cast<std::uint32_t>(value));
    WriteU32(bytes, offset + 4U, static_cast<std::uint32_t>(value >> 32U));
}

[[nodiscard]] std::uint32_t ReadU32(const std::vector<std::byte>& bytes, const std::size_t offset)
{
    return std::to_integer<std::uint32_t>(bytes[offset]) |
           (std::to_integer<std::uint32_t>(bytes[offset + 1U]) << 8U) |
           (std::to_integer<std::uint32_t>(bytes[offset + 2U]) << 16U) |
           (std::to_integer<std::uint32_t>(bytes[offset + 3U]) << 24U);
}

[[nodiscard]] std::uint8_t PatternByte(const std::size_t index)
{
    return static_cast<std::uint8_t>((73U * index + 19U) & 0xFFU);
}

void WritePacket(std::vector<std::byte>& bytes, const std::size_t object,
                  const std::uint16_t base_pointer, const std::uint16_t buffer_width,
                  const std::uint16_t width, const std::uint16_t height,
                  const std::uint32_t payload_bytes,
                  const std::uint32_t stored_transfer_count_scale = 1U)
{
    const std::uint32_t qword_count = payload_bytes / 16U;
    const std::uint32_t stored_transfer_count = qword_count * stored_transfer_count_scale;
    WriteU32(bytes, object + 0x00U, 0);
    WriteU32(bytes, object + 0x04U,
             base_pointer | (static_cast<std::uint32_t>(buffer_width) << 16U));
    WriteU64(bytes, object + 0x08U, 0x50U);
    WriteU64(bytes, object + 0x10U, 0);
    WriteU64(bytes, object + 0x18U, 0x51U);
    WriteU32(bytes, object + 0x20U, width);
    WriteU32(bytes, object + 0x24U, height);
    WriteU64(bytes, object + 0x28U, 0x52U);
    WriteU64(bytes, object + 0x30U, 0);
    WriteU64(bytes, object + 0x38U, 0x53U);
    WriteU32(bytes, object + 0x40U, stored_transfer_count);
    WriteU32(bytes, object + 0x44U, 0x08000000U);
    WriteU64(bytes, object + 0x48U, 0);
    WriteU32(bytes, object + 0x50U, 0x30000000U | stored_transfer_count);
    WriteU32(bytes, object + 0x54U, 0);
    WriteU64(bytes, object + 0x58U, 0);
}

void WriteTransferControlPrefix(std::vector<std::byte>& bytes, const std::size_t prefix)
{
    constexpr std::uint32_t kDmaCntTag = (1U << 28U) | 6U;
    constexpr std::uint64_t kPackedAdGifTag = (1ULL << 60U) | 4U;
    constexpr std::uint64_t kAdRegisterDescriptor = 0x0EULL;
    WriteU32(bytes, prefix + 0x00U, kDmaCntTag);
    WriteU32(bytes, prefix + 0x04U, 0U);
    WriteU64(bytes, prefix + 0x08U, 0U);
    WriteU64(bytes, prefix + 0x10U, kPackedAdGifTag);
    WriteU64(bytes, prefix + 0x18U, kAdRegisterDescriptor);
}

[[nodiscard]] std::vector<std::byte> MakeFixture(const omega::asset::IndexedImageEncoding encoding,
                                                 const std::uint16_t base_pointer = 0,
                                                 const std::uint16_t width = 32,
                                                 const std::uint16_t height = 32,
                                                 const std::uint16_t flags = 5)
{
    const bool indexed8 = encoding == omega::asset::IndexedImageEncoding::Indexed8;
    const std::uint16_t bits_per_pixel = indexed8 ? 8U : 4U;
    const std::uint16_t storage_format = indexed8 ? 0x13U : 0x14U;
    const std::uint16_t texture_buffer_width = std::max<std::uint16_t>(2U, width / 64U);
    const std::uint16_t upload_width = width / 2U;
    const std::uint16_t upload_height = static_cast<std::uint16_t>(height / (indexed8 ? 2U : 4U));
    const std::uint32_t primary_bytes =
        static_cast<std::uint32_t>(upload_width) * upload_height * 4U;
    const std::uint16_t palette_width = indexed8 ? 16U : 8U;
    const std::uint16_t palette_height = indexed8 ? 16U : 2U;
    const std::uint32_t palette_bytes =
        static_cast<std::uint32_t>(palette_width) * palette_height * 4U;
    const std::uint32_t stride = 0x520U + primary_bytes;

    std::vector<std::byte> bytes(kHeaderBytes + stride, std::byte{0});
    WriteU16(bytes, 0x00, 5);
    WriteU16(bytes, 0x02, flags);
    WriteU16(bytes, 0x04, width);
    WriteU16(bytes, 0x06, height);
    WriteU16(bytes, 0x08, bits_per_pixel);
    WriteU16(bytes, 0x0A, storage_format);
    WriteU16(bytes, 0x0C, texture_buffer_width);
    WriteU16(bytes, 0x0E,
             static_cast<std::uint16_t>(static_cast<std::uint32_t>(width) * height *
                                        bits_per_pixel / 8U / 256U));
    WriteU16(bytes, 0x10, palette_width);
    WriteU16(bytes, 0x12, palette_height);
    WriteU16(bytes, 0x14, 32);
    WriteU16(bytes, 0x16, 0);
    WriteU16(bytes, 0x18, 1);
    WriteU16(bytes, 0x1A, 4);
    WriteU16(bytes, 0x22, 1);
    WriteU16(bytes, 0x24, 1);
    WriteU16(bytes, 0x26, 1);
    WriteU16(bytes, 0x34, 128);
    WriteU16(bytes, 0x36, 128);
    WriteU32(bytes, 0x38, stride);

    WriteU32(bytes, kHeaderBytes + 0x00U, 0x20U);
    WriteU32(bytes, kHeaderBytes + 0x14U, 0x20U);
    WriteU32(bytes, kHeaderBytes + 0x18U, 0xA0U);
    WriteU32(bytes, kHeaderBytes + 0x1CU, 0x20U);
    WriteTransferControlPrefix(bytes, kSecondaryControlPrefix);
    WriteTransferControlPrefix(bytes, kPrimaryControlPrefix);
    WritePacket(bytes, kPrimaryObject, base_pointer, texture_buffer_width / 2U, upload_width,
                upload_height, primary_bytes, indexed8 ? 1U : 2U);
    WritePacket(bytes, kPaletteObject, base_pointer, 1U, palette_width, palette_height,
                palette_bytes);

    for (std::uint32_t index = 0; index < palette_bytes / 4U; ++index)
    {
        bytes[kPaletteData + index * 4U] = static_cast<std::byte>(index & 0xFFU);
        bytes[kPaletteData + index * 4U + 1U] = std::byte{0x22};
        bytes[kPaletteData + index * 4U + 2U] = std::byte{0x33};
        bytes[kPaletteData + index * 4U + 3U] = static_cast<std::byte>(index % 129U);
    }
    bytes[kPaletteData + 5U * 4U] = std::byte{0x11};
    bytes[kPaletteData + 5U * 4U + 1U] = std::byte{0x22};
    bytes[kPaletteData + 5U * 4U + 2U] = std::byte{0x33};
    bytes[kPaletteData + 5U * 4U + 3U] = std::byte{0x40};

    for (std::uint32_t index = 0; index < primary_bytes; ++index)
        bytes[kPrimaryData + index] = static_cast<std::byte>(PatternByte(index));
    return bytes;
}

[[nodiscard]] std::string Sha256(const std::span<const std::uint8_t> input)
{
    constexpr std::array<std::uint32_t, 64> constants{
        0x428A2F98U, 0x71374491U, 0xB5C0FBCFU, 0xE9B5DBA5U, 0x3956C25BU, 0x59F111F1U, 0x923F82A4U,
        0xAB1C5ED5U, 0xD807AA98U, 0x12835B01U, 0x243185BEU, 0x550C7DC3U, 0x72BE5D74U, 0x80DEB1FEU,
        0x9BDC06A7U, 0xC19BF174U, 0xE49B69C1U, 0xEFBE4786U, 0x0FC19DC6U, 0x240CA1CCU, 0x2DE92C6FU,
        0x4A7484AAU, 0x5CB0A9DCU, 0x76F988DAU, 0x983E5152U, 0xA831C66DU, 0xB00327C8U, 0xBF597FC7U,
        0xC6E00BF3U, 0xD5A79147U, 0x06CA6351U, 0x14292967U, 0x27B70A85U, 0x2E1B2138U, 0x4D2C6DFCU,
        0x53380D13U, 0x650A7354U, 0x766A0ABBU, 0x81C2C92EU, 0x92722C85U, 0xA2BFE8A1U, 0xA81A664BU,
        0xC24B8B70U, 0xC76C51A3U, 0xD192E819U, 0xD6990624U, 0xF40E3585U, 0x106AA070U, 0x19A4C116U,
        0x1E376C08U, 0x2748774CU, 0x34B0BCB5U, 0x391C0CB3U, 0x4ED8AA4AU, 0x5B9CCA4FU, 0x682E6FF3U,
        0x748F82EEU, 0x78A5636FU, 0x84C87814U, 0x8CC70208U, 0x90BEFFFAU, 0xA4506CEBU, 0xBEF9A3F7U,
        0xC67178F2U,
    };
    std::array<std::uint32_t, 8> state{
        0x6A09E667U, 0xBB67AE85U, 0x3C6EF372U, 0xA54FF53AU,
        0x510E527FU, 0x9B05688CU, 0x1F83D9ABU, 0x5BE0CD19U,
    };
    std::vector<std::uint8_t> message(input.begin(), input.end());
    const std::uint64_t bit_length = static_cast<std::uint64_t>(message.size()) * 8U;
    message.push_back(0x80U);
    while (message.size() % 64U != 56U)
        message.push_back(0);
    for (int shift = 56; shift >= 0; shift -= 8)
        message.push_back(static_cast<std::uint8_t>(bit_length >> shift));

    for (std::size_t offset = 0; offset < message.size(); offset += 64U)
    {
        std::array<std::uint32_t, 64> words{};
        for (std::size_t index = 0; index < 16U; ++index)
        {
            const std::size_t byte = offset + index * 4U;
            words[index] = (static_cast<std::uint32_t>(message[byte]) << 24U) |
                           (static_cast<std::uint32_t>(message[byte + 1U]) << 16U) |
                           (static_cast<std::uint32_t>(message[byte + 2U]) << 8U) |
                           static_cast<std::uint32_t>(message[byte + 3U]);
        }
        for (std::size_t index = 16U; index < words.size(); ++index)
        {
            const std::uint32_t s0 = std::rotr(words[index - 15U], 7) ^
                                     std::rotr(words[index - 15U], 18) ^ (words[index - 15U] >> 3U);
            const std::uint32_t s1 = std::rotr(words[index - 2U], 17) ^
                                     std::rotr(words[index - 2U], 19) ^ (words[index - 2U] >> 10U);
            words[index] = words[index - 16U] + s0 + words[index - 7U] + s1;
        }

        std::uint32_t a = state[0];
        std::uint32_t b = state[1];
        std::uint32_t c = state[2];
        std::uint32_t d = state[3];
        std::uint32_t e = state[4];
        std::uint32_t f = state[5];
        std::uint32_t g = state[6];
        std::uint32_t h = state[7];
        for (std::size_t index = 0; index < words.size(); ++index)
        {
            const std::uint32_t sum1 = std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25);
            const std::uint32_t choose = (e & f) ^ (~e & g);
            const std::uint32_t temp1 = h + sum1 + choose + constants[index] + words[index];
            const std::uint32_t sum0 = std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22);
            const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t temp2 = sum0 + majority;
            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }
        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
        state[4] += e;
        state[5] += f;
        state[6] += g;
        state[7] += h;
    }

    std::ostringstream text;
    text << std::hex << std::setfill('0');
    for (const std::uint32_t word : state)
        text << std::setw(8) << word;
    return text.str();
}

[[nodiscard]] std::vector<std::uint8_t> GeneratedSource(const std::size_t size)
{
    std::vector<std::uint8_t> bytes(size);
    for (std::size_t index = 0; index < size; ++index)
        bytes[index] = PatternByte(index);
    return bytes;
}

void CheckAddressVectors()
{
    using omega::retail::GsPsmct32WordAddress;
    using omega::retail::GsPsmt4NibbleAddress;
    using omega::retail::GsPsmt8ByteAddress;

    constexpr std::array<std::array<std::uint32_t, 3>, 7> psmct32{{
        {0, 0, 0},
        {7, 0, 13},
        {8, 0, 64},
        {0, 8, 128},
        {32, 0, 1024},
        {64, 0, 2048},
        {0, 32, 4096},
    }};
    bool vectors_match = true;
    for (const auto& vector : psmct32)
        vectors_match =
            vectors_match && GsPsmct32WordAddress(0, 2, vector[0], vector[1]) == vector[2];
    vectors_match = vectors_match && GsPsmct32WordAddress(3, 2, 0, 0) == 192U;
    Check(vectors_match, "PSMCT32 address vectors match the independent formula contract");

    constexpr std::array<std::array<std::uint32_t, 3>, 7> psmt8{{
        {0, 0, 0},
        {1, 0, 4},
        {8, 0, 2},
        {16, 0, 256},
        {64, 0, 4096},
        {128, 0, 8192},
        {0, 64, 16384},
    }};
    vectors_match = true;
    for (const auto& vector : psmt8)
        vectors_match =
            vectors_match && GsPsmt8ByteAddress(0, 4, vector[0], vector[1]) == vector[2];
    vectors_match = vectors_match && GsPsmt8ByteAddress(3, 4, 0, 0) == 768U;
    Check(vectors_match, "PSMT8 address vectors match the independent formula contract");

    constexpr std::array<std::array<std::uint32_t, 3>, 7> psmt4{{
        {0, 0, 0},
        {1, 0, 8},
        {8, 0, 2},
        {16, 0, 4},
        {32, 0, 1024},
        {128, 0, 16384},
        {0, 128, 32768},
    }};
    vectors_match = true;
    for (const auto& vector : psmt4)
        vectors_match =
            vectors_match && GsPsmt4NibbleAddress(0, 4, vector[0], vector[1]) == vector[2];
    vectors_match = vectors_match && GsPsmt4NibbleAddress(3, 4, 0, 0) == 1536U;
    Check(vectors_match, "PSMT4 address vectors match the independent formula contract");

    std::vector<bool> seen32(2048U, false);
    bool unique32 = true;
    for (std::uint32_t y = 0; y < 32U; ++y)
    {
        for (std::uint32_t x = 0; x < 64U; ++x)
        {
            const auto address = GsPsmct32WordAddress(0, 1, x, y);
            unique32 = unique32 && address && *address < seen32.size() && !seen32[*address];
            if (address && *address < seen32.size())
                seen32[*address] = true;
        }
    }
    Check(unique32 && std::ranges::all_of(seen32, [](const bool value) { return value; }),
          "PSMCT32 exhaustively visits every word in one page exactly once");

    std::vector<bool> seen8(8192U, false);
    bool unique8 = true;
    for (std::uint32_t y = 0; y < 64U; ++y)
    {
        for (std::uint32_t x = 0; x < 128U; ++x)
        {
            const auto address = GsPsmt8ByteAddress(0, 2, x, y);
            unique8 = unique8 && address && *address < seen8.size() && !seen8[*address];
            if (address && *address < seen8.size())
                seen8[*address] = true;
        }
    }
    Check(unique8 && std::ranges::all_of(seen8, [](const bool value) { return value; }),
          "PSMT8 exhaustively visits every byte in one page exactly once");

    std::vector<bool> seen4(16384U, false);
    bool unique4 = true;
    for (std::uint32_t y = 0; y < 128U; ++y)
    {
        for (std::uint32_t x = 0; x < 128U; ++x)
        {
            const auto address = GsPsmt4NibbleAddress(0, 2, x, y);
            unique4 = unique4 && address && *address < seen4.size() && !seen4[*address];
            if (address && *address < seen4.size())
                seen4[*address] = true;
        }
    }
    Check(unique4 && std::ranges::all_of(seen4, [](const bool value) { return value; }),
          "PSMT4 exhaustively visits every nibble in one page exactly once");

    Check(!GsPsmct32WordAddress(0, 0, 0, 0) && !GsPsmct32WordAddress(0, 64, 0, 0) &&
              !GsPsmct32WordAddress(0x4000, 1, 0, 0) && !GsPsmt8ByteAddress(0, 1, 0, 0) &&
              !GsPsmt8ByteAddress(0, 3, 0, 0) && !GsPsmt8ByteAddress(0, 64, 0, 0) &&
              !GsPsmt4NibbleAddress(0, 1, 0, 0) && !GsPsmt4NibbleAddress(0, 3, 0, 0) &&
              !GsPsmt4NibbleAddress(0x4000, 2, 0, 0),
          "address functions reject out-of-field base pointers and buffer widths");
}

void CheckEndToEnd()
{
    const auto indexed8_source = GeneratedSource(1024U);
    const auto indexed4_source = GeneratedSource(512U);
    Check(Sha256(indexed8_source) ==
              "e051d1007607de494c073da3c29903d6c0abfee7a4c0609f560a340a1947b470",
          "generated indexed-8 transfer source matches its pinned SHA-256");
    Check(Sha256(indexed4_source) ==
              "8d35978b98877da24dc6904f1a27aaf031f9103991d3ed868ae7dc938a6a00fd",
          "generated indexed-4 transfer source matches its pinned SHA-256");

    auto indexed8_bytes = MakeFixture(omega::asset::IndexedImageEncoding::Indexed8);
    auto indexed4_bytes = MakeFixture(omega::asset::IndexedImageEncoding::Indexed4);
    const auto indexed8 = omega::retail::DecodeFrontEndTdx(indexed8_bytes);
    const auto indexed4 = omega::retail::DecodeFrontEndTdx(indexed4_bytes);
    Check(indexed8 && indexed8->image.width == 32U && indexed8->image.height == 32U &&
              indexed8->image.indices.size() == 1024U && indexed8->image.palette.size() == 256U &&
              indexed8->upload_plan.sampling_format ==
                  omega::retail::TdxGsPixelStorageFormat::Psmt8,
          "generated indexed-8 transfer decodes to owned canonical image state");
    Check(indexed4 && indexed4->image.width == 32U && indexed4->image.height == 32U &&
              indexed4->image.indices.size() == 1024U && indexed4->image.palette.size() == 16U &&
              indexed4->upload_plan.sampling_format ==
                  omega::retail::TdxGsPixelStorageFormat::Psmt4,
           "generated indexed-4 transfer decodes to one-byte canonical indices");
    if (!indexed8 || !indexed4)
        return;
    Check(indexed8->upload_plan.texture_alpha_enabled &&
              indexed4->upload_plan.texture_alpha_enabled,
          "ordinary flag-5 frontend textures retain the proven alpha mode");

    Check(Sha256(indexed8->image.indices) ==
              "e3a5d313cb3382e418c974ca35e3ad32a68a5b4668c8c1291506cddaf52602ca",
          "indexed-8 GS-local replay matches its pinned output SHA-256");
    Check(Sha256(indexed4->image.indices) ==
              "7453247c2f6e36e331b2e5bf6743fe453e8e5e2a7159fc814120b1bf168585b3",
          "indexed-4 GS-local replay matches its pinned output SHA-256");

    struct Anchor8
    {
        std::uint16_t x;
        std::uint16_t y;
        std::uint16_t source;
    };
    constexpr std::array<Anchor8, 9> anchors8{{
        {0, 0, 0},
        {1, 0, 4},
        {8, 0, 2},
        {16, 0, 32},
        {0, 1, 64},
        {0, 7, 193},
        {0, 8, 256},
        {0, 16, 512},
        {31, 31, 1023},
    }};
    bool anchors_match = true;
    for (const Anchor8& anchor : anchors8)
        anchors_match = anchors_match && indexed8->image.indices[anchor.y * 32U + anchor.x] ==
                                             PatternByte(anchor.source);
    Check(anchors_match, "indexed-8 coordinate-to-source anchors all match");

    struct Anchor4
    {
        std::uint16_t x;
        std::uint16_t y;
        std::uint16_t source_byte;
        bool high;
    };
    constexpr std::array<Anchor4, 9> anchors4{{
        {0, 0, 0, false},
        {1, 0, 4, false},
        {8, 0, 1, false},
        {16, 0, 2, false},
        {0, 7, 192, true},
        {0, 8, 256, false},
        {0, 16, 32, false},
        {31, 31, 511, true},
        {2, 0, 8, false},
    }};
    anchors_match = true;
    for (const Anchor4& anchor : anchors4)
    {
        const std::uint8_t packed = PatternByte(anchor.source_byte);
        const std::uint8_t expected =
            static_cast<std::uint8_t>((anchor.high ? packed >> 4U : packed) & 0x0FU);
        anchors_match =
            anchors_match && indexed4->image.indices[anchor.y * 32U + anchor.x] == expected;
    }
    Check(anchors_match, "indexed-4 coordinate-to-source nibble anchors all match");

    constexpr std::array<std::uint8_t, 32> first32{
        0, 1, 2,  3,  4,  5,  6,  7,  16, 17, 18, 19, 20, 21, 22, 23,
        8, 9, 10, 11, 12, 13, 14, 15, 24, 25, 26, 27, 28, 29, 30, 31,
    };
    bool palette_matches = true;
    for (std::size_t index = 0; index < first32.size(); ++index)
    {
        const std::uint8_t expected = index == 5U ? 0x11U : first32[index];
        palette_matches = palette_matches && indexed8->image.palette[index].red == expected;
    }
    for (std::size_t index = 0; index < indexed4->image.palette.size(); ++index)
    {
        const std::uint8_t expected = index == 5U ? 0x11U : static_cast<std::uint8_t>(index);
        palette_matches = palette_matches && indexed4->image.palette[index].red == expected;
    }
    Check(palette_matches, "CSM1 palette mapping is swapped for indexed-8 and "
                           "identity for indexed-4");

    const auto color = indexed8->image.palette[5];
    Check(color == omega::asset::RawGsRgba8{0x11, 0x22, 0x33, 0x40} &&
              omega::retail::GsAlphaCoefficient(color.alpha) == 0.5F &&
              omega::retail::GsAlphaToRgba8(color.alpha) == 128U,
          "palette channels remain straight RGBA with raw GS alpha");
    Check(omega::retail::GsAlphaToRgba8(0) == 0U && omega::retail::GsAlphaToRgba8(1) == 2U &&
              omega::retail::GsAlphaToRgba8(64) == 128U &&
              omega::retail::GsAlphaToRgba8(127) == 253U &&
              omega::retail::GsAlphaToRgba8(128) == 255U &&
              omega::retail::GsAlphaToRgba8(255) == 255U,
          "conventional RGBA8 alpha bridge rounds and clamps at the proven "
          "boundaries");

    auto based_bytes = MakeFixture(omega::asset::IndexedImageEncoding::Indexed8, 3U);
    const auto based = omega::retail::DecodeFrontEndTdx(based_bytes);
    Check(based && based->image == indexed8->image &&
              based->upload_plan.texture_base_pointer == 3U &&
              based->upload_plan.primary_upload.destination_base_pointer == 3U &&
              based->upload_plan.palette_upload.destination_base_pointer == 3U,
          "relocated base-pointer values are retained while producing stable "
          "owned output");

    const auto flags7_bytes =
        MakeFixture(omega::asset::IndexedImageEncoding::Indexed4, 0U, 64U, 32U, 7U);
    const auto flags7 = omega::retail::DecodeFrontEndTdx(flags7_bytes);
    Check(flags7 && flags7->upload_plan.header_flags == 7U && flags7->image.width == 64U &&
              flags7->image.height == 32U && flags7->upload_plan.texture_alpha_enabled,
          "the second observed alpha-enabled header flag and a nonsquare display decode");

    const auto scoped_flags1_bytes =
        MakeFixture(omega::asset::IndexedImageEncoding::Indexed8, 0U, 32U, 32U, 1U);
    CheckError(omega::retail::DecodeFrontEndTdx(scoped_flags1_bytes),
               omega::asset::DecodeErrorCode::UnsupportedVariant,
               "ordinary frontend decoding rejects the scope-only indexed-8 flag-1 family");
    const auto scoped_flags1 = omega::retail::DecodeScopedFrontEndTdx(scoped_flags1_bytes);
    Check(scoped_flags1 && scoped_flags1->upload_plan.header_flags == 1U &&
              !scoped_flags1->upload_plan.texture_alpha_enabled &&
              scoped_flags1->image == indexed8->image,
          "bound external visuals admit exact indexed-8 flag-1 records with alpha disabled");

    const auto scoped_flags3_bytes =
        MakeFixture(omega::asset::IndexedImageEncoding::Indexed8, 0U, 32U, 32U, 3U);
    CheckError(omega::retail::DecodeScopedFrontEndTdx(scoped_flags3_bytes),
               omega::asset::DecodeErrorCode::UnsupportedVariant,
               "bound external visuals reject the unobserved indexed-8 flag-3 family");

    for (const std::uint16_t flags : {std::uint16_t{1U}, std::uint16_t{3U}})
    {
        const auto unobserved = MakeFixture(
            omega::asset::IndexedImageEncoding::Indexed4, 0U, 32U, 32U, flags);
        CheckError(omega::retail::DecodeScopedFrontEndTdx(unobserved),
                   omega::asset::DecodeErrorCode::UnsupportedVariant,
                   "bound external visuals reject every new indexed-4 flag family");
    }
    const auto scoped_canonical = omega::retail::DecodeScopedFrontEndTdx(indexed8_bytes);
    Check(scoped_canonical && *scoped_canonical == *indexed8,
          "bound external visual decoding preserves the ordinary canonical family exactly");

    const auto expected_image = indexed8->image;
    std::fill(indexed8_bytes.begin(), indexed8_bytes.end(), std::byte{0});
    Check(indexed8->image == expected_image,
          "decoded image owns its pixels and palette after source destruction");
    const auto repeated_bytes = MakeFixture(omega::asset::IndexedImageEncoding::Indexed8);
    const auto repeated = omega::retail::DecodeFrontEndTdx(repeated_bytes);
    Check(repeated && repeated->image == expected_image &&
              repeated->upload_plan == indexed8->upload_plan,
          "repeated generated decodes are deterministic");
}

void CheckBudgets()
{
    const auto bytes = MakeFixture(omega::asset::IndexedImageEncoding::Indexed8);
    const auto baseline = omega::retail::DecodeFrontEndTdx(bytes);
    Check(baseline.has_value(), "baseline generated fixture is available for budget checks");
    if (!baseline)
        return;

    omega::asset::DecodeLimits limits;
    limits.maximum_input_bytes = bytes.size();
    Check(omega::retail::DecodeFrontEndTdx(bytes, limits).has_value(),
          "frontend TDX exact input-byte budget succeeds");
    limits.maximum_input_bytes = bytes.size() - 1U;
    CheckError(omega::retail::DecodeFrontEndTdx(bytes, limits),
               omega::asset::DecodeErrorCode::LimitExceeded,
               "frontend TDX one-below input-byte budget fails");

    limits = omega::asset::DecodeLimits{};
    limits.maximum_items = baseline->decoded_items;
    Check(omega::retail::DecodeFrontEndTdx(bytes, limits).has_value(),
          "frontend TDX exact operation budget succeeds");
    limits.maximum_items = baseline->decoded_items - 1U;
    CheckError(omega::retail::DecodeFrontEndTdx(bytes, limits),
               omega::asset::DecodeErrorCode::LimitExceeded,
               "frontend TDX one-below operation budget fails");

    limits = omega::asset::DecodeLimits{};
    limits.maximum_output_bytes = baseline->logical_output_bytes;
    Check(omega::retail::DecodeFrontEndTdx(bytes, limits).has_value(),
          "frontend TDX exact owned-output budget succeeds");
    limits.maximum_output_bytes = baseline->logical_output_bytes - 1U;
    CheckError(omega::retail::DecodeFrontEndTdx(bytes, limits),
               omega::asset::DecodeErrorCode::LimitExceeded,
               "frontend TDX one-below owned-output budget fails");

    limits = omega::asset::DecodeLimits{};
    limits.maximum_scratch_bytes = baseline->peak_scratch_bytes;
    limits.maximum_nesting_depth = 0;
    Check(omega::retail::DecodeFrontEndTdx(bytes, limits).has_value(),
          "frontend TDX exact scratch budget succeeds without recursion");
    limits.maximum_scratch_bytes = baseline->peak_scratch_bytes - 1U;
    CheckError(omega::retail::DecodeFrontEndTdx(bytes, limits),
               omega::asset::DecodeErrorCode::LimitExceeded,
               "frontend TDX one-below scratch budget fails");
}

void CheckStrictRejections()
{
    const auto fixture = MakeFixture(omega::asset::IndexedImageEncoding::Indexed8);
    const auto indexed4_fixture = MakeFixture(omega::asset::IndexedImageEncoding::Indexed4);

    for (const std::size_t prefix : {kSecondaryControlPrefix, kPrimaryControlPrefix})
    {
        auto corrupt = fixture;
        std::fill_n(corrupt.begin() + static_cast<std::ptrdiff_t>(prefix), 0x20U, std::byte{0});
        CheckError(omega::retail::DecodeFrontEndTdx(corrupt),
                   omega::asset::DecodeErrorCode::Malformed,
                   "frontend TDX rejects a missing active transfer control prefix");

        for (std::size_t byte = 0; byte < 0x20U; ++byte)
        {
            corrupt = fixture;
            corrupt[prefix + byte] ^= std::byte{1};
            CheckError(omega::retail::DecodeFrontEndTdx(corrupt),
                       omega::asset::DecodeErrorCode::Malformed,
                       "frontend TDX rejects every corrupted transfer control field");
        }
    }

    for (std::size_t byte = 0x04U; byte < 0x14U; ++byte)
    {
        auto corrupt = fixture;
        corrupt[kHeaderBytes + byte] ^= std::byte{1};
        CheckError(omega::retail::DecodeFrontEndTdx(corrupt),
                   omega::asset::DecodeErrorCode::Malformed,
                   "frontend TDX rejects every nonzero inactive pointer byte");
    }

    for (const std::size_t word : {0x00U, 0x14U, 0x18U, 0x1CU})
    {
        auto corrupt = fixture;
        corrupt[kHeaderBytes + word] ^= std::byte{1};
        CheckError(omega::retail::DecodeFrontEndTdx(corrupt),
                   omega::asset::DecodeErrorCode::UnsupportedVariant,
                   "frontend TDX rejects every corrupted active pointer-layout word");
    }

    for (const auto* packet_fixture : {&fixture, &indexed4_fixture})
    {
        for (const std::size_t packet : {kPrimaryObject, kPaletteObject})
        {
            for (const std::size_t register_offset : {0x08U, 0x18U, 0x28U, 0x38U})
            {
                auto corrupt = *packet_fixture;
                corrupt[packet + register_offset] ^= std::byte{1};
                CheckError(omega::retail::DecodeFrontEndTdx(corrupt),
                           omega::asset::DecodeErrorCode::Malformed,
                           "frontend TDX rejects every corrupted packet register identifier");
            }

            for (unsigned bit = 0; bit < 15U; ++bit)
            {
                auto corrupt = *packet_fixture;
                corrupt[packet + 0x40U + bit / 8U] ^=
                    static_cast<std::byte>(1U << (bit % 8U));
                CheckError(omega::retail::DecodeFrontEndTdx(corrupt),
                           omega::asset::DecodeErrorCode::Malformed,
                           "frontend TDX rejects every corrupted stored IMAGE count bit");
            }

            auto corrupt = *packet_fixture;
            corrupt[packet + 0x41U] ^= std::byte{0x80};
            CheckError(omega::retail::DecodeFrontEndTdx(corrupt),
                       omega::asset::DecodeErrorCode::Malformed,
                       "frontend TDX rejects a nonzero stored IMAGE EOP bit");

            for (std::size_t byte = 0x42U; byte < 0x44U; ++byte)
            {
                corrupt = *packet_fixture;
                corrupt[packet + byte] ^= std::byte{1};
                CheckError(omega::retail::DecodeFrontEndTdx(corrupt),
                           omega::asset::DecodeErrorCode::Malformed,
                           "frontend TDX rejects every nonzero stored IMAGE reserved byte");
            }
            for (std::size_t byte = 0x44U; byte < 0x48U; ++byte)
            {
                corrupt = *packet_fixture;
                corrupt[packet + byte] ^= std::byte{1};
                CheckError(omega::retail::DecodeFrontEndTdx(corrupt),
                           omega::asset::DecodeErrorCode::Malformed,
                           "frontend TDX rejects every corrupted stored IMAGE mode byte");
            }
            for (std::size_t byte = 0x48U; byte < 0x50U; ++byte)
            {
                corrupt = *packet_fixture;
                corrupt[packet + byte] ^= std::byte{1};
                CheckError(omega::retail::DecodeFrontEndTdx(corrupt),
                           omega::asset::DecodeErrorCode::Malformed,
                           "frontend TDX rejects every nonzero stored IMAGE register byte");
            }
            for (std::size_t byte = 0x50U; byte < 0x54U; ++byte)
            {
                corrupt = *packet_fixture;
                corrupt[packet + byte] ^= std::byte{1};
                CheckError(omega::retail::DecodeFrontEndTdx(corrupt),
                           omega::asset::DecodeErrorCode::Malformed,
                           "frontend TDX rejects every corrupted DMA tag or count byte");
            }
            for (std::size_t byte = 0x54U; byte < 0x58U; ++byte)
            {
                corrupt = *packet_fixture;
                corrupt[packet + byte] ^= std::byte{1};
                CheckError(omega::retail::DecodeFrontEndTdx(corrupt),
                           omega::asset::DecodeErrorCode::InvalidReference,
                           "frontend TDX rejects every nonzero DMA data-reference byte");
            }
            for (std::size_t byte = 0x58U; byte < 0x60U; ++byte)
            {
                corrupt = *packet_fixture;
                corrupt[packet + byte] ^= std::byte{1};
                CheckError(omega::retail::DecodeFrontEndTdx(corrupt),
                           omega::asset::DecodeErrorCode::Malformed,
                           "frontend TDX rejects every nonzero DMA reserved byte");
            }
        }
    }

    constexpr std::uint32_t indexed4_primary_qwords = 32U;
    constexpr std::uint32_t indexed4_primary_stored_count = indexed4_primary_qwords * 2U;
    constexpr std::uint32_t indexed4_palette_qwords = 4U;
    Check((ReadU32(indexed4_fixture, kPrimaryObject + 0x40U) & 0x7FFFU) ==
                  indexed4_primary_stored_count &&
              (ReadU32(indexed4_fixture, kPrimaryObject + 0x50U) & 0xFFFFU) ==
                  indexed4_primary_stored_count,
          "generated indexed-4 primary fixture carries the exact doubled stored counts");
    auto corrupt = indexed4_fixture;
    WriteU32(corrupt, kPrimaryObject + 0x40U, indexed4_primary_qwords);
    CheckError(omega::retail::DecodeFrontEndTdx(corrupt),
               omega::asset::DecodeErrorCode::Malformed,
               "indexed-4 primary rejects an undoubled stored IMAGE count");

    corrupt = indexed4_fixture;
    WriteU32(corrupt, kPrimaryObject + 0x50U, 0x30000000U | indexed4_primary_qwords);
    CheckError(omega::retail::DecodeFrontEndTdx(corrupt),
               omega::asset::DecodeErrorCode::Malformed,
               "indexed-4 primary rejects an undoubled stored DMA count");

    corrupt = indexed4_fixture;
    WriteU32(corrupt, kPaletteObject + 0x40U, indexed4_palette_qwords * 2U);
    CheckError(omega::retail::DecodeFrontEndTdx(corrupt),
               omega::asset::DecodeErrorCode::Malformed,
               "indexed-4 palette rejects a doubled stored IMAGE count");

    corrupt = indexed4_fixture;
    WriteU32(corrupt, kPaletteObject + 0x50U,
             0x30000000U | (indexed4_palette_qwords * 2U));
    CheckError(omega::retail::DecodeFrontEndTdx(corrupt),
               omega::asset::DecodeErrorCode::Malformed,
               "indexed-4 palette rejects a doubled stored DMA count");

    constexpr std::uint32_t indexed8_primary_qwords = 64U;
    corrupt = fixture;
    WriteU32(corrupt, kPrimaryObject + 0x40U, indexed8_primary_qwords * 2U);
    CheckError(omega::retail::DecodeFrontEndTdx(corrupt),
               omega::asset::DecodeErrorCode::Malformed,
               "indexed-8 primary rejects a doubled stored IMAGE count");

    corrupt = fixture;
    WriteU32(corrupt, kPrimaryObject + 0x50U, 0x30000000U | indexed8_primary_qwords * 2U);
    CheckError(omega::retail::DecodeFrontEndTdx(corrupt),
               omega::asset::DecodeErrorCode::Malformed,
               "indexed-8 primary rejects a doubled stored DMA count");

    corrupt = fixture;
    corrupt[kPrimaryObject + 0x07U] = std::byte{0x13};
    CheckError(omega::retail::DecodeFrontEndTdx(corrupt),
               omega::asset::DecodeErrorCode::UnsupportedVariant,
               "frontend TDX rejects a direct PSMT8 primary upload");

    corrupt = fixture;
    corrupt[kPrimaryObject + 0x06U] = std::byte{2};
    CheckError(omega::retail::DecodeFrontEndTdx(corrupt),
               omega::asset::DecodeErrorCode::UnsupportedVariant,
               "frontend TDX rejects an inconsistent primary DBW");

    corrupt = fixture;
    corrupt[kPrimaryObject + 0x30U] = std::byte{1};
    CheckError(omega::retail::DecodeFrontEndTdx(corrupt),
               omega::asset::DecodeErrorCode::UnsupportedVariant,
               "frontend TDX rejects a non-host-to-local TRXDIR");

    corrupt = fixture;
    corrupt[kPrimaryObject + 0x17U] = std::byte{8};
    CheckError(omega::retail::DecodeFrontEndTdx(corrupt),
               omega::asset::DecodeErrorCode::UnsupportedVariant,
               "frontend TDX rejects a nonzero transfer direction bit");

    corrupt = fixture;
    WriteU32(corrupt, kPrimaryObject + 0x20U, 17U);
    CheckError(omega::retail::DecodeFrontEndTdx(corrupt),
               omega::asset::DecodeErrorCode::UnsupportedVariant,
               "frontend TDX rejects a rectangle outside the display relationship");

    corrupt = fixture;
    WriteU16(corrupt, 0x22U, 2U);
    CheckError(omega::retail::DecodeFrontEndTdx(corrupt),
               omega::asset::DecodeErrorCode::UnsupportedVariant,
               "frontend TDX rejects multiple counted blocks");

    corrupt = fixture;
    WriteU32(corrupt, 0x38U, ReadU32(corrupt, 0x38U) + 16U);
    CheckError(omega::retail::DecodeFrontEndTdx(corrupt), omega::asset::DecodeErrorCode::Malformed,
               "frontend TDX rejects a counted-length mismatch");

    corrupt = fixture;
    corrupt[kPrimaryObject + 0x05U] |= std::byte{0x40};
    CheckError(omega::retail::DecodeFrontEndTdx(corrupt), omega::asset::DecodeErrorCode::Malformed,
               "frontend TDX rejects a destination-address reserved-bit corruption");

    corrupt = fixture;
    WriteU16(corrupt, 0x16U, 1U);
    CheckError(omega::retail::DecodeFrontEndTdx(corrupt),
               omega::asset::DecodeErrorCode::UnsupportedVariant,
               "frontend TDX rejects an alternate palette storage format");

    corrupt = fixture;
    WriteU16(corrupt, 0x02U, 1U);
    CheckError(omega::retail::DecodeFrontEndTdx(corrupt),
               omega::asset::DecodeErrorCode::UnsupportedVariant,
               "frontend TDX rejects header flags without the proven alpha mode");

    corrupt = MakeFixture(omega::asset::IndexedImageEncoding::Indexed4);
    corrupt[kPaletteData + 64U] = std::byte{1};
    CheckError(omega::retail::DecodeFrontEndTdx(corrupt), omega::asset::DecodeErrorCode::Malformed,
               "frontend TDX rejects nonzero palette-slot padding");

    corrupt = fixture;
    corrupt.resize(corrupt.size() + 16U, std::byte{0});
    CheckError(omega::retail::DecodeFrontEndTdx(corrupt), omega::asset::DecodeErrorCode::Malformed,
               "frontend TDX rejects an all-zero trailing region");
    corrupt.back() = std::byte{1};
    CheckError(omega::retail::DecodeFrontEndTdx(corrupt), omega::asset::DecodeErrorCode::Malformed,
               "frontend TDX rejects a nonzero trailing region");

    corrupt = fixture;
    corrupt.resize(corrupt.size() - 16U);
    CheckError(omega::retail::DecodeFrontEndTdx(corrupt), omega::asset::DecodeErrorCode::Malformed,
               "frontend TDX rejects a truncated counted block");

    corrupt = fixture;
    WriteU32(corrupt, kHeaderBytes, 0xFFFFFFF0U);
    CheckError(omega::retail::DecodeFrontEndTdx(corrupt),
               omega::asset::DecodeErrorCode::UnsupportedVariant,
               "frontend TDX rejects an unsupported block pointer layout");
}
} // namespace

int main()
{
    CheckAddressVectors();
    CheckEndToEnd();
    CheckBudgets();
    CheckStrictRejections();
    if (failures == 0)
        std::cout << "omega_frontend_tdx_decoder_tests: all checks passed\n";
    return failures == 0 ? 0 : 1;
}
