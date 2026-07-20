# .pss — occurrence-only archive-member dossier

## 1. Identity

The suffix `.pss` is observed as a HOG member but has no dedicated structural aggregate,
falsifiable grammar, or native decoder. The authoritative coverage matrix therefore classifies it
`aggregate_scanner_only`. Suffix spelling and population size establish no asset role.

## 2. Occurrence evidence

| Scope | Count | Tracked source |
| --- | ---: | --- |
| Recursive HOG inventory | 54 | `analysis/formats/asset-fingerprints.json` |
| Top-level HOG inventory | 54 | `analysis/formats/hog-validation.json` |
| Whole-disc inventory | 0 | `analysis/manifests/disc-summary.json` |

## 3. Confirmed facts

- The tracked inventories establish 54 recursive and 54 direct top-level HOG
  occurrences, with 0 whole-disc files.
- Subtracting the direct count from the recursive count establishes 0 occurrences reached
  through nested HOG descent. This is a count relation only; it assigns no role.
- `analysis/formats/DECODER-COVERAGE.md` classifies `.pss` as
  `aggregate_scanner_only`: no dedicated native boundary is claimed.
## 4. Aggregate-only facts

The available public evidence carries occurrence totals and the derived nested count only. It does
not publish a size fingerprint, header value, field position, per-container row, or payload byte for
this suffix. The current hardened size-only member collector does not allowlist this suffix. Expanding a frozen public schema requires a separate reviewed change; do not silently add it.

## 5. Hypotheses

- **H1 — one structural family.** Members may share a grammar, or the suffix may group unrelated
  payloads. A size-only result can test uniformity but cannot by itself prove a parser grammar.
- **H2 — runtime role.** A consumer may use the members in one or more flows. A tracked static or
  behavioral consumer observation is required; the suffix name is not evidence.

## 6. Missing observations

- No sanitized owner-corpus size result is tracked for this suffix.
- No evidence-backed header, field, envelope, or malformed boundary is established.
- No tracked consumer observation assigns lookup, menu, render, audio, gameplay, or other semantics.

## 7. Decoder/tooling status

**Classification: `aggregate_scanner_only`.**

Generic occurrence counting is not a decoder. An offline or size-only measurement must not be
promoted to a native descriptor without a falsifiable grammar and generated rejection cases.

## 8. Codex work order

1. Keep this suffix outside the frozen collector allowlist unless a separate evidence plan justifies a schema revision.
2. Preserve size GCD as a divisor of observed sizes, never an address-alignment claim.
3. Add no raw magic-value histogram, member identity, per-file row, or payload excerpt.
4. Consider a native boundary only after tracked structure defines valid and malformed cases; then
   follow `docs/native-scaffolds/README.md`.
