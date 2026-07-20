# VPK wrapper-envelope contract

## Public evidence boundary

The aggregate-only fingerprint covers 85 VPK spans. Every physical span is between 1,320,960 and
9,005,056 bytes inclusive, and every span is divisible by 2,048. Every available 16-byte prefix has
the exact raw signature bytes `20 4b 50 56` (`b" KPV"`) at offset `0x00` and the little-endian
unsigned word 2,048 at offset `0x08`. The signature is deliberately described only as raw bytes;
the visually forward bytes `b"VPK "` do not match the proven representation.

Bytes `0x04..0x07` and `0x0c..0x0f` have no established field meaning. The aggregate fingerprint
does not inspect or characterize bytes from `0x10` onward beyond the complete physical-span size.

## Native contract

`DecodeVpkWrapperEnvelope` is a stateless, reentrant passive decoder over one borrowed byte span. It
requires the exact signature, the exact little-endian 2,048 word at `0x08`, the observed inclusive
physical-size range, and divisibility by 2,048. Its fixed owned
`VpkWrapperEnvelopeDescriptor` retains the two unassigned four-byte prefix fields in source order,
the physical byte count, and the derived aligned-block count. The aligned-block count ranges from
645 through 4,397 for accepted inputs. It is only an arithmetic quotient and is not named or used as
a storage sector, packet, sample, or audio frame.

The complete input is charged to the input budget. One fixed descriptor is one item, and
`sizeof(VpkWrapperEnvelopeDescriptor)` is the fixed logical-output charge. The observed
9,005,056-byte physical maximum, one-item maximum, and fixed output size are unraiseable project
security ceilings; caller `DecodeLimits` may only tighten them. The inspection allocates no output
buffer, uses zero dynamic scratch, and treats its root as nesting depth zero.

The input exists only for the duration of the call. No view, path, filename, payload byte, or byte
after the proven prefix is retained in the result. Changes to the uninspected wrapper remainder or
opaque payload therefore do not change the descriptor when the physical span is unchanged.

## Nonclaims

The observed word at `0x08` and the physical alignment are independent observations. Their equal
numeric values do not establish that the word declares a header size, block size, or alignment.

This boundary assigns no codec, ADPCM variant, sample rate, channel count, audio or music role,
seek table, packet structure, streaming policy, playback behavior, storage-device geometry,
compression, encryption, asset lookup, runtime wiring, or emulator semantics. The observed 2,048
word and separate 2,048-byte alignment do not establish any of those meanings. Tests use only
project-generated fixtures and contain no owner or retail bytes.
