# Front-end passive-envelope coverage

## Scope

`omega_tool frontend-envelope-coverage-verify-tree <root>` measures whether HOG-contained `.fnt`,
`.gui`, and `.ie` candidates pass the project's existing bounded passive prefix/envelope
descriptors. It is a coverage command for project-defined hypotheses, not a retail-validity check,
semantic decoder, UI/menu reconstruction, or behavioral observation.

## Command

```powershell
.\build\msvc\Debug\omega_tool.exe frontend-envelope-coverage-verify-tree <root>
```

The command recursively discovers regular top-level `.HOG` files below one supplied root. It uses
identity-guarded traversal and identity-bound HOG reads, rejects links, Windows reparse points, and
other unsafe entries, and follows nested members classified by an ASCII-case-insensitive `.hog`
suffix through the established bounded HOG span parser. Nested indexes are followed to a maximum
depth of 32. Loose `.fnt`, `.gui`, and `.ie` files are ignored; their HOG-member suffix matching is
also ASCII-case-insensitive.

## Schema

Standard output is one deterministic JSON object with `schema_version` 1 and exactly the `fnt`,
`gui`, and `ie` family objects. Each family contains:

- `candidates`
- `accepted`
- `rejected_truncated`
- `rejected_malformed`
- `rejected_overflow`
- `rejected_limit_exceeded`
- `rejected_unsupported_variant`
- `rejected_invalid_reference`
- `rejected_duplicate_reference`

The report emits no input path, archive or member name, identity, hash, offset, payload byte, raw
suffix, per-archive row, or exception message. Schema version 1 belongs only to this acceptance
report. It does not supersede E-0110's front-end topology schema version 3; `.fnt` remains `other`
in that separate topology vocabulary.

## Exit contract

- Exit `0`: every family has at least one candidate and every candidate is accepted.
- Exit `2`: one or more families are absent, or at least one descriptor rejects a candidate.
- Exit `1`: discovery, unsafe-entry, resource-limit, I/O, HOG-open, or member-read failure.

Descriptor rejection preserves the complete typed aggregate. An infrastructure failure discards
partial observations, emits the all-zero schema, and writes one fixed path-free category to standard
error.

## Safety and resource contract

The scanner uses bounded random access for HOG indexes. After checking the family ceiling, it reads
one complete candidate into a temporary owned buffer of at most 1 MiB and releases that buffer before
the next candidate. It does not materialize a separate full-archive buffer; it owns only the bounded
indexes for the active nesting chain and at most one candidate buffer at a time. This is not a
constant-memory claim. Nested HOG and index traversal is capped at depth 32, and fixed count, byte,
directory, path-metadata, and aggregate ceilings fail closed. Caller input cannot widen those hard
limits.

## Generated-fixture coverage

The focused suite supplies only project-generated data. It covers nested exact and zero-padded HOG
spans, ASCII-case-insensitive candidates, typed descriptor rejections, ignored loose files,
deterministic privacy-equivalent output, invalid and empty roots, malformed archives, depth and
resource failures, link or reparse rejection where the platform permits the fixture, file
deletion/replacement/mutation, and directory-replacement races. POSIX coverage also substitutes a
FIFO before open to prove that a non-regular replacement cannot block the scan.

## Evidence boundaries

No owner-corpus coverage result is recorded. Existing tracked occurrence evidence remains:

| Family | Recursive HOG inventory | Top-level HOG inventory | Whole-disc inventory |
| --- | ---: | ---: | ---: |
| FNT | 3 | 3 | 0 |
| GUI | 77 | 21 | 0 |
| IE | 79 | 23 | 0 |

Acceptance means only that a candidate satisfies the corresponding project-defined passive
descriptor. It establishes no retail provenance for the descriptor constants and assigns no font,
glyph, widget, node, layout, lookup, binding, rendering, menu, audio, gameplay, consumer, or PCSX2
semantics.

## Next evidence

If an authorized private owner-corpus run is later performed, review and retain only the fixed-schema
aggregate. A result may measure descriptor coverage but cannot promote an opaque range to a semantic
field. A deeper decoder still requires a falsifiable grammar and independent consumer evidence.
