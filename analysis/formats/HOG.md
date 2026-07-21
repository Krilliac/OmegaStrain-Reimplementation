# Omega Strain HOG container

## Validation scope

This layout was inferred from the owner's NTSC-U `SCUS-97264` image and then checked against
all 273 top-level `.HOG` files on that disc. The validator accepted all 273 archives, covering
32,351 directory entries, with zero boundary, filename-count, or monotonic-offset failures.
The span-aware pass also validates 6,677 embedded HOGs: 501 exact spans and 6,176 whose logical
payload is followed only by zero sector padding.

The deterministic evidence is in `hog-validation.json`; `tools/validate_hogs.py` regenerates
it. `tools/hog.py` implements parsing and path-safe extraction.

## Layout

All integer fields are little-endian unsigned 32-bit values.

| File offset | Field | Meaning |
| ---: | --- | --- |
| `0x00` | `tag` | Varies by archive; checksum/tag algorithm is not yet identified. |
| `0x04` | `count` | Number of directory entries. |
| `0x08` | `offsets_offset` | Always `0x14` in the validated set. |
| `0x0C` | `names_offset` | `offsets_offset + 4 * (count + 1)`. |
| `0x10` | `data_offset` | Absolute start of concatenated payload data. |

At `offsets_offset` is an array of `count + 1` offsets relative to `data_offset`. Offset zero
must be zero; offsets must be monotonic; the final offset identifies the logical payload end.

That final offset is the archive's logical end. A top-level HOG must end there exactly. An embedded
HOG occupies a parent-directory span and may have an all-zero tail after the logical end; nonzero
tail bytes are rejected. Native callers must use the explicit range/span API for that case so a
padded top-level file is not accepted accidentally.

Between `names_offset` and `data_offset` is a sequence of `count` NUL-terminated ASCII names,
followed by alignment padding. Entry `i` occupies:

```text
[data_offset + offsets[i], data_offset + offsets[i + 1])
```

The validator proves this directory interpretation across the current corpus. It does not yet
prove what the first word means or the semantics of any embedded asset payload.

## MINSK example

`GAMEDATA/MINSK/SCRIPTS.HOG` is 640,976 bytes. Its header has tag `0x4052673D`, count 6,
name table offset 48, and data offset 108.

| Entry | Size (bytes) |
| --- | ---: |
| `INIT.SO` | 84,080 |
| `MUSIC.SO` | 57,612 |
| `OBJECTIVES.SO` | 87,332 |
| `PRAGUE.SO` | 279,660 |
| `UTILS.SO` | 52,312 |
| `VOICE.SO` | 79,872 |

## Reproduce

```powershell
python -B .\tools\validate_hogs.py `
  .\private\extracted-disc `
  .\analysis\formats\hog-validation.json

python -B .\tools\hog.py `
  .\private\extracted-disc\GAMEDATA\MINSK\SCRIPTS.HOG `
  --json .\analysis\formats\minsk-scripts-hog.json `
  --extract .\analysis\output\MINSK\SCRIPTS

.\build\msvc\Debug\omega_tool.exe hog-verify-tree .\private\extracted-disc
.\build\msvc\Debug\omega_tool.exe hog-verify-nested-tree .\private\extracted-disc
.\build\msvc\Debug\omega_tool.exe frontend-envelope-coverage-verify-tree .\private\extracted-disc
```

Extraction refuses absolute paths, `.`/`..` components, empty components, and any destination
that resolves outside the requested output root.

## Passive-envelope coverage consumer

`frontend-envelope-coverage-verify-tree` uses identity-guarded traversal and content-pinned HOG
reads. Successful discovery reads every admitted top-level `.HOG` and retains SHA-256 for each
64 KiB chunk; the 32-byte digest payload is globally capped at exactly 8 MiB. Parsing exposes bytes
through one verified 64 KiB cache and grants bounded chunk-rounded allowances for admitted nested
spans, including legal padding backtracks. It rejects links and reparse points and follows nested
members classified by an ASCII-case-insensitive `.hog` suffix through bounded random-access
directory parsing to depth 32. Each FNT, GUI, or IE candidate is size-checked, read into one
temporary owned buffer of at most 1 MiB, inspected, and released before the next candidate. The
scanner does not materialize a separate full-archive buffer. The unkeyed digests are consistency
pins, not source authentication, provenance, a signature, or an atomic filesystem snapshot. This is
not a constant-memory claim or a new HOG-layout inference. See `FRONTEND-ENVELOPE-COVERAGE.md` and
ledger E-0113.
