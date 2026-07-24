#include "omega/runtime/free_fly_camera.h"

#include "omega/runtime/scene_transform.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string_view>

namespace
{
int g_failures = 0;

void Check(const bool condition, const std::string_view message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        ++g_failures;
    }
}

// Apply a row-major, column-vector matrix to a homogeneous point.
[[nodiscard]] std::array<float, 4> Apply(
    const omega::asset::Matrix4x4IR& matrix, const float x, const float y,
    const float z)
{
    const std::array<float, 4> point{x, y, z, 1.0F};
    std::array<float, 4> result{};
    for (std::size_t row = 0U; row < 4U; ++row)
    {
        float value = 0.0F;
        for (std::size_t col = 0U; col < 4U; ++col)
            value += matrix.row_major[row * 4U + col] * point[col];
        result[row] = value;
    }
    return result;
}

[[nodiscard]] bool Near(const float a, const float b, const float tolerance = 1e-4F)
{
    return std::fabs(a - b) <= tolerance;
}

void TestForwardProjectsToClipCentre()
{
    // Camera at origin looking down +Z; a point straight ahead must land on the
    // clip centre (x=y=0), with a normalised depth strictly inside (0,1).
    const omega::runtime::FreeFlyPose pose{};
    const auto view = omega::runtime::FreeFlyViewMatrix(pose);
    const auto proj = omega::runtime::PerspectiveProjection(
        1.0472F /* 60deg */, 16.0F / 9.0F, 1.0F, 1000.0F);
    const omega::asset::SceneCameraIR camera{.world_to_view = view, .view_to_clip = proj};
    const auto object_to_clip =
        omega::runtime::ComposeObjectToClip(camera, omega::asset::kIdentityMatrix4x4IR);
    Check(object_to_clip.has_value(), "object_to_clip composes");
    if (!object_to_clip)
        return;

    const auto clip = Apply(*object_to_clip, 0.0F, 0.0F, 50.0F);
    Check(clip[3] > 0.0F, "point ahead is in front of the camera (w>0)");
    const float ndc_x = clip[0] / clip[3];
    const float ndc_y = clip[1] / clip[3];
    const float ndc_z = clip[2] / clip[3];
    Check(Near(ndc_x, 0.0F) && Near(ndc_y, 0.0F),
        "a point straight ahead projects to the clip centre");
    Check(ndc_z > 0.0F && ndc_z < 1.0F,
        "a point between near and far has depth strictly inside (0,1)");
}

void TestDepthRangeEndpoints()
{
    const auto proj = omega::runtime::PerspectiveProjection(1.0472F, 1.0F, 2.0F, 100.0F);
    const auto at_near = Apply(proj, 0.0F, 0.0F, 2.0F);
    const auto at_far = Apply(proj, 0.0F, 0.0F, 100.0F);
    Check(Near(at_near[2] / at_near[3], 0.0F), "near plane maps to depth 0");
    Check(Near(at_far[2] / at_far[3], 1.0F), "far plane maps to depth 1");
}

void TestOffAxisSign()
{
    // Looking down +Z, a point to the world +X should land on the +X side of the
    // clip (right), and +Y (world up) on the +Y side (up).
    const omega::runtime::FreeFlyPose pose{};
    const auto view = omega::runtime::FreeFlyViewMatrix(pose);
    const auto proj = omega::runtime::PerspectiveProjection(1.0472F, 1.0F, 1.0F, 1000.0F);
    const omega::asset::SceneCameraIR camera{.world_to_view = view, .view_to_clip = proj};
    const auto m =
        omega::runtime::ComposeObjectToClip(camera, omega::asset::kIdentityMatrix4x4IR);
    Check(m.has_value(), "off-axis compose");
    if (!m)
        return;
    const auto right_point = Apply(*m, 10.0F, 0.0F, 50.0F);
    const auto up_point = Apply(*m, 0.0F, 10.0F, 50.0F);
    Check(right_point[0] > 0.0F, "world +X projects to clip +X (right)");
    Check(up_point[1] > 0.0F, "world +Y projects to clip +Y (up)");
}

void TestAdvanceMovesAlongForward()
{
    omega::runtime::FreeFlyPose pose{};
    const auto moved =
        omega::runtime::AdvanceFreeFly(pose, {.forward = 1.0F}, 5.0F);
    // Forward at yaw=0 is +Z.
    Check(Near(moved.position.z, 5.0F) && Near(moved.position.x, 0.0F),
        "forward input moves along +Z at yaw 0");

    const auto strafed =
        omega::runtime::AdvanceFreeFly(pose, {.strafe = 1.0F}, 5.0F);
    Check(Near(strafed.position.x, 5.0F), "strafe-right moves along +X at yaw 0");

    const auto lifted =
        omega::runtime::AdvanceFreeFly(pose, {.vertical = 1.0F}, 3.0F);
    Check(Near(lifted.position.y, 3.0F), "vertical input moves along world up");
}

void TestPitchClamp()
{
    omega::runtime::FreeFlyPose pose{};
    const auto pitched =
        omega::runtime::AdvanceFreeFly(pose, {.pitch_delta = 10.0F}, 0.0F);
    Check(pitched.pitch < 1.5708F && pitched.pitch > 1.5F,
        "pitch clamps below +90 degrees");
}

void TestParse()
{
    const auto ok = omega::runtime::ParseFreeFlyPose("1.5,-2,3,0.5,-0.25");
    Check(ok.has_value(), "valid pose string parses");
    if (ok)
    {
        Check(Near(ok->position.x, 1.5F) && Near(ok->position.y, -2.0F) &&
                  Near(ok->position.z, 3.0F) && Near(ok->yaw, 0.5F) &&
                  Near(ok->pitch, -0.25F),
            "parsed pose fields match");
    }
    Check(!omega::runtime::ParseFreeFlyPose("1,2,3,4").has_value(),
        "too few fields rejected");
    Check(!omega::runtime::ParseFreeFlyPose("1,2,3,4,5,6").has_value(),
        "too many fields rejected");
    Check(!omega::runtime::ParseFreeFlyPose("1,2,x,4,5").has_value(),
        "non-numeric field rejected");
    Check(!omega::runtime::ParseFreeFlyPose("1,2,,4,5").has_value(),
        "empty field rejected");
}
} // namespace

int main()
{
    TestForwardProjectsToClipCentre();
    TestDepthRangeEndpoints();
    TestOffAxisSign();
    TestAdvanceMovesAlongForward();
    TestPitchClamp();
    TestParse();
    if (g_failures != 0)
    {
        std::cerr << g_failures << " free-fly camera test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "free-fly camera tests passed\n";
    return EXIT_SUCCESS;
}
