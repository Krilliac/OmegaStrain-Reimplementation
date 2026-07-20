# .ICO — Dossier

## 1. Identity

`.ico` on this disc is a **PS2 platform/system file, not a game asset** — classified `system_file_out_of_scope`. The suffix is used exactly twice on the whole disc, under a directory named `NETGUI/CNF` and a mirrored copy under `GAMEDATA/NETWORK`, both sharing the literal name `SYS_NET.ICO`. This directory naming (`NETGUI`, `CNF`) and the paired presence of `.cnf`-suffixed files elsewhere on the disc are consistent with the PS2 platform convention of shipping a network-configuration file set (browser/system-menu icon + config files) for titles that support network play, rather than with the title's own HOG-packed asset pipeline. No tracked source provides byte-level grammar for PS2 system icon files, so this dossier does not attempt to reverse a container format — it documents occurrence evidence and scopes the family out of the asset-fingerprinting effort.

## 2. Occurrence evidence

| Metric | Value | Source |
|---|---|---|
| Recursive-in-HOG occurrences | 0 | analysis/formats/asset-fingerprints.json (suffix absent from its per-format aggregate table) |
| Top-level-HOG occurrences | 0 | analysis/formats/hog-validation.json (suffix absent from its top-level member-suffix counts) |
| Whole-disc occurrences | 2 | analysis/manifests/disc-summary.json — `"extensions"` → `".ico": 2` |
| Whole-disc file rows | 2 (paths `GAMEDATA/NETWORK/SYS_NET.ICO`, `NETGUI/CNF/SYS_NET.ICO`) | analysis/manifests/disc-files.jsonl |
| Evidence ledger (E-####) entries mentioning `.ico` | 0 (one substring collision on the word "Unicode" in E-0089 was checked and ruled a false positive — E-0089 is unrelated front-end/menu work) | analysis/evidence/ledger.jsonl |

## 3. Confirmed facts

| # | Fact | Citation |
|---|---|---|
| C1 | Exactly two `.ico` files exist anywhere on the tracked whole-disc manifest, and zero occur inside any `.HOG` archive (recursive or top-level). | analysis/formats/asset-fingerprints.json; analysis/formats/hog-validation.json; analysis/manifests/disc-summary.json |
| C2 | Both `.ico` disc rows share an identical `sha256` and an identical `size` of 33,688 bytes — the two on-disc copies are byte-identical. | analysis/manifests/disc-files.jsonl (lines for `GAMEDATA/NETWORK/SYS_NET.ICO` and `NETGUI/CNF/SYS_NET.ICO`) |
| C3 | Both files carry the identical literal name `SYS_NET.ICO`, differing only by container directory (`GAMEDATA/NETWORK` vs `NETGUI/CNF`). | analysis/manifests/disc-files.jsonl |
| C4 | No `FORMAT_HANDLERS` entry, native header/descriptor, or CMake/test registration exists for `.ico` anywhere in the tracked tree. | tools/fingerprint_assets.py (FORMAT_HANDLERS list has no `ico` key); grep of native/include/omega/retail/*.h, native/include/omega/asset/*.h, and CMakeLists.txt returned no `.ico`/`ico` matches |
| C5 | No published grammar document (`analysis/formats/*.md`) mentions `.ico`. | analysis/formats/*.md (grep returned no matches) |

## 4. Aggregate-only facts

- The whole-disc extension histogram places `.ico` (count 2) alongside other low-count, non-HOG-packed suffixes that read as platform/boot material rather than title assets: `.cnf` (8), `.icn` (1), `.irx` (46), `.elf` (4), `.bd`/`.hd` (1 each). Source: analysis/manifests/disc-summary.json `"extensions"` block. This is a co-occurrence observation only — no semantic link between `.ico` and any specific `.cnf`/`.icn`/`.irx` file is asserted.
- Both `.ico` files are single-size (33,688 bytes each), i.e. the aggregate size range for this suffix on the tracked disc manifest is a single point value, not a distribution. Source: analysis/manifests/disc-files.jsonl.
- `.ico` never appears inside a `.HOG` container in the tracked evidence, i.e. its aggregate HOG-membership count is 0/0 (recursive/top-level) against a corpus where `.hog` itself has 273 whole-disc occurrences. Source: analysis/manifests/disc-summary.json; analysis/formats/hog-validation.json.

## 5. Hypotheses

- **H1 — PS2 network-configuration bundle.** The identical `SYS_NET.ICO` name, its presence in a `NETGUI/CNF`-named directory, and duplication into `GAMEDATA/NETWORK` are consistent with the PS2 platform's network-adaptor configuration file set (an icon shown by the console's system menu/browser for titles offering network play), rather than an in-game texture/asset. *Privacy-safe confirmation/refutation:* extend analysis/manifests/disc-files.jsonl enumeration (already tracked-safe, whole-disc metadata only) to list every sibling filename inside both `NETGUI/CNF` and `GAMEDATA/NETWORK` (names only, no payload) and check for known PS2 network-config companion files (e.g. additional `.cnf`/`.dat` config files) alongside `SYS_NET.ICO`; a consistent companion-file pattern would support H1, and its absence would weaken it.
- **H2 — Boot-icon rather than network-icon.** Alternatively, `.ico` at this location could be a generic PS2 boot/browser icon unrelated to network setup specifically, with "NET" in the name referring to something title-internal rather than the platform network stack. *Privacy-safe confirmation/refutation:* compare the byte-identical `.ico` file's magic/header bytes (first few bytes only, not full payload) against the published PS2 system-icon format signature if one is ever added to analysis/formats/*.md; currently no tracked source records this magic, so H2 cannot be adjudicated from tracked evidence alone.

## 6. Missing observations

- No tracked source records the internal byte layout, magic number, or header fields of `SYS_NET.ICO` — no structural handler, no grammar doc, no ledger entry inspects its bytes. *Privacy-safe collection:* a bounded, header-only structural probe (first N bytes, no full payload, no path disclosure beyond the generic container name already used above) added to tools/fingerprint_assets.py, scoped only to files matching this exact whole-disc filename pattern, would let a future pass record a magic/constant without touching payload content.
- No tracked source enumerates the full sibling-file set of the `NETGUI/CNF` or `GAMEDATA/NETWORK` directories beyond the two `.ico` rows already in analysis/manifests/disc-files.jsonl. *Privacy-safe collection:* a directory-scoped listing (filenames + sizes only, already the shape of disc-files.jsonl) would let H1 be tested without exposing payload bytes.
- No tracked source confirms whether this suffix is unique to `SYS_NET.ICO` or whether other differently-named `.ico` files could appear on other regional/disc variants; the current corpus reflects one disc image only. *Privacy-safe collection:* re-run the existing whole-disc manifest generator against any additional tracked disc image (if one is ever added to the repo) and diff the `.ico` row set.

## 7. Decoder/tooling status

**Classification: `system_file_out_of_scope`.**

- No canonical decoder, structural envelope parser, passive descriptor, or aggregate scanner exists for `.ico` in the tracked tree: `tools/fingerprint_assets.py`'s `FORMAT_HANDLERS` (which does cover tdx/ska/skas/skm/skl/vag/lpd/par/col/vum/vpk) has no entry for `.ico`, and no `native/include/omega/retail/*.h` or `native/include/omega/asset/*.h` header, no `native/src/**/*.cpp` decoder, and no `CMakeLists.txt` registration reference `.ico` or an icon format in any form.
- This is a deliberate scope decision, not a tooling gap to fill under the current program: the occurrence evidence (2 whole-disc, 0 HOG-packed, identical byte-for-byte duplicate, platform-conventional naming/location) indicates a PS2 system/network-configuration file rather than a title asset that the retail-format reverse-engineering effort targets. No adversarial or resource-boundary test gap is tracked because no decoder/descriptor exists to test.

## 8. Codex work order

1. **(Verification, not decoding)** Confirm H1 by adding a bounded, filename/size-only sibling-directory listing pass (extending the existing whole-disc manifest generator, not a new payload-reading tool) for `NETGUI/CNF` and `GAMEDATA/NETWORK`, to check for the platform-conventional network-config companion files alongside `SYS_NET.ICO`. Output only names/sizes into the existing manifest JSONL shape — no payload bytes.
2. **(Low priority, optional)** If future work wants a magic-number record for completeness, add a header-only (first-N-bytes) structural probe scoped exclusively to files matching this exact whole-disc filename, emitting only an aggregate magic/constant into asset-fingerprints.json — never a per-file byte dump. This is optional because the family is already scoped out of the asset-recon effort.
3. **(No action needed)** Do not add `.ico` to `FORMAT_HANDLERS` or write a native decoder/descriptor for it — the occurrence evidence and naming convention do not support treating it as a title asset format in scope for this project's reverse-engineering goals.
