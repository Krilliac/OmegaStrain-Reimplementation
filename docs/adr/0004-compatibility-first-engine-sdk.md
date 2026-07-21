# ADR 0004: Compatibility-first engine and SDK evolution

- Status: accepted
- Date: 2026-07-20

## Context

OpenOmega's immediate obligation is a clean-room, pure-native compatibility runtime for the
owner-supplied NTSC-U release of *Syphon Filter: The Omega Strain*. That work is already producing
independently designed, reusable boundaries: owned asset and structural values, deterministic simulation,
generation-based resource services, renderer-neutral packets, native persistence, platform leaves,
and bounded format adapters.

Public historical material supports the inference of a proprietary Syphon Filter engine and
production toolchain, but it does not disclose their complete implementation. OpenOmega can
reproduce observable contracts and provide interoperable tools; it cannot claim to recover Bend
Studio's original source code, editor, or internal workflow.

## Decision

Omega Strain compatibility remains the proving workload and near-term priority. As evidence-backed
contracts stabilize, the implementation will evolve along one compatibility-first path into:

- the **OpenOmega Runtime**, which runs compatible games and project-owned content;
- the **OpenOmega Engine**, the reusable promoted semantic data, simulation, resource, media, render,
  audio, input, persistence, and platform contracts below title composition; and
- the **OpenOmega SDK**, the versioned command-line tools, asset compiler, inspection/debug surfaces,
  authoring interfaces, documentation, and examples above those contracts.

These names describe independently written OpenOmega technology. Documentation must not call it the
original Bend engine or imply historical source-level identity.

The architecture follows these rules:

1. Retail formats remain bounded compatibility adapters at the edge. Executable retail code is
   never run by the shipping runtime.
2. An adapter may publish promoted semantic engine data only when independent evidence supports the
   assigned semantics. Passive descriptors and structural envelopes remain inspection values.
3. Every published value owns its data. Existing compatibility values may intentionally preserve
   structural text, raw flags, observed fields, or source locators; they are not automatically stable
   semantic/cooked IR. A promoted semantic/cooked IR contains no borrowed retail spans, retail
   offsets, platform objects, or speculative meanings, and keeps provenance separate.
4. Retail inputs and project-authored inputs eventually converge at the same promoted semantic/import
   boundary. A deterministic native asset compiler may then produce versioned cooked data for the
   runtime.
5. The promoted-engine target is for runtime services to consume promoted semantic or cooked values.
   They do not parse title formats, depend on editor code, or retain proprietary input paths in public
   diagnostics. Until its provider/import boundary is extracted, the current `omega_content` and
   `AssetService` path may reach retail adapters as an explicitly transitional compatibility
   composition; that path is not a stable SDK edge.
6. Compatibility import/export modules remain separate from authoritative native storage. Original-
   format export is optional, tool-only, separately evidenced, bounded, and never required to run.
7. `OmegaApp` remains the composition root. Services have one owner, borrow stable dependencies,
   and communicate across threads through owned immutable packets, bounded queues, or generation
   handles.
8. Thread affinity and reload behavior are part of every public subsystem contract. When hot reload
   is implemented, it may exchange promoted assets or project-owned declarative data; it may not
   carry vtables, borrowed views, entity storage, persistence owners, or platform resources across
   the boundary.
9. A header under `native/include/omega/` is only a candidate for future engine API stability, never
   sufficient proof of it. Retail, compatibility, platform-named, provisional/source-preserving,
   application-host, diagnostic, and milestone-scaffolding contracts remain internal until an
   explicit decision promotes them.
10. Generalization is demand-driven. Introduce a shared abstraction only when a second real consumer
    or a concrete retail-independence test demonstrates the boundary.

Format capability advances explicitly through evidence, bounded inspection/import, promoted
semantic IR, content composition, runtime integration, behavioral comparison, and authoring support.
Parsing or owning a payload does not by itself authorize promotion to semantic IR or a cooked asset.

## Consequences

- Compatibility slices should leave reusable seams when the evidence makes those seams honest, but
  they do not pause to build speculative general-purpose systems.
- Project-generated fixtures and project-authored sample content validate the reusable engine path.
  Private owner data validates compatibility adapters and stays outside version control and releases.
- The current private dependency from `omega_content` to `omega_retail_formats` is a transitional
  compatibility composition, not proof of a retail-independent engine. A provider/import boundary
  will be extracted when the native asset path becomes a real second consumer.
- A future SDK is successful only when project-owned content can be authored, cooked, and run without
  owner data. A reusable-engine claim additionally requires a sample whose link allowlist contains
  only deliberately promoted engine/SDK targets, with no retail adapter or Omega title-composition
  and app-host target.
- The detailed staged plan lives in
  [`../07-Engine-and-SDK-Roadmap.md`](../07-Engine-and-SDK-Roadmap.md). Compatibility completion
  remains governed by [`../03-Milestones.md`](../03-Milestones.md).

## Non-goals

- Reconstructing or representing Bend Studio's exact historical source tree, editor, or build farm.
- Turning OpenOmega into a PlayStation 2 emulator or executing translated retail instructions.
- Competing immediately with general-purpose modern engines.
- Freezing title-specific quirks into engine APIs for hypothetical future consumers.
- Shipping proprietary game content, owner-derived cooked caches, or private research artifacts.

## Public historical references

- [Bend Studio history](https://www.bendstudio.com/blog/history-of-bend-studio/)
- [Paul Simon Martin, GDC Europe 2011 production slides](https://media.gdcvault.com/gdceurope2011/slides/Paul_Martin_Production_SupersizeYourProduction.pdf)
- [Jeff Ross interview discussing SyphonScript and production tools](https://www.thepixelempire.net/pixel-qa---jeff-ross-lead-designer-syphon-filter-3.html)
