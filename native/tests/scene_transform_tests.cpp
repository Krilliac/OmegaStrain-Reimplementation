#include "omega/runtime/scene_transform.h"

#include <cstdlib>
#include <expected>
#include <iostream>
#include <limits>
#include <string_view>
#include <type_traits>

namespace
{
int failures = 0;

void Check(const bool condition, const std::string_view message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}
} // namespace

int main()
{
    using omega::asset::Matrix4x4IR;
    using omega::asset::SceneCameraIR;
    using omega::runtime::ComposeObjectToClip;
    using omega::runtime::SceneTransformError;
    using ComposeResult = std::expected<Matrix4x4IR, SceneTransformError>;

    static_assert(std::is_same_v<decltype(ComposeObjectToClip(
                                     SceneCameraIR{}, Matrix4x4IR{})),
        ComposeResult>);
    static_assert(noexcept(ComposeObjectToClip(SceneCameraIR{}, Matrix4x4IR{})));

    const SceneCameraIR identity_camera;
    const ComposeResult identity = ComposeObjectToClip(
        identity_camera, omega::asset::kIdentityMatrix4x4IR);
    Check(identity && *identity == omega::asset::kIdentityMatrix4x4IR,
        "identity camera stages and identity local transform compose to identity");

    constexpr Matrix4x4IR world_to_view{
        .row_major = {
            2.0F, 0.0F, 0.0F, 0.0F,
            0.0F, 3.0F, 0.0F, 0.0F,
            0.0F, 0.0F, 4.0F, 0.0F,
            0.0F, 0.0F, 0.0F, 1.0F,
        },
    };
    constexpr Matrix4x4IR view_to_clip{
        .row_major = {
            1.0F, 1.0F, 0.0F, 0.0F,
            0.0F, 1.0F, 0.0F, 0.0F,
            0.0F, 0.0F, 1.0F, 0.0F,
            0.5F, 0.0F, 0.0F, 1.0F,
        },
    };
    constexpr Matrix4x4IR local_to_world{
        .row_major = {
            1.0F, 0.0F, 0.0F, 4.0F,
            0.0F, 1.0F, 0.0F, 5.0F,
            0.0F, 0.0F, 1.0F, 6.0F,
            0.0F, 0.0F, 0.0F, 1.0F,
        },
    };
    constexpr Matrix4x4IR expected_object_to_clip{
        .row_major = {
            2.0F, 3.0F, 0.0F, 23.0F,
            0.0F, 3.0F, 0.0F, 15.0F,
            0.0F, 0.0F, 4.0F, 24.0F,
            1.0F, 0.0F, 0.0F, 5.0F,
        },
    };
    const SceneCameraIR staged_camera{
        .world_to_view = world_to_view,
        .view_to_clip = view_to_clip,
    };
    const SceneCameraIR camera_before = staged_camera;
    const Matrix4x4IR local_before = local_to_world;
    const ComposeResult composed = ComposeObjectToClip(staged_camera, local_to_world);
    Check(composed && *composed == expected_object_to_clip,
        "composition applies local-to-world, world-to-view, then view-to-clip");
    Check(staged_camera == camera_before && local_to_world == local_before,
        "composition leaves all input matrices unchanged");

    SceneCameraIR nonfinite_world_to_view;
    nonfinite_world_to_view.world_to_view.row_major[0] =
        std::numeric_limits<float>::infinity();
    const ComposeResult rejected_world_to_view = ComposeObjectToClip(
        nonfinite_world_to_view, omega::asset::kIdentityMatrix4x4IR);
    Check(!rejected_world_to_view &&
              rejected_world_to_view.error() == SceneTransformError::NonFiniteInput,
        "a non-finite world-to-view input reports NonFiniteInput");

    SceneCameraIR nonfinite_view_to_clip;
    nonfinite_view_to_clip.view_to_clip.row_major[7] =
        std::numeric_limits<float>::quiet_NaN();
    const ComposeResult rejected_view_to_clip = ComposeObjectToClip(
        nonfinite_view_to_clip, omega::asset::kIdentityMatrix4x4IR);
    Check(!rejected_view_to_clip &&
              rejected_view_to_clip.error() == SceneTransformError::NonFiniteInput,
        "a non-finite view-to-clip input reports NonFiniteInput");

    Matrix4x4IR nonfinite_local = omega::asset::kIdentityMatrix4x4IR;
    nonfinite_local.row_major[15] = -std::numeric_limits<float>::infinity();
    const ComposeResult rejected_local =
        ComposeObjectToClip(identity_camera, nonfinite_local);
    Check(!rejected_local &&
              rejected_local.error() == SceneTransformError::NonFiniteInput,
        "a non-finite local-to-world input reports NonFiniteInput");

    SceneCameraIR intermediate_overflow_camera;
    intermediate_overflow_camera.world_to_view.row_major[0] = 2.0F;
    Matrix4x4IR intermediate_overflow_local = omega::asset::kIdentityMatrix4x4IR;
    intermediate_overflow_local.row_major[0] = std::numeric_limits<float>::max();
    const ComposeResult rejected_intermediate = ComposeObjectToClip(
        intermediate_overflow_camera, intermediate_overflow_local);
    Check(!rejected_intermediate &&
              rejected_intermediate.error() == SceneTransformError::NonFiniteResult,
        "a non-finite object-to-view intermediate reports NonFiniteResult");

    SceneCameraIR final_overflow_camera;
    final_overflow_camera.view_to_clip.row_major[0] = 2.0F;
    Matrix4x4IR final_overflow_local = omega::asset::kIdentityMatrix4x4IR;
    final_overflow_local.row_major[0] = std::numeric_limits<float>::max();
    const ComposeResult rejected_final =
        ComposeObjectToClip(final_overflow_camera, final_overflow_local);
    Check(!rejected_final &&
              rejected_final.error() == SceneTransformError::NonFiniteResult,
        "a non-finite object-to-clip result reports NonFiniteResult");

    if (failures != 0)
    {
        std::cerr << failures << " scene transform test(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "scene transform tests passed\n";
    return EXIT_SUCCESS;
}
