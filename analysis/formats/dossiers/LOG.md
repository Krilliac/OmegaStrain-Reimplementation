# .log — Dossier (stub classification)

## 1. Identity

`.log` is a whole-disc-only suffix appearing exactly once in the tracked
disc inventory. It never occurs inside any `.hog` archive. Tracked evidence
places the single occurrence at the top level of the disc image inside the
`NETGUI` directory — a directory the repository's own `.GUI` dossier
already characterizes, from tracked manifests, as the PS2 network-boot
stack (its other established members being `.RGB`/`.BIN`/`.IRX`/`.ELF`/
`.TM2`/`.PF` files, none of which are game-content asset formats). On that
basis this dossier classifies `.log` as a **PS2 system/network-boot support
file**, not a game data asset, and is therefore written as a **concise
stub** per the task's occurrence-gate instruction rather than a full
asset-format dossier.

## 2. Occurrence evidence

| Scope | Count | Tracked source |
|---|---|---|
| Recursive-in-HOG (inside any `.hog` archive, any depth) | 0 | `analysis/formats/asset-fingerprints.json` (recursive suffix inventory) |
| Top-level-in-HOG (direct members of a top-level `.hog`) | 0 | `analysis/formats/hog-validation.json` (top-level HOG member-suffix counts) |
| Whole-disc (anywhere on the extracted disc tree) | 1 | `analysis/manifests/disc-summary.json`, key `extensions[".log"] = 1`; corroborated by exactly one matching row in `analysis/manifests/disc-files.jsonl` |

Per clean-room rules (and matching this repository's own `.BD` dossier
precedent), the specific leaf filename, size, and hash of that one
`disc-files.jsonl` row are per-file/private-input detail and are withheld
here. Only the top-level container (`NETGUI`) — already a generic,
previously-published container name per the tracked `.GUI` dossier and
`disc-summary.json`'s `top_level_bytes` — is cited.

## 3. Confirmed facts

| # | Claim | Tracked citation |
|---|---|---|
| 1 | Disc-wide inventory records exactly one `.log`-suffixed file. | `analysis/manifests/disc-summary.json` (`extensions[".log"] = 1`), `analysis/manifests/disc-files.jsonl` |
| 2 | No `.log` member occurs inside any tracked HOG archive (recursive or top-level). | `analysis/formats/asset-fingerprints.json`, `analysis/formats/hog-validation.json` |
| 3 | The disc's top-level `NETGUI` directory is documented, in the repo's own tracked `.GUI` dossier, as containing PS2 network-boot-stack members (`.RGB`, `.BIN`, `.IRX`, `.ELF`, `.TM2`, `.PF`) — none of which is a game-content asset family. | `analysis/formats/dossiers/GUI.md` (§ occurrence note); `analysis/manifests/disc-summary.json` (`top_level_bytes.NETGUI`) |
| 4 | `.log` has no entry in `tools/fingerprint_assets.py`'s `FORMAT_HANDLERS` (which lists only `.tdx`, `.ska`, `.skas`, `.skm`, `.skl`, `.vag`, `.lpd`, `.par`, `.col`, `.vum`, `.vpk`). | `tools/fingerprint_assets.py` |
| 5 | No `analysis/formats/*.md` published grammar document (`HOG.md`, `TDX.md`, `COL.md`, `VUM.md`, `POP.md`, `SO.md`, `LPD.md`, `PAR.md`, `VAG.md`, `VPK.md`, `SKAS.md`, `ASSET-RECON.md`, `FRONTEND-TOPOLOGY.md`) mentions `.log`. | direct inspection, `analysis/formats/*.md` |
| 6 | No `analysis/evidence/ledger.jsonl` entry makes a claim about a `.log` container format, header, or decoder. (Ledger entry `E-0002` references a *runtime tool-output* file path ending `.log` as evidence for an unrelated boot-CRC claim — that is the analysis tooling's own log output, not the tracked disc's `.log` file, and is not evidence about this format family.) | `analysis/evidence/ledger.jsonl` |
| 7 | No `native/include/omega/retail/*.h` or `native/include/omega/asset/*.h` decoder/descriptor type references `.log`, and no `CMakeLists.txt` target registers a `.log` source, decoder, or test. (`native/include/omega/runtime/log_service.h` defines the *emulator's own* in-process logging severity/record types — an engine-authoring facility, unrelated to parsing the tracked disc's `.log` file.) | `native/include/omega/retail/*.h`, `native/include/omega/asset/*.h`, `native/include/omega/runtime/log_service.h`, `CMakeLists.txt` |

## 4. Aggregate-only facts

- `analysis/manifests/disc-summary.json`'s whole-disc extension histogram
  records `.log` at count 1 out of `file_count: 448`, alongside other
  single-count top-level extensions in the same histogram (`.64`, `.bd`,
  `.hd`, `.icn`, `.ini`, `.map`, `.sys`, `.tbl`, `.wdb`) — a pattern of
  unique, non-repeating top-level artifacts rather than a bulk asset class.
- `analysis/formats/asset-fingerprints.json` contains no per-format
  structural aggregate block for `.log` (such blocks exist only for the
  `FORMAT_HANDLERS`-listed suffixes named in §3 row 4), confirming `.log`
  is scan-visible (counted into the generic extension histogram) but never
  structurally opened by the fingerprinting tool.
- `tools/fingerprint_assets.py`'s generic `scan_asset()` pass tallies every
  scanned span — `.log` included — into `scan.count("extensions", ...)`
  and runs the shared, extension-agnostic `compression_magic(head)` sniff
  against it; this is a passive count/compression-sniff identical to the
  treatment of every other unhandled suffix, not a format-specific decode.
- `disc-summary.json`'s `top_level_bytes.NETGUI` records only the
  directory's aggregate byte total (30,306,038 bytes across all of
  `NETGUI`'s members combined); no per-member size breakdown for `.log`
  specifically is published anywhere in tracked docs.

## 5. Hypotheses

- **H1 — Network/DNAS initialization log.** The `.log` file could be a
  diagnostic or status log emitted by the PS2 network-boot stack housed in
  `NETGUI` (the directory tracked `.GUI` dossier already characterizes as
  network-boot infrastructure), rather than by the game's asset-loading or
  gameplay systems.
  *Confirmation/refutation:* a privacy-safe passive descriptor (see §6)
  that reports only aggregate structural signals (size bucket, ASCII-vs-
  binary head-byte classification, line-count-if-text) without emitting
  per-file identifiers; if the head bytes classify as plain ASCII text with
  a line-oriented structure, that is consistent with H1 (a text log) but
  does not by itself prove network-boot origin.
- **H2 — Build/dev-tool artifact, not a runtime-emitted file.** The `.log`
  file could be a leftover build-pipeline or authoring-tool log
  accidentally included on the retail disc image, unrelated to any
  in-game or boot-time system.
  *Confirmation/refutation:* would require comparing the file's aggregate
  size/text-classification against known PS2 SDK/devkit log conventions
  already reflected in tracked public documentation — no such comparison
  exists yet in this repository.

## 6. Missing observations

- **No structural or passive descriptor for `.log` exists** in
  `tools/fingerprint_assets.py` — no head-byte classification, size
  bucket, or text/binary determination has ever been mechanically
  extracted or aggregated for this suffix.
- **No published grammar doc** covers `.log`, and none is expected if the
  system-file classification in §1 holds.
- **No ledger entry** records a confirmed or rejected claim about the
  tracked disc's `.log` file specifically (as distinct from unrelated
  runtime tool-output files that happen to share the `.log` extension).
- Because whole-disc occurrence is exactly 1, no size range, alignment, or
  bucket distribution is available; any future aggregate would require
  either (a) a second same-suffix instance from a differently-sourced disc
  dump processed through the same privacy-safe pipeline, or (b) a passive
  descriptor added to `fingerprint_assets.py` that emits only aggregate
  statistics (head-byte histogram, size bucket, ASCII/binary flag) for the
  single existing instance without exposing its private per-file content.

## 7. Decoder/tooling status

**Classification: `system_file_out_of_scope`**

Justification (one paragraph): the tracked evidence mechanically
establishes that the sole `.log` occurrence lives at the top level of the
`NETGUI` directory, which the repository's own tracked `.GUI` dossier
already documents (from `disc-summary.json`/`disc-files.jsonl`) as the
PS2 network-boot-stack container holding `.RGB`/`.BIN`/`.IRX`/`.ELF`/
`.TM2`/`.PF` system files rather than game-content assets; `.log` has zero
occurrences inside any HOG archive (recursive or top-level), is absent from
every published `analysis/formats/*.md` grammar document, has no entry in
`tools/fingerprint_assets.py`'s `FORMAT_HANDLERS`, and no
`native/include/omega/retail/*.h` or `native/include/omega/asset/*.h`
decoder/descriptor or `CMakeLists.txt` registration treats it as a
decodable/registered asset format. The only native header whose name
contains "log" (`native/include/omega/runtime/log_service.h`) is the
emulator's own in-process logging facility, not a parser for the disc's
`.log` file, and is not evidence of any decoder for this family. Building
an asset decoder for `.log` would therefore be unjustified scope creep for
what the tracked, directory-level evidence indicates is a system/network
support artifact, not a class of game data asset.

## 8. Codex work order

Ranked, privacy-safe, no semantic speculation:

1. **(Highest priority) No new decoder or fingerprint-handler work for
   `.log`.** Do not add `.log` to `tools/fingerprint_assets.py`
   `FORMAT_HANDLERS` or to any `native/include/omega/asset/*.h` descriptor
   set — tracked directory-level evidence classifies this as a system/
   network-boot-stack support file, and adding asset-decoder machinery for
   it would be unjustified scope creep with no supporting grammar.
2. **Add a passive, aggregate-only descriptor** for `.log` in
   `tools/fingerprint_assets.py` (mirroring the existing `FORMAT_HANDLERS`
   registration pattern, but emitting only aggregate counters: size
   bucket, ASCII-vs-binary head-byte classification, 2048-byte-alignment
   flag, compression-magic hit/no-hit — never per-file paths, names, or
   hashes). This would let H1/H2 in §5 move toward Confirmed/Refuted
   without exposing the private input.
3. **Run the extended scanner against the existing tracked corpus** (no
   new inputs) and record the resulting aggregate JSON block into
   `analysis/formats/asset-fingerprints.json`, so a future revision of
   this dossier has real aggregate facts instead of the single occurrence
   count currently available.
4. **Re-run the existing recursive/top-level HOG suffix scans** after any
   future disc re-extraction to confirm `.log`'s recursive-in-HOG and
   top-level-HOG counts remain 0; record the result as a new ledger entry
   rather than assuming permanence.
5. **Do not** attempt to open, name, or hash-fingerprint the single
   private `.log` instance beyond what `disc-summary.json`/
   `disc-files.jsonl` already record in aggregate; do not access
   `private/`, `runtime/`, or `third_party/` in pursuit of a second `.log`
   sample.
