# .fnt — passive prefix/envelope dossier

## 1. Identity

The suffix `.fnt` is observed as a HOG member and now has one bounded native passive descriptor for
a small project-defined prefix hypothesis. `InspectFntEnvelope` reports neutral values and byte
ranges, leaving the payload opaque and retaining no source bytes. The authoritative coverage matrix
therefore classifies the implementation `passive_descriptor_only`. No tracked evidence records the
retail provenance of its constants, and neither suffix spelling nor prefix shape establishes font,
glyph, metric, texture, render, menu, or other asset semantics.

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
- `analysis/formats/DECODER-COVERAGE.md` classifies `.fnt` as
  `passive_descriptor_only`; no canonical or semantic decoder is claimed.
- E-0113's `frontend-envelope-coverage-verify-tree` command can count project-passive descriptor
  acceptance for HOG-contained FNT candidates. Its generated fixtures are implementation evidence;
  no owner-corpus coverage result is recorded, and the tracked occurrence totals remain unchanged.
## 4. Aggregate-only facts

The aggregate inventories still carry occurrence totals and the derived nested count only; no
sanitized owner-corpus size or accepted-prefix coverage result is tracked. The hardened size-only
member collector includes this suffix in its default set. Its implementation and synthetic tests do
not become corpus evidence until a sanitized result is independently reviewed. The native
descriptor's bounded checks establish only its implemented passive boundary.

## 5. Hypotheses

- **H1 — one structural family.** Members may share a grammar, or the suffix may group unrelated
  payloads. A size-only result can test uniformity but cannot by itself prove a parser grammar.
- **H2 — runtime role.** A consumer may use the members in one or more flows. A tracked static or
  behavioral consumer observation is required; the suffix name is not evidence.

## 6. Missing observations

- No sanitized owner-corpus size or accepted-prefix coverage result is tracked for this suffix.
- Retail provenance and owner-corpus coverage for the project-defined prefix constants are not
  recorded; the payload after that prefix remains opaque and unparsed.
- No tracked consumer observation assigns font, glyph, metric, texture, lookup, menu, render,
  audio, gameplay, or other semantics.

## 7. Decoder/tooling status

**Classification: `passive_descriptor_only`.**

`InspectFntEnvelope` is a stateless/reentrant, bounded passive hypothesis descriptor backed by
generated fixtures. It reports neutral scalars and ranges only. It is not a font decoder, semantic
IR, renderer binding, retail-prefix evidence, owner-corpus acceptance claim, or retail behavior
claim.

## 8. Codex work order

1. Preserve the project-defined hypothesis and its opaque payload; do not rename neutral values as
   observed retail facts or semantic fields.
2. If run, review only the hardened collector's fixed-schema size aggregate and treat it as coverage
   evidence rather than a decoder promotion.
3. Preserve size GCD as a divisor of observed sizes, never an address-alignment claim.
4. Add no raw magic-value histogram, member identity, per-file row, or payload excerpt.
5. Require independent consumer evidence and a falsifiable deeper grammar before adding font,
   glyph, metric, texture, render, or menu semantics.
