# OpenOmega engine and SDK roadmap

This document is the long-horizon implementation plan accepted by
[`ADR 0004`](adr/0004-compatibility-first-engine-sdk.md). It complements the compatibility milestones
in [`03-Milestones.md`](03-Milestones.md); it does not postpone reusable design until compatibility is
complete or expand the clean-room boundary.

## Target product shape

- **Runtime:** will load supported owner data or project-owned cooked content and run the game.
- **Engine:** will own platform-neutral promoted semantic data and runtime subsystem contracts.
- **SDK:** will import, inspect, validate, cook, debug, author, and package content through those
  contracts.
- **Compatibility modules:** understand retail containers, formats, save interchange, and any
  optional original-format export. They are engine leaves, not the engine's storage model.

The evidence ledger, format dossiers, and decoder coverage matrix remain authoritative for current
format status. This roadmap defines promotion rules and future exits rather than duplicating that
changing inventory.

## Target data flow

```text
private behavioral research
        |
        v
public-safe evidence -> bounded retail adapters --+
                                                   |
project-owned source -> native source importers ----+-> promoted semantic IR
                                                        |
                                                        v
                                            deterministic asset compiler
                                                        |
                                                        v
                                           versioned cooked data/cache
                                                        |
                                                        v
                                             AssetService and runtime

promoted semantic or source-preserving compatibility values -> optional tool-only exporters
```

Owner-derived cooked caches are rebuildable private outputs. They remain ignored and never become
release payloads. The runtime will consume promoted semantic or cooked values rather than teaching
every engine subsystem about every retail layout.

## Capability maturity

Every asset or behavior family is tracked independently through these levels:

0. **Recorded:** aggregate evidence or an explicitly labeled falsifiable hypothesis exists.
1. **Inspected:** a bounded native descriptor/envelope validates its declared structural boundary;
   the evidence state remains explicit.
2. **Semantic:** an importer publishes owned semantic IR supported by independent evidence.
3. **Composed:** content services resolve dependencies without leaking retail representations.
4. **Integrated:** runtime systems consume the promoted semantic value through explicit ownership and thread
   contracts.
5. **Matched:** named behavioral comparisons and regressions justify the compatibility claim.
6. **Authorable:** project-owned source can validate, cook, reload, and run through the same engine
   contract.
A family may stop at any level. In particular, an inspected passive descriptor or source-preserving
owned value is not promoted semantic IR, and a semantic value is not automatically safe to serialize
as a stable SDK format.

**Exportable** is an optional, independent facet rather than a sequential maturity level. An exporter
earns that facet only through its own lawful, bounded, synthetic round-trip and target-acceptance
evidence. Exportability may be established whenever its contract is ready; it neither implies runtime
maturity nor gates authoring or the reusable-engine proof.

## Cross-cutting invariants

- Retail parsing stays in stateless or explicitly owned compatibility leaves.
- Every published value owns its data. Existing source-preserving compatibility values may retain
  structural text, raw flags, observed fields, or locators without becoming stable engine IR.
- Promoted semantic/cooked values express only evidenced semantics; provenance, source locators, and
  round-trip state remain separate from them.
- The composition root owns lifecycle-heavy services; components remain world-owned plain state.
- Worker threads read/decode, the game thread publishes and mutates simulation, the render thread owns
  GPU work, and the audio callback never performs file I/O or blocking decode.
- Cross-thread boundaries use immutable packets, bounded queues, or generation handles.
- Public APIs name thread affinity, ownership, failure, bounds, and reload behavior.
- Editor and command-line tools depend on engine/SDK contracts; shipping engine targets never depend
  on editor code.
- New abstractions require a second real consumer or a test proving title-independent use.
- Every promotion cites synthetic tests, corpus-safe evidence where applicable, and a behavioral
  oracle before claiming parity.

## Staged work

### SDK-0: Documentation and evidence baseline

- Keep the decoder matrix, dossiers, evidence ledger, ADRs, architecture, and milestones consistent.
- Track structure, semantic promotion, runtime integration, and behavioral parity as separate facts.
- Record rejected hypotheses so tools and APIs do not silently revive them.

Exit: source and documentation agree, and every promoted capability names its evidence and tests.

### SDK-1: Unified importer boundary

- Converge eligible semantic decoders behind a bounded typed import contract while leaving
  source-preserving structural values explicitly classified.
- Keep passive descriptors available to inspection tools but ineligible for runtime cooking.
- Preserve typed failures and resource budgets without exposing private identities or bytes.

Exit: eligible semantic decoders can be invoked deterministically through one import surface while
retail-specific and source-preserving types remain below it.

### SDK-2: Semantic IR v1

- Version scene, render mesh, collision, material, texture, skeleton, animation, audio, UI, and
  mission values incrementally as their semantics become proven.
- Define dependency references, migrations, and deterministic serialization only for stable values.
- Keep source-preserving round-trip data outside promoted runtime IR; wrap or migrate existing
  compatibility `*IR` values rather than treating their current names as stability promises.

Exit: project-generated semantic fixtures serialize, migrate, and round-trip deterministically.

### SDK-3: Native asset compiler and cache

- Add a deterministic asset compiler (working name `omega_assetc`).
- Build a dependency graph and versioned cooked chunks with atomic publication and bounded reads.
- Keep owner-derived outputs in an ignored, reproducible cache.
- Move `AssetService` toward immutable cooked generations without making it retail-format-aware.

Exit: a project-generated scene boots solely from native cooked assets.

### SDK-4: Read-only Workbench and diagnostics

- Provide asset, dependency, semantic-IR, scene, texture-policy, audio, and structural-diff views.
- Keep editor state above importers and renderer resources behind render-service contracts.
- Make every report safe for the public/private boundary by construction.

Exit: every semantic family and approved passive descriptor can be inspected without mutation or
private-data leakage.

### SDK-5: Evidence-gated vertical slices

- Scene: placement, visibility, cameras, material/texture binding, and render geometry.
- Actors: skeleton hierarchy, skinning, animation timing, collision, and controllers.
- Missions: independently rewritten objectives, triggers, AI, inventory, and checkpoints.
- Front end: UI/font semantics only after consumer and behavioral evidence.
- Media: title selection, timing, subtitles, mixing, and lifecycle policy.

Each slice follows evidence -> importer -> semantic IR -> compiler -> runtime/tool -> regression.

Exit: at least one complete level and mission loop use stable semantic/cooked contracts rather than
diagnostic projections.

### SDK-6: Authoring and mod SDK

- Define project-owned source formats and validators.
- Add standard interchange/DCC import where it preserves the engine contract.
- Add scene/prefab/UI/mission authoring and frame-boundary hot reload through immutable generations.
- Publish documentation and completely project-authored examples.

Exit: a contributor can author, cook, package, and play a project-authored sample without retail data.

### SDK-7: Optional exporters

- Prefer native project/cooked export, then standard interchange such as glTF, PNG, and WAV.
- Isolate original-format writers/repackers in optional compatibility tooling.
- Require synthetic exact round-trips and independent target acceptance before claiming compatibility.
- Never emit or translate executable retail instruction payloads.

Exit: each advertised exporter has a bounded contract and an automated acceptance test.

SDK-7 is independent and optional. It does not gate SDK-8.

### SDK-8: Reusable-engine proof

- Freeze an initial public API/version policy only after real external use.
- Supply a sample project, packaging contract, extension points, reference documentation, and CI.
- Demonstrate that title compatibility is an adapter/composition layer rather than a core dependency.

Exit: a non-Omega, project-owned demo uses the same engine and toolchain successfully without owner
data. Its link graph is checked against an allowlist of deliberately promoted engine/SDK targets and
contains no retail/compatibility adapter or Omega title-composition/app-host target.

## Decision checkpoints

Create or amend an ADR before stabilizing a new public subsystem, cooked format, plugin boundary,
scripting/mission language, editor persistence format, network replacement protocol, or original-
format exporter. Each decision must state callers, sole owner, borrowed dependencies, thread
affinity, teardown order, reload boundary, compatibility impact, and the regression that proves it.
