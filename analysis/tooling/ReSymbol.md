# ReSymbol dogfood: Omega Strain ELF intake

Verified on 2026-07-16 against the live ReSymbol integration worktree. This report contains only file identity and container metadata; it does not contain game bytes.

## Live ReSymbol state

- Source worktree: private local ReSymbol checkout (machine path intentionally omitted)
- Branch: `codex/integrate-resymbol`
- HEAD: `574325ba6254a34695247a38e22f522ccbe2d1bb`
- Upstream state at inspection: 46 commits ahead of `origin/codex/integrate-resymbol`
- Git status at inspection: clean
- Build/output root: private local target directory (machine path intentionally omitted)
- CLI version: `resymbol 0.1.0-alpha.1`
- CLI SHA-256: `4EF7BFE593A52EA93B51E94479282B6D5F1B41129D3F0F77E447BEF7A22B04E0`

The other Codex session was actively running this command while the inspection happened:

```text
cargo test --workspace --locked --release -j 1
```

No ReSymbol files, branches, commits, or build processes were changed by this Omega inspection.

## Owned input identity

Keep the original executable only in Omega's ignored private tree:

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

The non-private metadata source is `analysis/elf/SCUS_97264.metadata.json`.

## Current CLI workflow and observed failure

For supported inputs, ReSymbol's project artifact is a `.resym` package:

```powershell
resymbol analyze application.exe --output application.resym
resymbol inspect application.resym --binary application.exe
resymbol export application.resym --format json
resymbol export application.resym --format markdown
resymbol export application.resym --format ida-python
resymbol export application.resym --format ghidra-java
```

The package records identity and analysis data, not the input executable bytes. Once ELF/MIPS
intake exists, run from the Omega repository root and write the derived package only into the
ignored output directory:

```powershell
resymbol --safe-mode analyze `
  .\private\extracted-disc\SCUS_972.64 `
  --output .\analysis\output\SCUS_97264.resym
```

The current build exits 1 before creating a package:

```text
unsupported binary format (first bytes: 7f 45 4c 46 01 01 01 00)
```

This was tested with a unique temporary output path; no output file was created.

## Concrete ReSymbol gap

ReSymbol cannot currently create any project for this file:

1. `resymbol-analysis::analyze_bytes` recognizes only `MZ` and documents PE32+ x86-64 as the sole accepted format (`crates/resymbol-analysis/src/lib.rs`).
2. `BinaryFormat` already includes `Elf`, but `BinaryAnalysis` has only the `Pe` variant (`crates/resymbol-core/src/symbols.rs` and `crates/resymbol-analysis/src/types.rs`).
3. The CLI performs built-in base analysis before plugin discovery/execution (`crates/resymbol-cli/src/main.rs`), so a plugin cannot currently act as the "plugin-provided loader" anticipated by the `BinaryFormat` documentation.
4. The workbench, static address-space model, linear preview, patch flow, and several exporters match exhaustively on PE and/or x86-64.
5. ReSymbol's roadmap explicitly leaves ELF/Mach-O and additional architectures for a future milestone.

The owned file also exposes an important modeling constraint: `EM_MIPS` does not by itself mean a generic MIPS32 decoder is sufficient. PlayStation 2 code targets the Emotion Engine/R5900 family. The first container slice should preserve this uncertainty and make no instruction claims until an R5900-aware decoder is selected and tested.

## Smallest useful next action

Implement a bounded, container-only ELF32 little-endian intake slice before attempting MIPS disassembly:

1. Parse and validate ELF identity, `e_machine`, entry VA, flags, program headers, and section headers with checked arithmetic and fixed collection limits.
2. Add a format-neutral ELF analysis variant and sparse load-segment address mapping. Ignore zero-sized `PT_LOAD` records when computing the image extent; do not allocate a buffer spanning virtual gaps.
3. Represent this file with image base `0x00100000` and entry RVA `0x8`; preserve segment permissions exactly, including the main RWX load segment.
4. Produce a `.resym` package with an empty deterministic symbol graph plus container evidence. Because this adds a new producer payload shape to the strict package contract, evaluate it as schema 14 rather than silently emitting a novel shape under schema 13.
5. Add a synthetic, redistributable ELF32 little-endian `EM_MIPS` fixture to ReSymbol tests. Use `SCUS_972.64` only for local manual dogfooding, never as a fixture or committed input.
6. Keep x64-only preview, PE patching, MAP, and PDB actions explicitly unavailable. JSON, Markdown, and eventually IDA/Ghidra projection are the first relevant outputs.

That slice would make Omega project creation and address review possible without claiming MIPS decoding. An R5900 decoder and call/data-reference recovery should be a separate follow-up. These are reverse-engineering inputs only: the Omega native runtime remains pure modern x86-64/ARM64 code and does not ship or execute PS2 instructions.

## Dogfood decision

A ReSymbol feature is warranted. This is not an Omega configuration problem: a valid, small ELF reaches the intended bounded reader and is rejected at the format dispatcher. The safe handoff is this report plus the exact reproduction above. Do not patch the live ReSymbol integration worktree until its active owner finishes the 46-commit integration/test run.
