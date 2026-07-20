# LPD counted-envelope contract

## Status

The native adapter implements only the aggregate-proven counted envelope. It preserves 21
source-order tracks and each track's source-order four-byte entries as owned opaque bytes. It does
not assign a track role, scalar type, timing unit, sample rate, interpolation rule, pose, animation,
audio relationship, or playback behavior.

## Proven structural envelope

All multibyte header values are unsigned little-endian 32-bit words:

```text
u32 header_word_count = 22
u32 entry_counts[21]
opaque_4_byte_entry tracks[0][entry_counts[0]]
...
opaque_4_byte_entry tracks[20][entry_counts[20]]
optional zero-only physical tail
```

The checked logical extent is:

```text
logical_bytes = 88 + 4 * sum(entry_counts)
```

The tracked aggregate covers 862 inputs spanning 2,048 through 4,096 physical bytes. One ends at
the exact logical extent and 861 have an all-zero tail. Those observed tails range from 8 through
1,932 bytes. The observed 4,096-byte span maximum and 1,932-byte tail maximum are fixed,
unraiseable decoder ceilings. The observed lower tail endpoint does not establish a minimum or
alignment rule, so synthetic all-zero tails from one byte through the ceiling remain valid. Every
physical byte, including omitted zero padding, is still charged to the caller's
`maximum_input_bytes` limit.

## Canonical result and limits

`omega::asset::LpdEnvelopeIR` embeds exactly 21 track objects. Each track owns a vector of
`array<byte, 4>` entries. The decoder retains no source span and discards only validated zero
padding.

Resource accounting is deterministic:

- items are one envelope root, 21 track objects, and every opaque entry;
- logical output is `sizeof(LpdEnvelopeIR)` plus four bytes per entry;
- input is the complete physical input, including any zero tail; and
- the flat two-pass decoder uses no dynamic scratch and no nesting edge.

The 4,096-byte hard input ceiling and 88-byte header derive hard maxima of 1,002 entries, 1,024
items, and `sizeof(LpdEnvelopeIR) + 4,008` logical output bytes. Caller limits can only tighten
those maxima.

The decoder rejects an oversized physical input before parsing, checks fixed root budgets before
reading the header, validates the available first word before later header truncation, checks every
count and extent before allocation, reports the first missing payload byte for truncation, and
reports the first nonzero tail byte exactly. A tail beyond 1,932 bytes fails at the first byte
outside the fixed ceiling unless an earlier nonzero byte already invalidates it. All 21 final-sized
track vectors are explicitly constructed inside the allocation exception boundary; first and later
allocation failures map to typed bounded decode errors instead of escaping or terminating.

## Evidence boundary

The structural inputs are the aggregate-only rows in `asset-fingerprints.json` and the corresponding
summary in `ASSET-RECON.md`. Decoder fixtures are entirely synthetic and contain no retail payload.
The 852 same-directory VAG basename companions remain prioritization evidence only; the native IR
contains no audio link. Retail consumption, track meaning, timing, interpolation, and playback must
come from separate controlled behavioral observations before any semantic adapter is added.
