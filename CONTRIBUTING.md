# Contributing

This is a clean-room, pure-native game reimplementation. Contributions must preserve both
boundaries.

## Never submit proprietary material

Do not commit, attach to issues, paste into discussions, or upload to CI:

- PlayStation 2 firmware, BIOS files, keys, or memory dumps.
- Disc images, original executables, overlays, or save states.
- Extracted textures, models, maps, audio, movies, scripts, dialogue, or other retail payloads.
- Decompiled source or mechanically translated retail instruction blocks.
- Network credentials, personal packet captures, or service secrets.

Hashes, sizes, addresses, aggregate counts, independently written format descriptions, and
small synthetic fixtures constructed in test source are acceptable when they do not reconstruct
copyrighted content. Committing a binary fixture requires an explicit public-tree policy review.

## Pure-native rule

The runtime executes only independently written code compiled for modern host CPUs. Do not add
a MIPS/R5900 interpreter, static/dynamic recompiler, PCSX2 runtime dependency, translated retail
instruction block, or mechanism that executes a retail executable/module. See
`docs/adr/0001-pure-native-runtime.md`.

Retail code may be inspected offline to establish observable contracts. Implement the resulting
behavior independently and cite reproducible metadata or behavioral evidence.

## Compatibility-to-engine boundary

Retail parsers and original-format interchange belong at bounded compatibility edges. Public engine
values own their data and express only independently evidenced semantics; they must not expose
borrowed retail spans, source offsets, proprietary paths, platform objects, or speculative field
names. A passive descriptor is inspection evidence, not a cookable asset.

Project-authored fixtures validate reusable engine contracts. Owner data may validate compatibility
adapters locally, but owner-derived content and cooked caches remain outside version control and
releases. Add a shared abstraction only for a demonstrated second consumer or a concrete
retail-independence test. See
[`ADR 0004`](docs/adr/0004-compatibility-first-engine-sdk.md) and the
[`engine and SDK roadmap`](docs/07-Engine-and-SDK-Roadmap.md).

## Build and test

Use a VS2022 developer environment on Windows:

```powershell
cmake --preset msvc
cmake --build --preset msvc-debug
ctest --preset msvc-debug
python -B tools/check_public_tree.py
```

Format decoders need three validation layers:

1. Synthetic valid and malformed fixtures committed with the test.
2. A metadata-only corpus check against an owner's locally supplied data.
3. A behavioral comparison against a named reference build when semantics are claimed.

Corpus data must never become a test fixture or CI artifact.

## Evidence language

Use the states defined in `docs/01-Clean-Room-Method.md`: confirmed, inferred, hypothesis, and
rejected. Do not promote an inference because it is convenient for implementation.

## Source license

Original project source and documentation are licensed under Apache-2.0. Contributions require
a Developer Certificate of Origin 1.1 sign-off:

```text
Signed-off-by: Your Name <your-email@example.com>
```

Add it with `git commit -s`. The sign-off certifies the provenance statements in `DCO.txt`; it
is not a copyright assignment. Do not sign off on copied/decompiled retail material. GitHub's
verified Dependabot update PRs are the sole automated exception; maintainers must still review the
upstream revision, license, and exact dependency change.
