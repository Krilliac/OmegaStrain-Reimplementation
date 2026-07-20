#pragma once

#include <array>
#include <cstddef>
#include <vector>

namespace omega::asset
{
inline constexpr std::size_t kLpdSourceTrackCount = 21;

struct LpdTrackIR
{
    // Four source bytes per entry, in source order. Their numeric or behavioral
    // meaning is deliberately unassigned.
    std::vector<std::array<std::byte, 4>> entries;

    bool operator==(const LpdTrackIR&) const = default;
};

// Canonical, fully owned LPD envelope data. Track order and entry boundaries
// are preserved, but track purpose, timing, interpolation, pose, animation, and
// audio relationships remain unassigned.
struct LpdEnvelopeIR
{
    std::array<LpdTrackIR, kLpdSourceTrackCount> tracks;

    bool operator==(const LpdEnvelopeIR&) const = default;
};
} // namespace omega::asset
