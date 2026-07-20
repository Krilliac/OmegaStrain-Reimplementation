# .IMG — Dossier (system-file stub)

**Identity (evidence-level):** `.img` occurs only as whole-disc filesystem entries, never inside any `.hog` archive. Tracked structural evidence — exclusive placement in the same top-level disc directories that also hold `.irx` entries and other PS2 boot/network/memory-card support files, and the total absence of `.img` from every game-asset grammar doc, format handler, and evidence-ledger entry in this repo — supports classifying `.img` as a **PS2 system/support file family, out of scope for the OpenOmega asset decoder**, rather than a game asset. No tracked source defines its internal grammar, so this is a structural/placement inference, not a confirmed header decode. Per the task brief, this dossier is a concise stub rather than a full asset-grammar dossier.

## 1. Occurrence evidence

| Scope | Count | Tracked source |
|---|---|---|
| Recursive-in-HOG (inside any `.hog` archive, any depth) | 0 | `analysis/formats/asset-fingerprints.json` (recursive suffix inventory) |
| Top-level-in-HOG (direct members of a top-level `.hog`) | 0 | `analysis/formats/hog-validation.json` (top-level HOG member-suffix counts) |
| Whole-disc (anywhere on the extracted disc tree) | 3 | `analysis/manifests/disc-summary.json`, key `extensions[".img"] = 3`; corroborated by exactly three matching rows in `analysis/manifests/disc-files.jsonl` |

`.img` is therefore a whole-disc-only suffix: it exists strictly as top-level filesystem entries on the disc image and is never packed as content inside any `.hog` container, recursive or top-level. Per the clean-room rules, the specific per-row paths, sizes, and hashes in `disc-files.jsonl` are per-file/private-input detail and are withheld beyond the aggregate/relational facts stated in §3.

## 2. Confirmed facts

None at the grammar/decoder level. A mechanically citable Confirmed fact requires a named tracked file that defines grammar, header magic, a ledger entry (`E-####`), or a native decoder/descriptor for this suffix. Checked and absent:

- `tools/fingerprint_assets.py` — `FORMAT_HANDLERS` contains only `.tdx`, `.ska`, `.skas`, `.skm`, `.skl`, `.vag`, `.lpd`, `.par`, `.col`, `.vum`, `.vpk`. `.img` is **not** a key, and the tool's whole-disc scan pass (`target_extensions = set(FORMAT_HANDLERS)`) explicitly skips extensions outside that set, so `.img` is not even passed to the generic per-span scanner during whole-disc scanning.
- `analysis/formats/*.md` — no published grammar document exists for `.img` (grep of the whole directory for `img` found no hits prior to this dossier).
- `analysis/evidence/ledger.jsonl` — no entry's claim text mentions `.img`.
- `native/include/omega/retail/*.h`, `native/include/omega/asset/*.h`, `native/src/**/*.cpp`, `CMakeLists.txt` — no `.img`-related type, descriptor, source file, or test/target registration.

The one fact that *is* mechanically confirmed is structural placement, not grammar:

- `analysis/manifests/disc-summary.json`, `top_level_bytes` — the disc's top-level entries include an `IRX` directory and a `NETGUI` directory (among `GAMEDATA`, `OVL_DNAS.BIN`, `SCUS_972.64`, `SFO_GAME.INI`, `SYSTEM.CNF`). `analysis/manifests/disc-files.jsonl` confirms all three whole-disc `.img` rows live under these two top-level directories, alongside the disc's `.irx` entries (`extensions[".irx"] = 46` in `disc-summary.json`) and, in the `NETGUI` tree, alongside a `.sys`-suffixed memory-card icon-config entry and other network-GUI support files. No tracked source defines what `.irx` or the `NETGUI`/`SYSTEM.CNF`/`OVL_DNAS.BIN` top-level entries mean either, so this is cited only as co-location, not as a semantic claim about either extension.

## 3. Aggregate-only facts

- `analysis/manifests/disc-summary.json`: `.img` totals 3 of `file_count: 448` whole-disc files — a low-count extension in the same histogram tier as other single/low-count non-`.hog` suffixes (`.bd`, `.hd`, `.icn`, `.ini`, `.log`, `.map`, `.sys`, `.tbl`, `.wdb`), distinct from the multi-hundred/multi-thousand `.hog`-family and game-asset counts (`.hog`=273, `.pop`=18, `.tm2`=16).
- `analysis/formats/asset-fingerprints.json`: no per-format aggregate block exists for `.img` (only `FORMAT_HANDLERS` suffixes get one), and the tool's whole-disc pass does not even tally `.img` into its generic scan counters (see §2) because the pass is restricted to `FORMAT_HANDLERS` keys. There is consequently **no** magic-byte, alignment, or compression-sniff aggregate for `.img` anywhere in tracked output.
- `analysis/manifests/disc-files.jsonl` (relational comparison only, no raw values disclosed): of the three whole-disc `.img` rows, two are byte-identical to each other (identical SHA-256 digest and identical size), and the third is distinct from both. The two identical rows sit in two different top-level directories (one under `IRX`, one under a subdirectory of `NETGUI`), i.e. the same content is duplicated verbatim across two separate on-disc locations. This is reported only as a same/different relation between rows, per the clean-room ban on disclosing per-file size/hash values.

## 4. Hypotheses

All statements below are explicitly labeled hypotheses, not facts.

- **H1 — PS2 IOP system-module image, not a game asset.** The exclusive whole-disc placement, total absence from every `.hog` archive, co-location with the disc's `.irx` entries under both the `IRX` and `NETGUI` top-level directories, and the verbatim duplication of one `.img` payload across two separate module-loading locations (§3) are consistent with `.img` being a PS2 IOP-side system/boot-support container (e.g., a bundled IOP module or reset-program image) rather than a game render/audio asset.
  - *Privacy-safe confirmation/refutation:* add a passive, `FORMAT_HANDLERS`-style descriptor for `.img` in `tools/fingerprint_assets.py` that reports only aggregate structural signals (head-byte histogram bucket, size bucket, 2048-byte-alignment flag) across the three existing whole-disc instances, with no per-file identifiers. If the resulting aggregate matches a documented PS2 SDK/BIOS structural signature already reflected in a tracked doc, that would move this toward Confirmed; absence of a match leaves it a hypothesis, not a refutation.
- **H2 — Compression-wrapped or opaque blob.** Because `.img` is never passed through `scan_asset()`'s generic `compression_magic()` sniff during whole-disc scanning (§2), it is possible the payload is itself a compressed or encrypted container unrelated to any tracked asset family.
  - *Privacy-safe confirmation/refutation:* extend the whole-disc scan pass to also run the existing generic `compression_magic(head)` check against `.img` spans and record only an aggregate hit/no-hit counter (no per-file output) in `asset-fingerprints.json`.

## 5. Missing observations

- **No structural handler for `.img` exists** in `tools/fingerprint_assets.py`, and — unlike unhandled-but-scanned suffixes such as `.bd` — `.img` is not even reached by the tool's generic whole-disc extension/compression counters, because the whole-disc scan loop is restricted to `FORMAT_HANDLERS` keys. This is a strictly larger observation gap than for a typical unhandled suffix.
- **No published grammar doc, ledger entry, or native decoder/descriptor** references `.img` anywhere in the tracked tree.
- With whole-disc n=3 and zero HOG occurrences, there is no size-range, alignment, or bucket-distribution aggregate beyond the relational duplicate-detection fact already stated in §3.
- No tracked source defines `.irx` semantics either, so the co-location evidence in §2/§4 cannot be strengthened into a Confirmed classification without either (a) a tracked doc that defines `.irx`/`.img` as a known PS2 SDK module-format pair, or (b) an aggregate structural descriptor (per H1/H2) that surfaces a recognizable, already-published magic constant.

## 6. Decoder/tooling status

**Classification: `system_file_out_of_scope`**

- Justification: zero HOG occurrences, whole-disc-only placement, no `FORMAT_HANDLERS` entry, no aggregate scan coverage at all (not even the generic extension/compression counters other unhandled suffixes receive), no published grammar doc, no ledger entry, and no native decoder/descriptor or CMake/test registration (§2). Combined with the structural co-location evidence in §2/§4 (exclusive placement alongside `.irx` entries and other disc-root boot/network/memory-card support files, never among the `.hog`-packed game-asset corpus), the tracked evidence supports treating `.img` as outside the OpenOmega game-asset decoder's scope rather than as an undecoded game format.
- This is a structural-placement inference, not a confirmed magic-byte decode — no tracked source has opened an `.img` header. If future tracked evidence (a grammar doc, ledger entry, or structural descriptor per §4) established that `.img` payloads carry game-relevant content, this classification would need to be revisited.
- No adversarial or resource-boundary test exists for `.img` because no decoder exists to test; this is a decoder-absence/out-of-scope gap, not a hardening gap in an existing decoder.

## 7. Codex work order

Ranked, privacy-safe, no semantic speculation:

1. **Confirm the out-of-scope call cheaply before building anything:** grep the full tracked corpus once more (case-insensitive, whole-word) for `img`, `IOPRP`, `DNAS`, and general PS2-SDK boot/IOP vocabulary across `docs/*.md`/`README.md` to positively rule in/out whether any tracked document already classifies this family; none was found in this pass.
2. **If broader system-file coverage is later wanted**, add a passive aggregate descriptor for `.img` in `tools/fingerprint_assets.py` (mirror the `FORMAT_HANDLERS` pattern, emit only aggregate counters — head-byte histogram bucket, size bucket, alignment flag, compression-magic hit/no-hit — never per-file paths, names, or hashes) and route it through the whole-disc scan loop, which currently skips it entirely (§5). This would upgrade `.img` from `system_file_out_of_scope`/no-coverage toward `aggregate_scanner_only` without new private-input access or semantic inference.
3. **Do not** build a canonical or structural-envelope decoder for `.img` unless a tracked doc or ledger entry first establishes it carries game-relevant content — per H1, current tracked evidence points away from that, and inventing a decoder for a plausible-but-unconfirmed system file would be exactly the kind of regression the clean-room rules forbid.
4. **Do not** attempt to open, name, or hash-fingerprint the three private `.img` instances beyond what `disc-summary.json`/`disc-files.jsonl` already record in aggregate/relational form; do not access `private/`, `runtime/`, or `third_party/` in pursuit of additional samples or magic-byte confirmation.
