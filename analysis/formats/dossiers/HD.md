# .hd — Dossier

## 1. Identity

`.hd` is a whole-disc-only file extension with exactly one tracked
occurrence. No tracked source establishes what `.hd` contains, what game
system consumes it, or how it relates to any other extension. It could be
a PS2 IOP/SDK convention (e.g. a "header" half of a header/body pair) or a
game data asset — the tracked evidence in this repository does not
mechanically settle the question either way. Per the task's own
instruction, families the tracked evidence does not explicitly establish
must be kept **UNKNOWN** rather than resolved by plausibility, so this
dossier does **not** assert a `system_file_out_of_scope` stub the way the
`.64` dossier could (that family had a ledger-cited ELF/boot claim; `.hd`
has no equivalent ledger entry). This is therefore a conservative
Unknown-classification dossier, not a confirmed system-file stub and not a
full asset-format dossier.

## 2. Occurrence evidence

| Scope | Count | Source |
|---|---|---|
| Recursive-in-HOG | 0 | `analysis/formats/asset-fingerprints.json` (no `.hd`/`hd` key present in the recursive suffix inventory) |
| Top-level-HOG | 0 | `analysis/formats/hog-validation.json` (no `.hd`/`hd` key present in the top-level HOG member-suffix counts) |
| Whole-disc | 1 | `analysis/manifests/disc-summary.json` (`"extensions": {".hd": 1, ...}`) |

The `.hd` family never appears inside `DATA.HOG`, `LOADING.HOG`, or any
other tracked HOG archive — its single tracked occurrence exists solely as
a top-level disc file, per `analysis/manifests/disc-summary.json`'s
extension histogram.

## 3. Confirmed facts

| # | Claim | Tracked citation |
|---|---|---|
| 1 | Disc-wide inventory records exactly one `.hd`-suffixed file. | `analysis/manifests/disc-summary.json` (`"extensions": {".hd": 1}`) |
| 2 | No `.hd` member occurs inside any tracked HOG archive (recursive or top-level). | `analysis/formats/asset-fingerprints.json`, `analysis/formats/hog-validation.json` |
| 3 | No entry in `tools/fingerprint_assets.py`'s `FORMAT_HANDLERS` dict references `.hd`. | `tools/fingerprint_assets.py` (inspected; `.hd`/`hd` absent from the handler-registered suffix set: `tdx`, `ska`, `skas`, `skm`, `skl`, `vag`, `lpd`, `par`, `col`, `vum`, `vpk`) |
| 4 | No published format-grammar doc under `analysis/formats/*.md` (`HOG.md`, `TDX.md`, `COL.md`, `VUM.md`, `POP.md`, `SO.md`, `LPD.md`, `PAR.md`, `VAG.md`, `VPK.md`, `SKAS.md`, `ASSET-RECON.md`, `FRONTEND-TOPOLOGY.md`) mentions `.hd`. | `analysis/formats/*.md` (inspected; no match) |
| 5 | No `evidence/ledger.jsonl` entry (E-0001 or later) cites `.hd`. | `analysis/evidence/ledger.jsonl` (inspected; no match) |
| 6 | No header in `native/include/omega/retail/*.h` or `native/include/omega/asset/*.h`, and no `native/src/**/*.cpp` decoder or `CMakeLists.txt` registration, references `.hd`. | `native/include/omega/retail/*.h`, `native/include/omega/asset/*.h`, `native/src/**/*.cpp`, `CMakeLists.txt` (inspected; no match) |

## 4. Aggregate-only facts

- In the whole-disc extension histogram, `.hd` sits at count 1, alongside
  other single-occurrence top-level extensions such as `.64` (1), `.bd`
  (1), `.icn` (1), `.ini` (1), `.tbl` (1), and `.wdb` (1) — a cluster of
  extensions that each appear exactly once at the disc root/subtree level
  rather than as a repeating bulk asset class. Source:
  `analysis/manifests/disc-summary.json`.
- `.hd` and `.bd` are both present at count 1 in the same whole-disc
  histogram. This is an aggregate co-occurrence observation only; the
  tracked evidence does not establish any relationship, pairing, or shared
  container between the two files, and none should be inferred here.
  Source: `analysis/manifests/disc-summary.json`.
- `asset-fingerprints.json`'s recursive suffix inventory contains no `hd`
  key of any kind (neither a literal-extension bucket nor a numeric/
  dimension bucket), so there is no recursive-in-HOG aggregate that could
  be mistaken for a `.hd` signal. Source:
  `analysis/formats/asset-fingerprints.json`.

## 5. Hypotheses

- **H1**: `.hd` is a PS2 SDK-style "header" half of a two-part
  header/body streaming convention, possibly paired with the disc's sole
  `.bd` ("body") file. *Confirmation/refutation*: this is currently
  unsupported by any tracked grammar or ledger entry — the tracked data
  shows only that both extensions independently have whole-disc count 1
  (section 4). Confirming would require a privacy-safe, aggregate-only
  structural probe (e.g. extending `tools/fingerprint_assets.py` to record
  the `.hd` file's byte size and a magic-number/structural class, the way
  `FORMAT_HANDLERS` already does for `tdx`/`vag`/etc.) published into a
  tracked `*.md` grammar doc or `asset-fingerprints.json` aggregate —
  without exposing the file's name, path, hash, or payload bytes. A
  refutation would be a structural probe showing no size/format
  relationship between the `.hd` and `.bd` files at all.
- **H2**: `.hd` is a PS2 system/OS-level file (e.g. an IOP module
  descriptor, boot configuration fragment, or save-icon header) rather
  than a decodable game asset, analogous to the `.64` boot-executable
  classification. *Confirmation/refutation*: this cannot be confirmed from
  currently tracked evidence — unlike `.64` (backed by ledger `E-0002`/
  `E-0004` ELF metadata), no ledger entry or format doc characterizes
  `.hd`'s internal structure at all. Confirming would require a new,
  privacy-safe, aggregate-only structural inspection (magic bytes/size
  class only, recorded as a ledger entry) showing PS2-system-file
  structural markers; absent that, H2 stays a hypothesis and the family
  stays classified `unknown`, not `system_file_out_of_scope`.
- **H3**: `.hd` is a game data asset the fingerprinting pipeline has not
  yet been extended to recognize (i.e., a structural-handler gap, not a
  system file). *Confirmation/refutation*: adding `.hd` to
  `tools/fingerprint_assets.py`'s aggregate scanning (size/count bucketing
  only, no payload interpretation) and observing whether it clusters with
  known asset-size distributions in `asset-fingerprints.json` would
  support H3; a result indistinguishable from tiny system-config files
  (e.g. `SYSTEM.CNF`-scale, sub-kilobyte) would weaken it relative to H2.

## 6. Missing observations

- No published `analysis/formats/*.md` grammar document exists for `.hd`.
  A privacy-safe first step is a structural-only pass (magic bytes, total
  size, section/field count if any header is present) recorded as a new
  ledger entry and summarized in an aggregate doc — never a full byte
  dump.
- No `tools/fingerprint_assets.py` `FORMAT_HANDLERS` entry exists for
  `.hd`, so `asset-fingerprints.json` currently cannot report any
  size/bucket aggregate for this family beyond the bare whole-disc count.
  Extending the aggregate scanner (not a semantic decoder) to include
  `.hd` would close this gap.
- No ledger entry documents whether the single tracked `.hd` file was ever
  opened/inspected for magic bytes. This is a pure gap: neither confirmed
  nor rejected in `analysis/evidence/ledger.jsonl`.
- No tracked evidence establishes any relationship (or lack thereof)
  between `.hd` and the disc's sole `.bd` file beyond both having
  whole-disc count 1. A privacy-safe aggregate comparison (size deltas,
  structural-class match/mismatch) is the natural next collection.

## 7. Decoder/tooling status

**Classification: `unknown`**

Justification: none of the four decoder/tooling tiers apply. There is no
canonical decoder (`.hd` is absent from `FORMAT_HANDLERS` in
`tools/fingerprint_assets.py` and from every `native/include/omega/asset/
*.h` / `native/include/omega/retail/*.h` header and `CMakeLists.txt`
registration — inspected, no match). There is no structural-envelope-only
or passive-descriptor-only handler either, for the same reason. There is
no aggregate-scanner entry specific to `.hd` beyond the generic whole-disc
extension-histogram count already present in `disc-summary.json` (that
histogram is produced by the manifest tooling generically for every
extension on disc, not by a `.hd`-aware scanner). And critically, the
task's system-file stub path (`system_file_out_of_scope`) requires a
citable basis for classifying the file as PS2 system/OS content — the
`.64` dossier had ledger-backed ELF/boot evidence to justify that stub;
`.hd` has no analogous ledger entry, grammar mention, or structural
citation. Asserting `system_file_out_of_scope` here without that basis
would be exactly the kind of plausibility-driven semantic invention the
clean-room rules forbid. The correct, conservative classification is
therefore `unknown`.

## 8. Codex work order

1. **(Highest priority)** Extend `tools/fingerprint_assets.py`'s
   aggregate scanning to record a structural class (magic-bytes bucket,
   size) for the whole-disc `.hd` file — mirroring how `FORMAT_HANDLERS`
   already reports structural aggregates for `tdx`/`vag`/`col`/etc. —
   without emitting the file's name, path, hash, or payload bytes into any
   published doc. Publish the result as a new `analysis/evidence/ledger.jsonl`
   entry (aggregate-only) rather than editing this dossier's prose
   directly, then fold the confirmed fact back into section 3 here.
2. Run the same structural probe against the disc's sole `.bd` file and
   record both results side by side (aggregate size/magic-class only) to
   mechanically test H1 (header/body pairing) instead of leaving it as an
   unverified hypothesis.
3. Grep the tracked `SYSTEM.CNF`/boot-config aggregate fields (already
   present in `disc-summary.json`'s `top_level_bytes`) for any reference
   token that might name an `.hd`-suffixed target, the same privacy-safe
   technique proposed for `.64`'s H1 — this would let H2 (system-file)
   move from Hypothesis toward Confirmed or get refuted without opening
   any private input.
4. If the structural probe in item 1 shows `.hd` clustering with known
   tiny system-config files (sub-kilobyte, no repeating asset-size
   pattern), promote the classification to `system_file_out_of_scope`
   with a ledger citation; if it shows a repeating structural signature
   comparable to a known asset family, treat it as a fingerprinting-gap
   candidate (H3) and scope a `HD.md` grammar doc — but do not make either
   call without the item-1 evidence in hand.
