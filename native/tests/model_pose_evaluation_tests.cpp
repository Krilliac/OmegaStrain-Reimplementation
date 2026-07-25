#include "omega/runtime/model_pose_evaluation.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <span>
#include <string_view>
#include <vector>

namespace
{
using omega::asset::DecodeErrorCode;
using omega::asset::DecodeLimits;
using omega::asset::Float3IR;
using omega::asset::JointIR;
using omega::asset::kIdentityMatrix4x4IR;
using omega::asset::kMaximumSkeletonJoints;
using omega::asset::kMaximumSkinInfluencesPerVertex;
using omega::asset::Matrix4x4IR;
using omega::asset::PoseIR;
using omega::asset::SkeletonIR;
using omega::asset::SkinInfluenceIR;
using omega::runtime::ComposeSkinningMatrices;
using omega::runtime::EvaluateBindPose;
using omega::runtime::EvaluatePose;
using omega::runtime::SkinningError;
using omega::runtime::SkinVertexPosition;

int failures = 0;

void Check(const bool condition, const std::string_view message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

template <typename Result>
void CheckError(
    const Result& result, const DecodeErrorCode code, const std::string_view message)
{
    Check(!result && result.error().code == code, message);
}

[[nodiscard]] Matrix4x4IR Translation(const float x, const float y, const float z)
{
    Matrix4x4IR matrix = kIdentityMatrix4x4IR;
    matrix.row_major[3] = x;
    matrix.row_major[7] = y;
    matrix.row_major[11] = z;
    return matrix;
}

[[nodiscard]] Matrix4x4IR Scale(const float x, const float y, const float z)
{
    Matrix4x4IR matrix = kIdentityMatrix4x4IR;
    matrix.row_major[0] = x;
    matrix.row_major[5] = y;
    matrix.row_major[10] = z;
    return matrix;
}

[[nodiscard]] bool IsTranslation(
    const Matrix4x4IR& matrix, const float x, const float y, const float z)
{
    return matrix == Translation(x, y, z);
}

// root -> child -> grandchild chain: local translations (1,0,0), (0,2,0), (0,0,3).
[[nodiscard]] SkeletonIR MakeTranslationChain()
{
    SkeletonIR skeleton;
    JointIR root;
    root.local_bind_transform = Translation(1.0F, 0.0F, 0.0F);
    skeleton.joints.push_back(root);

    JointIR child;
    child.parent_index = 0;
    child.local_bind_transform = Translation(0.0F, 2.0F, 0.0F);
    skeleton.joints.push_back(child);

    JointIR grandchild;
    grandchild.parent_index = 1;
    grandchild.local_bind_transform = Translation(0.0F, 0.0F, 3.0F);
    skeleton.joints.push_back(grandchild);

    return skeleton;
}

[[nodiscard]] SkinInfluenceIR MakeInfluence(const std::uint32_t joint_index, const float weight)
{
    SkinInfluenceIR influence;
    influence.joint_indices[0] = joint_index;
    influence.weights[0] = weight;
    influence.used_influences = static_cast<std::uint8_t>(1);
    return influence;
}

[[nodiscard]] SkinInfluenceIR MakeInfluencePair(
    const std::uint32_t first_joint, const float first_weight,
    const std::uint32_t second_joint, const float second_weight)
{
    SkinInfluenceIR influence;
    influence.joint_indices[0] = first_joint;
    influence.weights[0] = first_weight;
    influence.joint_indices[1] = second_joint;
    influence.weights[1] = second_weight;
    influence.used_influences = static_cast<std::uint8_t>(2);
    return influence;
}

// Two-joint skinning palette used by the linear-blend tests: joint 0 translates by +10 in x,
// joint 1 translates by +20 in y. Both are exactly representable, so the expected blends below can
// be compared for exact equality.
[[nodiscard]] std::vector<Matrix4x4IR> MakeSkinningPalette()
{
    return {Translation(10.0F, 0.0F, 0.0F), Translation(0.0F, 20.0F, 0.0F)};
}

constexpr Float3IR kBindVertex{.x = 1.0F, .y = 2.0F, .z = 3.0F};
constexpr Float3IR kJoint0Only{.x = 11.0F, .y = 2.0F, .z = 3.0F};
constexpr Float3IR kJoint1Only{.x = 1.0F, .y = 22.0F, .z = 3.0F};
constexpr Float3IR kHalfBlend{.x = 6.0F, .y = 12.0F, .z = 3.0F};
} // namespace

int ModelPoseEvaluationFailureCount()
{
    // Empty skeleton composes to an empty world pose.
    {
        const auto empty = EvaluateBindPose(SkeletonIR{});
        Check(empty.has_value() && empty->joint_global_transforms.empty(),
            "bind pose of an empty skeleton is an empty world pose");
    }

    // A single root joint's world transform equals its own local bind transform.
    {
        SkeletonIR single;
        single.joints.push_back(JointIR{.local_bind_transform = Translation(5.0F, 0.0F, 0.0F)});
        const auto pose = EvaluateBindPose(single);
        Check(pose.has_value() && pose->joint_global_transforms.size() == 1 &&
                  IsTranslation(pose->joint_global_transforms[0], 5.0F, 0.0F, 0.0F),
            "a root joint's bind-pose world transform equals its own local transform");
    }

    // A three-joint translation chain composes additively along the parent chain.
    {
        const auto chain = MakeTranslationChain();
        const auto pose = EvaluateBindPose(chain);
        Check(pose.has_value() && pose->joint_global_transforms.size() == 3, "chain bind pose has 3 joints");
        if (pose)
        {
            Check(IsTranslation(pose->joint_global_transforms[0], 1.0F, 0.0F, 0.0F),
                "chain root world transform is its own local translation");
            Check(IsTranslation(pose->joint_global_transforms[1], 1.0F, 2.0F, 0.0F),
                "chain child world transform accumulates parent and local translation");
            Check(IsTranslation(pose->joint_global_transforms[2], 1.0F, 2.0F, 3.0F),
                "chain grandchild world transform accumulates the full parent chain");
        }

        // Determinism: repeated evaluation of the same input yields an equal result.
        const auto repeat = EvaluateBindPose(chain);
        Check(pose.has_value() && repeat.has_value() && *pose == *repeat,
            "bind pose evaluation is deterministic across repeated calls");
    }

    // EvaluatePose composes a caller-supplied local pose instead of the skeleton's own bind
    // transforms, distinguishing the generic evaluator from EvaluateBindPose.
    {
        const auto chain = MakeTranslationChain();
        PoseIR generic_pose;
        generic_pose.joint_local_transforms = {
            Translation(0.0F, 0.0F, 0.0F),
            Translation(10.0F, 0.0F, 0.0F),
            Translation(0.0F, 0.0F, 0.0F),
        };
        const auto world = EvaluatePose(chain, generic_pose);
        Check(world.has_value() && world->joint_global_transforms.size() == 3 &&
                  IsTranslation(world->joint_global_transforms[1], 10.0F, 0.0F, 0.0F),
            "the generic evaluator composes the supplied local pose, not the skeleton's bind pose");
    }

    // Non-commutative coverage freezes parent-global * local order. A parent scale must scale the
    // child's local translation; local * parent would incorrectly leave it at (5,7,0).
    {
        SkeletonIR skeleton;
        skeleton.joints.push_back(JointIR{.local_bind_transform = Scale(2.0F, 3.0F, 1.0F)});
        skeleton.joints.push_back(
            JointIR{.parent_index = 0, .local_bind_transform = Translation(5.0F, 7.0F, 0.0F)});
        const auto pose = EvaluateBindPose(skeleton);
        Check(pose.has_value() && pose->joint_global_transforms.size() == 2 &&
                  pose->joint_global_transforms[1].row_major[0] == 2.0F &&
                  pose->joint_global_transforms[1].row_major[5] == 3.0F &&
                  pose->joint_global_transforms[1].row_major[3] == 10.0F &&
                  pose->joint_global_transforms[1].row_major[7] == 21.0F,
            "pose composition applies parent-global before non-commutative local transform");
    }

    // Joint-count mismatch is rejected.
    {
        const auto chain = MakeTranslationChain();
        PoseIR undersized;
        undersized.joint_local_transforms = {kIdentityMatrix4x4IR, kIdentityMatrix4x4IR};
        CheckError(EvaluatePose(chain, undersized), DecodeErrorCode::Malformed,
            "pose evaluation rejects a joint-transform count mismatch");
    }

    // A non-finite local transform is rejected even without prior validation.
    {
        SkeletonIR single;
        single.joints.push_back(JointIR{});
        PoseIR non_finite;
        Matrix4x4IR nan_matrix = kIdentityMatrix4x4IR;
        nan_matrix.row_major[0] = std::numeric_limits<float>::quiet_NaN();
        non_finite.joint_local_transforms = {nan_matrix};
        CheckError(EvaluatePose(single, non_finite), DecodeErrorCode::Malformed,
            "pose evaluation rejects a non-finite local transform");
    }

    // A parent_index that does not strictly precede its own index is rejected even without
    // prior structural validation (defense in depth, not reliance on ValidateSkeletonIR).
    {
        SkeletonIR self_ref;
        self_ref.joints.push_back(JointIR{.parent_index = 0});
        PoseIR pose;
        pose.joint_local_transforms = {kIdentityMatrix4x4IR};
        CheckError(EvaluatePose(self_ref, pose), DecodeErrorCode::Malformed,
            "pose evaluation rejects a self-referencing parent index");
    }

    // Joint counts above the fixed ceiling are rejected.
    {
        SkeletonIR oversized;
        oversized.joints.assign(kMaximumSkeletonJoints + 1U, JointIR{});
        CheckError(EvaluateBindPose(oversized), DecodeErrorCode::LimitExceeded,
            "bind pose evaluation rejects joint counts above the fixed ceiling");

        PoseIR mismatched;
        CheckError(EvaluatePose(oversized, mismatched), DecodeErrorCode::LimitExceeded,
            "generic pose evaluation prioritizes the fixed joint ceiling over cardinality");
    }

    // Caller item-budget boundaries.
    {
        const auto chain = MakeTranslationChain();
        DecodeLimits limits;
        limits.maximum_items = 1 + chain.joints.size();
        Check(EvaluateBindPose(chain, limits).has_value(),
            "bind pose succeeds at the exact root-plus-joints item budget");
        limits.maximum_items = chain.joints.size();
        CheckError(EvaluateBindPose(chain, limits), DecodeErrorCode::LimitExceeded,
            "bind pose rejects one item below the root-plus-joints item budget");
    }

    // Caller output-byte-budget boundaries.
    {
        const auto chain = MakeTranslationChain();
        DecodeLimits limits;
        limits.maximum_output_bytes =
            sizeof(omega::asset::GlobalPoseIR) + chain.joints.size() * sizeof(Matrix4x4IR);
        Check(EvaluateBindPose(chain, limits).has_value(),
            "bind pose succeeds at the exact output-byte budget");
        --limits.maximum_output_bytes;
        CheckError(EvaluateBindPose(chain, limits), DecodeErrorCode::LimitExceeded,
            "bind pose rejects one byte below the output-byte budget");
    }

    // ---- ComposeSkinningMatrices -------------------------------------------------------------

    // The skinning matrix is global * inverse_bind: an inverse bind that exactly undoes the posed
    // global transform yields identity, which is the whole point of the product.
    {
        const std::vector<Matrix4x4IR> global = {Translation(4.0F, 0.0F, 0.0F)};
        const std::vector<Matrix4x4IR> inverse_bind = {Translation(-4.0F, 0.0F, 0.0F)};
        std::vector<Matrix4x4IR> skinning(1);
        const auto composed = ComposeSkinningMatrices(global, inverse_bind, skinning);
        Check(composed.has_value() && skinning[0] == kIdentityMatrix4x4IR,
            "a global transform times its exact inverse bind composes to identity");
    }

    // Non-commutative coverage freezes global * inverse_bind order. inverse_bind * global would
    // leave the translation at 2 instead of 1.
    {
        const std::vector<Matrix4x4IR> global = {Translation(1.0F, 0.0F, 0.0F)};
        const std::vector<Matrix4x4IR> inverse_bind = {Scale(2.0F, 2.0F, 2.0F)};
        std::vector<Matrix4x4IR> skinning(1);
        const auto composed = ComposeSkinningMatrices(global, inverse_bind, skinning);
        Check(composed.has_value() && skinning[0].row_major[0] == 2.0F &&
                  skinning[0].row_major[3] == 1.0F,
            "skinning matrices compose the posed global transform before the inverse bind");
    }

    // Empty input is a well-formed no-op rather than an error.
    {
        const std::vector<Matrix4x4IR> empty;
        std::vector<Matrix4x4IR> skinning;
        Check(ComposeSkinningMatrices(empty, empty, skinning).has_value(),
            "composing an empty skinning palette succeeds");
    }

    // Every span length must agree; a short output buffer is never partially filled past its end.
    {
        const std::vector<Matrix4x4IR> global(2, kIdentityMatrix4x4IR);
        const std::vector<Matrix4x4IR> short_inverse_bind(1, kIdentityMatrix4x4IR);
        std::vector<Matrix4x4IR> skinning(2);
        const auto mismatched_input =
            ComposeSkinningMatrices(global, short_inverse_bind, skinning);
        Check(!mismatched_input && mismatched_input.error() == SkinningError::CountMismatch,
            "skinning composition rejects an inverse-bind count mismatch");

        std::vector<Matrix4x4IR> short_output(1);
        const auto mismatched_output = ComposeSkinningMatrices(global, global, short_output);
        Check(!mismatched_output && mismatched_output.error() == SkinningError::CountMismatch,
            "skinning composition rejects an undersized output buffer");
    }

    // A non-finite input matrix is reported rather than propagated into the palette.
    {
        Matrix4x4IR nan_matrix = kIdentityMatrix4x4IR;
        nan_matrix.row_major[0] = std::numeric_limits<float>::quiet_NaN();
        const std::vector<Matrix4x4IR> global = {nan_matrix};
        const std::vector<Matrix4x4IR> inverse_bind = {kIdentityMatrix4x4IR};
        std::vector<Matrix4x4IR> skinning(1);
        const auto composed = ComposeSkinningMatrices(global, inverse_bind, skinning);
        Check(!composed && composed.error() == SkinningError::NonFiniteInput,
            "skinning composition rejects a non-finite input transform");
    }

    // ---- SkinVertexPosition ------------------------------------------------------------------

    // Identity pose leaves a vertex unchanged: an evaluated bind pose skinned against its own
    // inverse bind pose is the identity palette, so the vertex must come back bit-identical.
    {
        const auto chain = MakeTranslationChain();
        const auto pose = EvaluateBindPose(chain);
        Check(pose.has_value(), "the translation chain's bind pose evaluates");
        if (pose)
        {
            const std::vector<Matrix4x4IR> inverse_bind = {
                Translation(-1.0F, 0.0F, 0.0F),
                Translation(-1.0F, -2.0F, 0.0F),
                Translation(-1.0F, -2.0F, -3.0F),
            };
            std::vector<Matrix4x4IR> skinning(inverse_bind.size());
            const auto composed =
                ComposeSkinningMatrices(pose->joint_global_transforms, inverse_bind, skinning);
            Check(composed.has_value(),
                "the bind pose composes against its own inverse bind pose");
            if (composed)
            {
                Check(skinning[2] == kIdentityMatrix4x4IR,
                    "a joint posed at its bind pose has an identity skinning matrix");
                Check(
                    SkinVertexPosition(kBindVertex, skinning, MakeInfluence(2U, 1.0F)) ==
                        kBindVertex,
                    "an identity skinning palette leaves the vertex unchanged");
            }
        }
    }

    // A single full-weight influence equals that joint's transform applied on its own.
    {
        const auto palette = MakeSkinningPalette();
        Check(SkinVertexPosition(kBindVertex, palette, MakeInfluence(0U, 1.0F)) == kJoint0Only,
            "a single full-weight influence equals that joint's own transform");
        Check(SkinVertexPosition(kBindVertex, palette, MakeInfluence(1U, 1.0F)) == kJoint1Only,
            "a single full-weight influence on the second joint uses the second joint's transform");
    }

    // Two half weights land exactly midway between the two single-joint results.
    {
        const auto palette = MakeSkinningPalette();
        Check(SkinVertexPosition(kBindVertex, palette, MakeInfluencePair(0U, 0.5F, 1U, 0.5F)) ==
                  kHalfBlend,
            "two half-weight influences blend midway between the two single-joint results");
    }

    // Unnormalised weights are normalised defensively: only the ratio matters, so (2, 2) must
    // reproduce (0.5, 0.5) rather than translating the vertex four times as far.
    {
        const auto palette = MakeSkinningPalette();
        Check(SkinVertexPosition(kBindVertex, palette, MakeInfluencePair(0U, 2.0F, 1U, 2.0F)) ==
                  kHalfBlend,
            "unnormalised equal weights normalise to the same blend as (0.5, 0.5)");
        Check(SkinVertexPosition(kBindVertex, palette, MakeInfluence(0U, 4.0F)) == kJoint0Only,
            "a single oversized weight normalises to the plain single-joint transform");
        Check(SkinVertexPosition(kBindVertex, palette, MakeInfluence(0U, 0.25F)) == kJoint0Only,
            "a single undersized weight normalises to the plain single-joint transform");
    }

    // Zero total weight fails soft to the UNSKINNED position -- never a collapsed vertex at the
    // model origin, which is the classic catastrophic skinning bug.
    {
        const auto palette = MakeSkinningPalette();
        Check(SkinVertexPosition(kBindVertex, palette, MakeInfluencePair(0U, 0.0F, 1U, 0.0F)) ==
                  kBindVertex,
            "an all-zero weight set returns the unskinned position, not the origin");
        Check(SkinVertexPosition(kBindVertex, palette, MakeInfluencePair(0U, 1.0F, 1U, -1.0F)) ==
                  kBindVertex,
            "weights that cancel to zero return the unskinned position, not the origin");
        Check(SkinVertexPosition(kBindVertex, palette, SkinInfluenceIR{}) == kBindVertex,
            "a vertex with zero used influences returns the unskinned position");
    }

    // An out-of-range joint index is skipped fail-soft and is never read; the surviving influence
    // renormalises to the full weight.
    {
        const auto palette = MakeSkinningPalette();
        Check(SkinVertexPosition(kBindVertex, palette, MakeInfluencePair(0U, 0.5F, 7U, 0.5F)) ==
                  kJoint0Only,
            "an out-of-range joint index is skipped and the remaining influence renormalises");
        Check(SkinVertexPosition(kBindVertex, palette, MakeInfluence(7U, 1.0F)) == kBindVertex,
            "a vertex whose only influence is out of range returns the unskinned position");
        const std::vector<Matrix4x4IR> empty_palette;
        Check(SkinVertexPosition(kBindVertex, empty_palette, MakeInfluence(0U, 1.0F)) ==
                  kBindVertex,
            "an empty skinning palette makes every index out of range and fails soft");
    }

    // used_influences above the fixed per-vertex ceiling is clamped, so no slot past the fixed
    // storage is ever read.
    {
        const auto palette = MakeSkinningPalette();
        SkinInfluenceIR overrun;
        for (std::size_t slot = 0; slot < kMaximumSkinInfluencesPerVertex; ++slot)
        {
            overrun.joint_indices[slot] = 0U;
            overrun.weights[slot] = 0.25F;
        }
        overrun.used_influences = std::numeric_limits<std::uint8_t>::max();
        Check(SkinVertexPosition(kBindVertex, palette, overrun) == kJoint0Only,
            "used_influences above the fixed ceiling is clamped to the fixed influence storage");
    }

    // Non-finite inputs fail soft to the unskinned position.
    {
        auto palette = MakeSkinningPalette();
        Check(SkinVertexPosition(kBindVertex, palette,
                  MakeInfluencePair(0U, std::numeric_limits<float>::quiet_NaN(), 1U, 1.0F)) ==
                  kBindVertex,
            "a NaN influence weight fails soft to the unskinned position");
        Check(SkinVertexPosition(kBindVertex, palette,
                  MakeInfluence(0U, std::numeric_limits<float>::infinity())) == kBindVertex,
            "an infinite influence weight fails soft to the unskinned position");

        palette[0].row_major[3] = std::numeric_limits<float>::quiet_NaN();
        Check(SkinVertexPosition(kBindVertex, palette, MakeInfluence(0U, 1.0F)) == kBindVertex,
            "a non-finite skinning matrix fails soft to the unskinned position");

        const Float3IR nan_vertex{
            .x = std::numeric_limits<float>::quiet_NaN(), .y = 2.0F, .z = 3.0F};
        const auto finite_palette = MakeSkinningPalette();
        const Float3IR skinned =
            SkinVertexPosition(nan_vertex, finite_palette, MakeInfluence(0U, 1.0F));
        Check(std::isnan(skinned.x) && skinned.y == 2.0F && skinned.z == 3.0F,
            "a non-finite vertex position is returned unskinned rather than zeroed");
    }

    // Negative weights extrapolate rather than being rejected, which is the documented reading of
    // the same affine combination. Extrapolating past the representable float range then fails
    // soft instead of narrowing an out-of-range accumulator into an infinity.
    {
        const auto palette = MakeSkinningPalette();
        Check(SkinVertexPosition(kBindVertex, palette, MakeInfluencePair(0U, 2.0F, 1U, -1.0F)) ==
                  Float3IR{.x = 21.0F, .y = -18.0F, .z = 3.0F},
            "a negative weight extrapolates through the same normalised affine combination");

        const std::vector<Matrix4x4IR> extreme = {
            Translation(std::numeric_limits<float>::max(), 0.0F, 0.0F),
            Translation(-std::numeric_limits<float>::max(), 0.0F, 0.0F)};
        Check(SkinVertexPosition(kBindVertex, extreme, MakeInfluencePair(0U, 2.0F, 1U, -1.0F)) ==
                  kBindVertex,
            "a blend outside the representable float range fails soft to the unskinned position");
    }

    return failures;
}
