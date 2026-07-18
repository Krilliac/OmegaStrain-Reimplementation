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
the exact `GOB:` offset derived under the proven terrain-prefix contract. It does not decode a
`GOB`, `SND`, `ACL`, `INL`, `NPC`, or other later section. It inventories only four-byte-aligned
occurrences of the literal marker spellings already present in public reconstruction tooling and
evidence; such an occurrence remains a candidate marker and may be coincidental payload data.

The scanner:

- streams each POP through fixed-size windows and bounds traversal entries, file count, individual
  and cumulative bytes, terrain records, terrain-name length, and marker hits;
- requires the validated header, complete terrain records, and exact `GOB:` boundary before
  examining later bytes;
- suppresses all structural aggregates and exits unsuccessfully if any candidate is malformed,
  truncated, unsafe, unreadable, or over budget;
- emits only corpus totals/ranges, literal-tag occurrence and ordinal counts, candidate transition
  counts/ranges, and the number of distinct ordered sequences; and
- emits no paths, asset names, hashes, payload bytes, executable data, or per-file fingerprints.

Owned-corpus scanning accepts all 18 level POPs with zero errors. It finds 342 aligned literal-tag
candidate hits: exactly 19 per file, each candidate once per file, with one distinct ordered
sequence across the corpus. This confirms only the bounded literal envelope. Before any candidate
marker can become a section boundary, private evidence must establish its header/count relationship
and exact bounded extent, disprove marker-shaped payload coincidences, and independently connect
the consumed fields to placement or visibility behavior. Until then, no post-TER native decoder or
canonical IR should be added.

## Candidate layout hypothesis scoring

`tools/score_pop_section_layout_hypotheses.py` is a second bounded, aggregate-only experiment. It
uses the terrain-prefix validator and aligned literal-marker inventory above; therefore it derives
no candidate marker before the proven `GOB:` boundary. For each marker-to-next-marker (or EOF)
span, it probes only the first eight aligned four-byte field positions following the literal. A
bounded nonzero word is treated as a candidate count, never as a decoded field. If dividing the
remaining opaque extent by that candidate count yields a four-byte-aligned fixed stride no larger
than 256 bytes, the tuple of literal spelling, marker-relative field offset, and stride receives one
exact-match score.

The report includes only tuples with at least one exact nonzero arithmetic match, together with
their bounded nonzero tests, exact matches, and mismatches. Zero words provide no stride evidence
and never create a tuple. For a tuple established by nonzero evidence, the report separately counts
whether zero-word occurrences have exactly the empty extent predicted by that candidate formula.
A match does not establish that the literal is a boundary, the word is a count, the opaque values
are records, or the stride has any runtime meaning.

The scorer streams files and applies independent traversal-entry, directory-depth, path-metadata,
file-size, cumulative-input, actual-read, per-file and cumulative terrain-record, name, marker,
candidate-span, field-probe, hypothesis-count, and serialized-output budgets. Links, junctions,
Windows reparse points, special filesystem entries, file-identity races, malformed POP candidates,
and budget violations fail the entire run and suppress every structural aggregate. Output contains
no paths, names, hashes, raw words, payload bytes, or per-file fingerprints.

The owned-corpus run accepts all 18 POPs with zero errors and tests 1,443 bounded nonzero candidate
count words across 342 marker spans. Five marker-relative `+4` word/stride tuples fit every nonzero
occurrence tested: `INL:`/36 bytes (18/18), `PNT:`/88 bytes (17/17), `DIR:`/44 bytes (18/18),
`ENV:`/76 bytes (18/18), and `INV:`/84 bytes (16/16). Separate zero-word accounting finds the empty
extent predicted by the same candidate formula in the remaining one `PNT:` and two `INV:` spans,
so each tuple fits all 18 occurrences. The zero cases add no stride evidence. These exact arithmetic
fits nominate the next structural proof only; they do not confirm markers, counts, records,
boundaries, placement, visibility, or any field semantics.

## Candidate record-shape profiling

`tools/profile_pop_candidate_record_shapes.py` is a privacy-safe follow-up experiment over the
proven TER-to-GOB validator and its ordered aligned-marker inventory. It accepts exactly five fixed
arithmetic candidates and does not search for additional layouts:

| Literal candidate | Candidate count-word delta | Candidate fixed stride |
| --- | ---: | ---: |
| `INL:` | +4 bytes | 36 bytes |
| `PNT:` | +4 bytes | 88 bytes |
| `DIR:` | +4 bytes | 44 bytes |
| `ENV:` | +4 bytes | 76 bytes |
| `INV:` | +4 bytes | 84 bytes |

Every candidate must occur exactly once in each accepted POP, and `marker + 8 + count * stride`
must equal the next ordered candidate-marker offset (or EOF). A zero count is accepted only when
that candidate extent is empty. These are arithmetic guards, not decoded section, count, record, or
field contracts.

For each literal and four-byte record column, the report contains only aggregate record and column
counts; zero/nonzero bit-pattern counts; IEEE-754 finite/nonfinite bit-pattern counts; the count
below the fixed neutral unsigned threshold 4096; and distinct-bit-pattern cardinality capped by a
declared per-column limit. IEEE-754 classification operates on bits and does not assign a floating-
point, integer, coordinate, identifier, or other numeric field type.

The profiler streams bounded chunks and independently caps traversal, depth, path metadata,
individual and cumulative input, actual reads, terrain records, marker hits, formula occurrences,
candidate records, column observations, retained distinct patterns, and serialized output. Links,
junctions, reparse points, special entries, identity races, malformed candidates, formula
mismatches, and budget violations fail the entire run and suppress all structural output. It emits
no paths, names, hashes, raw words or values, extrema, payload bytes, per-file records, or
fingerprints.

The owned-corpus pass accepts all 18 POPs and all 90 guarded candidate occurrences with zero errors.
It profiles 8,019 candidate records and 105,985 opaque four-byte column observations:

| Literal candidate | Candidate records | Four-byte columns | Zero bit patterns | Nonfinite IEEE-754 bit patterns | Raw unsigned patterns below 4096 | Columns whose distinct-pattern count exceeded the 4,096 cap |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `INL:` | 5,351 | 9 | 163 | 0 | 16,085 | 4 |
| `PNT:` | 2,358 | 22 | 9,691 | 7,074 | 19,111 | 0 |
| `DIR:` | 43 | 11 | 41 | 43 | 170 | 0 |
| `ENV:` | 65 | 19 | 181 | 338 | 298 | 0 |
| `INV:` | 202 | 21 | 1,128 | 942 | 2,504 | 0 |

These are bit-pattern shape measurements only. They strengthen the exact arithmetic envelope and
bound later experiments, but do not turn any literal, count word, record, or column into a decoded
contract and do not identify numeric types, placement, direction, visibility, or gameplay meaning.

## Reproduce

```powershell
python -B .\tools\fingerprint_assets.py `
  .\private\extracted-disc `
  .\analysis\formats\asset-fingerprints.json

.\build\msvc\Debug\omega_tool.exe pop-verify-tree .\private\extracted-disc
.\build\msvc\Debug\omega_tool.exe level-manifest-verify-tree .\private\extracted-disc
python -B .\tools\scan_pop_post_terrain.py .\private\extracted-disc --pretty
python -B .\tools\score_pop_section_layout_hypotheses.py .\private\extracted-disc --pretty
python -B .\tools\profile_pop_candidate_record_shapes.py .\private\extracted-disc --pretty
```

The Python reports are metadata-only. The native commands emit aggregate counts only. Review the
post-TER scanner and profiler output privately before publishing any aggregate or evidence-ledger
entry.
