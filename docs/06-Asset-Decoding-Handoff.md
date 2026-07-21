# Asset-decoding handoff (for the Codex workstream)

Originally prepared 2026-07-20 from the `claude/frontend-asset-decoding` workstream (PR #58),
refreshed after PR #70, and updated for the schema-version-3 front-end topology vocabulary in PR #82
and ledger E-0110. This document hands forward adapter hardening, evidence collection, and RE-tool
integration.
It records only tracked, path-free facts and keeps implementation capability distinct from retail
behavioral parity.

The governing rules are unchanged: never access `D:`; never commit private/runtime/third_party
content; a format claim requires tracked aggregate evidence plus adversarial validation; unknown
fields stay opaque; public tools emit fixed-schema aggregates only; a plausible invented decoder is
a regression. See `docs/01-Clean-Room-Method.md` and `analysis/evidence/ledger.jsonl`.

## State this workstream is handing over

- `.gui` and `.ie` are now neutral first-class categories in the front-end HOG topology scanner
  (schema_version 3), with `.gui+.ie` in the fixed aggregate sibling-pair vocabulary; `.fnt` stays
  in `other`. See `tools/measure_frontend_hog_topology.py` and
  `analysis/formats/FRONTEND-TOPOLOGY.md`.
- The front-end evidence for `.gui`/`.fnt`/`.ie` is audited in
  `analysis/formats/FRONTEND-EVIDENCE-AUDIT.md`. The original broad semantic-decoder gate remains
  **NO**, but three later `Inspect*` APIs now provide bounded passive prefix/envelope descriptions:
  FNT reports a small project-defined prefix hypothesis plus an opaque payload; GUI and IE implement
  narrow project-defined prefix hypotheses and stop at opaque root boundaries. No tracked evidence
  records the retail provenance of their constants. They assign no font, glyph, widget, node,
  layout, lookup, render, menu, or consumer semantics.
- Every observed format family is classified in `analysis/formats/DECODER-COVERAGE.md` with a
  tracked-source citation, plus a ranked next-evidence queue.
- Ledger entry `E-0095` records the original decoder-coverage pass. `E-0110` supersedes only its
  schema-version-2 front-end topology statements with the neutral schema-version-3 `.ie` category
  and fixed `.gui+.ie` aggregate pair vocabulary.
- Ledger entry `E-0097` records the later size-only collector contract, corrected 47-dossier
  catalog, and synthetic/privacy validation; it records no owner-corpus measurement.
- Ledger entry `E-0098` promotes `.so` from aggregate-scanner-only to a native passive descriptor.
  The descriptor is analysis-only, uses generated fixtures, retains no strings or code cells, and
  does not authorize retail script execution or runtime integration.
- Ledger entry `E-0101` records the FNT/GUI/IE passive-descriptor boundary and keeps owner-corpus
  acceptance, UI/font semantics, consumer bindings, runtime integration, and retail parity
  explicitly unclaimed.
- Ledger entry `E-0102` records the bounded private-stream PCM planner/deinterleaver and its native
  opening-movie SDL presentation path. That path presents one external stream which passes a
  project-defined provisional MPEG-PS/H.262/PCM compatibility shape; it does not independently
  establish the custom PCM field meanings or deinterleave semantics, suffix-wide `.pss` coverage,
  exact retail A/V parity, or portable end-to-end playback.

## Queue for Codex

### 1. Evidence collection to unblock semantic front-end decoding (highest priority)

The bounded size-only collector is implemented and synthetically verified; see
`tools/measure_member_structural_fingerprint.py`,
`analysis/formats/MEMBER-STRUCTURAL-FINGERPRINT.md`, and ledger E-0097. The next step is a private
owner-corpus run followed by independent review of only the fixed-schema aggregate. That result is
not yet tracked. It is now a coverage aid for the passive descriptors, not a prerequisite for their
existence and not a semantic promotion. Consumer behavior and a falsifiable deeper grammar still
need independent corroboration before any opaque region becomes a typed field or IR.

Frozen contract for the bounded, privacy-safe structural fingerprint collector:

- **Input scope:** members inside HOG containers whose name carries a configured suffix; default to
  `.gui`, `.fnt`, `.ie`. Derive each member's payload extent only from the already-proven HOG
  offset-table grammar (`analysis/formats/HOG.md`); do not re-infer container layout.
- **Output (fixed-schema aggregate only):** per suffix — candidate count; payload-size distribution
  as `min`, `max`, and distinct-size count; and `size_gcd`, the common divisor of observed payload
  sizes. `size_gcd` is not payload-address alignment and must never be reported as such. Nothing
  else.
- **Explicitly forbidden in output:** input paths, member names, hashes, per-file rows, byte values,
  offsets tied to an individual member, unknown raw suffixes, and exception messages. Typed,
  path-free error categories only. Add a privacy test that asserts secret paths/names never appear
  in stdout or stderr.
- **Bounds:** bounded reads, checked arithmetic, fail closed; caller limits intersect with fixed
  hard ceilings and may only tighten them.
- **Tests:** synthetic exact/malformed/truncated/limit/determinism/privacy, before any corpus run.
- **Gate discipline:** a size-only result cannot widen the existing accept/reject boundaries or
  justify semantic fields. Only after a corpus result motivates a falsifiable deeper grammar,
  generated malformed boundaries, and independent consumer evidence may a semantic
  `GuiEnvelopeIR`/font/UI decoder be considered. If those gates fail, preserve the passive
  descriptors and extend the gap note. A plausible invented decoder is a regression.

`.bnk` and `.gun` are optional explicit allowlist choices. Their spelling assigns no audio, weapon,
or menu role.

### 2. Maintain adversarial / resource-boundary coverage for existing structural adapters

LPD, PAR, SKAS, and VPK now have dedicated, registered generated-fixture suites covering the
applicable truncation and malformed boundaries, physical and caller limits, unaligned backing
storage, deterministic repeated decoding, and typed path-free failures without changing format
semantics. LPD, PAR, and SKAS also inject failures at their owned-allocation boundaries.

VPK returns a fixed descriptor and retains no source or payload bytes. Its suite exercises zero
scratch, string, and nesting budgets, but does not inject allocation failure into string-owning
`DecodeError` construction. That narrow error-path hardening question remains open and must not be
conflated with missing adversarial coverage.

These suites validate only the implemented structural envelopes. Owner-corpus acceptance, semantic
interpretation, runtime consumer integration, and retail or PCSX2 behavioral parity remain
unproven. Add cases only for separately demonstrated variants or newly introduced resource
boundaries. See `analysis/formats/DECODER-COVERAGE.md`, the corresponding format dossiers, and
ledger entries `E-0091` through `E-0094` for the exact current evidence and nonclaims.

The newer FNT, GUI, and IE boundaries already have dedicated generated-fixture suites for accepted
prefixes, truncation/malformed cases, hard and caller limits, determinism, unaligned input, and
path-free typed failures. Extend those suites only when separately demonstrated structure widens a
descriptor; do not use test construction to invent deeper payload semantics.

### 3. Mechanical verification for existing retail adapters

Confirm, mechanically, for every retail adapter: CMake source + focused-test registration;
`tools/check_native_dependencies.py` coverage; `tools/check_public_tree.py` cleanliness; and
documentation consistency. This workstream already found the CMake/test registration **clean** and
recorded mechanical inconsistencies in `analysis/formats/DECODER-COVERAGE.md` (§ inconsistencies) to
reconcile — including decode-result contract fragmentation across `asset::DecodeResult<T>`,
`std::expected<T,std::string>`, and the POP terrain types, and singular/plural test-name drift.

For media work, keep `omega_media` and `openomega` presentation claims separate from the retail
suffix matrix. The tracked path bounds MPEG-PS inspection, H.262 video decoding on Windows, one
project-defined provisional SShd/SSbd signed-PCM plan/deinterleave hypothesis, the SDL audio ring,
and the audio-demand presentation clock. Future tests should target demonstrated variants and
lifecycle/fail-open behavior; they must not turn one accepted external stream into independent
format semantics, a general `.pss` claim, or a retail-sync claim.

### 4. AI-brief hot-file corrections

The brief cites three paths that do not exist in the tree:
`native/include/omega/asset/decode_result.h`, `native/include/omega/asset/decode_test_hooks.h`,
and `native/include/omega/retail/tdx_decoder.h`. The real contracts are
`native/include/omega/asset/decode.h` (the shared `DecodeLimits`/`DecodeError`/`DecodeResult`) and
`native/include/omega/retail/tdx_texture_storage_decoder.h`. There is no shared allocation-failure
test-hook header. Correct the brief before the next pass.

### 5. RE-tool integration (private oracle only)

An in-progress local ReSymbol `resymbol-ps2` facade binds owned PS2 Emotion Engine ELF bytes to
ReSymbol's authoritative, bounded ELF analysis without execution or emulation. Its workspace
registration and focused synthetic validation are still under review, so do not treat it as a
finished or independently validated oracle yet.

Intended use: run it on the retail boot ELF **in a private workspace** to recover front-end
loader function symbols and cross-references as a behavioral-oracle input. Its output is a private
hypothesis source only — like IDA/Ghidra output — and must never be committed, nor may any recovered
name/offset be promoted to a tracked claim without independent tracked-evidence corroboration.

## What remains explicitly unproven

Retail menu role, lookup, field semantics beyond the project-defined prefix hypotheses, layout, state,
timing, rendering, audio, and consumer binding for `.gui`/`.fnt`/`.ie`; any semantic native
front-end decoder; retail provenance for the prefix constants; and owner-corpus or PCSX2-equivalence
validation remain unproven. The project now
has three passive front-end descriptors and a native opening-movie video/PCM presentation path, but
that does not establish general `.pss` member compatibility, all private-stream variants, subtitles,
seeking, exact retail A/V behavior, or non-Windows end-to-end playback.
