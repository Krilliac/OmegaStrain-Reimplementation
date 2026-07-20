# VPK Dossier

## 1. Identity

`.vpk` is a Syphon Filter: The Omega Strain HOG-member suffix identified only by a validated
**wrapper envelope**: a fixed 16-byte prefix (raw signature + one observed 32-bit word) sitting
atop a physical span that is always a multiple of 2,048 bytes. Nothing beyond that prefix has
been decoded, characterized, or named. No asset role, codec, or storage/audio semantics are
established by tracked evidence; those remain explicitly unknown (see §5–6).

## 2. Occurrence evidence

| Observation | Count | Tracked source |
|---|---|---|
| Recursive-in-HOG occurrences | 85 | `analysis/formats/asset-fingerprints.json` (`entry_extensions[".vpk"]` and the `"vpk"` format-aggregate block) |
| Top-level-HOG member count | 85 | `analysis/formats/hog-validation.json` (`entry_extensions[".vpk"]`, over `archive_count: 273`, `total_entries: 32351`) |
| Whole-disc filesystem-entry occurrences | 0 | `analysis/manifests/disc-summary.json` (`extensions`, no `.vpk` key) |

In the current tracked inventories, all 85 observed `.vpk` occurrences are HOG members and the
whole-disc filesystem-entry count is 0. This establishes no universal placement rule for other
releases or corpora.

## 3. Confirmed facts

Each row is mechanically citable from the named tracked file.

| # | Fact | Tracked source |
|---|---|---|
| C1 | All 85 tracked `.vpk` spans begin with the exact raw byte sequence `20 4B 50 56` at offset `0x00`. | `analysis/formats/VPK.md`; `analysis/formats/asset-fingerprints.json` (`"little_endian_vpk_fourcc": 85`); `analysis/formats/ASSET-RECON.md` |
| C2 | The visually forward reading `b"VPK "` does NOT match the proven byte representation; the tracked evidence documents the raw bytes only. | `analysis/formats/VPK.md` ("Nonclaims") |
| C3 | All 85 spans have the little-endian unsigned 32-bit word value `2048` at offset `0x08`. | `analysis/formats/VPK.md`; `analysis/formats/asset-fingerprints.json` (`"declared_header_bytes": {"2048": 85}`) |
| C4 | All 85 spans are evenly divisible by 2,048 bytes. | `analysis/formats/asset-fingerprints.json` (`"span_2048_byte_aligned": 85`); `analysis/formats/VPK.md` |
| C5 | The full physical-span size range across all 85 tracked instances is 1,320,960 to 9,005,056 bytes inclusive. | `analysis/formats/asset-fingerprints.json` (`"span_bytes_range": [1320960, 9005056]`); `analysis/formats/VPK.md` |
| C6 | Bytes `0x04..0x07` and `0x0c..0x0f` have no established field meaning in tracked evidence (explicitly called out as unassigned). | `analysis/formats/VPK.md` |
| C7 | Bytes from `0x10` onward are not inspected or characterized by the tracked aggregate fingerprint beyond total physical-span size. | `analysis/formats/VPK.md` |
| C8 | Ledger entry E-0094 (state: `confirmed`) documents the addition of a stateless, reentrant, passive VPK wrapper-envelope decoder scoped exactly to the above aggregate facts, with the same acceptance bounds (signature, word-at-`0x08`, size range, 2048-byte alignment) and explicitly assigns no codec/audio/asset-role/streaming/emulator semantics. | `analysis/evidence/ledger.jsonl` (id `E-0094`) |
| C9 | A native decoder exists: `DecodeVpkWrapperEnvelope(std::span<const std::byte>, asset::DecodeLimits)` returning `asset::DecodeResult<VpkWrapperEnvelopeDescriptor>`. It validates, in order: caller/fixed byte-size ceilings, caller item/output ceilings, a 16-byte truncation floor, the 1,320,960-byte observed-minimum floor, 2048-byte alignment, the exact 4-byte raw signature at `0x00`, and the exact `2048` word at `0x08`. | `native/include/omega/retail/vpk_wrapper_envelope_decoder.h`; `native/src/retail/vpk_wrapper_envelope_decoder.cpp` |
| C10 | `VpkWrapperEnvelopeDescriptor` stores exactly: `opaque_prefix_bytes_0x04` (4 bytes), `opaque_prefix_bytes_0x0c` (4 bytes), `physical_byte_count` (uint64), and `aligned_block_count` (uint64, `= physical_byte_count / 2048`). No other field exists. | `native/include/omega/retail/vpk_wrapper_envelope_decoder.h` |
| C11 | Fixed constants in the header: `kVpkObservedWord0x08 = 2048`, `kVpkPhysicalAlignmentBytes = 2048`, `kVpkWrapperMinimumInputBytes = 1'320'960`, `kVpkWrapperMaximumInputBytes = 9'005'056`, `kVpkWrapperMaximumDecodedItems = 1`, `kVpkWrapperMaximumLogicalOutputBytes = sizeof(VpkWrapperEnvelopeDescriptor)`. | `native/include/omega/retail/vpk_wrapper_envelope_decoder.h` |
| C12 | The decoder is registered in the build: source file compiled into the retail-formats target, and a dedicated test executable `omega_vpk_wrapper_envelope_decoder_tests` is built, linked against `omega_retail_formats`, and registered as a CTest with a 10-second timeout. | `CMakeLists.txt` (lines listing `native/src/retail/vpk_wrapper_envelope_decoder.cpp` and the `omega_vpk_wrapper_envelope_decoder_tests` target/test) |
| C13 | A dedicated native test file exists covering this decoder. | `native/tests/vpk_wrapper_envelope_decoder_tests.cpp` |
| C14 | `fingerprint_assets.py`'s `FORMAT_HANDLERS` includes a structural handler for `vpk` (grouped with tdx/ska/skas/skm/skl/vag/lpd/par/col/vum as formats with structural — not merely inventory — handlers). | `tools/fingerprint_assets.py` |
| C15 | The published grammar doc's own "Nonclaims" section states the observed `0x08` word and the physical 2,048-byte alignment are independent observations; their equal numeric values do not establish a header-size, block-size, or alignment-declaration relationship. | `analysis/formats/VPK.md` |

## 4. Aggregate-only facts

These are tracked aggregates with no semantic interpretation attached.

- Count of tracked `.vpk` spans: 85 (both recursive-in-HOG and top-level-HOG, i.e., the same 85 members, per `asset-fingerprints.json` / `hog-validation.json`).
- Span-size distribution: minimum 1,320,960 bytes, maximum 9,005,056 bytes, all values multiples of 2,048 — no further bucket/histogram breakdown of intermediate sizes is present in the tracked aggregate.
- `declared_header_bytes` aggregate bucket: single value `2048` across all 85 spans (i.e., no variance in the observed `0x08` word).
- `little_endian_vpk_fourcc`: 85/85 spans matched (aggregate count only, no per-file breakdown retained).
- `span_2048_byte_aligned`: 85/85 spans matched the alignment predicate.
- Derived `aligned_block_count` (physical_byte_count / 2048) ranges from 645 through 4,397 for accepted inputs — this is an arithmetic quotient over the confirmed size range, not an independently observed field, and per the native contract and `VPK.md` "Nonclaims" is explicitly not asserted to correspond to any storage sector, packet, sample, or audio-frame unit.
- `ASSET-RECON.md` places `.VPK` in a table of per-suffix aggregate one-line summaries alongside PAR/COL/VUM/POP, and separately in a "Wrappers and compression check" section noting the scanner examined the first bytes of many non-HOG asset spans for common compression-container magics (gzip/ZIP/bzip2/XZ/7zip) — VPK's own signature is a distinct, unrelated 4-byte value, not one of those.

## 5. Hypotheses

No new hypothesis is promoted here. The established evidence above remains the claim ceiling, and
this dossier authorizes no owner-corpus measurement recipe. Before any future measurement is
implemented, a separate reviewed contract must predeclare its fixed public schema, fixed minimum
cohort threshold, bounded execution and typed failures, and project-generated privacy tests.

An authorized report may contain only fixed anonymous corpus-wide totals for cohorts meeting that
threshold. Smaller cohorts must collapse to one typed suppression result. The report must not emit
raw values, signatures, payloads, owner-derived strings, paths, file, container, or archive names,
suffix-derived labels, per-file, per-container, or per-archive rows, or cross-tabulations keyed by
raw fields.

## 6. Missing observations

Unresolved structural, semantic, consumer, and validation questions remain missing observations.
This section deliberately defines no executable collection recipe. Closing any gap requires the
separately reviewed contract and suppression policy stated above; absent that contract, the gap
remains UNKNOWN.

## 7. Decoder/tooling status

**Classification: `structural_envelope_only`**

- A native decoder (`DecodeVpkWrapperEnvelope`) exists and is registered in the build (`CMakeLists.txt`) with a dedicated test target and CTest entry (`omega_vpk_wrapper_envelope_decoder_tests`, 10s timeout) — see C9–C13.
- It is explicitly scoped to the 16-byte wrapper prefix plus whole-span size/alignment checks. It does not parse, name, or interpret anything past `0x0f`; the descriptor carries no payload, no sub-structure, and no semantic tag. This matches `structural_envelope_only` rather than `canonical_decoder` (which would imply full-format understanding) or `passive_descriptor_only`/`aggregate_scanner_only` (which would imply no validating decode logic at all — here there is real branch-by-branch rejection logic in `vpk_wrapper_envelope_decoder.cpp`, not just descriptive metadata).
- Header/source citation: `native/include/omega/retail/vpk_wrapper_envelope_decoder.h`, `native/src/retail/vpk_wrapper_envelope_decoder.cpp`.
- Build/test registration citation: `CMakeLists.txt` (source-file inclusion in the retail-formats target; `add_executable(omega_vpk_wrapper_envelope_decoder_tests ...)`; `target_link_libraries(... PRIVATE omega_retail_formats)`; `add_test(NAME omega_vpk_wrapper_envelope_decoder_tests ...)`; `set_tests_properties(... TIMEOUT 10)`).
- Ledger corroboration: E-0094 (`analysis/evidence/ledger.jsonl`) records passing focused Debug/Release builds and CTest (1/1 each), plus a full runtime-OFF Debug integration build and CTest (40/40), all against project-generated fixtures only.
- **Test-coverage gap noted by the evidence itself:** E-0094 states owner-corpus validation and runtime integration are "deliberately unclaimed." This dossier does not independently re-run those tests (out of scope — no private-input access), so the gap between "fixture-tested" and "owner-corpus-validated" is carried forward as an open item (§6, §8).
- `fingerprint_assets.py` also carries a structural handler for `vpk` in `FORMAT_HANDLERS` (C14) — this is the *aggregate-scanning* side (the source of the `span_bytes_range`/alignment/signature aggregates in `asset-fingerprints.json`), distinct from but consistent with the native decoder; both exist, so `aggregate_scanner_only` would understate the native side, and `structural_envelope_only` is the correct single label for the family as a whole.

## 8. Codex work order

1. Preserve the established facts, aggregates, decoder classification, and nonclaims above.
2. Before implementing or running any new owner-corpus measurement, land a separate reviewed
   contract that freezes its public schema, hard bounds, typed failures, deterministic behavior,
   synthetic privacy tests, and fixed minimum cohort threshold.
3. Permit only fixed anonymous corpus-wide totals for cohorts meeting that threshold.
4. Collapse every smaller cohort to one typed suppression result; do not publish a partial result.
5. Reject any contract or output containing raw values, signatures, payloads, owner-derived strings,
   paths, file, container, or archive names, suffix-derived labels, per-file, per-container, or
   per-archive rows, or cross-tabulations keyed by raw fields.
