#include "omega/runtime/model_pose_evaluation.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
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

[[nodiscard]] SkinInfluenceIR Influence(const std::uint32_t joint, const float weight)
{
    SkinInfluenceIR influence;
    influence.joint_indices[0] = joint;
    influence.weights[0] = weight;
    influence.used_influences = 1U;
    return influence;
}

[[nodiscard]] SkinInfluenceIR InfluencePair(const std::uint32_t first_joint,
    const float first_weight, const std::uint32_t second_joint, const float second_weight)
{
    SkinInfluenceIR influence;
    influence.joint_indices[0] = first_joint;
    influence.weights[0] = first_weight;
    influence.joint_indices[1] = second_joint;
    influence.weights[1] = second_weight;
    influence.used_influences = 2U;
    return influence;
}

[[nodiscard]] std::vector<Matrix4x4IR> SkinningPalette()
{
    return {Translation(10.0F, 0.0F, 0.0F), Translation(0.0F, 20.0F, 0.0F)};
}

constexpr Float3IR kBindPosition{.x = 1.0F, .y = 2.0F, .z = 3.0F};
constexpr Float3IR kJoint0Position{.x = 11.0F, .y = 2.0F, .z = 3.0F};
constexpr Float3IR kJoint1Position{.x = 1.0F, .y = 22.0F, .z = 3.0F};
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

    // A posed global transform composed with its matching inverse bind yields identity.
    {
        const std::vector<Matrix4x4IR> global{Translation(4.0F, 0.0F, 0.0F)};
        const std::vector<Matrix4x4IR> inverse_bind{Translation(-4.0F, 0.0F, 0.0F)};
        std::vector<Matrix4x4IR> skinning(1U);
        const auto result = ComposeSkinningMatrices(global, inverse_bind, skinning);
        Check(result.has_value() && skinning[0] == kIdentityMatrix4x4IR,
            "matching global and inverse-bind transforms compose to identity");
    }

    // Non-commutative coverage freezes global * inverse-bind order.
    {
        const std::vector<Matrix4x4IR> global{Translation(1.0F, 0.0F, 0.0F)};
        const std::vector<Matrix4x4IR> inverse_bind{Scale(2.0F, 2.0F, 2.0F)};
        std::vector<Matrix4x4IR> skinning(1U);
        const auto result = ComposeSkinningMatrices(global, inverse_bind, skinning);
        Check(result.has_value() && skinning[0].row_major[0] == 2.0F &&
                  skinning[0].row_major[3] == 1.0F,
            "skinning matrices use global before inverse-bind order");
    }

    // Fixed staging makes partially overlapping caller spans deterministic.
    {
        std::array<Matrix4x4IR, 3U> storage{
            Translation(1.0F, 0.0F, 0.0F),
            Translation(2.0F, 0.0F, 0.0F),
            Translation(99.0F, 0.0F, 0.0F),
        };
        const std::array<Matrix4x4IR, 2U> inverse_bind{
            Translation(-1.0F, 0.0F, 0.0F),
            Translation(-1.0F, 0.0F, 0.0F),
        };
        const auto result = ComposeSkinningMatrices(
            std::span<const Matrix4x4IR>{storage.data(), 2U}, inverse_bind,
            std::span<Matrix4x4IR>{storage.data() + 1U, 2U});
        Check(result.has_value() && storage[1] == kIdentityMatrix4x4IR &&
                  storage[2] == Translation(1.0F, 0.0F, 0.0F),
            "overlapping input and output spans use the complete original input");
    }

    // Cardinality is checked before writes; any later numerical failure also preserves output.
    {
        const std::vector<Matrix4x4IR> two(2U, kIdentityMatrix4x4IR);
        std::vector<Matrix4x4IR> one(1U, Translation(9.0F, 9.0F, 9.0F));
        const Matrix4x4IR sentinel = one[0];
        const auto mismatch = ComposeSkinningMatrices(two, two, one);
        Check(!mismatch && mismatch.error() == SkinningError::CountMismatch &&
                  one[0] == sentinel,
            "a count mismatch leaves the output buffer unchanged");

        const std::vector<Matrix4x4IR> oversized(
            static_cast<std::size_t>(kMaximumSkeletonJoints) + 1U, kIdentityMatrix4x4IR);
        std::vector<Matrix4x4IR> oversized_output(oversized.size(), sentinel);
        const auto capacity =
            ComposeSkinningMatrices(oversized, oversized, oversized_output);
        Check(!capacity && capacity.error() == SkinningError::CapacityExceeded &&
                  oversized_output.front() == sentinel && oversized_output.back() == sentinel,
            "a palette above the canonical joint ceiling leaves output unchanged");

        Matrix4x4IR nonfinite = kIdentityMatrix4x4IR;
        nonfinite.row_major[0] = std::numeric_limits<float>::quiet_NaN();
        const std::vector<Matrix4x4IR> globals{kIdentityMatrix4x4IR, nonfinite};
        std::vector<Matrix4x4IR> output(2U, sentinel);
        const auto invalid = ComposeSkinningMatrices(globals, two, output);
        Check(!invalid && invalid.error() == SkinningError::NonFiniteInput &&
                  output[0] == sentinel && output[1] == sentinel,
            "a late non-finite input leaves every output matrix unchanged");

        const std::vector<Matrix4x4IR> extreme{
            Scale(std::numeric_limits<float>::max(), 1.0F, 1.0F)};
        const std::vector<Matrix4x4IR> doubled{Scale(2.0F, 1.0F, 1.0F)};
        std::vector<Matrix4x4IR> overflow_output(1U, sentinel);
        const auto overflow = ComposeSkinningMatrices(extreme, doubled, overflow_output);
        Check(!overflow && overflow.error() == SkinningError::NonFiniteResult &&
                  overflow_output[0] == sentinel,
            "an unrepresentable product leaves the output matrix unchanged");
    }

    {
        const std::vector<Matrix4x4IR> empty;
        std::vector<Matrix4x4IR> output;
        Check(ComposeSkinningMatrices(empty, empty, output).has_value(),
            "an empty skinning palette is a valid no-op");
    }

    // Basic affine blends and defensive normalization.
    {
        const auto palette = SkinningPalette();
        Check(SkinVertexPosition(kBindPosition, palette, Influence(0U, 1.0F)) ==
                  kJoint0Position,
            "one full-weight influence applies its joint transform");
        Check(SkinVertexPosition(kBindPosition, palette, Influence(1U, 1.0F)) ==
                  kJoint1Position,
            "the second joint selects the second palette transform");
        Check(SkinVertexPosition(kBindPosition, palette,
                  InfluencePair(0U, 0.5F, 1U, 0.5F)) == kHalfBlend,
            "two half weights blend midway between joint results");
        Check(SkinVertexPosition(kBindPosition, palette,
                  InfluencePair(0U, 2.0F, 1U, 2.0F)) == kHalfBlend,
            "unnormalized weights are normalized by their finite total");
    }

    // Zero totals and malformed canonical influences fail soft to the unskinned position.
    {
        const auto palette = SkinningPalette();
        Check(SkinVertexPosition(kBindPosition, palette,
                  InfluencePair(0U, 0.0F, 1U, 0.0F)) == kBindPosition,
            "zero weights do not collapse a vertex to the origin");
        Check(SkinVertexPosition(kBindPosition, palette,
                  InfluencePair(0U, 1.0F, 1U, -1.0F)) == kBindPosition,
            "a negative used weight fails soft to the unskinned position");
        Check(SkinVertexPosition(kBindPosition, palette, SkinInfluenceIR{}) == kBindPosition,
            "zero used influences fail soft to the unskinned position");
        Check(SkinVertexPosition(kBindPosition, palette,
                  InfluencePair(0U, 0.5F, 7U, 0.5F)) == kBindPosition,
            "one out-of-range joint invalidates the complete canonical influence");
        Check(SkinVertexPosition(kBindPosition, palette, Influence(7U, 1.0F)) ==
                  kBindPosition,
            "an entirely out-of-range influence set fails soft");
    }

    // The fixed canonical array bounds every read and rejects a malformed used count.
    {
        const auto palette = SkinningPalette();
        SkinInfluenceIR overrun;
        for (std::size_t slot = 0; slot < kMaximumSkinInfluencesPerVertex; ++slot)
        {
            overrun.joint_indices[slot] = 0U;
            overrun.weights[slot] = 0.25F;
        }
        overrun.used_influences = std::numeric_limits<std::uint8_t>::max();
        Check(SkinVertexPosition(kBindPosition, palette, overrun) == kBindPosition,
            "used influence count above fixed canonical storage fails soft");

        SkinInfluenceIR invalid_padding = Influence(0U, 1.0F);
        invalid_padding.joint_indices[1] = 1U;
        Check(SkinVertexPosition(kBindPosition, palette, invalid_padding) == kBindPosition,
            "a nonzero unused joint index invalidates canonical padding");

        invalid_padding = Influence(0U, 1.0F);
        invalid_padding.weights[1] = -0.0F;
        Check(SkinVertexPosition(kBindPosition, palette, invalid_padding) == kBindPosition,
            "negative zero in an unused weight invalidates canonical padding");

        const std::vector<Matrix4x4IR> oversized_palette(
            static_cast<std::size_t>(kMaximumSkeletonJoints) + 1U, kIdentityMatrix4x4IR);
        Check(SkinVertexPosition(kBindPosition, oversized_palette, Influence(0U, 1.0F)) ==
                  kBindPosition,
            "a palette above the project joint ceiling fails soft");
    }

    // Non-finite inputs are deterministic fail-soft errors, including an invalid joint slot.
    {
        auto palette = SkinningPalette();
        Check(SkinVertexPosition(kBindPosition, palette,
                  InfluencePair(0U, std::numeric_limits<float>::quiet_NaN(), 1U, 1.0F)) ==
                  kBindPosition,
            "a non-finite valid-slot weight fails soft");
        Check(SkinVertexPosition(kBindPosition, palette,
                  InfluencePair(0U, 1.0F, 7U,
                      std::numeric_limits<float>::quiet_NaN())) == kBindPosition,
            "a non-finite out-of-range-slot weight still fails soft");

        palette[0].row_major[3] = std::numeric_limits<float>::infinity();
        Check(SkinVertexPosition(kBindPosition, palette, Influence(0U, 1.0F)) ==
                  kBindPosition,
            "a non-finite referenced matrix fails soft");

        const Float3IR nonfinite_position{
            .x = std::numeric_limits<float>::quiet_NaN(), .y = 2.0F, .z = 3.0F};
        const Float3IR unchanged =
            SkinVertexPosition(nonfinite_position, SkinningPalette(), Influence(0U, 1.0F));
        Check(std::isnan(unchanged.x) && unchanged.y == 2.0F && unchanged.z == 3.0F,
            "a non-finite bind position is returned unchanged");
    }

    // Negative canonical weights and unrepresentable output both fail soft.
    {
        const auto palette = SkinningPalette();
        Check(SkinVertexPosition(kBindPosition, palette,
                  InfluencePair(0U, 2.0F, 1U, -1.0F)) == kBindPosition,
            "a negative canonical weight never drives extrapolation");

        const Float3IR overflow_position{.x = 2.0F, .y = 2.0F, .z = 3.0F};
        const std::vector<Matrix4x4IR> extreme{
            Scale(std::numeric_limits<float>::max(), 1.0F, 1.0F)};
        Check(SkinVertexPosition(overflow_position, extreme, Influence(0U, 1.0F)) ==
                  overflow_position,
            "an out-of-float-range blend fails soft to the unskinned position");
    }

    return failures;
}
