# COL spatial mesh contract

## Scope and evidence state

This document records only aggregate format contracts independently reproduced across the owned
NTSC-U corpus. It contains no retail payload bytes, file paths, code, or executable instructions.
The native adapter treats COL as passive spatial geometry. Collision response, triangle winding,
coordinate conversion, material meaning, and opaque primitive fields remain unassigned.

State: **confirmed** for all 7,036 observed COL spans (7,033 version 5 and three version 3).

## Counted layout

All integers and floats are little-endian. Offsets are relative to the start of the COL span.

| Region | Header count | Header end | Record bytes | Confirmed native fields |
| --- | ---: | ---: | ---: | --- |
| Node table, starting at 48 | `0x04` | `0x10` | 64 | two homogeneous bounds vectors, eight child offsets |
| Leaf table | `0x0C` | `0x18` | 48 | two homogeneous bounds vectors, reference count/offset, two zero words |
| Triangle table | `0x14` | `0x20` | 16 | three unsigned 16-bit vertex indices at `+0x04/+0x06/+0x08` |
| Vertex table | `0x1C` | `0x24` | 16 | finite `x/y/z` and homogeneous `w=1` |

The header is 48 bytes (`u32@0x08 == 48`). Each counted endpoint exactly equals the prior endpoint
plus count times record size. The endpoint at `0x2C` is
`align16(vertex_end + 4 * triangle_count)`. The intervening words are leaf-addressed unsigned
32-bit triangle references and final zero alignment padding. The word at `0x28` and all bytes after
the `0x2C` endpoint remain opaque and are omitted from canonical output.

## Spatial and geometry invariants

- A nonzero child word identifies the exact start of a node or leaf record. Zero is an empty slot.
- Root is node zero when nodes exist; otherwise it is leaf zero. Every non-root record has exactly
  one parent, every record is reachable, and no graph contains a cycle. Maximum observed root edge
  depth is eight.
- Normal bounds are finite and ordered. Child bounds are contained by their parent. Each leaf bound
  exactly equals the extrema of the vertices used by its referenced triangles; no tolerance was
  needed.
- Leaf reference ranges cover an exact permutation of triangle indices. Each triangle belongs to
  exactly one leaf.
- The first three triangle indices are distinct, have no high bit, and address existing vertices.
  Primitive bytes at `+0x00`, `+0x0A`, and `+0x0C` are not indices and remain opaque.
- 2,716 spans contain the exact sole-node empty sentinel: one node, no leaves/triangles/vertices,
  inverse maximum-float bounds, and no children. The adapter normalizes it to an empty mesh rather
  than publishing sentinel floats. Another 2,678 spans use a direct leaf root.

## Canonical adapter

`DecodeColSpatialMesh` returns owned `SpatialMeshIR` vectors for vertices, triangles, a packed leaf
triangle-reference permutation, nodes, and leaves. File offsets and opaque retail fields are
converted or discarded at the adapter boundary. The renderer and simulation consume only the
canonical asset header; neither needs a retail-format include or any MIPS/VU/VIF representation.

The decoder preflights caller-provided input, item, logical-output, and scratch limits before
dynamic allocation, then enforces the edge-depth limit during iterative traversal. It validates
counted arithmetic, numerical fields, reference ranges, topology, containment, triangle ownership,
and exact leaf extents.

`GameDataService::LoadLevelSpatial` resolves the manifest's normalized DATA.HOG source, any nested
container chain, and each cell HOG before requiring exactly one COL member. It returns one owned
mesh per manifest cell and debits all container/cell/COL work against a shared operation budget.
Archive/source depth and spatial edge depth compose; the default maximum of nine admits the observed
eight-edge COL maximum beneath one cell-HOG edge.

## Aggregate native validation

`build/msvc/Debug/omega_tool.exe asset-metadata-verify-tree private/extracted-disc` independently
decodes all 7,036 spans with zero semantic errors and reports only aggregate values:

| Measure | Total |
| --- | ---: |
| Source nodes | 23,913 |
| Canonical nodes after empty normalization | 21,197 |
| Leaves | 99,193 |
| Triangles | 1,327,714 |
| Vertices | 949,762 |
| Leaf triangle references | 1,327,714 |
| Empty meshes | 2,716 |
| Direct leaf roots | 2,678 |
| Maximum edge depth | 8 |

Synthetic regressions cover both supported versions, direct and node roots, empty normalization,
input ownership, opaque-field immunity, malformed numerics/references/topology, and exact/one-below
resource budgets.

`build/msvc/Debug/omega_tool.exe level-spatial-verify-tree private/extracted-disc` additionally
drives the complete content-service path for all 18 levels and 5,351 manifest cells. It reports
5,351 meshes, 20,203 canonical nodes, 93,356 leaves, 889,640 vertices, 1,239,980 triangles and leaf
references, 2,137 normalized empty meshes, and zero errors. `tools/probe_native_levels.py` verifies
the same 18/18 level and 5,351/5,351 manifest/spatial cardinality through native startup.
