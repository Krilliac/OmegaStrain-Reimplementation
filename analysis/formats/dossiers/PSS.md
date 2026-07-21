# .pss — occurrence dossier with adjacent native MPEG-PS presentation

## 1. Identity

The suffix `.pss` is observed as a HOG member, but the tracked aggregate inventories still establish
only occurrences for the suffix family. The authoritative coverage matrix therefore retains the
suffix classification `aggregate_scanner_only`. Separately, the native media and application layers
can inspect and present one bounded, explicitly selected MPEG-PS/H.262/PCM input shape. That
capability does not establish that every retail `.pss` member uses the accepted shape or has general
retail behavior parity.

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
  `aggregate_scanner_only` at the suffix-family level.
- `omega::media::InspectMpegProgramStream` provides bounded, codec-neutral MPEG-2 Program Stream/PES
  framing; the video layer builds an offset-only payload plan and inspects H.262 sequence facts.
- `BuildPssPcmAudioStreamPlan` validates one narrow private-stream SShd/SSbd signed-PCM shape and
  returns offset-only metadata; `DecodePssPcm16Interleaved` revalidates that plan and deinterleaves
  exact caller-requested frame ranges without allocation.
- On Windows, `OpeningMoviePlayer` composes those boundaries with the Media Foundation H.262
  decoder. `openomega` presents owned video frames and decoded stereo PCM through bounded SDL
  queues and an isolated audio-demand clock, with explicit skip/fail-open teardown.
## 4. Aggregate-only facts

The available aggregate evidence still carries occurrence totals and the derived nested count only.
It does not publish a suffix-wide size fingerprint, accepted-variant distribution, per-container
row, or payload byte. The current hardened size-only member collector does not allowlist this
suffix. Expanding a frozen public schema requires a separate reviewed change; do not silently add
it. Generated media fixtures prove implemented parser/presentation boundaries, not corpus coverage.

## 5. Hypotheses

- **H1 — one structural family.** Members may share a grammar, or the suffix may group unrelated
  payloads. A size-only result can test uniformity but cannot by itself prove a parser grammar.
- **H2 — runtime role.** A consumer may use the members in one or more flows. A tracked static or
  behavioral consumer observation is required; the suffix name is not evidence.

## 6. Missing observations

- No sanitized owner-corpus size or accepted-shape coverage result is tracked for this suffix.
- Alternate private-stream encodings, multi-video selection policy, subtitles, seeking, looping,
  and general retail member variants remain uncharacterized.
- Exact retail A/V timing, full-playback behavior, audible-content equivalence, and PCSX2 parity are
  not established. Non-Windows end-to-end video presentation remains unsupported.

## 7. Decoder/tooling status

**Classification: `aggregate_scanner_only`.**

Generic suffix occurrence counting is not a decoder. The native tree nevertheless contains reusable
bounded media infrastructure: MPEG-PS/PES inspection, MPEG-video range planning, H.262 inspection
and Windows decode, one PSS-named PCM plan/deinterleave shape, and project-owned opening-movie
presentation. These interfaces have focused generated-fixture and lifecycle tests. They do not
produce a canonical `.pss` asset IR, accept all retail members, or establish retail playback policy
or behavior parity, so the suffix classification does not change.

## 8. Codex work order

1. Keep this suffix outside the frozen collector allowlist unless a separate evidence plan justifies
   a schema revision.
2. Preserve the separation between suffix-family coverage, generic MPEG syntax, narrow PSS PCM
   support, and application presentation.
3. Add support one demonstrated variant at a time with generated malformed boundaries and bounded
   lifecycle tests; do not infer variants from the suffix.
4. Add no raw magic-value histogram, member identity, per-file row, or payload excerpt.
5. Require reviewed sanitized coverage and independent behavior evidence before claiming general
   `.pss` decoding, exact retail timing, or PCSX2 parity.
