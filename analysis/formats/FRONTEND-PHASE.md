# Bounded front-end phase/draw attribution fragment

Status: contract and Python tooling only. This repository defines and validates
the public wire contract and its sanitized output. No producer implements this
format yet: a separately queued private PCSX2-side lane owns any future
producer/emitter, is not present in this repository, and must not be inferred
from this document. No C++ mirror test exists yet (see "Deferred" below).

This document defines the public clean-room boundary implemented by
`tools/assemble_frontend_phase.py`. Binary inputs are research artifacts and
must remain outside version control. Only canonical sanitized JSON produced by
this tool may be considered for promotion, after an ordinary public-tree
review.

## Purpose

`omega-frontend-trace-v1` (`docs/05-Frontend-Trace-Contract.md`) aggregates
same-frame observation sites and loses chronological lane, text, and tick
order. `scene-fragment-v1` (`SCENE-FRAGMENT.md`, this directory) preserves
final anonymous GS draw order and record-to-draw membership edges, but has no
join to any front-end call/interval structure. Neither existing contract can
answer "which named front-end phase (a widget, visual, text, or action
lifecycle interval) was open when the vertices behind finalized draw N were
submitted?" `OMEGAFRPHASE0001` is a narrow, phase-only fragment that answers
only that question, so the two existing contracts remain byte-identical and
unaffected.

This fragment never proves ownership, visibility, occlusion, glyph identity,
asset identity, pixel contribution, or causality. An edge from an interval to
a draw means only that at least one primitive in that finalized draw used a
vertex accepted from a packet produced while that interval was open. Turning a
repeat-stable set of these edges into a canonical submission/lifecycle
ordering rule additionally requires independent static call-flow evidence in
agreement; observations from this fragment never auto-promote into canonical
semantics on their own. This document defines only the wire contract and its
bounded validator.

## Input envelope

The observer emits a little-endian, fixed-width `OMEGAFRPHASE0001` envelope.
Version 1 begins with the 16-byte magic, a `u32` version, a 32-byte nonzero
runtime-configuration SHA-256 (the same repeatability-gate role as
`scene-fragment-v1`'s digest), and four `u32` table counts. The tables follow
in this exact order:

| Table | Row width | Maximum rows | Retained meaning |
|---|---:|---:|---|
| invocations | 20 | 4,096 | Dense ID, frame, parent invocation, enter/exit event references |
| events | 17 | 8,192 | Dense ID, frame, owning invocation, Enter/Exit kind, reserved |
| draws | 13 | 32,768 | Dense final-draw ID, frame, submitted/skipped, reserved |
| phase/draw edges | 8 | 131,072 | Enter-event reference, final-draw reference |

The parser caps the whole fragment at 4 MiB, calculates the sole possible
length from the table counts before allocating row state, rejects trailing
data, and validates dense IDs, cross-table references, monotonic frame and
draw order, and reserved-must-be-zero padding. `draws` reuses
`scene-fragment-v1`'s own per-capture draw and edge capacity (32,768 /
131,072) because both fragments describe the same underlying producer-side
dense final-draw-finalization ordinal domain (`GSState::FlushPrim`,
`OmegaSceneTrace::OnGsDraw`, and `OmegaSceneTraceModel::Model::FinalizeGsDraw`
in the pre-audited native observer seams); this is a shared-domain
convenience, not a claim that the two fragments share a capture or a file.

### Invocation rows

`{id, frame, parent_invocation, enter_event, exit_event}`, all `u32`.

- `id` is dense and one-based (row position plus one).
- `frame` is the capture-relative frame the invocation entered on, 1 through
  600, non-decreasing across the table.
- `parent_invocation` is `0` for a root invocation, or a dense ID strictly
  less than `id` — a forest, never a cycle or a forward reference.
- `enter_event` and `exit_event` are both required dense event IDs with
  `enter_event < exit_event`. A deferred cross-table pass (below) requires the
  referenced events to name this same invocation with the matching Enter/Exit
  kind, and requires this row's `frame` to equal its enter event's `frame`.

### Event rows

`{id, frame, invocation, closed_kind, reserved}` — four `u32` fields with one
`u8 closed_kind` ahead of the trailing `u32 reserved`.

- `id` is dense and one-based; because events are this fragment's only
  cross-referenced monotonic sequence, this dense ID also serves as that
  monotonic sequence number.
- `frame` is bounded as above and non-decreasing across the table.
- `invocation` is a dense one-based reference into the invocation table.
- `closed_kind` is exactly `0` (Enter) or `1` (Exit).
- `reserved` must be exactly `0`. It carries no meaning in version 1 and
  exists only so a future version can widen this row without changing its
  width; a nonzero value is rejected rather than silently ignored.

Because invocation and event rows reference each other, the parser reads both
tables fully before cross-checking them. That deferred pass requires every
invocation's `enter_event`/`exit_event` to resolve to event rows naming that
invocation with the matching kind, and requires every invocation's `frame` to
equal its enter event's `frame`. Given the exact event-count-is-twice-
invocation-count precondition above, this per-invocation check alone already
forces a bijection between invocations and the events they claim: no fixed
event row can validly name two different invocations, and the count leaves no
room for a spare row, so every event is automatically exactly one invocation's
`enter_event` or `exit_event` with no orphan and no double claim. This is a
consequence of the two checks above, not a separately tracked totality pass.

### Draw rows

`{dense_final_draw_id, frame, submitted_or_skipped, reserved}`.

- `dense_final_draw_id` is dense and one-based, in the same producer-side
  finalization-ordinal domain described above and in `SCENE-FRAGMENT.md`.
- `frame` is bounded and non-decreasing as above.
- `submitted_or_skipped` is exactly `0` (submitted) or `1` (skipped — for
  example a forced renderer-neutral flush placeholder). It is completeness
  bookkeeping only and asserts no visibility or pixel contribution.
- `reserved` must be exactly `0`, for the same forward-compatibility reason as
  the event table.

### Phase/draw edge rows

`{event, draw}`, both `u32` dense references.

- `event` must reference an Enter-kind event row. Attribution is fixed at
  packet-submission time, when only the interval's Enter boundary is already
  known; the fragment never labels a packet with the interval active at the
  later draw-finalization time, so an edge can never reference an Exit event.
- `draw` is a dense reference into the draw table.
- Rows are producer-ordered: `draw` is non-decreasing across the table, and
  each `(event, draw)` pair is unique. At least one edge row must be present;
  a fragment with none establishes no attribution and is rejected as vacuous.

## Anonymous attribution boundary

The fragment carries no invocation name, source location, symbol, address, or
string of any kind. A consumer can recover only: (1) the anonymous invocation
forest shape, from `parent_invocation` links; and (2) which anonymous
invocation ordinal was open, if any, when a given finalized draw's
contributing vertices were produced, from the sanitized edge list. It does not
recover which draws contributed to which glyph, action, or widget, and it does
not recover an ordering relationship between two invocations that never share
an edge to a common draw.

When more than one fragment is supplied, every input must be byte-identical,
exactly as in `scene-fragment-v1`: a repeatability gate, not a multi-capture
merge rule. Version 1 has no public cross-fragment identity with which to
concatenate independent captures safely.

## Sanitized observation v1

Output is canonical ASCII JSON followed by one LF. Its sole shape is
explicitly non-semantic:

```json
{"schema":"openomega-sanitized-frontend-phase-observation-v1","invocation_parents":[0,1],"draw_count":2,"skipped_draw_count":0,"phase_draw_edges":[[1,1],[2,1],[2,2]]}
```

- `invocation_parents` is the complete dense array of `parent_invocation`
  values (`0` for a root), in dense-ID order.
- `draw_count` and `skipped_draw_count` are the total and skipped-only draw
  row counts.
- `phase_draw_edges` lists `[invocation_ordinal, draw_ordinal]` pairs, sorted
  by draw then invocation, with the raw `event` ID replaced by its owning
  invocation's ordinal — narrowing the wire fragment's event-level detail down
  to the only unit this contract calls meaningful.

The sample values above illustrate shape only. The document contains no
runtime-configuration digest, fragment digest, raw event ordinal, frame
number, draw disposition, source identity, memory or instruction provenance,
address, packet, register, raw payload, file path, executable identifier, or
retail identifier.

## Deferred

No C++ mirror test (compare `native/tests/scene_fragment_wire_contract_tests.cpp`)
exists yet for this contract; that requires a build-capable session and is not
claimed here. Until it exists, "mechanical mirroring" is one-sided: only the
Python parser, its generated fixture, and its own unit tests are verified this
session. No PCSX2-side producer for this format exists in any lane's tree yet,
and this document does not claim otherwise.

## Usage

```text
python -I -E -s -S -B tools/assemble_frontend_phase.py \
  --expected-sha256 <64-hex-digest> \
  --output <new-phase-observation.json> \
  <fragment> [<repeat-fragment> ...]
```

Isolated Python startup is required so user-site, `sitecustomize`, and
`PYTHONPATH` code cannot run before private inputs are handled. The output
path is opened exclusively and is never overwritten. A failed or short write
is left in place rather than unlinked by a racy pathname; retry with a fresh
path after inspecting or explicitly removing that file. Success and failure
diagnostics are fixed strings so private paths or parser details cannot be
echoed into logs.
