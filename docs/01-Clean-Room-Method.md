# Clean-room method and scope

## Target

The target is a new native runtime that can consume data from an owner's NTSC-U retail image
of *Syphon Filter: The Omega Strain* and reproduce observable game behavior. PCSX2 and the
retail executable are research oracles only; neither is a shipping dependency.

The runtime is pure native code. It never interprets, recompiles, translates, or executes PS2
MIPS/R5900 instructions. Retail executable and script code are offline research inputs only;
their behavior is independently rewritten.

Windows x64 is the first supported host. The core remains platform-neutral so additional
hosts can be added after behavioral parity is established.

## Boundary

Allowed in version control:

- Original implementation source and tests.
- Independently written format descriptions and compatibility metadata.
- Hashes, sizes, addresses, call graphs, behavioral traces, and screenshots used as evidence.
- Small synthetic fixtures created for this project.

Never allowed in version control or releases:

- BIOS/firmware, disc images, original executables, save states, keys, or credentials.
- Extracted models, textures, audio, movies, scripts, dialogue, or other proprietary payloads.
- Decompiled source copied or mechanically translated from the retail executable.
- Any MIPS interpreter, recompiler, translated retail instruction block, or executable payload.

The implementation derives contracts from observed inputs/outputs and independently described
data layouts. Every compatibility claim must identify evidence and a reproducible check.

## Evidence states

- **Confirmed:** reproduced by a deterministic tool, debugger trace, or behavioral comparison.
- **Inferred:** the evidence fits, but at least one competing explanation remains.
- **Hypothesis:** a lead requiring a designed experiment.
- **Rejected:** tested and contradicted; retained to prevent repeated dead ends.

`analysis/evidence/ledger.jsonl` is the canonical index. Detailed reports live beside the
relevant ELF or format metadata.

## Validation ladder

1. Parse synthetic fixtures, including malformed boundaries.
2. Validate against the entire owned-disc corpus without exporting payloads.
3. Compare structural results with an independent implementation where possible.
4. Capture PCSX2 behavior at a named build, game hash, and breakpoint.
5. Run the same scenario in the native runtime and compare observable state.
6. Promote the behavior to an automated regression before claiming parity.
