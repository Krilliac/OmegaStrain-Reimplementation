# .skel — Evidence Dossier

## 1. Identity

`.skel` is a game-asset suffix that occurs only *inside* `.HOG` archives (never as a
whole-disc file). At the evidence level this repository can defend, `.skel` is an
**observed member-name suffix** with a small, closed occurrence count. No native decoder
or structural handler exists for `.skel` payloads themselves. Separately, and by
cross-reference only, the `.SKL` container grammar (a distinct suffix, `.skl`) mandates
that one specific text-line token in every valid `.SKL` file end in the literal,
case-insensitive substring `.SKEL`. Beyond "a dot-suffixed marker/name token exists and
must carry this suffix," no semantic role, content layout, or payload structure for
`.skel` is established by tracked evidence.

## 2. Occurrence evidence

| Scope | Count | Source |
| --- | ---: | --- |
| Recursive-in-HOG (all depths, incl. nested HOGs) | 4 | `analysis/formats/asset-fingerprints.json`, `scan.extensions[".skel"]` |
| Top-level-HOG (member-suffix tally across all 273 top-level `.HOG` files) | 4 | `analysis/formats/hog-validation.json`, `entry_extensions[".skel"]` |
| Whole-disc (as a standalone disc file, outside any HOG) | 0 | `analysis/manifests/disc-summary.json` / `analysis/manifests/disc-files.jsonl` (no `.skel` path present) |

Both counts (recursive and top-level) agree at 4, which is consistent with the container
finding in §3 below: two identical top-level containers, each holding a 2-entry name
table whose entries are `.skel`-suffixed (2 × 2 = 4). This is an aggregate arithmetic
observation, not an inferred causal claim beyond what the cited files show.

## 3. Confirmed facts

Each row is mechanically citable from the named tracked file.

1. **`.skel` has no entry in `FORMAT_HANDLERS`.** `tools/fingerprint_assets.py` defines
   `FORMAT_HANDLERS = {".tdx": ..., ".ska": ..., ".skas": ..., ".skm": ..., ".skl": ...,
   ".vag": ..., ".lpd": ..., ".par": ..., ".col": ..., ".vum": ..., ".vpk": ...}` (lines
   499–511); `.skel` is absent. `scan_asset` (same file, ~line 536) only invokes a handler
   `if handler:` is truthy for the entry's extension, so `.skel` spans are counted into the
   generic `scan.extensions` / `entry_extensions` tallies but receive no structural
   inspection and produce no per-format `formats.skel` aggregate block. (Confirmed: no
   `"skel"` key exists under the `formats` object of `analysis/formats/asset-fingerprints.json`.)

2. **The four `.skel` members live in two byte-identical top-level `.HOG` containers.**
   Per `analysis/manifests/disc-files.jsonl`, two container copies of equal size (1,432
   bytes) exist at two distinct disc locations and are byte-identical. This dossier does not
   reproduce their paths or digest; the aggregate fact is that the corpus holds one distinct
   container payload duplicated once (2 copies × a 2-entry name table = the 4 tallied `.skel`
   occurrences).

3. **Both copies parse as valid HOG directories under the published `HOG.md` grammar.**
   Applying the `HOG.md` layout yields a 2-entry directory with a 56-byte header and two
   equal 688-byte payload spans (2 × 688 = 1,376), which plus the 56-byte header equals the
   1,432-byte file size exactly. This is a structural-grammar match expressed as derived
   aggregate sizes only; no raw header bytes, per-file tag, or per-file offset values are
   reproduced, and no payload bytes are interpreted.

4. **The name table of both containers holds exactly 2 NUL-terminated ASCII tokens, and
   both tokens end in the case-sensitive literal suffix `.SKEL`.** This is visible directly
   in the `header_ascii` field of both `hog-headers.jsonl` records (dot-suffixed ASCII text
   ending `...SKEL` appears twice per header, matching `count=2`). No name beyond the
   generic `.SKEL` suffix itself is repeated here, per the path-free/no-member-name
   constraint on this dossier.

5. **A separate, distinct suffix's decoder — `.SKL` (not `.SKEL`) — hard-codes a grammar
   dependency on the `.SKEL` suffix.** `native/src/retail/skl_container_descriptor.cpp`
   defines `EndsWithSkel(...)` (line 87) checking a token against the 5-byte literal
   `".SKEL"` case-insensitively (`AsciiUpper`, lines 78–103), and `ValidateMarkerFamily`
   (lines 105–141) requires that the token at `record_index == 3` (the 4th text line)
   satisfy `EndsWithSkel`, else the line is rejected with `DecodeErrorCode::UnsupportedVariant`
   ("SKL record markers are outside the observed family"). The header
   `native/include/omega/retail/skl_container_descriptor.h` documents `InspectSklContainer`
   as "[a]ny worker thread; reentrant. Passive structural inspection only. The descriptor
   retains no input or token bytes and assigns no meaning to records from the observed
   marker family" (lines 43–46). `native/tests/skl_container_descriptor_tests.cpp` exercises
   this constraint with synthetic fixture tokens (e.g. accepted/rejected fourth-line markers
   ending or not ending in the suffix, both upper- and lower-case) and with a "missing
   marker" / "moved marker" / "duplicate marker" negative-test family (lines ~292–306);
   these are test-authored fixture strings, not corpus-derived filenames, and are cited only
   to establish the grammar constraint, not any specific real name.

6. **The `.SKL` descriptor is registered in the build and test suite.**
   `CMakeLists.txt` lists `native/src/retail/skl_container_descriptor.cpp` (line 102) as a
   library source and `native/tests/skl_container_descriptor_tests.cpp` (line 1404) as a
   registered test target.

7. **No ledger entry (`analysis/evidence/ledger.jsonl`) exists for
   `skl_container_descriptor` / `InspectSklContainer` / `SklContainerDescriptor`** — a
   direct grep of the ledger for these identifiers returns zero matches. The `.SKL`
   descriptor's existence is confirmed by source/header/CMake citation only (items 5–6
   above), not by an E-#### ledger claim.

8. **`ASSET-RECON.md` documents `.SKL` (not `.SKEL`) separately** ("SKL skeleton/loadout
   list", lines 127–132): "All 1,261 SKLs are ASCII... Files contain 10-60 lines; 1,212
   begin with the common `BONENOSCALE` profile and 49 use other profile labels. The
   evidence supports a line-oriented skeleton/loadout reference list, not yet a decoded
   transform hierarchy." This corroborates the `kMinimumRecordCount`/`kMaximumRecordCount`
   (10/60) constants in `skl_container_descriptor.cpp` (lines 12–13) but is evidence about
   `.SKL`, a different tracked suffix, not about `.skel` payload content.

## 4. Aggregate-only facts

No semantic interpretation is attached to any of these.

- Recursive and top-level `.skel` suffix counts are both exactly 4 (§2), and no `.skel`
  occurrence exists as a standalone whole-disc file.
- `.skel` is one of the suffixes tallied in the shared `scan.extensions` /
  `entry_extensions` aggregate blocks. Its count of 4 places it among the rarer tallied
  suffixes; suffixes with strictly lower counts include `.prn` and `.scc` (1 each) and
  `.skas` (2), with `.txt` and `.fnt` at 3. No precise overall rank is claimed.
  (`analysis/formats/asset-fingerprints.json`, `analysis/formats/hog-validation.json`.)
- The two known-owning containers are both exactly 1,432 bytes and byte-identical, i.e. the
  corpus contains one distinct container payload duplicated at two disc locations, not two
  distinct containers. (`analysis/manifests/disc-files.jsonl`.)
- Within that container, the two `.skel`-suffixed name-table entries reference equally
  sized payload spans (688 bytes each, from the offsets array in item 3 of §3) — an
  aggregate size fact only; no claim is made about what the 688-byte span encodes.
- The published `HOG.md` grammar under which this container was validated has been
  checked against all 273 top-level `.HOG` archives on the reference disc with zero
  boundary, filename-count, or monotonic-offset failures, and against 6,677 embedded HOGs
  (501 exact spans, 6,176 zero-padded tails) — i.e. the grammar used in §3 item 3 is the
  same corpus-wide-validated grammar, not a one-off fit. (`analysis/formats/HOG.md`.)

## 5. Hypotheses

Explicitly labeled; none are treated as fact anywhere above.

- **H1 — Container role.** *Hypothesis:* the two byte-identical `SKEL`-named top-level
  containers hold a small, fixed 2-entry table of what a future decoder might call
  "skeleton" definitions for two variants, given the pairing pattern and the container's
  disc-path pairing (COMMON vs. FRONTEND) mirroring other paired containers on the disc.
  *Confirming/refuting observation (privacy-safe):* extend `tools/fingerprint_assets.py`
  with a passive `.skel`-name-table dumper — modeled on the existing HOG directory parser
  in `tools/hog.py` — that reports, in aggregate only (counts, size buckets, no payload
  bytes, no names beyond the already-published `.SKEL` suffix), whether all `.skel`
  name-table entries across the full corpus share the fixed 688-byte span size seen here,
  and whether the count is always exactly 2 per container. A "yes" strengthens the
  fixed-shape observation without validating the semantic label "skeleton definition."
- **H2 — Cross-suffix dependency is corpus-wide, not incidental.** *Hypothesis:* every one
  of the 1,261 `.SKL` files' 4th text line ends in a `.SKEL`-suffixed token, meaning the
  `.SKL`→`.SKEL` grammar dependency in `skl_container_descriptor.cpp` is a general corpus
  invariant rather than something proven only by the descriptor's unit tests.
  *Confirming/refuting observation:* run the existing `InspectSklContainer` decoder (already
  built and tested per §3 items 5–6) against the full owned `.SKL` corpus via the project's
  existing tree-verification harness pattern (as used for other retail descriptors, e.g.
  `omega_tool.exe asset-metadata-verify-tree`) and report only the aggregate pass/fail
  count — no per-file rows, no token text.
- **H3 — 688-byte payload is a fixed-size record, not variable text/binary.** *Hypothesis:*
  because both known `.skel` payload spans are identically sized (688 bytes) and the
  container that holds them is byte-identical across its two disc copies, `.skel` payloads
  may be a fixed-size binary record type distinct from the ASCII line-oriented `.SKL`
  grammar. *Confirming/refuting observation:* a privacy-safe structural probe (magic-byte
  histogram, printable-ASCII-ratio bucket, NUL-tail detection) run over just these two
  known 688-byte spans, reported as an aggregate (ratio/bucket only, no bytes), would show
  whether the span is ASCII-text-shaped like `.SKL`/`.SKAS` or binary-shaped like `.TDX`/`.SKM`.

## 6. Missing observations

- **No per-format aggregate block for `.skel`.** Unlike `.tdx`/`.ska`/`.skas`/`.skm`/`.skl`/
  `.vag`/`.lpd`/`.par`/`.col`/`.vum`/`.vpk`, there is no `formats.skel` object in
  `analysis/formats/asset-fingerprints.json` because `.skel` is not in `FORMAT_HANDLERS`
  (§3 item 1). Privacy-safe collection: add a no-op-semantics aggregate-only handler
  (`fingerprint_skel`, mirroring the existing handlers' shape) to `tools/fingerprint_assets.py`
  that records only size, magic-byte-class, and printable-ratio buckets across the full
  corpus, then regenerate `asset-fingerprints.json`.
- **No native decoder, descriptor, or even a passive structural header check for `.skel`
  content.** There is no `native/include/omega/**/*skel*.h` or
  `native/src/**/*skel*.cpp` file of any kind, and no CMake registration — confirmed by the
  earlier searches of `native/` returning only `.skl`-suffixed source (`.SKL`, a different
  format), whose filename happens not to contain "skel" as a substring but whose *content*
  references `.SKEL` for grammar reasons only (§3 item 5).
- **No ledger (`analysis/evidence/ledger.jsonl`) entry for `.skel` at all.** No `E-####`
  claim mentions `.skel`/`SKEL` directly (only `SklContainerDescriptor`-adjacent code exists,
  itself unlogged — §3 item 7). Privacy-safe collection: once a `.skel` aggregate handler or
  passive envelope exists (see above), log its build/test verification the same way other
  E-#### entries do (build config, CTest pass counts, no payload bytes).
- **No adversarial/resource-boundary test coverage for `.skel` at all**, because no decoder
  exists to test. If a `.skel` structural envelope is later built, it should follow the
  adversarial-test pattern already used for the SKAS envelope (E-0093: boundary sizes,
  padding classes, limit tightening, ownership/determinism checks) before any claim of
  "hardened" is made.
- **No confirmation of whether 4 is the true corpus-wide ceiling** versus an artifact of
  the specific reference disc image validated so far. Privacy-safe collection: re-run
  `tools/fingerprint_assets.py` against any additional owned disc/region variant already
  covered by this repository's existing disc-manifest tooling, and diff the `.skel` counts
  in the regenerated `asset-fingerprints.json` / `hog-validation.json`.

## 7. Decoder/tooling status

**Classification: `aggregate_scanner_only`.**

- `.skel` spans are counted by the generic recursive HOG scanner in
  `tools/fingerprint_assets.py` (`scan_asset`, `scan_hog_tree`) purely because that scanner
  tallies every extension it encounters into `scan.extensions` — this is the *only*
  tooling that touches `.skel`.
- `.skel` is absent from `FORMAT_HANDLERS` (§3 item 1), so no structural envelope, no
  passive descriptor, and no content decoder exists for `.skel` payloads. There is no
  native header, no native source file, and no CMake target for `.skel` of any kind.
- The one piece of `.skel`-adjacent native code is `native/src/retail/skl_container_descriptor.cpp`
  / `native/include/omega/retail/skl_container_descriptor.h` (registered at
  `CMakeLists.txt` lines 102 and 1404, tested by
  `native/tests/skl_container_descriptor_tests.cpp`) — but this decodes **`.SKL`**, a
  different tracked suffix, and only *validates a grammar dependency* on a `.SKEL`-suffixed
  token; it neither opens nor decodes any `.skel` file. That descriptor is itself
  `passive_descriptor_only` for `.SKL`'s own scope ("retains no input or token bytes and
  assigns no meaning to records" — header lines 43–45), and it has no ledger entry (§3
  item 7), so even its own status is not corroborated by an E-#### build/test claim.
- Adversarial/resource-boundary test gap: N/A for `.skel` directly (no decoder to test).
  For the adjacent `.SKL` descriptor, `skl_container_descriptor_tests.cpp` does exercise
  boundary cases for the `.SKEL`-suffix grammar check (marker present/absent/moved/duplicate,
  case variants), but this repository's ledger does not record an E-#### entry certifying
  that suite's build/CTest status the way it does for, e.g., SKA (E-0027) and SKAS (E-0093).

## 8. Codex work order

Ranked, concrete, privacy-safe. None of these presume a decoded semantic.

1. **Highest priority — build a `.skel` aggregate-only structural probe and regenerate
   the fingerprint report.** Add a `fingerprint_skel` handler to `tools/fingerprint_assets.py`
   (mirroring the shape of the existing 11 handlers) that records, per the existing
   `Aggregate` accumulator pattern: size distribution, magic-byte-class buckets, and
   printable-ASCII-ratio buckets — no payload bytes, no names. Register it in
   `FORMAT_HANDLERS`. Regenerate `analysis/formats/asset-fingerprints.json` against the
   owned corpus and confirm the `formats.skel` block appears with exactly the 4 recursive
   occurrences already tallied in §2, cross-checking that the 2-entries-per-container ×
   2-containers arithmetic in §2/§3 holds exactly.
2. **Run the two known 688-byte spans through a printable-ratio / magic-byte check only**
   (as in H3, §5) to decide whether `.skel` payload should be modeled as text-line-oriented
   (like `.SKL`/`.SKAS`) or fixed-binary-record-oriented (like `.SKM`/`.TDX`) *before* any
   decoder is designed — this determines which existing decoder family to pattern-match,
   without yet claiming a decoded structure.
3. **Verify H2 corpus-wide:** run the already-built, already-tested `InspectSklContainer`
   decoder against the full owned `.SKL` corpus (1,261 files) via a tree-verification
   command mirroring the existing `omega_tool.exe asset-metadata-verify-tree`-style pattern,
   and record only the aggregate pass/fail count plus any rejection-reason histogram (no
   per-file rows) in a fresh ledger E-#### entry — closing the gap noted in §3 item 7 and
   §7 for the `.SKL` descriptor's own missing build/test certification.
4. **If the H1/H3 probes in items 1–2 show a stable, corpus-wide fixed shape** (fixed
   count-per-container, fixed span size, consistent magic/printable class), only then draft
   a `structural_envelope_only`-tier passive descriptor for `.skel` (bounds/counts only, no
   semantics), following the SKAS envelope's adversarial-test discipline (E-0093) as the
   template for boundary/padding/limit test coverage before it is proposed for merge.
5. **Do not assign any role, label, or lookup relationship** (e.g. "male/female skeleton
   selector", "menu asset") to the `.skel` name-table entries or their 688-byte payloads
   until a decoder exists and its output is corroborated by a logged ledger entry — per the
   clean-room conservatism rule, absence of a decoder is a Missing-observation, not license
   to infer.
