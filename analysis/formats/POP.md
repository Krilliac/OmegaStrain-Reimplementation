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

## Reproduce

```powershell
python -B .\tools\fingerprint_assets.py `
  .\private\extracted-disc `
  .\analysis\formats\asset-fingerprints.json

.\build\msvc\Debug\omega_tool.exe pop-verify-tree .\private\extracted-disc
.\build\msvc\Debug\omega_tool.exe level-manifest-verify-tree .\private\extracted-disc
```

The Python report is metadata-only. The native command emits aggregate counts only.
