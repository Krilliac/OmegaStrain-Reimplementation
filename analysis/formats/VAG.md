# VAG mono audio decode boundary

## Scope

This note freezes the native boundary implemented by E-0090. It uses the aggregate, path-free
VAG observations already recorded in `ASSET-RECON.md` and standard PS-ADPCM frame behavior. The
implementation and every committed fixture are project-authored. No retail audio, filename, path,
disc byte, executable byte, decoded owner sample, or emulator capture is included.

The decoder is a stateless, reentrant format adapter. It produces a fully owned mono PCM16 value;
it does not open a device, choose a voice, resample, mix, stop at an end marker, repeat a loop, or
otherwise apply playback policy.

## Accepted envelope

The input header is exactly 48 bytes before frame data:

| Offset | Width | Meaning | Accepted value |
| --- | ---: | --- | --- |
| `0x00` | 4 | magic | ASCII `VAGp` |
| `0x04` | 4 | big-endian version | `0`, `4`, or `0x20` |
| `0x08` | 4 | big-endian reserved word | zero |
| `0x0c` | 4 | big-endian frame-data bytes | multiple of 16 |
| `0x10` | 4 | big-endian sample rate | 22,050 Hz |
| `0x14` | 28 | opaque header bytes | ignored |

The declared frame extent must be present. The physical input must end there or carry 16 through
2,032 zero bytes. Other tail lengths and every nonzero tail fail closed. This matches the complete
8,665-entry aggregate: 53 exact spans and 8,612 zero-padded spans. Version acceptance likewise
matches the observed 8,497/166/2 split for `0`/`4`/`0x20`.

An empty aligned frame region is accepted as an empty canonical stream because the aggregate does
not establish that the declared byte count is nonzero. Header bytes `0x14..0x2f` are deliberately
not interpreted or copied into the canonical result.

## Frame transform

Each 16-byte frame yields 28 samples. Byte zero carries the predictor in its high nibble and the
shift in its low nibble; byte one is retained verbatim as the frame's source flag byte. Bytes two
through fifteen carry signed four-bit values in low-nibble then high-nibble order. Predictors zero
through four and shifts zero through twelve are accepted. The predictor coefficient pairs are:

```text
0:    0,   0
1:   60,   0
2:  115, -52
3:   98, -55
4:  122, -60
```

For every signed nibble `n`, latest decoded sample `h1`, next-latest sample `h2`, coefficient pair
`c1,c2`, and shift `s`, the project implementation evaluates:

```text
scaled     = n * 2^(12 - s)
prediction = floor((c1*h1 + c2*h2 + 32) / 64)
sample     = clamp_pcm16(scaled + prediction)
```

The clamped sample becomes history before the next nibble. History continues across source-frame
boundaries and starts at zero for each decoder call. The C++ implementation uses explicit signed
floor division so output is independent of implementation-defined negative-shift behavior.

The synthetic test freezes complete 28-sample vectors for all five predictors, a complete second
predictor-four frame for cross-frame history, shifts zero and twelve, nibble order/sign expansion,
both clamp directions, and raw flag/marker retention. Expected vectors are literals produced
independently of the decoder under test.

## Canonical ownership and budgets

`omega::asset::MonoPcm16IR` owns:

- the 22,050 Hz sample rate;
- every signed 16-bit mono sample; and
- one `AudioSourceFrameIR` per source frame, containing its zero-based sample offset and raw flag
  byte.

The IR contains no retail offset, borrowed byte span, filename, device, voice, resampler, or loop
state. Different accepted version values, opaque header bytes, and legal zero tails therefore
produce equal canonical values when frame bytes are equal.

Caller `DecodeLimits` bound the complete input span, root-plus-source-frame item count, and logical
owned output bytes. PCM samples are payload bytes rather than separately charged logical items.
The flat transform requires zero dynamic scratch and nesting depth zero. Fixed ceilings, which a
caller cannot widen, are 4 MiB of declared ADPCM data, 2,032 tail bytes, and 32 MiB of logical
output. The data ceiling is more than four times the observed largest complete VAG span of 929,792
bytes.

All extent, sample-count, item-count, and logical-output calculations use checked 64-bit arithmetic.
The accepted 32-bit source field plus the tighter fixed ceiling makes arithmetic overflow
unreachable for an accepted input; the synthetic maximum-field case proves rejection before
allocation or wrap. `std::length_error` is converted to typed `Overflow`, while `std::bad_alloc` is
converted to typed `LimitExceeded` without exposing exception text. Allocation injection is not a
public API and remains unclaimed.

## Explicit non-claims

E-0090 does not identify any sound's role, select a VAG from a container, bind audio to a menu,
dialogue line, effect, actor, level, or script, connect LPD data, infer flag usage by this title,
choose loop points, stop or repeat playback, resample, mix, stream, cache, upload to SDL, or claim
sample-for-sample comparison with a retail execution. It adds no filesystem I/O, runtime service,
audio-backend dependency, public file/wire ABI, or proprietary fixture.
