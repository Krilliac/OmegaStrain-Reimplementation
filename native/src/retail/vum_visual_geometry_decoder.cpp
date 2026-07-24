#include "omega/retail/vum_visual_geometry_decoder.h"

#include "vum_layout_internal.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <utility>
#include <vector>

namespace omega::retail
{
namespace
{
using asset::DecodeError;
using asset::DecodeErrorCode;
using asset::DecodeLimits;
using asset::Float3IR;

[[nodiscard]] DecodeError Error(std::string message)
{
    return DecodeError{.code = DecodeErrorCode::Malformed,
                       .byte_offset = std::nullopt,
                       .message = std::move(message)};
}

[[nodiscard]] std::uint32_t ReadU32(const std::span<const std::byte> bytes,
                                    const std::size_t offset) noexcept
{
    return static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset])) |
           (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset + 1U])) << 8U) |
           (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset + 2U])) << 16U) |
           (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset + 3U])) << 24U);
}

[[nodiscard]] float ReadF32(const std::span<const std::byte> bytes,
                            const std::size_t offset) noexcept
{
    return std::bit_cast<float>(ReadU32(bytes, offset));
}

[[nodiscard]] std::int16_t ReadS16(const std::span<const std::byte> bytes,
                                   const std::size_t offset) noexcept
{
    return static_cast<std::int16_t>(
        static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(bytes[offset])) |
        (static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(bytes[offset + 1U])) << 8U));
}

// One VIF UNPACK: bytes-per-component for the vl field, and component count for vn.
[[nodiscard]] std::uint32_t VlBytes(const std::uint8_t vl) noexcept
{
    switch (vl)
    {
    case 0U:
        return 4U; // 32-bit
    case 1U:
        return 2U; // 16-bit
    case 2U:
        return 1U; // 8-bit
    default:
        return 2U; // v5 packed -- not used by level geometry; keep the walk synced
    }
}

[[nodiscard]] std::uint32_t AlignUp4(const std::uint32_t value) noexcept
{
    return (value + 3U) & ~static_cast<std::uint32_t>(3U);
}

// Accumulated attribute blocks for one geometry batch, in stream order.
struct BatchState
{
    std::array<Float3IR, 2> anchors{};
    bool has_anchors = false;
    std::vector<std::array<std::int16_t, 3>> raw_positions; // V3-16 box fractions
    std::vector<std::array<std::uint8_t, 3>> colors;        // first V3-8 block
    std::vector<std::array<std::int16_t, 2>> uvs;           // V2-16
};

[[nodiscard]] std::optional<VumVisualMeshIR> FlushBatch(const BatchState& batch)
{
    if (!batch.has_anchors || batch.raw_positions.size() < 3U || batch.uvs.empty())
        return std::nullopt;

    const std::size_t vertex_count =
        std::min(batch.raw_positions.size(), batch.uvs.size());
    if (vertex_count < 3U)
        return std::nullopt;

    const Float3IR a0 = batch.anchors[0];
    const Float3IR a1 = batch.anchors[1];
    const Float3IR center{
        .x = (a0.x + a1.x) * 0.5F,
        .y = (a0.y + a1.y) * 0.5F,
        .z = (a0.z + a1.z) * 0.5F,
    };
    const Float3IR halfext{
        .x = std::fabs(a1.x - a0.x) * 0.5F,
        .y = std::fabs(a1.y - a0.y) * 0.5F,
        .z = std::fabs(a1.z - a0.z) * 0.5F,
    };

    VumVisualMeshIR mesh;
    mesh.positions.reserve(vertex_count);
    mesh.uvs.reserve(vertex_count);
    mesh.colors.reserve(vertex_count);
    for (std::size_t index = 0U; index < vertex_count; ++index)
    {
        const auto& raw = batch.raw_positions[index];
        // pos_c = center_c + (s16 / 32768) * halfext_c (validated: bounds == collision cell).
        mesh.positions.push_back(Float3IR{
            .x = center.x + (static_cast<float>(raw[0]) / 32768.0F) * halfext.x,
            .y = center.y + (static_cast<float>(raw[1]) / 32768.0F) * halfext.y,
            .z = center.z + (static_cast<float>(raw[2]) / 32768.0F) * halfext.z,
        });
        const auto& uv = batch.uvs[index];
        mesh.uvs.push_back(std::array<float, 2>{
            static_cast<float>(uv[0]) / 4096.0F, static_cast<float>(uv[1]) / 4096.0F});
        if (index < batch.colors.size())
        {
            const auto& color = batch.colors[index];
            mesh.colors.push_back(Float3IR{
                .x = static_cast<float>(color[0]) / 255.0F,
                .y = static_cast<float>(color[1]) / 255.0F,
                .z = static_cast<float>(color[2]) / 255.0F,
            });
        }
        else
        {
            mesh.colors.push_back(Float3IR{.x = 1.0F, .y = 1.0F, .z = 1.0F});
        }
    }

    // One triangle strip per batch (strip-break topology is a documented follow-up). Winding is
    // flipped on odd triangles to keep a consistent front face across the strip.
    for (std::uint32_t index = 0U;
         index + 2U < static_cast<std::uint32_t>(vertex_count); ++index)
    {
        if ((index & 1U) == 0U)
        {
            mesh.triangle_indices.push_back(index);
            mesh.triangle_indices.push_back(index + 1U);
            mesh.triangle_indices.push_back(index + 2U);
        }
        else
        {
            mesh.triangle_indices.push_back(index + 1U);
            mesh.triangle_indices.push_back(index);
            mesh.triangle_indices.push_back(index + 2U);
        }
    }
    if (mesh.triangle_indices.empty())
        return std::nullopt;
    return mesh;
}
} // namespace

asset::DecodeResult<VumVisualGeometryIR> DecodeVumVisualGeometryBatches(
    const std::span<const std::byte> final_payload, const DecodeLimits limits)
{
    VumVisualGeometryIR out;
    const std::size_t size = final_payload.size();
    std::size_t pos = 0U;
    BatchState batch;
    std::uint64_t total_vertices = 0U;

    const auto flush = [&batch, &out]() {
        auto mesh = FlushBatch(batch);
        if (mesh)
            out.meshes.push_back(std::move(*mesh));
        batch = BatchState{};
    };

    // VIF stream walk. We handle the codes level geometry uses; the framing codes (STMOD/STMASK/
    // STROW/STCYCL/FLUSH/MSCAL) are handled only to keep the walk byte-synced -- their register
    // semantics are not applied (the face-value box reconstruction already matches collision bounds).
    while (pos + 4U <= size)
    {
        const std::uint8_t immediate_low = std::to_integer<std::uint8_t>(final_payload[pos]);
        const std::uint8_t immediate_high = std::to_integer<std::uint8_t>(final_payload[pos + 1U]);
        const std::uint8_t num = std::to_integer<std::uint8_t>(final_payload[pos + 2U]);
        const std::uint8_t cmd =
            std::to_integer<std::uint8_t>(final_payload[pos + 3U]) & 0x7FU; // drop interrupt bit
        (void)immediate_low;
        (void)immediate_high;
        pos += 4U;

        if (cmd == 0x00U)
            continue; // NOP
        if (cmd == 0x01U || cmd == 0x02U || cmd == 0x03U || cmd == 0x04U || cmd == 0x05U ||
            cmd == 0x06U || cmd == 0x07U)
            continue; // STCYCL/OFFSET/BASE/ITOP/STMOD/MSKPATH3/MARK -- immediate only
        if (cmd >= 0x10U && cmd <= 0x17U)
            continue; // FLUSH family / MSCAL / MSCNT -- no inline data we consume
        if (cmd == 0x20U)
        {
            pos += 4U; // STMASK: mask word follows
            continue;
        }
        if (cmd == 0x30U || cmd == 0x31U)
        {
            pos += 16U; // STROW / STCOL: four 32-bit words follow
            continue;
        }
        if (cmd >= 0x60U)
        {
            // UNPACK. cmd = 0b011 m vn vl.
            const std::uint8_t vn = static_cast<std::uint8_t>((cmd >> 2U) & 0x3U);
            const std::uint8_t vl = static_cast<std::uint8_t>(cmd & 0x3U);
            const std::uint32_t components = static_cast<std::uint32_t>(vn) + 1U;
            const std::uint32_t element_bytes = VlBytes(vl);
            const std::uint32_t data_bytes =
                AlignUp4(static_cast<std::uint32_t>(num) * components * element_bytes);
            if (pos + data_bytes > size)
                break; // truncated -- stop, keep coherent batches
            const std::size_t data = pos;

            if (vn == 3U && vl == 0U) // V4-32 -> anchors, start of a batch
            {
                if (batch.has_anchors || !batch.raw_positions.empty())
                    flush();
                std::array<Float3IR, 2> anchors{};
                std::uint32_t found = 0U;
                for (std::uint32_t vertex = 0U; vertex < num && found < 2U; ++vertex)
                {
                    const std::size_t voff = data + static_cast<std::size_t>(vertex) * 16U;
                    const Float3IR v{.x = ReadF32(final_payload, voff),
                                     .y = ReadF32(final_payload, voff + 4U),
                                     .z = ReadF32(final_payload, voff + 8U)};
                    if (v.x != 0.0F || v.y != 0.0F || v.z != 0.0F)
                        anchors[found++] = v;
                }
                if (found >= 2U)
                {
                    batch.anchors = anchors;
                    batch.has_anchors = true;
                }
            }
            else if (vn == 2U && vl == 1U) // V3-16 -> positions
            {
                total_vertices += num;
                if (total_vertices > limits.maximum_items)
                    return std::unexpected(Error("vum visual geometry exceeds the item limit"));
                batch.raw_positions.clear();
                batch.raw_positions.reserve(num);
                for (std::uint32_t vertex = 0U; vertex < num; ++vertex)
                {
                    const std::size_t voff = data + static_cast<std::size_t>(vertex) * 6U;
                    batch.raw_positions.push_back(std::array<std::int16_t, 3>{
                        ReadS16(final_payload, voff), ReadS16(final_payload, voff + 2U),
                        ReadS16(final_payload, voff + 4U)});
                }
            }
            else if (vn == 2U && vl == 2U) // V3-8 -> colors (keep the first block per batch)
            {
                if (batch.colors.empty())
                {
                    batch.colors.reserve(num);
                    for (std::uint32_t vertex = 0U; vertex < num; ++vertex)
                    {
                        const std::size_t voff = data + static_cast<std::size_t>(vertex) * 3U;
                        batch.colors.push_back(std::array<std::uint8_t, 3>{
                            std::to_integer<std::uint8_t>(final_payload[voff]),
                            std::to_integer<std::uint8_t>(final_payload[voff + 1U]),
                            std::to_integer<std::uint8_t>(final_payload[voff + 2U])});
                    }
                }
            }
            else if (vn == 1U && vl == 1U) // V2-16 -> UVs
            {
                batch.uvs.clear();
                batch.uvs.reserve(num);
                for (std::uint32_t vertex = 0U; vertex < num; ++vertex)
                {
                    const std::size_t voff = data + static_cast<std::size_t>(vertex) * 4U;
                    batch.uvs.push_back(std::array<std::int16_t, 2>{
                        ReadS16(final_payload, voff), ReadS16(final_payload, voff + 2U)});
                }
            }
            // Other UNPACK shapes are skipped by data size (walk stays synced).
            pos += data_bytes;
            continue;
        }
        // Unknown VIF command: cannot know its data size -> stop, keep coherent batches.
        break;
    }
    flush();
    return out;
}

asset::DecodeResult<VumVisualGeometryIR> DecodeVumVisualGeometry(
    const std::span<const std::byte> vum_bytes, const DecodeLimits limits)
{
    const auto layout = detail::ValidateVumPayloadLayout(vum_bytes, limits);
    if (!layout)
        return std::unexpected(layout.error());
    if (layout->final_payload_begin > layout->primary_end ||
        layout->primary_end > vum_bytes.size())
        return std::unexpected(Error("vum final payload region is out of range"));
    const std::span<const std::byte> final_payload = vum_bytes.subspan(
        layout->final_payload_begin,
        static_cast<std::size_t>(layout->primary_end - layout->final_payload_begin));
    return DecodeVumVisualGeometryBatches(final_payload, limits);
}
} // namespace omega::retail
