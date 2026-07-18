#pragma once

#include "omega/asset/decode.h"
#include "omega/retail/container_descriptors.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace omega::retail
{
enum class PopPostTerrainCandidate : std::uint8_t
{
    Inl,
    Pnt,
    Dir,
    Env,
    Inv,
};

struct PopPostTerrainCandidateExtent
{
    PopPostTerrainCandidate candidate = PopPostTerrainCandidate::Inl;
    std::uint32_t observed_word_at_plus_4 = 0;
    std::uint32_t arithmetic_stride_bytes = 0;
    ObservedByteRange opaque_region;

    bool operator==(const PopPostTerrainCandidateExtent&) const = default;
};

// Retail-only passive hypothesis metadata. A literal hit is not a proven section boundary, the
// observed word is not assigned count semantics, the arithmetic stride is not assigned record
// semantics, and the opaque ranges expose no payload, column, placement, visibility, rendering,
// or gameplay meaning. This fixed owned value is not canonical asset IR and retains no input span.
struct PopPostTerrainHypothesisDescriptor
{
    std::uint32_t observed_aligned_literal_count = 0;
    std::array<PopPostTerrainCandidateExtent, 5> guarded_extents{};

    bool operator==(const PopPostTerrainHypothesisDescriptor&) const = default;
};

// [any worker thread; reentrant] Validates only the published TER-to-GOB prefix, the established
// ordered aligned-literal envelope, and five bounded arithmetic extent hypotheses. Deviations from
// that post-terrain hypothesis family are reported as UnsupportedVariant.
[[nodiscard]] asset::DecodeResult<PopPostTerrainHypothesisDescriptor>
InspectPopPostTerrainHypotheses(
    std::span<const std::byte> bytes, asset::DecodeLimits limits = {});
} // namespace omega::retail
