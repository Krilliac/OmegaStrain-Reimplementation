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
| Whole-disc occurrences | 0 | (per task framing; not separately re-derived from a whole-disc histogram file in this pass) |

Because the whole-disc count is 0 and the in-HOG count is 85, `.vpk` is exclusively a HOG-container
member format in the tracked evidence, which is why this dossier is a full evidence-tiered writeup
rather than a stub.

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

Each is explicitly labeled as unconfirmed. Per clean-room rules, none of these may be inferred as fact; they are recorded to guide privacy-safe future collection.

- **H1 — "VPK" reversed-bytes naming.** The raw signature `20 4B 50 56` reads as `b" KPV"`, which is `b"VPK "` byte-reversed within the 4-byte field (ignoring the leading space/word-order framing). *Confirms/refutes via*: a privacy-safe structural note already exists distinguishing raw bytes from the visually forward reading (`VPK.md` "Nonclaims"); this is flagged as a naming curiosity only. No further private-input inspection is needed or permitted to resolve intent — the raw-byte fact is already the ceiling of what can be claimed.
- **H2 — The `0x08` word denotes a declared header or block size.** Because the word's value (2048) numerically equals the observed physical alignment, one might guess it declares a header/block size. *Confirms/refutes via*: an aggregate, privacy-safe structural scan of bytes `0x10` onward across the existing 85 tracked spans (no new private inputs) to see whether any internal boundary, count, or sub-structure aligns with multiples of the `0x08` word in a way distinguishable from mere alignment coincidence. This must remain aggregate-only (counts/ranges, not per-file rows).
- **H3 — `.vpk` carries streaming or storage-media significance (e.g., disc-block staging).** The consistent 2,048-byte alignment and the format's HOG-only occurrence suggest possible read-block staging. *Confirms/refutes via*: aggregate comparison of `.vpk` alignment/size-quantization behavior against other already-characterized 2,048-or-similar-aligned formats in tracked docs (e.g., HOG container `data_offset` values, many of which are also multiples of 16/2048 per `hog-validation.json`), reported only as aggregate correlation statistics — never by asserting a role.
- **H4 — Bytes `0x04..0x07` and `0x0c..0x0f` are a version/tag pair (paralleling the HOG container's own per-archive `tag` field observed in `hog-validation.json`).** *Confirms/refutes via*: an aggregate-only frequency count of the two opaque 4-byte fields across all 85 spans (distinct-value counts and ranges only, no raw value-to-path mapping) to see if they cluster the way HOG `tag` values do per subdirectory.
- **H5 — `.vpk` payload (bytes beyond the 16-byte prefix) holds compressed or encoded data.** *Confirms/refutes via*: extending the existing compression-magic scan (already run for other non-HOG spans per `ASSET-RECON.md` §"Wrappers and compression check") to the VPK payload region specifically, reporting only aggregate hit/miss counts per known magic — never payload bytes or per-file identifiers.

## 6. Missing observations

Tracked evidence does not currently establish, and privacy-safe collection could add:

- **No sub-structure inventory beyond the 16-byte prefix.** `asset-fingerprints.json`'s `vpk` aggregate block stops at the header word, alignment, and total span size. A deeper structural pass (still aggregate-only: value-frequency tables, range tables, no per-file rows) over bytes `0x10` onward would be needed to test H2/H5.
- **No per-directory/per-archive distribution of `.vpk` counts.** `hog-validation.json`'s archive list does not appear to name which specific `.HOG` archives contain `.vpk` members (the `entry_extensions` histogram is corpus-wide, not per-archive) — an aggregate per-archive count (archive path → vpk member count only, no member names) would let H3/H4 be tested against HOG subdirectory groupings already visible in `hog-validation.json`.
- **No opaque-field value distribution.** The two 4-byte opaque fields (`0x04..0x07`, `0x0c..0x0f`) have no recorded distinct-value counts or ranges in any tracked doc — needed for H4.
- **No compression/encoding magic scan specific to VPK payload.** `ASSET-RECON.md`'s compression check is described corpus-wide over "non-HOG asset spans"; it is not clear from tracked text whether `.vpk` (a HOG member) was included. An aggregate hit-count table scoped to VPK spans would resolve H5.
- **No adversarial/fuzz corpus size documented beyond the native test file's existence.** The test file `native/tests/vpk_wrapper_envelope_decoder_tests.cpp` exists and E-0094's ledger entry claims coverage of specific boundary classes (min/interior/max/below-range/misaligned/over-limit spans, truncated prefixes, signature-byte rejection, endian adversary, opaque-prefix preservation, payload independence, lifetime, determinism, budgets), but this dossier's tracked-source pass did not itself re-enumerate individual test cases; a Codex follow-up could verify the test file's case count matches the ledger's claimed coverage classes (see §8).
- **Owner-corpus validation is explicitly unclaimed.** E-0094 predates publication; the decoder is
  present on current main at commit `4163680`. It has not been run against the full owner corpus of
  85 real `.vpk` members to confirm 85/85 acceptance; only the aggregate fingerprint that supplied
  its constants covers all 85.

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

Ranked, concrete, privacy-safe. None of these steps require inventing semantics; all stay within the wrapper-envelope boundary already proven.

1. **Highest priority — Run the existing `DecodeVpkWrapperEnvelope` against the full owner corpus (all 85 tracked `.vpk` members) and record only aggregate pass/fail counts.** This closes the "owner-corpus validation... deliberately unclaimed" gap noted in E-0094, without emitting any per-file row, path, or byte content — output should be a single aggregate line (e.g., "85/85 accepted" or a failure count plus the rejection-code histogram from `asset::DecodeErrorCode`).
2. **Extend `fingerprint_assets.py`'s `vpk` structural handler to emit an aggregate distinct-value count (not full distribution) for the two opaque 4-byte fields (`0x04..0x07`, `0x0c..0x0f`) across all 85 members.** This directly tests H4 with zero privacy risk (counts only, e.g., "opaque_0x04: 12 distinct values across 85 spans").
3. **Add an aggregate-only structural pass over bytes `0x10` onward of the 85 VPK spans, scoped to answering H2/H5 (header/block-size correlation, compression-magic presence) as pure count/range tables**, mirroring the existing `ASSET-RECON.md` compression-magic methodology but explicitly scoped to VPK spans (current text does not confirm VPK was included in that earlier scan).
4. **Cross-reference `.vpk` member counts against the per-archive `HOG` list in `hog-validation.json` to produce an aggregate table of (archive-path → vpk-member-count) with no member names**, to test H3 (possible correlation with specific HOG archive types/subdirectories already named in that file, e.g., which `GAMEDATA/<LEVEL>/*.HOG` archives carry `.vpk` members vs. zero).
5. **Independently verify the native test file's case count against the coverage classes claimed in ledger entry E-0094** (min/interior/max/below-range/misaligned/over-limit, truncated prefixes, signature/endian adversaries, opaque-prefix preservation, payload independence, lifetime, determinism, budget boundaries) to confirm no coverage-class drift since E-0094 was recorded — a documentation-consistency check only, no new decode logic.
6. **Do not attempt to decode or characterize bytes past the 16-byte prefix as a semantic format (no payload parser, no "block" or "sector" reader) until Codex step 3 above produces aggregate evidence supporting a specific structural claim.** Any decoder work beyond the current envelope must stay behind a new, separately-justified aggregate observation to avoid the "plausible invented decoder" regression this task explicitly warns against.
