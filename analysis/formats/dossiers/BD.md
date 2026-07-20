# .BD — Dossier

**Identity (evidence-level):** An unclassified suffix appearing exactly once across the tracked whole-disc file inventory. No tracked source defines a `.bd` grammar, header magic, or semantic role. It is counted only by the generic recursive extension histogram in the fingerprinting pipeline; no dedicated structural handler exists for it. Whether it is a PS2 system/OS artifact or a game asset is **not established** by tracked evidence — this dossier does not assert either.

## 1. Occurrence evidence

| Scope | Count | Tracked source |
|---|---|---|
| Recursive-in-HOG (inside any `.hog` archive, any depth) | 0 | `analysis/formats/asset-fingerprints.json` (recursive suffix inventory) |
| Top-level-in-HOG (direct members of a top-level `.hog`) | 0 | `analysis/formats/hog-validation.json` (top-level HOG member-suffix counts) |
| Whole-disc (anywhere on the extracted disc tree) | 1 | `analysis/manifests/disc-summary.json`, key `extensions["\.bd"] = 1`; corroborated by exactly one matching row in `analysis/manifests/disc-files.jsonl` |

`.bd` is therefore a whole-disc-only suffix: it exists as a top-level filesystem entry on the disc image but never as packed content inside any `.hog` container, recursive or top-level. Per the clean-room rules, the specific path, filename, size, and hash of that one `disc-files.jsonl` row are per-file/private-input detail and are withheld from this dossier; only the aggregate count from `disc-summary.json` is cited.

## 2. Confirmed facts

None. A mechanically citable Confirmed fact requires a named tracked file that defines grammar, header magic, a ledger entry (`E-####`), or a native decoder/descriptor for this suffix. Checked and absent:

- `tools/fingerprint_assets.py` — `FORMAT_HANDLERS` (the dict mapping suffix → structural fingerprint function) contains only `.tdx`, `.ska`, `.skas`, `.skm`, `.skl`, `.vag`, `.lpd`, `.par`, `.col`, `.vum`, `.vpk`. `.bd` is **not** a key.
- `analysis/formats/*.md` — no published grammar document exists for `.bd` (the directory contains `ASSET-RECON.md`, `COL.md`, `HOG.md`, `LPD.md`, `PAR.md`, `POP.md`, `SKAS.md`, `SO.md`, `TDX.md`, `VAG.md`, `VPK.md`, `VUM.md`, `FRONTEND-TOPOLOGY.md`; none mention `.bd`).
- `analysis/evidence/ledger.jsonl` — no entry's claim text is about a `.bd` format, header, or decoder (targeted grep found no genuine `.bd`-related `E-####` row; incidental substring hits were unrelated hex/identifier text, not `.bd` claims).
- `native/include/omega/retail/*.h`, `native/include/omega/asset/*.h` — no `.bd`-related type, descriptor, or comment.
- `CMakeLists.txt` — no `.bd`-related source, test, or target registration.

## 3. Aggregate-only facts

- `analysis/manifests/disc-summary.json`: the whole-disc extension histogram records `".bd": 1` out of `file_count: 448` total whole-disc files, alongside other single-count OS/support extensions in the same histogram (`.64`, `.hd`, `.icn`, `.ini`, `.log`, `.map`, `.sys`, `.tbl`, `.wdb`) and the multi-count game-data extensions (`.hog`, `.pop`, `.tm2`, `.irx`, etc.).
- `analysis/formats/asset-fingerprints.json`: no per-format aggregate block exists for `.bd` (only suffixes with a `FORMAT_HANDLERS` entry — listed in §2 — receive a structural aggregate section in that file). This confirms `.bd` is scan-visible (counted) but never structurally opened by the fingerprinting tool.
- `tools/fingerprint_assets.py`, `scan_asset()`: every scanned span — regardless of extension — is tallied into `scan.count("extensions", extension)` and (for non-`.hog` spans) checked against `compression_magic(head)` for a coincidental compression-wrapper signature. This generic pass applies to `.bd` the same as to every other unhandled suffix; it is a passive count/compression-sniff, not a format-specific decode.

No size range, alignment, or bucket statistics beyond the single occurrence count are available: with n=1 whole-disc occurrence and zero HOG occurrences, there is no aggregate distribution to report, and the one existing size/hash datum lives in the per-file `disc-files.jsonl` row, which is out of scope to disclose per the clean-room rules.

## 4. Hypotheses

All statements below are explicitly labeled hypotheses, not facts. None are asserted in the dossier body above.

- **H1 — PS2 system/support file.** `.bd` could be a PS2 IOP/BIOS-adjacent, boot-configuration, network, or debug-support file rather than a game asset (its sole location sits under a top-level disc directory whose aggregate byte total, but not identity, is disclosed in `disc-summary.json`'s `top_level_bytes`).
  - *Privacy-safe confirmation/refutation:* extend `tools/fingerprint_assets.py` with a passive descriptor for `.bd` that reports only aggregate structural signals (e.g., magic-byte histogram across any future same-suffix files, size-bucket counts, alignment) without emitting per-file identifiers; if a magic constant matching a documented PS2 SDK/BIOS/network structure appears, that would move this toward Confirmed. Absent such a match, it stays a hypothesis.
- **H2 — Compression-wrapped or opaque blob.** The single `.bd` file could be a compressed or encrypted blob unrelated to any asset family already tracked.
  - *Privacy-safe confirmation/refutation:* the existing generic `compression_magic(head)` check in `scan_asset()` already runs against it; check whether `analysis/formats/asset-fingerprints.json`'s aggregate `standard_compression_magic` / `compression_hit_extension` counters attribute any hit to `.bd`. If they do, note the wrapper type as an aggregate fact; if not, the hypothesis is unconfirmed, not refuted (absence of a magic hit does not prove absence of compression).
- **H3 — Game-asset table/index file.** `.bd` could denote a small binary "database"/index (the two-letter suffix pattern echoes other short PS2 suffixes such as `.hd`, `.wdb`, `.tbl` also present once each in the same histogram).
  - *Privacy-safe confirmation/refutation:* would require a structural handler (per Missing Observations §5) that reports only aggregate header-field statistics; a match to a known table/index grammar already published in a tracked `*.md` doc would confirm it, otherwise it remains speculative.

## 5. Missing observations

- **No structural handler for `.bd` exists** in `tools/fingerprint_assets.py` (`FORMAT_HANDLERS` has no `.bd` key), so no header fields, magic bytes, record counts, or internal layout have ever been mechanically extracted or aggregated for this suffix.
- **No published grammar doc** (`analysis/formats/*.md`) covers `.bd`.
- **No ledger entry** (`analysis/evidence/ledger.jsonl`) records a confirmed or rejected claim about `.bd`.
- **No native decoder/descriptor or CMake/test registration** references `.bd`.
- Because whole-disc occurrence is exactly 1 for this suffix in the tracked corpus, any future aggregate ("size range," "bucket counts," "alignment observations") would need either (a) additional same-suffix instances from a differently-sourced disc/region dump processed through the same privacy-safe pipeline, or (b) a passive descriptor added to `fingerprint_assets.py` that emits only aggregate statistics (histogram of header bytes, size bucket, 2048-byte-alignment flag already computed generically for other spans) rather than per-file content, so a single-file aggregate can still be reported without exposing the private input.

## 6. Decoder/tooling status

**Classification: `aggregate_scanner_only`**

- Justification: `tools/fingerprint_assets.py`'s generic `scan_asset()` passively counts every `.bd` span into the recursive/whole-disc extension histograms and runs the shared, extension-agnostic `compression_magic()` sniff against it (same code path as every other unhandled suffix) — this is the "aggregate scanner" tier.
- It is **not** `structural_envelope_only` or `passive_descriptor_only` or `canonical_decoder`: `.bd` has no entry in `FORMAT_HANDLERS` (§2), so no header/field/record extraction of any kind is performed, and no `native/` decoder, descriptor type, or CMake/CTest target references it.
- No adversarial or resource-boundary test exists for `.bd` because no decoder exists to test; this is a decoder-absence gap, not a hardening gap in an existing decoder.

## 7. Codex work order

Ranked, privacy-safe, no semantic speculation:

1. **Add a passive aggregate descriptor for `.bd` in `tools/fingerprint_assets.py`** (mirror the existing `FORMAT_HANDLERS` pattern used for `.col`/`.vum`/etc., but emit only aggregate counters — head-byte histogram bucket, size bucket, 2048-byte-alignment flag, compression-magic hit/no-hit — never per-file paths, names, or hashes). This upgrades `.bd` from `aggregate_scanner_only` toward `passive_descriptor_only` without any semantic inference and without new private-input access.
2. **Run the extended scanner against the existing tracked corpus** (no new inputs) and record the resulting aggregate JSON block into `analysis/formats/asset-fingerprints.json` alongside the other per-format sections, so future dossier revisions have real aggregate facts (currently absent per §3/§5) instead of none.
3. **Grep the full tracked corpus once more for `.bd`, `BD`, and PS2-SDK boot/network/config vocabulary** (case-insensitive, whole-word) across `docs/*.md` and `README.md` to positively rule in/out whether any tracked document already classifies this suffix as a known PS2 system-file family; none was found in this pass, but a future doc addition could change that cheaply.
4. **If step 1's aggregate descriptor surfaces a recognizable magic constant**, cross-check it only against already-published, non-private grammar constants (this repo's own `*.md` docs or public PS2-devkit-format literature already reflected in tracked docs) before promoting any hypothesis in §4 to Confirmed — do not infer semantics from plausibility alone.
5. **Do not** attempt to open, name, or hash-fingerprint the single private `.bd` instance beyond what `disc-summary.json`/`disc-files.jsonl` already record in aggregate; do not access `private/`, `runtime/`, or `third_party/` in pursuit of a second `.bd` sample.
