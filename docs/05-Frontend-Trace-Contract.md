# Sanitized front-end trace contract

## Scope

`omega-frontend-trace-v1` is a repository-side interchange and validation contract for a
separately maintained private behavioral-observation producer. An out-of-tree custom PCSX2 branch
based on the pinned official source now implements that producer and its bounded runner; its source,
build tree, executable, configuration, raw observations, and private fragments remain outside
OpenOmega version control. This document defines only the public sanitized boundary. The schema
records bounded anonymous aggregates needed to compare five front-end actions. OpenOmega does not
vendor or invoke an emulator hook, identify executable locations, decode a retail state machine, or
assign meaning to any observed transition or resource.

Only a bundle accepted by `tools/validate_frontend_trace.py` may cross from private research into
the public clean-room repository. Producer logs, failed captures, temporary files, and all raw
observations remain private.

## Canonical document

The file is an ASCII-only subset of UTF-8 JSON. It is minified, uses the exact object-key and array
orders below, has no byte-order mark, and ends in exactly one line feed. Duplicate keys, alternate
whitespace, extra keys, and non-finite numbers are rejected.

The root keys, in order, are:

1. `schema`, exactly `omega-frontend-trace-v1`;
2. `input_relative_frame`, an integer from 1 through 120; and
3. `reports`, exactly ten reports.

Reports use scenario-major order: `idle`, `previous`, `next`, `confirm`, and `back`. Each scenario
has trial 0 followed immediately by trial 1. The shared input frame aligns all trials. In the four
action scenarios, a producer issues exactly one named action at that frame. In `idle`, it issues no
action; the same frame is only the aligned comparison marker. This one-action rule is a producer
invariant because an aggregate report cannot prove how many inputs were injected.

Each report has these keys in order:

| Key | Contract |
| --- | --- |
| `scenario` | The fixed scenario for this canonical position. |
| `trial` | Zero, then one, for the scenario. |
| `frame_count` | 1 through 600 and greater than `input_relative_frame`. |
| `event_class_counts` | Exact ordered keys `control`, `transition`, `render`, `resource`, `other`; each count is 0 through 1,000,000. |
| `site_events` | One through 32,768 aggregate rows. |
| `transition_ordinals` | One through 1,024 normalized transition rows. |
| `resource_class_totals` | Exact ordered keys `texture`, `font`, `audio`, `other`; each total is 0 through 1,000,000. |

A site-event row has exact ordered keys `frame_delta`, `site`, `event_class`, and `count`.
`frame_delta` advances from the previous row, with the first delta measured from trace start. The
cumulative frame must remain inside `frame_count`. Rows at one frame are strictly ordered by site
and fixed event-class ordinal. Anonymous sites are assigned 0, 1, 2, and so on at first appearance,
without gaps, and one site cannot change event class. Positive row counts must sum exactly to the
fixed event-class counts.

A transition row has exact ordered keys `frame_delta` and `ordinal`. Deltas have the same bounded
interpretation as site events. State identities are replaced by first-seen ordinals: the first is
zero, each unseen identity receives exactly the next ordinal, and a row cannot repeat its immediate
predecessor. The number of rows equals the `transition` event count. These ordinals are anonymous
comparison tokens, not recovered menu-state meanings.

The four resource totals sum exactly to the `resource` event count. They report resource-class
touches only. They carry no names, paths, hashes, payloads, dimensions, formats, bindings, or
ownership inference.

Trial is the only field removed when normalizing a repeat pair. The validator canonically encodes
the remaining fields and requires the two byte strings to be identical. It also requires at least
one action scenario's normalized transition sequence to differ from the idle sequence. This is a
minimum signal check, not proof that an action, transition, or resource has been understood.

## Fail-closed producer lifecycle

The reviewed out-of-tree private producer mirrors the established bounded tracer lifecycle:

`Disabled -> Configured -> Loading -> Armed -> Stopping -> Finalized`

Any rejected precondition or runtime error enters `Failed`; it never publishes a report. The
stages have these responsibilities:

- `Disabled`: no instrumentation or retained observation state.
- `Configured`: validate fixed scenarios, two-trial policy, the 1-through-600 frame bound, the
  1-through-120 input-frame bound, a 1-through-120 consecutive-frame quiet period that fits inside
  the configured frame span, output absence, and a privately configured expected-disc CRC gate.
  The CRC value is never serialized.
- `Loading`: verify the owned input against that private gate before observation begins.
- `Armed`: reject ordinary breakpoints or memchecks, install only the dedicated bounded observer,
  and begin anonymous first-seen numbering.
- `Stopping`: stop at the configured frame or quiet-period bound, remove instrumentation, normalize
  all aggregates, and scrub raw observation state.
- `Finalized`: validate canonical bytes, create a no-clobber private temporary in the destination
  directory, write and flush it, atomically publish it, and synchronize the directory entry.
- `Failed`: remove instrumentation, scrub maps and buffers, delete any unpublished temporary, and
  emit no partial public report.

The runner must never reuse ordinary debugging facilities while collecting a trace, overwrite an
existing destination, or publish directly to its final name. Every failure before the no-replace
rename emits no final report. After the rename succeeds, only nonthrowing cleanup may follow, so the
runner cannot report failure after the report is already visible. OpenOmega implements only the
public contract and validator; the producer and runner remain out of tree.

The exact combined local source at `d17a521ca03e4de1f063f704142540da5aaaa343` has passed
independent source/API review, compiled as both a one-job MSVC RelWithDebInfo `PCSX2` core target and
a true Release `pcsx2-qt` target, and reached isolated version and configuration initialization. This
establishes runnable-tool readiness only. The producer has not been armed or run, and no BIOS, disc,
savestate, private observer site, owner input, fragment, capture, or public report was used or
produced by that validation.

## Privacy boundary

The schema has no extension points. The validator rejects unknown or reordered fields recursively,
including fields that could carry paths, names, hashes, executable positions, addresses, offsets,
registers, raw state, instructions, or payload bytes. Its command-line diagnostics never echo an
input path or rejected value.

The fixtures under `analysis/fixtures/frontend_trace_v1/` are deterministic project-generated
documents. They contain no captured values. The accepted fixture demonstrates structure only; its
counts and transitions make no claim about retail behavior.

## Use

Validate a candidate bundle before review:

```powershell
python -B tools/validate_frontend_trace.py <sanitized-report.json>
```

Exit 0 means the bundle is canonical, sanitized, internally cross-counted, repeat-stable, and has a
minimum action-distinguishing transition signal. It does not establish menu fidelity, state names,
asset bindings, rendering, audio behavior, timing equivalence, or emulator equivalence.
