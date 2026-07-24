#include "omega/retail/vum_visual_geometry_decoder.h"

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <vector>

namespace
{
int failures = 0;

void Check(const bool condition, const std::string_view message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

[[nodiscard]] bool Near(const float a, const float b, const float tol = 0.02F)
{
    return std::fabs(a - b) <= tol;
}

void AppendU8(std::vector<std::byte>& bytes, const std::uint8_t value)
{
    bytes.push_back(static_cast<std::byte>(value));
}

void AppendS16(std::vector<std::byte>& bytes, const std::int16_t value)
{
    const auto raw = static_cast<std::uint16_t>(value);
    bytes.push_back(static_cast<std::byte>(raw & 0xFFU));
    bytes.push_back(static_cast<std::byte>((raw >> 8U) & 0xFFU));
}

void AppendU32(std::vector<std::byte>& bytes, const std::uint32_t value)
{
    for (unsigned shift = 0U; shift < 32U; shift += 8U)
        bytes.push_back(static_cast<std::byte>((value >> shift) & 0xFFU));
}

void AppendF32(std::vector<std::byte>& bytes, const float value)
{
    AppendU32(bytes, std::bit_cast<std::uint32_t>(value));
}

// Real VIF UNPACK data is padded so the following code word stays 32-bit aligned.
void Pad4(std::vector<std::byte>& bytes)
{
    while ((bytes.size() % 4U) != 0U)
        bytes.push_back(std::byte{0});
}

// A VIF code word: [imm_lo, imm_hi, num, cmd].
void AppendVifCode(std::vector<std::byte>& bytes, const std::uint8_t cmd,
                   const std::uint8_t num)
{
    AppendU8(bytes, 0U); // immediate low
    AppendU8(bytes, 0U); // immediate high
    AppendU8(bytes, num);
    AppendU8(bytes, cmd);
}

// Emit one geometry batch as the level format encodes it, exercising the framing codes the decoder
// must skip to stay byte-synced (STMOD/STMASK/STROW between the anchors and the positions).
void AppendBatch(std::vector<std::byte>& bytes,
                 const float ax, const float ay, const float az, const float bx,
                 const float by, const float bz, const std::uint8_t count,
                 const bool with_framing)
{
    // V4-32 anchors (cmd 0x6C = vn=3, vl=0), num=2.
    AppendVifCode(bytes, 0x6CU, 2U);
    AppendF32(bytes, ax);
    AppendF32(bytes, ay);
    AppendF32(bytes, az);
    AppendF32(bytes, 1.0F);
    AppendF32(bytes, bx);
    AppendF32(bytes, by);
    AppendF32(bytes, bz);
    AppendF32(bytes, 1.0F);

    if (with_framing)
    {
        AppendVifCode(bytes, 0x05U, 1U);           // STMOD (immediate only)
        AppendVifCode(bytes, 0x20U, 0U);           // STMASK ...
        AppendU32(bytes, 0x40404040U);             // ... mask word
        AppendVifCode(bytes, 0x30U, 0U);           // STROW ...
        AppendU32(bytes, 0U);
        AppendU32(bytes, 0U);
        AppendU32(bytes, 0U);
        AppendU32(bytes, 0U);
    }

    // V3-16 positions (cmd 0x69 = vn=2, vl=1). Spread across the box corners.
    AppendVifCode(bytes, 0x69U, count);
    for (std::uint8_t index = 0U; index < count; ++index)
    {
        // vertex 0 -> box center; 1 -> +box; 2 -> -box; others between.
        std::int16_t s = 0;
        if (index == 1U)
            s = 32767;
        else if (index == 2U)
            s = -32768;
        else if (index == 3U)
            s = 16384;
        AppendS16(bytes, s);
        AppendS16(bytes, s);
        AppendS16(bytes, s);
    }
    Pad4(bytes);

    // V3-8 colors (cmd 0x6A = vn=2, vl=2).
    AppendVifCode(bytes, 0x6AU, count);
    for (std::uint8_t index = 0U; index < count; ++index)
    {
        AppendU8(bytes, 255U);
        AppendU8(bytes, 128U);
        AppendU8(bytes, 0U);
    }
    Pad4(bytes);

    // V2-16 UVs (cmd 0x65 = vn=1, vl=1).
    AppendVifCode(bytes, 0x65U, count);
    for (std::uint8_t index = 0U; index < count; ++index)
    {
        AppendS16(bytes, static_cast<std::int16_t>(index == 0U ? 4096 : 2994));
        AppendS16(bytes, static_cast<std::int16_t>(index == 0U ? 2048 : 3837));
    }
}
} // namespace

int main()
{
    // Single batch, 4 vertices, with the framing codes interleaved. Box: a0=(-10,-20,-2),
    // a1=(-6,-16,2) -> center=(-8,-18,0), halfext=(2,2,2).
    {
        std::vector<std::byte> stream;
        AppendBatch(stream, -10.0F, -20.0F, -2.0F, -6.0F, -16.0F, 2.0F, 4U, true);

        const auto decoded = omega::retail::DecodeVumVisualGeometryBatches(stream);
        Check(decoded.has_value(), "framed VIF batch decodes without error");
        if (decoded)
        {
            Check(decoded->meshes.size() == 1U, "one batch -> one mesh");
            if (decoded->meshes.size() == 1U)
            {
                const auto& mesh = decoded->meshes.front();
                Check(mesh.positions.size() == 4U && mesh.uvs.size() == 4U &&
                          mesh.colors.size() == 4U,
                      "mesh carries 4 positions/uvs/colors");
                if (mesh.positions.size() == 4U)
                {
                    // vertex 0 (s16=0) -> box center.
                    Check(Near(mesh.positions[0].x, -8.0F) && Near(mesh.positions[0].y, -18.0F) &&
                              Near(mesh.positions[0].z, 0.0F),
                          "V3-16=0 reconstructs to the box center (== collision anchor midpoint)");
                    // vertex 1 (s16=+full) -> +box corner.
                    Check(Near(mesh.positions[1].x, -6.0F) && Near(mesh.positions[1].y, -16.0F) &&
                              Near(mesh.positions[1].z, 2.0F),
                          "V3-16=+32767 reconstructs to the +box corner (== anchor a1)");
                    // vertex 2 (s16=-full) -> -box corner.
                    Check(Near(mesh.positions[2].x, -10.0F) && Near(mesh.positions[2].y, -20.0F) &&
                              Near(mesh.positions[2].z, -2.0F),
                          "V3-16=-32768 reconstructs to the -box corner (== anchor a0)");
                    // reconstructed bounds are inside the anchor box.
                    for (const auto& p : mesh.positions)
                    {
                        Check(p.x >= -10.01F && p.x <= -5.99F && p.z >= -2.01F && p.z <= 2.01F,
                              "every reconstructed vertex lies within the collision box");
                    }
                }
                if (mesh.uvs.size() == 4U)
                {
                    Check(Near(mesh.uvs[0][0], 1.0F) && Near(mesh.uvs[0][1], 0.5F),
                          "UV V2-16 4096/2048 -> (1.0, 0.5) via /4096");
                    Check(mesh.uvs[1][0] > 0.0F && mesh.uvs[1][0] < 1.0F && mesh.uvs[1][1] > 0.0F &&
                              mesh.uvs[1][1] < 1.0F,
                          "UVs land in [0,1]");
                }
                if (mesh.colors.size() == 4U)
                {
                    Check(Near(mesh.colors[0].x, 1.0F) && Near(mesh.colors[0].y, 0.502F) &&
                              Near(mesh.colors[0].z, 0.0F),
                          "color V3-8 (255,128,0) -> (1.0, ~0.5, 0.0) via /255");
                }
                // 4 vertices -> 2 strip triangles -> 6 indices, all in range.
                Check(mesh.triangle_indices.size() == 6U, "4-vertex strip -> 6 triangle indices");
                for (const auto index : mesh.triangle_indices)
                    Check(index < mesh.positions.size(), "triangle index in range");
            }
        }
    }

    // Two batches (a new V4-32 anchor flushes the previous batch); second batch has 3 vertices.
    {
        std::vector<std::byte> stream;
        AppendBatch(stream, -10.0F, -20.0F, -2.0F, -6.0F, -16.0F, 2.0F, 4U, false);
        AppendBatch(stream, 2.0F, 2.0F, 2.0F, 6.0F, 6.0F, 6.0F, 3U, false);
        const auto decoded = omega::retail::DecodeVumVisualGeometryBatches(stream);
        Check(decoded.has_value() && decoded->meshes.size() == 2U,
              "a new V4-32 anchor starts a second batch -> two meshes");
        if (decoded && decoded->meshes.size() == 2U)
        {
            Check(decoded->meshes[1].positions.size() == 3U &&
                      decoded->meshes[1].triangle_indices.size() == 3U,
                  "second batch has 3 vertices -> 1 triangle");
        }
    }

    // Empty / truncated input is fail-soft (no meshes, no error).
    {
        const std::vector<std::byte> empty;
        const auto decoded = omega::retail::DecodeVumVisualGeometryBatches(empty);
        Check(decoded.has_value() && decoded->meshes.empty(),
              "empty payload decodes to zero meshes, fail-soft");
    }

    if (failures != 0)
    {
        std::cerr << failures << " vum visual geometry decoder test(s) failed\n";
        return 1;
    }
    std::cout << "vum visual geometry decoder tests passed\n";
    return 0;
}
