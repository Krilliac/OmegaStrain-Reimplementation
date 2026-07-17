# ReSymbol dogfood: Omega Strain ELF intake

Verified on 2026-07-17 against isolated local ReSymbol commits. This report contains only
container identity, validation results, and workflow behavior. It contains no game bytes,
decoded instructions, recovered strings, or executable code.

## Result

The bounded ELF32 intake slice now works end to end on the owned retail executable:

- ReSymbol created an identity-bound `.resym` package in `--safe-mode`.
- `resymbol inspect --binary` verified the package against the exact input.
- JSON, Markdown, IDA Python, and Ghidra Java exports succeeded deterministically.
- The base symbol graph is empty: no functions, globals, calls, thunks, strings, types, or
  data references are claimed.
- MAP and PDB exports rejected the ELF project with the documented PE/x86-64-only error.
- Both rejected exports failed before creating their destination files.
- A leak scan found no absolute local path, private-tree path, or retail filename in the
  non-log artifacts.

All packages, exports, and command logs remain under the ignored `analysis/output/` tree.
Nothing derived from the owned executable was added to the public repository.

## ReSymbol change set

The feature was developed in an isolated ReSymbol worktree so an active integration session
and its staged changes were not disturbed:

- `f126552a7e9656967d37c08a8877151b8106d176` — bounded ELF32 little-endian `ET_EXEC` /
  `EM_MIPS` container intake.
- `35116d7` — follow-up correction for a pre-existing Workbench capability-count fixture
  exposed by the clean validation run.
- Local validation branch: `codex/elf32-container-intake-validation`.

These commits are local integration inputs at the time of this report. Publishing or merging
them into ReSymbol is owned by the active ReSymbol integration session; Omega does not vendor
or depend on an unpublished ReSymbol build.

The feature adds:

- checked ELF header, program-header, and section-header parsing with fixed collection limits;
- sparse, non-empty `PT_LOAD` metadata without allocating virtual gaps;
- explicit 32-bit virtual-range and file-range overflow rejection;
- schema-14 ELF package semantics while retaining schema 1-13 readers;
- the container-only architecture token `elf32-em-mips-le`;
- empty deterministic graph projection to neutral export formats;
- explicit MAP/PDB and PE-only Workbench gates; and
- source-built synthetic tests with independently chosen values and no proprietary fixture.

This is container intake only. It does not add a MIPS or R5900 decoder, disassembler,
interpreter, recompiler, emulator, or execution path.

## Validation

Validation used MSVC through VS2022 `vcvars64.bat`, with `CC=cl`, `CXX=cl`, an isolated Cargo
target directory, and one build job at a time:

- `cargo fmt --all -- --check`: passed.
- `git diff --check`: passed.
- ELF analysis integration tests: 5 passed.
- PE regression integration tests: 140 passed.
- package, export, and application targets: passed.
- CLI unit tests: 98 passed.
- CLI inspect integration tests: 4 passed.
- CLI patch integration tests: 6 passed.
- optional managed and WASM drop-in tests: 2 ignored because their external artifacts were not
  installed.
- Workbench tests after the fixture correction: 112 passed.

The clean `f126552` run first reproduced one unrelated baseline Workbench failure: the test
passed `14` capabilities while its already-updated expected string asserted `17`. Blame and the
feature diff confirmed the ELF change did not touch that assertion. Commit `35116d7` corrected
the two stale fixture inputs, and the full Workbench binary then passed 112/112.

## Owned input identity

The original executable remains only in Omega's ignored private tree:

```text
<omega-root>\private\extracted-disc\SCUS_972.64
```

- Size: 4,071,592 bytes
- SHA-256: `9924DA91767C8145411F37FA6C14C9D77208264C17F1CE9EE157D51ABDD31DC6`
- Container: ELF32, little-endian
- Machine: `EM_MIPS` (8)
- Entry VA: `0x00100008`
- ELF flags: `0x20924000`
- Program headers: 5
- Section headers: 11

The public metadata mirror is `analysis/elf/SCUS_97264.metadata.json`.

## Reproducible private workflow

Run only with a personally owned executable and keep the destination ignored:

```powershell
$resymbol = '<private-resymbol-build>\resymbol.exe'
$input = '.\private\extracted-disc\SCUS_972.64'
$out = '.\analysis\output\resymbol-elf'

New-Item -ItemType Directory -Path $out | Out-Null

& $resymbol --safe-mode analyze $input --output "$out\SCUS_97264.resym"
& $resymbol inspect "$out\SCUS_97264.resym" --binary $input
& $resymbol export "$out\SCUS_97264.resym" --format json --output "$out\symbols.json"
& $resymbol export "$out\SCUS_97264.resym" --format markdown --output "$out\symbols.md"
& $resymbol export "$out\SCUS_97264.resym" --format ida-python --output "$out\symbols.py"
& $resymbol export "$out\SCUS_97264.resym" --format ghidra-java `
  --output "$out\ReSymbolImport_OmegaElf.java"
```

`--safe-mode` with no explicit plugin is deliberate: only the built-in bounded container
analysis runs, and no third-party plugin receives the executable. ReSymbol's create-new policy
also requires fresh output paths rather than overwriting existing artifacts.

## Artifact boundary

The `.resym` package does not embed the executable. It records exact identity and derived ELF
metadata, including the input hash and size, entry point, header records, load mappings, image
extent, schema, and generator version. Neutral exports contain the identity gate and an empty
symbol graph. They are still kept private because hashes and layout metadata fingerprint the
proprietary input even though they do not reproduce its code or assets.

## Pure-native runtime boundary

ReSymbol is an offline research tool, not part of OpenOmega. The OpenOmega runtime remains
modern native C++ for x86-64 first, with ARM64 kept as a future portability target. It will not
ship or execute PlayStation 2 instructions and will not embed PCSX2, a MIPS interpreter, a MIPS
JIT, a static recompiler, an Emotion Engine runtime, or translated instruction blocks.

Any future R5900-aware decoding belongs in isolated offline research tooling. Its output may
inform clean-room behavioral specifications, but original instructions and mechanically
translated code are not runtime dependencies or public repository inputs.
