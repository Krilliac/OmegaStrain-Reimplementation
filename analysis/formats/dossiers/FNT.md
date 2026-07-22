# .fnt — bounded version-3 decoder and passive envelope dossier

## 1. Identity

The suffix `.fnt` is observed as a HOG member and has two deliberately separate native boundaries.
`DecodeFntV3` owns the documented version-3 atlas reference and glyph-UV records while rejecting an
unproven optional pair-table variant. `InspectFntEnvelope` remains a smaller project-passive prefix
hypothesis that reports neutral byte ranges only. The authoritative coverage matrix classifies the
suffix `canonical_decoder` because of the former; the passive observation does not gain semantics
from that promotion. Raw source bytes 16 and 17 retain only the narrow behavior documented by the
public IR, and complete typography or rendering behavior is not claimed.

## 2. Occurrence evidence

| Scope | Count | Tracked source |
| --- | ---: | --- |
| Recursive HOG inventory | 3 | `analysis/formats/asset-fingerprints.json` |
| Top-level HOG inventory | 3 | `analysis/formats/hog-validation.json` |
| Whole-disc inventory | 0 | `analysis/manifests/disc-summary.json` |

## 3. Confirmed facts

- The tracked inventories establish 3 recursive and 3 direct top-level HOG
  occurrences, with 0 whole-disc files.
- Subtracting the direct count from the recursive count establishes 0 occurrences reached
  through nested HOG descent. This is a count relation only; it assigns no role.
- `InspectFntEnvelope` validates only its bounded project-defined prefix hypothesis, reports the
  printable and terminator regions plus the remaining opaque payload range, and enforces fixed
  ceilings that caller limits may only tighten. It assigns no meaning to the prefix values or
  regions; generated fixtures confirm code behavior, not retail provenance.
- The descriptor has a dedicated generated-fixture test executable covering its accepted boundary,
  truncation/malformed inputs, resource limits, unaligned input, determinism, and typed path-free
  failures.
- `DecodeFntV3` is a bounded stateless decoder that returns owned `FntV3IR`, including an atlas
  reference, glyph UV rectangles, a signed space advance, and the independently documented byte-17
  vertical-placement adapter. A nonzero optional pair-table marker fails closed.
- `analysis/formats/DECODER-COVERAGE.md` classifies `.fnt` as `canonical_decoder`; this is limited
  to the documented version-3 family and does not establish suffix-wide typography or rendering.
- E-0113's `frontend-envelope-coverage-verify-tree` command can count project-passive descriptor
  acceptance for HOG-contained FNT candidates. Its generated fixtures are implementation evidence;
  no owner-corpus coverage result is recorded, and the tracked occurrence totals remain unchanged.
## 4. Aggregate-only facts

The aggregate inventories still carry occurrence totals and the derived nested count only; no
sanitized owner-corpus size or accepted-prefix coverage result is tracked. The hardened size-only
member collector includes this suffix in its default set. Its implementation and synthetic tests do
not become corpus evidence until a sanitized result is independently reviewed. The native
descriptor's bounded checks establish only its implemented passive boundary. The canonical decoder
is implementation evidence for its exact accepted version-3 contract, not a tracked corpus-wide
acceptance result.

## 5. Hypotheses

- **H1 — one structural family.** Members may share a grammar, or the suffix may group unrelated
  payloads. A size-only result can test uniformity but cannot by itself prove a parser grammar.
- **H2 — runtime role.** A consumer may use the members in one or more flows. A tracked static or
  behavioral consumer observation is required; the suffix name is not evidence.

## 6. Missing observations

- No sanitized owner-corpus size or accepted-prefix coverage result is tracked for this suffix.
- Retail provenance and owner-corpus coverage for the older project-passive prefix constants are not
  recorded; those descriptor ranges remain neutral.
- Optional pair-table structure, raw byte-16 meaning, general kerning/line metrics, text encoding,
  sampling, and complete render behavior remain unproven.

## 7. Decoder/tooling status

**Classification: `canonical_decoder` (bounded version-3 family, with an independent passive view).**

`DecodeFntV3` is stateless/reentrant, bounded, and returns owned typed data for its exact accepted
family. `InspectFntEnvelope` remains a stateless/reentrant passive hypothesis descriptor and is not
a semantic IR or evidence that its prefix constants are retail-authentic. Neither boundary is a
renderer binding, owner-corpus acceptance claim, or complete retail behavior claim.

## 8. Codex work order

1. Preserve the independent passive hypothesis and do not transfer canonical decoder semantics into
   its neutral ranges.
2. Keep pair tables and raw metric meanings fail-closed until a separately reviewed grammar exists.
3. Treat any private corpus run as bounded coverage evidence, not automatic semantic promotion.
4. Add no raw magic-value histogram, member identity, per-file row, or payload excerpt.
5. Require independent consumer evidence before extending text layout, sampling, or render behavior.
