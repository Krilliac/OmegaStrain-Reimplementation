# ADR 0007: Project-owned model, skeleton, skin, clip, and pose IR

- Status: accepted
- Date: 2026-07-22

## Context

The SKM, SKA, and SKAS dossiers (`analysis/formats/dossiers/SKM.md`, `SKA.md`, `SKAS.md`) each stop
at `passive_descriptor_only`/`structural_envelope_only`: every observed span's chunk table, header
word family, or line/blank-line/colon-line shape is proven and bounded, but no tracked evidence
assigns vertex, index, bone, joint, weight, timing, channel, or clip semantics to any payload byte.
`InspectSkmContainer` and `InspectSkaContainer` retain no payload bytes;
`DecodeSkasTextEnvelope` retains only its bounded opaque structural text. None assigns model or
animation meaning. Wave C of the campaign queue nonetheless calls for a
project-owned `ModelIR`, skeleton, skin-weight, clip, and pose contract "with hard limits;
populate only fields established by tracked evidence" plus a deterministic bind-pose/pose
evaluator, so that a later evidence-backed bridge from SKM/SKA/SKAS has a safe, tested target
shape to populate instead of inventing one under decode-time pressure.

## Decision

`omega::asset::ModelIR` (`native/include/omega/asset/model_ir.h`) owns a `RenderMeshIR` (unchanged
from ADR 0005) plus an optional `SkeletonIR` and an optional parallel `std::vector<SkinInfluenceIR>`
sized exactly to `mesh.positions`. `SkeletonIR` is a flat `std::vector<JointIR>`; each `JointIR`
carries a `name`, an optional `parent_index`, and a `local_bind_transform`. `parent_index`, when
present, must be strictly less than the joint's own index — a project authoring convention, not a
retail bone-order claim, chosen so acyclicity reduces to one per-index bounds check instead of a
graph traversal. `SkinInfluenceIR` is a fixed 4-slot `joint_indices`/`weights` pair (
`kMaximumSkinInfluencesPerVertex = 4`) with a `used_influences` count; no retail influence count,
weight encoding, normalization rule, zero-influence fallback, or inverse-bind policy is asserted.
Those skinning policies must be defined as project-owned renderer input before this table is
consumed; clean-room separation does not require copying an unknown retail policy. `PoseIR` is one
local (joint-to-parent) transform per joint in skeleton source order; `GlobalPoseIR` is the
corresponding accumulated joint-to-model result. Scene instance placement remains separate, so this
type does not claim joint-to-world transforms. `ClipIR`/`ClipKeyframeIR` exist only as a bounded
owned target shape for a
future independently-established clip grammar — this slice decodes no retail bytes into them and
implements no time-based sampling or blending. `kMaximumSkeletonJoints = 256` and
`kMaximumClipKeyframes = 4096` are project-owned authoring ceilings, not corpus observations.

`omega::asset::ValidateSkeletonIR`/`ValidateModelIR`/`ValidatePoseAgainstSkeleton`/
`ValidateClipAgainstSkeleton`
(`native/include/omega/asset/model_ir_validate.h`, `native/src/asset/model_ir_validate.cpp`) prove
internal self-consistency only: finite transforms, in-range and strictly-precedes joint references,
whole-triangle and in-range mesh indices, skin-influence joint indices in range with finite
non-negative weights, `skin_influences` requiring an owned skeleton and matching `mesh.positions` in
length, finite clip sample coordinates, clip/skeleton pose cardinality, and every owned component
charged against aggregate caller item, output-byte, and string-byte budgets under checked
(overflow-safe) arithmetic. Count/output preflights happen before full mesh or pose traversal.
Failures use `ModelIrError::item_index`, not a source-byte offset that model IR does not possess.
They assign no retail semantics; they only gate what is safe to evaluate.

`omega::runtime::EvaluateBindPose` and `omega::runtime::EvaluatePose`
(`native/include/omega/runtime/model_pose_evaluation.h`,
`native/src/runtime/model_pose_evaluation.cpp`) compose `global[i] = global[parent[i]] * local[i]`
(or `local[i]` alone for a root joint) using the same finite-checked row-major 4x4 product
convention as `omega::runtime::ComposeObjectToClip` (ADR 0005, `scene_transform.cpp`). Both
functions preflight all fixed/caller limits before allocating output and independently re-check
every parent index and transform for validity regardless of
whether the caller already ran `ValidateSkeletonIR`/`ValidatePoseAgainstSkeleton` first.
`EvaluateBindPose` sources its local transforms from `SkeletonIR::joints[i].local_bind_transform`;
`EvaluatePose` is the generic evaluator, taking an arbitrary caller-supplied `PoseIR` against the
same skeleton. Because no clip grammar is independently established yet, no clip-time-sampling
function exists — a future evaluator would only need to select the local `PoseIR` to hand to
`EvaluatePose`, not reimplement composition.

## Consequences

- A future evidence-backed SKM/SKA/SKAS bridge can target `ModelIR`/`SkeletonIR`/`ClipIR` directly
  without first inventing an ad hoc in-memory shape, and its output is validated and pose-evaluated
  by code already covered by generated-fixture tests.
- No SKM chunk payload, SKA counted-word region, or SKAS line/colon content is interpreted by this
  slice; `.skm`/`.ska`/`.skas` remain `passive_descriptor_only`/`structural_envelope_only` in
  `analysis/formats/DECODER-COVERAGE.md`, which this ADR does not amend (that document is
  suffix-decoder-scoped; `ModelIR` is a project-owned generic contract with no suffix mapping yet).
- `used_influences`/`weights`/`parent_index` ordering are project conventions a future decoder must
  either satisfy while populating `ModelIR` or translate into, not retail facts to preserve as-is.
- `DiscoverModelMembers` is a separate VFS-facing passive discovery adapter. It exposes only
  project-owned bounded summary variants and cannot populate `ModelIR`; its private retail decoder
  dependency does not cross the `omega_content` public interface.

## Non-goals

- Decoding any SKM chunk, SKA counted-word region, or SKAS line into `ModelIR`/`SkeletonIR`/
  `ClipIR` fields.
- Establishing a retail clip/keyframe grammar, sample-time unit, interpolation rule, or blend
  policy.
- GPU upload, skinning, or any renderer/gameplay integration of `ModelIR`.
