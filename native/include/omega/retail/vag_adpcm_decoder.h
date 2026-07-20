#pragma once

#include "omega/asset/audio_ir.h"
#include "omega/asset/decode.h"

#include <cstddef>
#include <span>

namespace omega::retail
{
// [any worker thread; reentrant] Decodes the independently documented mono VAG
// envelope and PS-ADPCM frames into canonical owned PCM16. Source frame flags
// and their sample offsets are retained without applying end, repeat, loop,
// resampling, mixing, or playback policy.
[[nodiscard]] asset::DecodeResult<asset::MonoPcm16IR> DecodeVagAdpcm(
    std::span<const std::byte> bytes, asset::DecodeLimits limits = {});
} // namespace omega::retail
