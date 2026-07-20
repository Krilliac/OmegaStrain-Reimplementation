#pragma once

#include <cstdint>
#include <vector>

namespace omega::asset
{
struct AudioSourceFrameIR
{
    // Offset into samples. The source flag byte is retained as data only;
    // playback policy is deliberately outside the canonical asset.
    std::uint64_t sample_offset = 0;
    std::uint8_t source_flags = 0;

    bool operator==(const AudioSourceFrameIR&) const = default;
};

// Canonical, fully owned mono signed-16-bit PCM. It contains no source byte
// views, device objects, voices, resampling state, or automatic marker/loop
// behavior.
struct MonoPcm16IR
{
    std::uint32_t sample_rate_hz = 0;
    std::vector<std::int16_t> samples;
    std::vector<AudioSourceFrameIR> source_frames;

    bool operator==(const MonoPcm16IR&) const = default;
};
} // namespace omega::asset
