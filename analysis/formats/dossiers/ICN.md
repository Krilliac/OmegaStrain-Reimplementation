# .icn — Dossier

## 1. Identity

`.icn` is a whole-disc-only file extension with exactly one tracked
occurrence. No tracked source in this repository (grammar doc, evidence
ledger, native decoder, or fingerprinting handler) mechanically establishes
what the file's internal structure is, what subsystem consumes it, or
whether it is a PS2 platform/system artifact (e.g. an IOP module, boot
configuration fragment, or memory-card save icon) versus a game data
asset. The `.icn` extension is *externally* associated in general PS2
platform knowledge with memory-card save-data 3D icon files, but that
association is not sourced from any tracked file in this repository and
must not be used to assert a semantic classification here — doing so would
be exactly the plausibility-driven invention the clean-room rules forbid.
Per the task's own instruction, families the tracked evidence does not
explicitly establish must be kept **UNKNOWN** rather than resolved by
plausibility. This dossier therefore does **not** assert a
`system_file_out_of_scope` stub the way the `.64` dossier could (`.64` had
ledger-cited ELF/boot evidence, `E-0002`/`E-0004`); `.icn` has no equivalent
ledger entry. This is a conservative Unknown-classification dossier, not a
confirmed system-file stub and not a full asset-format dossier.

## 2. Occurrence evidence

| Scope | Count | Source |
|---|---|---|
| Recursive-in-HOG | 0 | `analysis/formats/asset-fingerprints.json` (no `.icn`/`icn` key present in the recursive suffix inventory) |
| Top-level-HOG | 0 | `analysis/formats/hog-validation.json` (no `.icn`/`icn` key present in the top-level HOG member-suffix counts) |
| Whole-disc | 1 | `analysis/manifests/disc-summary.json` (`"extensions": {".icn": 1, ...}`) |

The `.icn` family never appears inside `DATA.HOG`, `LOADING.HOG`, or any
other tracked HOG archive — its single tracked occurrence exists solely as
a top-level disc file, per `analysis/manifests/disc-summary.json`'s
extension histogram and confirmed independently by the single matching
record in `analysis/manifests/disc-files.jsonl`.

## 3. Confirmed facts

| # | Claim | Tracked citation |
|---|---|---|
| 1 | Disc-wide inventory records exactly one `.icn`-suffixed file. | `analysis/manifests/disc-summary.json` (`"extensions": {".icn": 1}`) |
| 2 | The single tracked `.icn` file is 39,224 bytes. | `analysis/manifests/disc-files.jsonl` (the one record whose path ends in `.ICN`) |
| 3 | No `.icn` member occurs inside any tracked HOG archive (recursive or top-level). | `analysis/formats/asset-fingerprints.json`, `analysis/formats/hog-validation.json` |
| 4 | No entry in `tools/fingerprint_assets.py`'s `FORMAT_HANDLERS` dict references `.icn`. | `tools/fingerprint_assets.py` (inspected; `.icn`/`icn` absent from the handler-registered suffix set: `tdx`, `ska`, `skas`, `skm`, `skl`, `vag`, `lpd`, `par`, `col`, `vum`, `vpk`) |
| 5 | No published format-grammar doc under `analysis/formats/*.md` (`HOG.md`, `TDX.md`, `COL.md`, `VUM.md`, `POP.md`, `SO.md`, `LPD.md`, `PAR.md`, `VAG.md`, `VPK.md`, `SKAS.md`, `ASSET-RECON.md`, `FRONTEND-TOPOLOGY.md`) mentions `.icn`. | `analysis/formats/*.md` (inspected; no match) |
| 6 | No `evidence/ledger.jsonl` entry (E-0001 or later) cites `.icn`. | `analysis/evidence/ledger.jsonl` (inspected; no match) |
| 7 | No header in `native/include/omega/retail/*.h` or `native/include/omega/asset/*.h`, and no `native/src/**/*.cpp` decoder or `CMakeLists.txt` registration, references `.icn`. | `native/include/omega/retail/*.h`, `native/include/omega/asset/*.h`, `native/src/**/*.cpp`, `CMakeLists.txt` (inspected; no match) |
| 8 | The native tree contains a synthetic PS2 memory-card filesystem/export subsystem (`native/include/omega/compat/ps2_memory_card_filesystem.h`, `ps2_memory_card_export.h`, `ps2_memory_card_image.h`, and their tests) that models a generic PS2 memory-card FAT layout, including a fixture entry literally named `"icon.sys"` in its test suite — but this subsystem parses only the card filesystem/directory structure, not any `.ICN` icon-geometry payload; no symbol, comment, or test in that subsystem decodes `.icn` file contents. | `native/tests/ps2_memory_card_filesystem_tests.cpp`, `native/tests/ps2_memory_card_export_tests.cpp`, `native/include/omega/compat/ps2_memory_card_filesystem.h`, `native/include/omega/compat/ps2_memory_card_export.h`, `native/include/omega/compat/ps2_memory_card_image.h` (inspected; `icon.sys` appears only as a synthetic fixture/test filename exercising directory-entry logic, never as an `.icn`-format decoder) |

## 4. Aggregate-only facts

- In the whole-disc extension histogram, `.icn` sits at count 1, alongside
  other single-occurrence top-level extensions such as `.64` (1), `.hd`
  (1), `.bd` (1), `.ini` (1), `.tbl` (1), and `.wdb` (1) — a cluster of
  extensions that each appear exactly once at the disc root/subtree level
  rather than as a repeating bulk asset class. Source:
  `analysis/manifests/disc-summary.json` (this same clustering observation
  is independently documented in the existing `HD.md` dossier).
- The single `.icn` file's size (39,224 bytes) is roughly an order of
  magnitude larger than a typical PS2 `SYSTEM.CNF`-scale text/config file
  and roughly an order of magnitude smaller than the disc's largest bulk
  assets; this is a bare size observation only — no structural or
  bucket-membership inference is drawn from it here. Source:
  `analysis/manifests/disc-files.jsonl`.
- `asset-fingerprints.json`'s recursive suffix inventory contains no `icn`
  key of any kind (neither a literal-extension bucket nor a numeric/
  dimension bucket), so there is no recursive-in-HOG aggregate that could
  be mistaken for an `.icn` signal. Source:
  `analysis/formats/asset-fingerprints.json`.

## 5. Hypotheses

- **H1**: `.icn` is a PS2 platform-level memory-card save-data icon file
  (the well-known `.ICN` 3D-icon convention used across many PS2 titles),
  making it a system/OS-adjacent artifact rather than a decodable game
  asset. This hypothesis is informed by general PS2 platform knowledge
  *external to this repository's tracked sources* — no tracked file here
  establishes it, and it must not be treated as Confirmed. *Confirmation/
  refutation*: a privacy-safe structural probe (magic bytes / header field
  layout only, no payload dump) recorded as a new ledger entry showing the
  well-documented PS2 icon-file magic/header shape would move this toward
  Confirmed; a structural probe showing no such header, or a shape
  matching a known bulk-asset family instead, would refute it.
- **H2**: `.icn` is a game data asset the fingerprinting pipeline has not
  yet been extended to recognize (a structural-handler gap, not a system
  file) — analogous to `HD.md`'s H3 for `.hd`. *Confirmation/refutation*:
  adding `.icn` to `tools/fingerprint_assets.py`'s aggregate scanning
  (size/magic-bucket only, no payload interpretation) and comparing its
  structural class against known asset families (`TDX.md`, `COL.md`,
  `VUM.md`, etc.) would support H2; a result matching the well-documented
  PS2 icon-file header shape instead would weaken it relative to H1.
- **H3**: The disc's single `.icn` file and the native, synthetic PS2
  memory-card filesystem subsystem (fact 8, section 3) are unrelated —
  that subsystem was built from public PS2 memory-card filesystem
  documentation as a generic save-data feature, not by reverse-engineering
  the disc's actual `.icn` file. *Confirmation/refutation*: this is
  supported by the tracked evidence already inspected (no code path
  reads/parses the disc's `.icn` bytes); it would only be refuted if a
  future tracked commit adds a code path or test that ingests the actual
  disc `.icn` file's structural signature.

## 6. Missing observations

- No published `analysis/formats/*.md` grammar document exists for `.icn`.
  A privacy-safe first step is a structural-only pass (magic bytes, total
  size, section/field count if any header is present) recorded as a new
  ledger entry and summarized in an aggregate doc — never a full byte
  dump, and never the file's on-disc name or path.
- No `tools/fingerprint_assets.py` `FORMAT_HANDLERS` entry exists for
  `.icn`, so `asset-fingerprints.json` currently cannot report any
  size/bucket or magic-class aggregate for this family beyond the bare
  whole-disc count.
- No ledger entry documents whether the single tracked `.icn` file was
  ever opened/inspected for magic bytes or header shape. This is a pure
  gap: neither confirmed nor rejected in `analysis/evidence/ledger.jsonl`.
- No tracked evidence establishes or refutes any relationship between the
  disc's `.icn` file and the native PS2 memory-card filesystem subsystem
  (fact 8, section 3) beyond both existing in the same tracked tree.

## 7. Decoder/tooling status

**Classification: `unknown`**

Justification: none of the other five tiers apply. There is no canonical
decoder (`.icn` is absent from `FORMAT_HANDLERS` in
`tools/fingerprint_assets.py` and from every `native/include/omega/asset/
*.h` / `native/include/omega/retail/*.h` header and `CMakeLists.txt`
registration — inspected, no match). There is no structural-envelope-only
or passive-descriptor-only handler either, for the same reason, and the
native PS2 memory-card filesystem/export subsystem (fact 8, section 3)
does not decode `.icn` payloads — it models the surrounding memory-card
FAT/directory structure only, using a synthetic `"icon.sys"` fixture name
that never touches the disc's actual `.icn` bytes; treating it as an
`.icn` decoder would misattribute an unrelated subsystem. There is no
aggregate-scanner entry specific to `.icn` beyond the generic whole-disc
extension-histogram count already present in `disc-summary.json` (produced
generically for every extension, not by an `.icn`-aware scanner). And
critically, the task's system-file stub path (`system_file_out_of_scope`)
requires a citable basis for classifying the file as PS2 system/OS
content — the `.64` dossier had ledger-backed ELF/boot evidence (`E-0002`,
`E-0004`) to justify that stub; `.icn` has no analogous ledger entry,
grammar mention, or structural citation in this repository. Asserting
`system_file_out_of_scope` here on the strength of general, out-of-band PS2
platform knowledge about the `.ICN` extension — however plausible — would
be exactly the kind of plausibility-driven semantic invention the
clean-room rules forbid. The correct, conservative classification is
therefore `unknown`, mirroring the existing `HD.md` precedent for a
sibling single-occurrence whole-disc extension with the same evidence
profile.

## 8. Codex work order

1. **(Highest priority)** Extend `tools/fingerprint_assets.py`'s
   aggregate scanning to record a structural class (magic-bytes bucket,
   header field count if present, size) for the whole-disc `.icn` file —
   mirroring how `FORMAT_HANDLERS` already reports structural aggregates
   for `tdx`/`vag`/`col`/etc. — without emitting the file's name, path,
   hash, or payload bytes into any published doc. Publish the result as a
   new `analysis/evidence/ledger.jsonl` entry (aggregate-only) rather than
   editing this dossier's prose directly, then fold the confirmed fact
   back into section 3 here. This single probe is the fastest path to
   resolving H1 vs. H2.
2. If the structural probe in item 1 shows a header/magic shape consistent
   with the well-documented PS2 memory-card icon-file convention (H1),
   promote the classification to `system_file_out_of_scope` with an
   explicit ledger citation for the structural evidence — do not make that
   call on general platform knowledge alone.
3. If the structural probe instead shows a shape matching a known bulk
   asset family or an unrecognized-but-repeating structural signature,
   treat it as a fingerprinting-gap candidate (H2) and scope a proper
   `ICN.md` grammar doc, following the same aggregate-only discipline as
   `TDX.md`/`COL.md`.
4. Cross-check whether the native PS2 memory-card filesystem/export
   subsystem (`native/include/omega/compat/ps2_memory_card_*.h`) was ever
   intended to consume the disc's actual `.icn` bytes; if a future commit
   wires that subsystem to the real file, record the resulting behavior as
   a new ledger entry rather than assuming H3's current "unrelated"
   conclusion holds indefinitely.
