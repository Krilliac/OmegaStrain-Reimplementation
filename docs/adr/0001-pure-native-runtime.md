# ADR 0001: Pure native runtime

- Status: accepted
- Date: 2026-07-16

## Decision

The shipping runtime executes only independently written native code compiled for modern host
CPUs. Windows x86-64 is the first target; portable core code may support ARM64 later.

The runtime will not include or depend on:

- A MIPS/R5900 interpreter or dynamic recompiler.
- Static translations of retail MIPS instruction blocks.
- PCSX2 libraries, PS2 BIOS/kernel emulation, or PS2 hardware emulation.
- Runtime execution of `SCUS_972.64`, retail overlays, or executable script modules.

The retail executable is a research artifact used to infer observable contracts. Asset files
from an owner's disc may be parsed as data. Any behavior discovered in executable or script
code is independently rewritten as native project code or project-owned declarative mission
data.

## Consequences

- Repository name: `OmegaStrain-Reimplementation`; “Recompiled” would misstate the method.
- PCSX2 remains a development-only behavioral oracle and debugger.
- Format importers must distinguish passive content from executable code.
- A retail module may be inspected by offline tools, but the shipping runtime never invokes
  its instructions or embeds a general mechanism capable of doing so.
- Compatibility work takes longer than instruction translation, but produces a maintainable,
  portable engine with no PS2 execution layer.
