# Bounded scene-fragment assembly

Status: implemented parser and camera-only sanitizer; indexed geometry blocked by the current
producer contract.

This document defines the public clean-room boundary implemented by
`tools/assemble_scene_fragment.py`. Binary inputs are research artifacts and must remain outside
version control. Only canonical sanitized JSON may be considered for promotion, after an ordinary
public-tree review.

## Input envelope

The observer emits a little-endian, fixed-width `OMEGASCNPART0001` envelope. Version 1 begins with
the 16-byte magic, a `u32` version, a 32-byte nonzero runtime-configuration SHA-256, and twelve
`u32` table counts. The tables follow in this exact order:

| Table | Row width | Maximum rows | Retained meaning |
|---|---:|---:|---|
| frames | 4 | 600 | Dense frame ordinal |
| spans | 4 | 16 | Anonymous span ordinal |
| EE sites | 12 | 512 | Dense ID, span ID, access width |
| records | 12 | 16,384 | Dense ID, span ID, byte size |
| camera components | 4 | 64 | Dense component ordinal |
| VIF destination extents | 4 | 32,768 | Dense destination ordinal |
| INL observations | 28 | 32,768 | Anonymous references, relative offset, width, occurrence count |
| VIF observations | 50 | 32,768 | Anonymous references and bounded execution metadata |
| camera samples | 12 | 38,400 | Frame ID, component ID, finite `f32` value |
| GS state classes | 4 | 4,096 | Dense anonymous state-class ordinal |
| draw summaries | 188 | 32,768 | Derived primitive counts, unique-reference count, and extrema |
| membership edges | 8 | 131,072 | Anonymous record-to-draw correlation |

The parser caps the whole fragment at 16 MiB, calculates the sole possible length from the table
counts before allocating row state, rejects trailing data, and validates dense IDs, references,
INL and VIF source extents against their referenced anonymous records, coalesced-row uniqueness,
numeric domains, canonical zeroes, finite values, primitive arities, range consistency, and producer
ordering. It uses the opaque runtime-configuration digest only to require identical repeat
configuration and never copies that digest into the sanitized observation.

When more than one fragment is supplied, every input must be byte-identical. The tool compares both
SHA-256 and bytes, and optionally checks `--expected-sha256`. This is a repeatability gate, not a
multi-part merge rule: version 1 has no public cross-fragment identity with which to concatenate
independent captures safely.

`native/tests/scene_fragment_wire_contract_tests.cpp` independently constructs the version-1
fixed-width envelope using C++ append operations corresponding to the observer serializer. CTest
requires it to match `analysis/fixtures/scene_fragment_v1/synthetic-producer.hex` byte for byte, and
the Python parser suite consumes that same ASCII-hex golden. This couples both implementations to
one public synthetic producer vector instead of letting a Python-only fixture drift with its parser.

## Anonymous transform boundary

The input carries anonymous, first-seen camera-component IDs; it does not carry a semantic matrix
slot mapping, transform direction, multiplication convention, or camera role. The current bounded
observation is structurally consistent with an affine orthonormal transform with translation, not a
projection matrix. That property is insufficient to decide whether the transform maps world to
view, view to world, or serves another role.

The sanitizer therefore emits only the final complete dense array under `anonymous_components`.
It has no attestation option and never emits `world_to_clip` or another matrix-semantic field. A
later revision may add a separately reviewed, digest-bound semantic attestation after transform
direction and convention are established. An incomplete frame is never padded, and an identity
transform is never substituted.

## Sanitized observation v1

Output is canonical ASCII JSON followed by one LF. Its sole shape is explicitly non-semantic:

```json
{"schema":"openomega-sanitized-scene-observation-v1","camera":{"anonymous_components":[0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0]},"material_buckets":[],"render_meshes":[],"mesh_instances":[]}
```

The sample zeroes illustrate shape only. The document contains no claimed matrix meaning,
runtime-configuration digest, fragment digest, component mapping, source identity, memory or
instruction provenance, address, site, span, record, offset, packet, register, raw payload, file
path, executable identifier, or retail identifier.

`material_buckets`, `render_meshes`, and `mesh_instances` must be empty in version 1. A future
evidence-backed geometry producer requires a new schema version instead of silently changing this
contract.

## Exact geometry blocker

The current fragment does not contain vertex positions or per-primitive vertex/index connectivity.
Each draw retains only aggregate primitive counts, a draw-wide unique-reference count, and minimum
and maximum coordinate values. Many unrelated meshes have the same counts and extrema, so turning
those extrema into corners, triangles, or an index buffer would fabricate geometry.

The anonymous GS state class similarly contains no renderer properties and cannot establish a
material. Without triangles there is also nothing to which a normalized material bucket could be
attached. The assembler therefore validates and discards draw summaries rather than promoting the
anonymous state ordinals or inventing material assignments.

Unblocking indexed rendering requires a separately reviewed producer revision that emits a bounded,
sanitizable stream of derived vertex positions and explicit triangle connectivity, plus enough
renderer-neutral state to justify material bucketing. Raw packets, raw registers, absolute source
locations, and private runtime identities remain outside that future boundary.

## Usage

```text
python -I -E -s -S -B tools/assemble_scene_fragment.py \
  --expected-sha256 <64-hex-digest> \
  --output <new-scene-observation.json> \
  <fragment> [<repeat-fragment> ...]
```

Isolated Python startup is required so user-site, `sitecustomize`, and `PYTHONPATH` code cannot run
before private inputs are handled. The output path is opened exclusively and is never overwritten.
A failed or short write is left in place rather than unlinked by a racy pathname; retry with a fresh
path after inspecting or explicitly removing that file. Success and failure diagnostics are fixed
strings so private paths or parser details cannot be echoed into logs.
