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
identity-guarded traversal, rejects links, Windows reparse points, and other unsafe entries, and
follows nested members classified by an ASCII-case-insensitive `.hog` suffix through the established
bounded HOG span parser. On successful discovery, every admitted top-level HOG has been read
completely and represented by SHA-256 digests of its 64 KiB chunks, including a shorter final chunk.
Parsing uses one verified 64 KiB cache; bytes become parser-visible only after matching their
discovery digest. Nested indexes are followed to a maximum depth of 32. Loose `.fnt`, `.gui`, and
`.ie` files are ignored; their HOG-member suffix matching is also ASCII-case-insensitive.

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
the next candidate. It does not materialize a separate full-archive buffer; it owns the bounded
indexes for the active nesting chain, at most one candidate buffer, one verified 64 KiB chunk cache,
and retained 32-byte discovery digests. The digest payload is globally limited to 262,144 entries,
exactly 8 MiB; container and allocator overhead are additional, so this is not a constant-memory
claim.

Each top-level HOG begins with a parser raw-read allowance equal to its discovered size. Every nested
HOG member that passes the count, size, and aggregate limits adds the complete 64 KiB chunk coverage
of its absolute span. This admits legal padded or unaligned nested archives while remaining bounded:
across one scan, top-level allowances total at most 16 GiB, nested member sizes total at most 16 GiB,
and at most 32,768 nested spans add less than 128 KiB of boundary rounding each. Parser-phase
authenticated raw reads are therefore at most 36 GiB minus 64 KiB. Including the mandatory discovery
pass, total HOG bytes requested through stable file handles are at most 52 GiB minus 64 KiB. These are
requested-byte bounds, not physical disk-I/O claims; the operating-system cache controls device
reads. Depth 32 and the other fixed count, byte, directory, path-metadata, and aggregate ceilings fail
closed. Caller input cannot widen those hard limits.

## Generated-fixture coverage

The focused suite supplies only project-generated data. It covers three SHA-256 known answers,
cross-chunk and shorter-final-chunk reads, deterministic post-read/pre-digest scratch corruption,
one-read-per-chunk cache reuse for 8,192 sequential tiny members, explicit discovery and parser
budget exhaustion, and a padded nested HOG whose bounded post-padding backtrack exceeds the outer
file's physical chunk count. It also covers nested exact and zero-padded HOG spans,
ASCII-case-insensitive candidates, typed descriptor rejections, ignored loose files, deterministic
privacy-equivalent output, invalid and empty roots, malformed archives, depth and resource failures,
link or reparse rejection where the platform permits the fixture, file deletion/replacement/mutation,
and directory-replacement races. POSIX coverage substitutes a FIFO before open to prove that a
non-regular replacement cannot block the scan.

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

The unkeyed digests are consistency pins for bytes observed by this command during discovery. They
are not source or authorship authentication, a signature or provenance claim, or an atomic
filesystem snapshot, and they do not prove that the file never changed outside an authenticated
read.

## Next evidence

If an authorized private owner-corpus run is later performed, review and retain only the fixed-schema
aggregate. A result may measure descriptor coverage but cannot promote an opaque range to a semantic
field. A deeper decoder still requires a falsifiable grammar and independent consumer evidence.
