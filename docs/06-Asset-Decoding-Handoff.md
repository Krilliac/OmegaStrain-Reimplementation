# Asset-decoding handoff (for the Codex workstream)

Prepared 2026-07-20 on branch `claude/frontend-asset-decoding` (see PR #58). This document hands the
Claude asset-decoding workstream's non-decoding follow-up to the Codex workstream: C++ adapter
tests, mechanical verification, evidence-collection runs against the owner corpus, and RE-tool
integration. It records only tracked, aggregate, path-free facts.

The governing rules are unchanged: never access `D:`; never commit private/runtime/third_party
content; a format claim requires tracked aggregate evidence plus adversarial validation; unknown
fields stay opaque; public tools emit fixed-schema aggregates only; a plausible invented decoder is
a regression. See `docs/01-Clean-Room-Method.md` and `analysis/evidence/ledger.jsonl`.

## State this workstream is handing over

- `.gui` is now a first-class category in the front-end HOG topology scanner (schema_version 2);
  `.fnt`/`.ie` stay in `other`. See `tools/measure_frontend_hog_topology.py` and
  `analysis/formats/FRONTEND-TOPOLOGY.md`.
- The front-end evidence for `.gui`/`.fnt`/`.ie` is fully audited in
  `analysis/formats/FRONTEND-EVIDENCE-AUDIT.md`. **The gate verdict is NO**: the tracked tree
  records only existence and occurrence counts (recursive 77/3/79, top-level 21/3/23) — no member
  size, byte, header field, or alignment. No front-end envelope decoder was built, by design.
- Every observed format family is classified in `analysis/formats/DECODER-COVERAGE.md` with a
  tracked-source citation, plus a ranked next-evidence queue.
- Ledger entry `E-0095` records this pass.
- Ledger entry `E-0097` records the later size-only collector contract, corrected 47-dossier
  catalog, and synthetic/privacy validation; it records no owner-corpus measurement.
- Ledger entry `E-0098` promotes `.so` from aggregate-scanner-only to a native passive descriptor.
  The descriptor is analysis-only, uses generated fixtures, retains no strings or code cells, and
  does not authorize retail script execution or runtime integration.

## Queue for Codex

### 1. Evidence collection to unblock front-end decoding (highest priority)

The bounded size-only collector is now implemented and synthetically verified; see
`tools/measure_member_structural_fingerprint.py`,
`analysis/formats/MEMBER-STRUCTURAL-FINGERPRINT.md`, and ledger E-0097. The next step is a private
owner-corpus run followed by independent review of only the fixed-schema aggregate. That result is
not yet tracked. Size regularity is not sufficient by itself: consumer behavior and a falsifiable
grammar still need independent corroboration before fields or a native decoder are proposed.

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
- **Gate discipline:** a size-only result cannot justify an accept/reject parser. Only after a
  corpus result motivates a falsifiable grammar, generated malformed boundaries, and independent
  consumer evidence may a native `GuiEnvelopeIR`/decoder be considered. If those gates fail, stop
  at the fingerprint and extend the gap note. A plausible invented decoder is a regression.

`.bnk` and `.gun` are optional explicit allowlist choices. Their spelling assigns no audio, weapon,
or menu role.

### 2. Adversarial / resource-boundary tests for existing passive adapters

Add missing tests to the LPD, PAR, SKAS, and VPK adapters **without changing their semantics**.
Sources and their current tests:

- LPD — `native/include/omega/retail/lpd_envelope_decoder.h` family; `native/src/retail/*`.
- PAR — `native/include/omega/retail/par_text_envelope_decoder.h`.
- SKAS — `native/include/omega/retail/skas_text_envelope_decoder.h`.
- VPK — `native/include/omega/retail/vpk_wrapper_envelope_decoder.h` (see ledger `E-0094`).

Cases to cover for each, if not already present: truncated prefix at every signature byte; span one
below the minimum and one above the fixed hard ceiling; an unaligned backing slice when the format
does not require alignment (do not invent an alignment rule); a caller `DecodeLimits` more generous
than the hard ceiling (assert the ceiling does **not** rise); repeated-call determinism; typed
path-free error on every rejection; and allocation-failure behavior (there is no shared
`decode_test_hooks.h` — each test rolls its own failure hook; see item 4). Serialize builds (`-j 1`)
and run focused + full CTest per the evidence bar in the AI brief.

### 3. Mechanical verification for existing retail adapters

Confirm, mechanically, for every retail adapter: CMake source + focused-test registration;
`tools/check_native_dependencies.py` coverage; `tools/check_public_tree.py` cleanliness; and
documentation consistency. This workstream already found the CMake/test registration **clean** and
recorded nine mechanical inconsistencies in `analysis/formats/DECODER-COVERAGE.md` (§ inconsistencies)
to reconcile — including a decode-result contract fragmentation across `asset::DecodeResult<T>`,
`std::expected<T,std::string>`, and the POP terrain types, and singular/plural test-name drift.

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

Retail menu role, lookup, field semantics, layout, state, timing, rendering, and audio for
`.gui`/`.fnt`/`.ie`; any native front-end decoder; and owner-corpus, behavioral-oracle, runtime,
packaged-host, and PCSX2-equivalence validation. None of these are claimed by this workstream.
