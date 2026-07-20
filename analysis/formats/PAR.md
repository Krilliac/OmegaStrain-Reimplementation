# PAR text envelope

## Public evidence boundary

The aggregate-only owned-corpus fingerprint contains 679 PAR spans. Every span is seven-bit ASCII,
uses CRLF without a lone carriage return or line feed, and ends in 1 through 2,040 NUL bytes. The
physical spans range from 2,048 through 4,096 bytes. The first line matches the public scanner's
numeric-token, optional-whitespace, literal `;version`, line-ending shape.

The fingerprint tool records its captured token verbatim; it does not normalize a number. Its eight
published keys are therefore the exact observed tokens:

```text
1.300000  1.400000  1.500000  1.700000
1.800000  1.900000  2.000000  2.100000
```

No `1.600000` key is present. Shorter spellings such as `1.3`, leading-zero spellings, additional
fractional zeroes, exponent syntax, signs, and floating-point equivalence are not established.

## Native contract

`DecodeParTextEnvelope` is a stateless, reentrant envelope decoder. It accepts only the observed
2,048- through 4,096-byte physical range, requires a 1- through 2,040-byte all-zero tail, validates
seven-bit ASCII and CRLF-only logical text, and recognizes only the eight exact tokens above. The
only allowed bytes between the token and lowercase `;version` are ASCII horizontal tab, vertical
tab, form feed, and space. The literal is followed immediately by CRLF.

The owned `ParTextEnvelopeIR` contains the exact logical text, source-order opaque line ranges, the
two-byte or absent line-terminator size, a fixed declared-version enum, and the omitted padding byte
count. The original token remains in the exact logical text. The decoder uses no floating point and
does not rewrite, trim, split, or otherwise normalize body content. Fixed physical, padding,
logical-text, line-count, item-count, and output-byte ceilings apply before caller limits; callers
may only tighten them. Allocation failures return a typed path-free decode error.

## Nonclaims

The first-line marker is the only interpreted body shape. The implementation assigns no key/value
grammar, field names or types, semicolon-comment behavior beyond that marker, path or asset-name
role, particle-system meaning, compatibility defaults, migration behavior, renderer binding,
timing, gameplay behavior, or emulator equivalence. It does not claim that every integer physical
size inside the accepted range occurs in the corpus. Tests use generated text only.
