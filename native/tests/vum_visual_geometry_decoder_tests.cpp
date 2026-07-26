#include "omega/retail/vum_visual_geometry_decoder.h"

#include <array>
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

template <typename Value>
void CheckError(const omega::asset::DecodeResult<Value>& result,
                const omega::asset::DecodeErrorCode expected_code,
                const std::string_view message)
{
    Check(!result.has_value(), message);
    if (!result)
    {
        Check(result.error().code == expected_code, "decoder failure uses the expected category");
        Check(!result.error().byte_offset.has_value(),
              "decoder failure does not disclose a source offset");
    }
}

template <typename Value>
void CheckLimitError(const omega::asset::DecodeResult<Value>& result,
                     const std::string_view message)
{
    CheckError(result, omega::asset::DecodeErrorCode::LimitExceeded, message);
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
void AppendVifCodeWithImmediate(std::vector<std::byte>& bytes, const std::uint8_t cmd,
                                const std::uint8_t num, const std::uint16_t immediate)
{
    AppendU8(bytes, static_cast<std::uint8_t>(immediate & 0xFFU));
    AppendU8(bytes, static_cast<std::uint8_t>(immediate >> 8U));
    AppendU8(bytes, num);
    AppendU8(bytes, cmd);
}

void AppendVifCode(std::vector<std::byte>& bytes, const std::uint8_t cmd,
                   const std::uint8_t num)
{
    AppendVifCodeWithImmediate(bytes, cmd, num, 0U);
}

// Emit one geometry batch as the level format encodes it, exercising the framing codes the decoder
// must skip to stay byte-synced (STMOD/STMASK/STROW between the anchors and the positions).
void AppendBatch(std::vector<std::byte>& bytes,
                 const float ax, const float ay, const float az, const float bx,
                 const float by, const float bz, const std::uint16_t count,
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
    const auto encoded_count = static_cast<std::uint8_t>(count == 256U ? 0U : count);
    AppendVifCode(bytes, 0x69U, encoded_count);
    for (std::uint16_t index = 0U; index < count; ++index)
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
    AppendVifCode(bytes, 0x6AU, encoded_count);
    for (std::uint16_t index = 0U; index < count; ++index)
    {
        AppendU8(bytes, 255U);
        AppendU8(bytes, 128U);
        AppendU8(bytes, 0U);
    }
    Pad4(bytes);

    // V2-16 UVs (cmd 0x65 = vn=1, vl=1).
    AppendVifCode(bytes, 0x65U, encoded_count);
    for (std::uint16_t index = 0U; index < count; ++index)
    {
        AppendS16(bytes, static_cast<std::int16_t>(index == 0U ? 4096 : 2994));
        AppendS16(bytes, static_cast<std::int16_t>(index == 0U ? 2048 : 3837));
    }
}

[[nodiscard]] constexpr std::uint64_t TriangleIndexCount(const std::uint64_t vertex_count)
{
    return vertex_count >= 3U ? (vertex_count - 2U) * 3U : 0U;
}

[[nodiscard]] constexpr std::uint64_t RetainedItemsForBatch(
    const std::uint64_t vertex_count)
{
    return 1U + vertex_count * 3U + TriangleIndexCount(vertex_count);
}

[[nodiscard]] constexpr std::uint64_t RetainedBytesForBatch(
    const std::uint64_t vertex_count)
{
    return sizeof(omega::retail::VumVisualMeshIR) +
           vertex_count * sizeof(omega::asset::Float3IR) +
           vertex_count * sizeof(std::array<float, 2>) +
           vertex_count * sizeof(omega::asset::Float3IR) +
           TriangleIndexCount(vertex_count) * sizeof(std::uint32_t);
}

[[nodiscard]] constexpr std::uint64_t ScratchBytesForBatch(
    const std::uint64_t vertex_count)
{
    return vertex_count * sizeof(std::array<std::int16_t, 3>) +
           vertex_count * sizeof(std::array<std::uint8_t, 3>) +
           vertex_count * sizeof(std::array<std::int16_t, 2>);
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

    // VIF encodes 256 UNPACK vectors as NUM=0. The zero byte must not become an empty block.
    {
        constexpr std::uint64_t kVertexCount = 256U;
        std::vector<std::byte> stream;
        AppendBatch(stream, -10.0F, -20.0F, -2.0F, -6.0F, -16.0F, 2.0F,
                    static_cast<std::uint16_t>(kVertexCount), false);

        auto limits = omega::asset::DecodeLimits{};
        limits.maximum_input_bytes = stream.size();
        limits.maximum_items = RetainedItemsForBatch(kVertexCount);
        limits.maximum_output_bytes =
            sizeof(omega::retail::VumVisualGeometryIR) + RetainedBytesForBatch(kVertexCount);
        limits.maximum_scratch_bytes = ScratchBytesForBatch(kVertexCount);
        const auto decoded = omega::retail::DecodeVumVisualGeometryBatches(stream, limits);
        Check(decoded.has_value() && decoded->meshes.size() == 1U,
              "NUM=0 decodes as one 256-vector batch under exact budgets");
        if (decoded && decoded->meshes.size() == 1U)
        {
            Check(decoded->meshes[0].positions.size() == kVertexCount &&
                      decoded->meshes[0].triangle_indices.size() ==
                          TriangleIndexCount(kVertexCount),
                  "NUM=0 retains all 256 vertices and their complete triangle strip");
        }
    }

    // Non-fill STCYCL remains byte-sized; fill mode and packed V4_5 fail closed before an
    // unsupported payload can be mistaken for subsequent VIF code words.
    {
        std::vector<std::byte> non_fill;
        AppendBatch(non_fill, -10.0F, -20.0F, -2.0F, -6.0F, -16.0F, 2.0F, 4U, false);
        AppendVifCodeWithImmediate(non_fill, 0x01U, 0U, 0x0102U); // CL=2, WL=1
        AppendBatch(non_fill, 2.0F, 2.0F, 2.0F, 6.0F, 6.0F, 6.0F, 3U, false);
        const auto non_fill_decoded =
            omega::retail::DecodeVumVisualGeometryBatches(non_fill);
        Check(non_fill_decoded.has_value() && non_fill_decoded->meshes.size() == 2U,
              "non-fill STCYCL preserves byte sync across the next batch");

        std::vector<std::byte> fill;
        AppendBatch(fill, -10.0F, -20.0F, -2.0F, -6.0F, -16.0F, 2.0F, 4U, false);
        AppendVifCodeWithImmediate(fill, 0x01U, 0U, 0x0201U); // CL=1, WL=2
        AppendBatch(fill, 2.0F, 2.0F, 2.0F, 6.0F, 6.0F, 6.0F, 3U, false);
        const auto fill_decoded = omega::retail::DecodeVumVisualGeometryBatches(fill);
        CheckError(fill_decoded, omega::asset::DecodeErrorCode::UnsupportedVariant,
                   "fill-mode STCYCL fails closed before the following UNPACK payload");

        std::vector<std::byte> packed;
        AppendBatch(packed, -10.0F, -20.0F, -2.0F, -6.0F, -16.0F, 2.0F, 4U, false);
        AppendVifCode(packed, 0x6FU, 1U); // packed V4_5: two payload bytes per vector
        AppendU8(packed, 0xFFU);
        AppendU8(packed, 0x7FU);
        Pad4(packed);
        AppendBatch(packed, 2.0F, 2.0F, 2.0F, 6.0F, 6.0F, 6.0F, 3U, false);
        const auto packed_decoded = omega::retail::DecodeVumVisualGeometryBatches(packed);
        CheckError(packed_decoded, omega::asset::DecodeErrorCode::UnsupportedVariant,
                   "packed V4_5 fails closed before its payload can desynchronize the walk");
    }

    // Empty / truncated input is fail-soft (no meshes, no error).
    {
        const std::vector<std::byte> empty;
        const auto decoded = omega::retail::DecodeVumVisualGeometryBatches(empty);
        Check(decoded.has_value() && decoded->meshes.empty(),
              "empty payload decodes to zero meshes, fail-soft");

        auto limits = omega::asset::DecodeLimits{};
        limits.maximum_input_bytes = 0U;
        limits.maximum_items = 0U;
        limits.maximum_scratch_bytes = 0U;
        limits.maximum_output_bytes = sizeof(omega::retail::VumVisualGeometryIR);
        Check(omega::retail::DecodeVumVisualGeometryBatches(empty, limits).has_value(),
              "empty payload accepts zero input, item, and scratch budgets plus the exact root");

        limits.maximum_output_bytes = sizeof(omega::retail::VumVisualGeometryIR) - 1U;
        CheckLimitError(omega::retail::DecodeVumVisualGeometryBatches(empty, limits),
                        "empty payload rejects one byte below its retained root");
    }

    // Every caller budget accepts its exact single-batch footprint and rejects one below it.
    {
        constexpr std::uint64_t kVertexCount = 4U;
        constexpr std::uint64_t kRetainedItems = RetainedItemsForBatch(kVertexCount);
        constexpr std::uint64_t kRetainedBytes =
            sizeof(omega::retail::VumVisualGeometryIR) + RetainedBytesForBatch(kVertexCount);
        constexpr std::uint64_t kScratchBytes = ScratchBytesForBatch(kVertexCount);

        std::vector<std::byte> stream;
        AppendBatch(stream, -10.0F, -20.0F, -2.0F, -6.0F, -16.0F, 2.0F,
                    static_cast<std::uint8_t>(kVertexCount), true);

        auto limits = omega::asset::DecodeLimits{};
        limits.maximum_input_bytes = stream.size();
        limits.maximum_items = kRetainedItems;
        limits.maximum_output_bytes = kRetainedBytes;
        limits.maximum_scratch_bytes = kScratchBytes;
        Check(omega::retail::DecodeVumVisualGeometryBatches(stream, limits).has_value(),
              "single batch accepts exact input, item, output, and scratch budgets");

        auto below = limits;
        below.maximum_input_bytes -= 1U;
        CheckLimitError(omega::retail::DecodeVumVisualGeometryBatches(stream, below),
                        "single batch rejects one byte below its exact input budget");

        below = limits;
        below.maximum_items -= 1U;
        CheckLimitError(omega::retail::DecodeVumVisualGeometryBatches(stream, below),
                        "single batch rejects one item below its retained item count");

        below = limits;
        below.maximum_items = kVertexCount * 3U - 1U;
        CheckLimitError(omega::retail::DecodeVumVisualGeometryBatches(stream, below),
                        "parsed position, color, and UV items are bounded before the UV reserve");

        below = limits;
        below.maximum_output_bytes -= 1U;
        CheckLimitError(omega::retail::DecodeVumVisualGeometryBatches(stream, below),
                        "single batch rejects one byte below its retained output footprint");

        below = limits;
        below.maximum_scratch_bytes -= 1U;
        CheckLimitError(omega::retail::DecodeVumVisualGeometryBatches(stream, below),
                        "single batch rejects one byte below its peak BatchState storage");

        below = limits;
        below.maximum_scratch_bytes = 0U;
        CheckLimitError(omega::retail::DecodeVumVisualGeometryBatches(stream, below),
                        "non-empty batch rejects a zero scratch budget before reserving");
    }

    // Output and item budgets remain cumulative across flushes rather than resetting per batch.
    {
        constexpr std::uint64_t kFirstVertexCount = 4U;
        constexpr std::uint64_t kSecondVertexCount = 3U;
        constexpr std::uint64_t kRetainedItems =
            RetainedItemsForBatch(kFirstVertexCount) +
            RetainedItemsForBatch(kSecondVertexCount);
        constexpr std::uint64_t kRetainedBytes =
            sizeof(omega::retail::VumVisualGeometryIR) +
            RetainedBytesForBatch(kFirstVertexCount) +
            RetainedBytesForBatch(kSecondVertexCount);

        std::vector<std::byte> stream;
        AppendBatch(stream, -10.0F, -20.0F, -2.0F, -6.0F, -16.0F, 2.0F,
                    static_cast<std::uint8_t>(kFirstVertexCount), false);
        AppendBatch(stream, 2.0F, 2.0F, 2.0F, 6.0F, 6.0F, 6.0F,
                    static_cast<std::uint8_t>(kSecondVertexCount), false);

        auto limits = omega::asset::DecodeLimits{};
        limits.maximum_items = kRetainedItems;
        limits.maximum_output_bytes = kRetainedBytes;
        limits.maximum_scratch_bytes = ScratchBytesForBatch(kFirstVertexCount);
        Check(omega::retail::DecodeVumVisualGeometryBatches(stream, limits).has_value(),
              "two batches accept their exact cumulative item and output budgets");

        auto below = limits;
        below.maximum_items -= 1U;
        CheckLimitError(omega::retail::DecodeVumVisualGeometryBatches(stream, below),
                        "second flush rejects one below the cumulative item budget");

        below = limits;
        below.maximum_output_bytes -= 1U;
        CheckLimitError(omega::retail::DecodeVumVisualGeometryBatches(stream, below),
                        "second flush rejects one below the cumulative output budget");
    }

    if (failures != 0)
    {
        std::cerr << failures << " vum visual geometry decoder test(s) failed\n";
        return 1;
    }
    std::cout << "vum visual geometry decoder tests passed\n";
    return 0;
}
