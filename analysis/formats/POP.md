# POP terrain-prefix format

## Validated scope

This contract covers only the leading terrain-reference section. It is validated against all 18
level POP files in the owner's NTSC-U corpus: 5,351 records, zero parse failures, and an immediate
`GOB:` tag after every terrain sequence. Later POP sections remain uninterpreted.

The native parser is passive host code. It does not execute retail scripts or PS2 instructions.

## Observed layout

All numeric fields are little-endian unsigned 32-bit values.

```text
u32 observed_header_word = 70
char[4] terrain_tag = "TER:"
u32 terrain_count

repeat terrain_count:
    u32 kind                 # numeric meaning not yet established
    u32 index                # numeric meaning not yet established
    char name[]              # printable ASCII, NUL-terminated
    byte alignment[]         # advance to the next four-byte boundary

char[4] next_tag = "GOB:"
```

Alignment bytes are not padding that can be required to equal zero. They are nonzero in 4,144 of
the 5,351 records (149 of 299 MINSK records), so readers must skip them without interpreting them.

Every terrain name has a case-insensitive basename match in the same level's `DATA.HOG`. Across
the corpus, those HOG directories contain 44 additional cell members not named by the terrain
prefix. Name resolution is confirmed; placement, visibility, and the meanings of `kind` and
`index` are not.

## Native contract

`omega::asset::PopTerrainIndex`:

- accepts a caller-owned byte span and returns owned record names;
- bounds record count and name length before allocation or scanning;
- rejects truncation, empty/non-ASCII names, unknown leading structure, and a missing `GOB:`
  boundary;
- records how many entries contain nonzero alignment bytes without assigning meaning to them;
- exposes the byte offset of `GOB:` but does not parse later sections.

`omega::retail::DecodePopLevelManifest` adds the first canonical level dependency layer:

- accepts caller-owned POP bytes, the matching `DATA.HOG` directory, and an owned source locator;
- normalizes VFS paths and resolves every terrain name case-insensitively by basename stem;
- maps the observed POP `.VUM` reference spelling to the canonical matching `.HOG` member name;
- rejects missing, unsafe, or duplicate normalized references;
- applies cumulative input, item, nesting, string, logical-output, and transient-scratch limits
  before publishing output;
- returns an independently owned `omega::asset::LevelManifestIR`; and
- preserves the two observed numeric fields without inventing placement, visibility, transform,
  collision, material, or geometry semantics.

The decoder is a stateless worker-thread function. No returned span, pointer, or string view
references the POP bytes or HOG directory supplied by the caller. The common `DATA.HOG` source is
stored once on the manifest; each terrain cell stores only its canonical member name.

The native corpus command resolves all 5,351 records across all 18 level manifests with zero
missing, duplicate, unsafe, or malformed references.

## Post-TER marker envelope

`tools/scan_pop_post_terrain.py` is a bounded research scanner for the opaque bytes beginning at
the exact `GOB:` offset returned by the proven terrain parser. It does not decode a `GOB`, `SND`,
`ACL`, `INL`, `NPC`, or other later section. It inventories only four-byte-aligned occurrences of
the literal marker spellings already published in `ASSET-RECON.md`; such an occurrence remains a
candidate marker and may be coincidental payload data.

The scanner:

- streams each POP through fixed-size windows and bounds file count, individual and cumulative
  bytes, terrain records, terrain-name length, and marker hits;
- requires the validated header, complete terrain records, and exact `GOB:` boundary before
  examining later bytes;
- suppresses all structural aggregates and exits unsuccessfully if any candidate is malformed,
  truncated, unsafe, unreadable, or over budget;
- emits only corpus totals/ranges, literal-tag occurrence and ordinal counts, candidate transition
  counts/ranges, and the number of distinct ordered sequences; and
- emits no paths, asset names, hashes, payload bytes, executable data, or per-file fingerprints.

Only synthetic coverage has exercised this scanner so far. No post-TER corpus result or format
claim is established by its presence. Before any candidate marker can become a section boundary,
private evidence must establish its header/count relationship and exact bounded extent across the
corpus, disprove marker-shaped payload coincidences, and independently connect the consumed fields
to placement or visibility behavior. Until then, no post-TER native decoder or canonical IR should
be added.

## Reproduce

```powershell
python -B .\tools\fingerprint_assets.py `
  .\private\extracted-disc `
  .\analysis\formats\asset-fingerprints.json

.\build\msvc\Debug\omega_tool.exe pop-verify-tree .\private\extracted-disc
.\build\msvc\Debug\omega_tool.exe level-manifest-verify-tree .\private\extracted-disc
python -B .\tools\scan_pop_post_terrain.py .\private\extracted-disc --pretty
```

The Python reports are metadata-only. The native commands emit aggregate counts only. Review the
post-TER scanner output privately before publishing any aggregate or evidence-ledger entry.
