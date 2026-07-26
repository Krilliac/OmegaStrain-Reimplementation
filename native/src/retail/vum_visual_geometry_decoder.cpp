#include "omega/retail/vum_visual_geometry_decoder.h"

#include "vum_layout_internal.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <string>
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

[[nodiscard]] DecodeError Error(const DecodeErrorCode code, std::string message)
{
    return DecodeError{.code = code,
                       .byte_offset = std::nullopt,
                       .message = std::move(message)};
}

[[nodiscard]] DecodeError Error(std::string message)
{
    return Error(DecodeErrorCode::Malformed, std::move(message));
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
    if (left != 0U && right > std::numeric_limits<std::uint64_t>::max() / left)
        return false;
    result = left * right;
    return true;
}

[[nodiscard]] asset::DecodeResult<void> Accumulate(std::uint64_t& total,
                                                   const std::uint64_t amount,
                                                   const std::uint64_t limit,
                                                   const char* overflow_message,
                                                   const char* limit_message)
{
    std::uint64_t next = 0U;
    if (!Add(total, amount, next))
        return std::unexpected(Error(DecodeErrorCode::Overflow, overflow_message));
    if (next > limit)
        return std::unexpected(Error(DecodeErrorCode::LimitExceeded, limit_message));
    total = next;
    return {};
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
        return 0U; // packed V4_5 is rejected before this helper is called
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

struct BatchShape
{
    std::size_t vertex_count = 0U;
    std::uint64_t triangle_index_count = 0U;
};

struct RetainedBudget
{
    std::uint64_t items = 0U;
    std::uint64_t output_bytes = 0U;
};

[[nodiscard]] asset::DecodeResult<void> CheckBatchScratchBudget(
    const std::uint64_t position_count, const std::uint64_t color_count,
    const std::uint64_t uv_count, const DecodeLimits limits)
{
    constexpr std::array<std::uint64_t, 3> element_bytes{
        sizeof(std::array<std::int16_t, 3>),
        sizeof(std::array<std::uint8_t, 3>),
        sizeof(std::array<std::int16_t, 2>),
    };
    const std::array<std::uint64_t, 3> counts{position_count, color_count, uv_count};
    std::uint64_t scratch_bytes = 0U;
    for (std::size_t index = 0U; index < counts.size(); ++index)
    {
        std::uint64_t bytes = 0U;
        if (!Multiply(counts[index], element_bytes[index], bytes) ||
            !Add(scratch_bytes, bytes, scratch_bytes))
        {
            return std::unexpected(
                Error(DecodeErrorCode::Overflow, "vum visual geometry scratch size overflows"));
        }
    }
    if (scratch_bytes > limits.maximum_scratch_bytes)
    {
        return std::unexpected(Error(DecodeErrorCode::LimitExceeded,
            "vum visual geometry batch exceeds decoder scratch limit"));
    }
    return {};
}

[[nodiscard]] asset::DecodeResult<std::optional<BatchShape>> DescribeBatch(
    const BatchState& batch)
{
    if (!batch.has_anchors || batch.raw_positions.size() < 3U || batch.uvs.empty())
        return std::nullopt;

    const std::size_t vertex_count =
        std::min(batch.raw_positions.size(), batch.uvs.size());
    if (vertex_count < 3U)
        return std::nullopt;

    std::uint64_t triangle_index_count = 0U;
    if (!Multiply(static_cast<std::uint64_t>(vertex_count - 2U), 3U, triangle_index_count))
    {
        return std::unexpected(Error(
            DecodeErrorCode::Overflow, "vum visual geometry triangle count overflows"));
    }
    return BatchShape{
        .vertex_count = vertex_count,
        .triangle_index_count = triangle_index_count,
    };
}

[[nodiscard]] asset::DecodeResult<RetainedBudget> ProjectRetainedBudget(
    const RetainedBudget current, const BatchShape shape, const DecodeLimits limits)
{
    RetainedBudget next = current;

    auto item_result = Accumulate(next.items, 1U, limits.maximum_items,
        "vum visual geometry decoded item count overflows",
        "vum visual geometry items exceed decoder limit");
    if (!item_result)
        return std::unexpected(item_result.error());

    std::uint64_t vertex_items = 0U;
    if (!Multiply(static_cast<std::uint64_t>(shape.vertex_count), 3U, vertex_items))
    {
        return std::unexpected(Error(
            DecodeErrorCode::Overflow, "vum visual geometry decoded item count overflows"));
    }
    item_result = Accumulate(next.items, vertex_items, limits.maximum_items,
        "vum visual geometry decoded item count overflows",
        "vum visual geometry items exceed decoder limit");
    if (!item_result)
        return std::unexpected(item_result.error());
    item_result = Accumulate(next.items, shape.triangle_index_count, limits.maximum_items,
        "vum visual geometry decoded item count overflows",
        "vum visual geometry items exceed decoder limit");
    if (!item_result)
        return std::unexpected(item_result.error());

    auto output_result = Accumulate(next.output_bytes, sizeof(VumVisualMeshIR),
        limits.maximum_output_bytes, "vum visual geometry decoded output size overflows",
        "vum visual geometry exceeds decoder output limit");
    if (!output_result)
        return std::unexpected(output_result.error());

    constexpr std::array<std::uint64_t, 3> vertex_element_bytes{
        sizeof(Float3IR), sizeof(std::array<float, 2>), sizeof(Float3IR)};
    for (const std::uint64_t element_bytes : vertex_element_bytes)
    {
        std::uint64_t bytes = 0U;
        if (!Multiply(static_cast<std::uint64_t>(shape.vertex_count), element_bytes, bytes))
        {
            return std::unexpected(Error(
                DecodeErrorCode::Overflow, "vum visual geometry decoded output size overflows"));
        }
        output_result = Accumulate(next.output_bytes, bytes, limits.maximum_output_bytes,
            "vum visual geometry decoded output size overflows",
            "vum visual geometry exceeds decoder output limit");
        if (!output_result)
            return std::unexpected(output_result.error());
    }

    std::uint64_t triangle_bytes = 0U;
    if (!Multiply(shape.triangle_index_count, sizeof(std::uint32_t), triangle_bytes))
    {
        return std::unexpected(Error(
            DecodeErrorCode::Overflow, "vum visual geometry decoded output size overflows"));
    }
    output_result = Accumulate(next.output_bytes, triangle_bytes, limits.maximum_output_bytes,
        "vum visual geometry decoded output size overflows",
        "vum visual geometry exceeds decoder output limit");
    if (!output_result)
        return std::unexpected(output_result.error());
    return next;
}

[[nodiscard]] VumVisualMeshIR FlushBatch(const BatchState& batch, const BatchShape shape)
{
    const std::size_t vertex_count = shape.vertex_count;
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
    mesh.triangle_indices.reserve(static_cast<std::size_t>(shape.triangle_index_count));
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
    return mesh;
}
} // namespace

asset::DecodeResult<VumVisualGeometryIR> DecodeVumVisualGeometryBatches(
    const std::span<const std::byte> final_payload, const DecodeLimits limits)
{
    if (final_payload.size() > limits.maximum_input_bytes)
    {
        return std::unexpected(Error(DecodeErrorCode::LimitExceeded,
            "vum visual geometry input exceeds decoder byte limit"));
    }

    VumVisualGeometryIR out;
    const std::size_t size = final_payload.size();
    std::size_t pos = 0U;
    BatchState batch;
    std::uint64_t parsed_items = 0U;
    RetainedBudget retained{
        .items = 0U,
        .output_bytes = sizeof(VumVisualGeometryIR),
    };
    if (retained.output_bytes > limits.maximum_output_bytes)
    {
        return std::unexpected(Error(DecodeErrorCode::LimitExceeded,
            "vum visual geometry root exceeds decoder output limit"));
    }

    const auto flush = [&batch, &out, &retained, limits]() -> asset::DecodeResult<void> {
        const auto shape = DescribeBatch(batch);
        if (!shape)
            return std::unexpected(shape.error());
        if (!*shape)
        {
            batch = BatchState{};
            return {};
        }

        const auto projected = ProjectRetainedBudget(retained, **shape, limits);
        if (!projected)
            return std::unexpected(projected.error());
        out.meshes.push_back(FlushBatch(batch, **shape));
        retained = *projected;
        batch = BatchState{};
        return {};
    };

    // VIF stream walk. Framing commands are consumed only where their inline size is fixed.
    // Non-fill STCYCL preserves source-vector sizing; fill mode and packed V4_5 fail closed before
    // any unsupported payload can desynchronize the walk.
    while (pos <= size && size - pos >= 4U)
    {
        const std::uint8_t immediate_low = std::to_integer<std::uint8_t>(final_payload[pos]);
        const std::uint8_t immediate_high = std::to_integer<std::uint8_t>(final_payload[pos + 1U]);
        const std::uint8_t num = std::to_integer<std::uint8_t>(final_payload[pos + 2U]);
        const std::uint8_t cmd =
            std::to_integer<std::uint8_t>(final_payload[pos + 3U]) & 0x7FU; // drop interrupt bit
        pos += 4U;

        if (cmd == 0x00U)
            continue; // NOP
        if (cmd == 0x01U)
        {
            // STCYCL stores zero as 256. Skip mode (WL <= CL) consumes every source vector, which
            // preserves this stream-order decoder's byte sizing. Fill mode consumes fewer source
            // vectors than NUM and requires VU destination semantics we do not model, so reject it
            // before interpreting any following UNPACK payload under a false size.
            const std::uint32_t cycle_length = immediate_low == 0U ? 256U : immediate_low;
            const std::uint32_t write_length = immediate_high == 0U ? 256U : immediate_high;
            if (write_length > cycle_length)
            {
                return std::unexpected(Error(DecodeErrorCode::UnsupportedVariant,
                    "vum visual geometry does not support fill-mode STCYCL"));
            }
            continue;
        }
        if (cmd == 0x02U || cmd == 0x03U || cmd == 0x04U || cmd == 0x05U || cmd == 0x06U ||
            cmd == 0x07U)
            continue; // OFFSET/BASE/ITOP/STMOD/MSKPATH3/MARK -- immediate only
        if (cmd >= 0x10U && cmd <= 0x17U)
            continue; // FLUSH family / MSCAL / MSCNT -- no inline data we consume
        if (cmd == 0x20U)
        {
            if (size - pos < 4U)
                break;
            pos += 4U; // STMASK: mask word follows
            continue;
        }
        if (cmd == 0x30U || cmd == 0x31U)
        {
            if (size - pos < 16U)
                break;
            pos += 16U; // STROW / STCOL: four 32-bit words follow
            continue;
        }
        if (cmd >= 0x60U)
        {
            // UNPACK. cmd = 0b011 m vn vl.
            const std::uint8_t vn = static_cast<std::uint8_t>((cmd >> 2U) & 0x3U);
            const std::uint8_t vl = static_cast<std::uint8_t>(cmd & 0x3U);
            // VL=3 is the packed V4_5 form (two bytes per vector), not four components of two
            // bytes each. Its packed attribute semantics are not modelled, so reject it before
            // using the generic size rule and potentially treating payload bytes as VIF code words.
            if (vl == 3U)
            {
                return std::unexpected(Error(DecodeErrorCode::UnsupportedVariant,
                    "vum visual geometry does not support packed V4_5"));
            }
            const std::uint32_t unpack_count = num == 0U ? 256U : num;
            const std::uint32_t components = static_cast<std::uint32_t>(vn) + 1U;
            const std::uint32_t element_bytes = VlBytes(vl);
            const std::uint32_t data_bytes =
                AlignUp4(unpack_count * components * element_bytes);
            if (data_bytes > size - pos)
                break; // truncated -- stop, keep coherent batches
            const std::size_t data = pos;

            if (vn == 3U && vl == 0U) // V4-32 -> anchors, start of a batch
            {
                if (batch.has_anchors || !batch.raw_positions.empty())
                {
                    const auto flush_result = flush();
                    if (!flush_result)
                        return std::unexpected(flush_result.error());
                }
                std::array<Float3IR, 2> anchors{};
                std::uint32_t found = 0U;
                for (std::uint32_t vertex = 0U; vertex < unpack_count && found < 2U; ++vertex)
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
                std::uint64_t next_parsed_items = parsed_items;
                auto item_result = Accumulate(
                    next_parsed_items, unpack_count, limits.maximum_items,
                    "vum visual geometry parsed item count overflows",
                    "vum visual geometry parsed items exceed decoder limit");
                if (!item_result)
                    return std::unexpected(item_result.error());
                const auto scratch_result = CheckBatchScratchBudget(
                    unpack_count, batch.colors.size(), batch.uvs.size(), limits);
                if (!scratch_result)
                    return std::unexpected(scratch_result.error());
                parsed_items = next_parsed_items;
                batch.raw_positions = decltype(batch.raw_positions){};
                batch.raw_positions.reserve(unpack_count);
                for (std::uint32_t vertex = 0U; vertex < unpack_count; ++vertex)
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
                    std::uint64_t next_parsed_items = parsed_items;
                    auto item_result = Accumulate(
                        next_parsed_items, unpack_count, limits.maximum_items,
                        "vum visual geometry parsed item count overflows",
                        "vum visual geometry parsed items exceed decoder limit");
                    if (!item_result)
                        return std::unexpected(item_result.error());
                    const auto scratch_result = CheckBatchScratchBudget(
                        batch.raw_positions.size(), unpack_count, batch.uvs.size(), limits);
                    if (!scratch_result)
                        return std::unexpected(scratch_result.error());
                    parsed_items = next_parsed_items;
                    batch.colors.reserve(unpack_count);
                    for (std::uint32_t vertex = 0U; vertex < unpack_count; ++vertex)
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
                std::uint64_t next_parsed_items = parsed_items;
                auto item_result = Accumulate(
                    next_parsed_items, unpack_count, limits.maximum_items,
                    "vum visual geometry parsed item count overflows",
                    "vum visual geometry parsed items exceed decoder limit");
                if (!item_result)
                    return std::unexpected(item_result.error());
                const auto scratch_result = CheckBatchScratchBudget(
                    batch.raw_positions.size(), batch.colors.size(), unpack_count, limits);
                if (!scratch_result)
                    return std::unexpected(scratch_result.error());
                parsed_items = next_parsed_items;
                batch.uvs = decltype(batch.uvs){};
                batch.uvs.reserve(unpack_count);
                for (std::uint32_t vertex = 0U; vertex < unpack_count; ++vertex)
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
    const auto flush_result = flush();
    if (!flush_result)
        return std::unexpected(flush_result.error());
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
