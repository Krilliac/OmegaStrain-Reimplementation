# Private front-end phase trace and public categorical reducer

Status: Python contract and generated fixture only. No C++ wire mirror, PCSX2
producer, owner capture, or runtime consumer exists yet.

`tools/assemble_frontend_phase.py` validates the fixed-width
`OMEGAFRPHASE0002` research envelope. The envelope and its normalized detailed
trace are private intermediates. They belong only at explicitly selected,
ignored destinations. They are not evidence-ledger artifacts and must not be
committed after an owner-data run.

The separate public report contains only fixed completeness/policy categories
and bounded aggregate record counts. It contains no invocation forest, site,
event, submission, membership, frame sequence, capture-domain value, digest,
path, address, symbol, name, asset identity, or payload byte.

This machinery never establishes ownership, visibility, occlusion, glyph or
asset identity, pixel contribution, causality, retail semantics, or an
ordering rule. A complete private trace is only eligible for comparison with
independent static call-flow evidence. Observation never auto-promotes into
canonical semantics.

## Private inputs and joins

The tool consumes three kinds of private input:

1. one reference phase fragment and optionally up to seven distinct stored
   copies used only for byte-for-byte repeat comparison;
2. a separately selected private site-map binding; and
3. a separately selected private capture-domain manifest.

The site-map binding has the exact private JSON shape:

```json
{"schema":"openomega-private-frontend-site-map-binding-v1","runtime_config_digest":"<64 lower-hex>","site_map_digest":"<64 lower-hex>","site_count":2}
```

The binding contains only the consistency values and neutral dense site count
needed by this validator. A producer's actual static locator-to-ordinal map is
owner-private and outside this format. The fragment's runtime-configuration
and site-map values must match the selected binding exactly. These digests are
consistency/deduplication aids only; they are not authentication, an
immutability proof, or evidence of semantic identity.

The capture-domain manifest has the exact private JSON shape:

```json
{"schema":"openomega-private-capture-domain-manifest-v1","phase_capture_domain":"<64 lower-hex>","scene_capture_domain":"<64 lower-hex>","frontend_capture_domain":"<64 lower-hex>"}
```

All three values must equal each other and the phase fragment's private
capture-domain value. Similar dense final-draw ordinals never authorize a
cross-artifact join. The future phase, scene, and front-end producers must
copy one capture-domain value from the same committed capture transaction.
The current scene/front-end contracts and producer branches do not yet carry
that field, so a real three-artifact join remains blocked.

## Version 2 envelope

All integers are little-endian. The header is:

| Field | Width |
| --- | ---: |
| magic `OMEGAFRPHASE0002` | 16 |
| version `2` | 4 |
| private capture-domain consistency value | 32 |
| runtime-configuration consistency digest | 32 |
| private site-map consistency digest | 32 |
| retained frame count | 4 |
| terminal status | 4 |
| commit state | 4 |
| seven failure-accounting counters | 28 |
| six discovered table counts | 24 |
| six retained/table counts | 24 |

The six tables follow in this order:

| Table | Row | Width | Hard maximum |
| --- | --- | ---: | ---: |
| sites | `{ordinal}` | 4 | 4,096 |
| invocations | `{ordinal, site, lane, parent, enter_event, exit_event}` | 24 | 4,096 |
| events | `{ordinal, sequence, frame, invocation, kind:u8, reserved}` | 21 | 8,192 |
| submissions | `{ordinal, sequence, frame, invocation, primitive_begin, primitive_count}` | 24 | 131,072 |
| draws | `{ordinal, sequence, frame, disposition:u8, reserved}` | 17 | 32,768 |
| memberships | `{submission, draw}` | 8 | 131,072 |

The header's retained counts are the physical row counts and determine the
only accepted byte length. Trailing bytes are rejected. Every identifier is a
dense one-based ordinal. The tracked fixture contains neutral ordinals and
project-generated consistency values only; it contains no source address,
symbol, name, locator, or retail identity.

### Site and invocation rows

`invocation.site` is a required reference to the bounded anonymous site table.
Unknown sites stay anonymous and unknown. Version 2 admits exactly one lane,
encoded as `lane = 0`; any other value is rejected. A root has `parent = 0`.
A child must name the invocation at the top of the one admitted lifecycle
stack and its parent must be an earlier dense ordinal.

Each invocation names exactly one Enter and one Exit event. The event count
must equal twice the invocation count. Cross-checking rejects orphan events,
double claims, missing lifecycle boundaries, crossings, and a nonempty stack
at termination. Parent/child intervals must satisfy:

```text
parent.enter < child.enter < child.exit < parent.exit
```

### Global chronology

Events, submissions, and finalized draws each carry a monotonic sequence in
one shared producer domain. The union must be exactly dense from one through
the number of retained temporal rows. A duplicate or gap is rejected.
Chronology is retained only in the private trace.

Event frames, submission frames, and draw frames must fall within the retained
frame count. An attributed submission must occur strictly between its named
invocation's Enter and Exit sequence and within that interval's frame range.
The producer records the immutable invocation context at submission time; a
consumer must never infer it from mutable "current phase" state at later draw
finalization.

Submission primitive ranges begin at zero and are exactly contiguous in
submission order. They preserve within-draw producer chronology without
sorting invocation ordinals or using traversal order.

### Draw disposition and membership

Each submission appears in exactly one membership row. Memberships are
strictly ordered by `(draw, submission)`. A submitted draw has at least one
membership. A skipped draw has none. Every member submission precedes its
final draw in the global sequence, and the draw frame cannot precede either
the submission frame or invocation entry frame.

The private model retains every submitted/skipped disposition and membership.
The public reducer emits only total submitted, skipped, and membership record
counts.

### Terminal state and reconciliation

Terminal status is exactly one of:

- `Complete`
- `TelemetryOverflow`
- `TelemetryDropped`
- `VmReset`
- `SavestateDiscontinuity`
- `QueueExhausted`
- `ProducerAborted`
- `OutputFailure`
- `InternalFailure`

Commit state is exactly `Committed` or `Aborted`. `Complete` requires
`Committed`, zero failure counters, and exact equality between all discovered
and retained table counts. Every incomplete category requires `Aborted`.
Nonzero failure counters use the fixed precedence shown above; the terminal
status must match the first nonzero counter. `ProducerAborted` has no failure
counter. Discovered counts may never be less than retained counts.

An incomplete fragment may still be structurally inspected and written to an
explicit private sink, but `ordering_evidence_valid` is false and its public
policy is `IncompleteCapture`. It cannot support an ordering conclusion.
Complete captures with no submission records have public policy
`NoOrderingEvidence`. Complete captures with submissions have policy
`PrivateReviewRequired`, which still requires independent static evidence.

## Output separation

### Explicit private trace

`--private-output FILE` is the only CLI route that writes
`openomega-private-frontend-phase-trace-v2`. It retains:

- private capture/configuration/site-map consistency values;
- frame count and terminal commit/abort reconciliation;
- anonymous site and invocation ordinals;
- exact lifecycle, submission, and final-draw chronology;
- submission primitive ranges;
- draw dispositions; and
- submission-to-draw memberships.

The option must point to an ignored private destination. It is absent by
default. If the resolved destination is inside this repository, the CLI
accepts it only under `/private/`, `/runtime/`, or `/analysis/output/`, which
are explicit repository ignore roots; a destination outside the repository is
accepted as the caller-selected private sink. Private output is ASCII JSON
with one LF and is capped during streaming encoding before write.
If the destination resolves inside this repository, it must fall under
`private/`, `runtime/`, or `analysis/output/`; any other in-repository path is
refused before any input is read or any file is written. A destination that
resolves outside this repository is accepted unconditionally, since this
module has no tracked tree to check it against there.

### Default public report

`openomega-public-frontend-phase-report-v1` contains exactly:

- a fixed terminal-status category;
- `Complete` or `Incomplete`;
- `PrivateReviewRequired`, `NoOrderingEvidence`, or `IncompleteCapture`; and
- eight fixed aggregate count rows.

It contains no private relation or identity and is byte-deterministic for the
same validated model. A public report is not automatically suitable for the
evidence ledger; it still requires ordinary public-tree review and cannot
upgrade hypotheses.

## Limits and I/O behavior

Every capacity can be tightened through `PhaseLimits`; CLI callers may repeat
`--limit NAME=VALUE`. No value may exceed its immutable hard ceiling:

| Capacity | Hard ceiling |
| --- | ---: |
| one fragment | 4 MiB |
| all fragment copies | 32 MiB |
| fragment copies | 8 |
| one private manifest | 64 KiB |
| one manifest string | 256 bytes |
| frames | 600 |
| sites | 4,096 |
| lifecycle nesting | 256 |
| invocations | 4,096 |
| events | 8,192 |
| submissions | 131,072 |
| draws | 32,768 |
| memberships | 131,072 |
| failure accounting | 131,072 |
| logical lookup work | 1,048,576 |
| logical parser scratch | 16 MiB |
| private output | 16 MiB |
| public aggregate rows | 8 |
| public output | 4 KiB |

The parser checks calculated wire size, logical scratch, and logical lookup
work before table allocation. A path-based run snapshots the reference bytes,
checks file identity/size/timestamp before and after the bounded read, verifies
the caller-supplied SHA-256 over exactly the captured bytes, and parses only
that immutable snapshot. Each repeat must be a different stored file and is
stream-compared against the reference without being parsed or retained.
In-memory repeat verification similarly rejects two references to the same
byte object.

SHA-256 here is only a caller-selected consistency guard. Because parsing
starts only after the exact snapshot digest is verified, bytes cannot be
consumed by the parser before that check. Manifest reads are also converted to
one immutable bounded snapshot before parsing.

Outputs use exclusive create and exact write accounting. Existing files are
never overwritten. A short or failed output is left in place; the tool never
unlinks by pathname after a failure. Ordinary stdout/stderr diagnostics are
fixed and contain no path or exception text. An explicitly incomplete capture
writes its requested reports, returns exit status 2, and cannot be mistaken
for valid ordering evidence.

## Usage

```text
python -I -E -s -S -B tools/assemble_frontend_phase.py \
  --site-map-binding <ignored-private-binding.json> \
  --capture-manifest <ignored-private-capture-manifest.json> \
  --expected-sha256 <64-hex-consistency-value> \
  --public-output <new-public-report.json> \
  [--private-output <new-ignored-private-trace.json>] \
  [--limit NAME=VALUE ...] \
  <fragment> [<distinct-repeat-copy> ...]
```

Isolated Python startup prevents user-site, `sitecustomize`, and `PYTHONPATH`
code from running before private inputs are handled.

## Mandatory next blocker

The generated Python builder, parser, and checked-in fixture are not
independent producer evidence. Before any owner capture:

1. a C++ producer-wire mirror test must independently emit the exact fixture
   bytes and exercise every field/ceiling;
2. the PCSX2 producer must preserve immutable submission-time context and
   terminal reconciliation;
3. the scene and front-end artifacts must gain the shared capture-domain
   field and the private combined validator must be exercised across all
   three artifacts; and
4. the resulting private trace must agree with independent static call-flow
   evidence before any narrow ordering policy is proposed.

No producer, owner capture, named phase, grounded layer/text/animation order,
retail menu, frame parity, or PCSX2 equivalence is claimed here.
