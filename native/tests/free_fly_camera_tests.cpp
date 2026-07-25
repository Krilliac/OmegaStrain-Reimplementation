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

void TestParseScript()
{
    const auto empty = omega::runtime::ParseFreeFlyScript("", 0.03F);
    Check(empty.forward == 0.0F && empty.strafe == 0.0F &&
              empty.vertical == 0.0F && empty.yaw_delta == 0.0F &&
              empty.pitch_delta == 0.0F,
        "empty script yields zero input");

    const auto fwd = omega::runtime::ParseFreeFlyScript("forward", 0.03F);
    Check(Near(fwd.forward, 1.0F), "forward token sets forward +1");

    const auto turn = omega::runtime::ParseFreeFlyScript("forward right", 0.05F);
    Check(Near(turn.forward, 1.0F) && Near(turn.yaw_delta, 0.05F),
        "forward+right drives and turns (yaw by look_step)");

    const auto combo = omega::runtime::ParseFreeFlyScript(
        "back,straferight,up,pitchup", 0.02F);
    Check(Near(combo.forward, -1.0F) && Near(combo.strafe, 1.0F) &&
              Near(combo.vertical, 1.0F) && Near(combo.pitch_delta, 0.02F),
        "comma-separated tokens accumulate across fields");

    const auto opposed = omega::runtime::ParseFreeFlyScript(
        "left right nonsense", 0.1F);
    Check(Near(opposed.yaw_delta, 0.0F) && opposed.forward == 0.0F,
        "opposed turns cancel and unknown tokens are ignored");
}
void TestLookAtViewMatrix()
{
    // Camera 10 units back along -Y, looking at the origin, world up = +Z.
    const auto m = omega::runtime::LookAtViewMatrix(
        omega::asset::Float3IR{.x = 0.0F, .y = -10.0F, .z = 0.0F},
        omega::asset::Float3IR{.x = 0.0F, .y = 0.0F, .z = 0.0F},
        omega::asset::Float3IR{.x = 0.0F, .y = 0.0F, .z = 1.0F});
    // forward = +Y, up-row = +Z, right-row = -X (left-handed right = up x forward).
    Check(Near(m.row_major[8], 0.0F) && Near(m.row_major[9], 1.0F) &&
              Near(m.row_major[10], 0.0F),
          "look-at forward row is +Y");
    Check(Near(m.row_major[4], 0.0F) && Near(m.row_major[6], 1.0F),
          "look-at up row is +Z (Z-up)");
    Check(Near(m.row_major[0], -1.0F), "look-at right row is -X");
    // The forward-row translation is -dot(forward, eye): the target (origin) sits
    // at view depth 10 in front of the camera, and the eye at depth 0.
    Check(Near(m.row_major[11], 10.0F), "look-at target is 10 units in front");
    Check(Near(m.row_major[3], 0.0F) && Near(m.row_major[7], 0.0F),
          "look-at centres the eye's right/up view coordinates");
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
    TestParseScript();
    TestLookAtViewMatrix();
    if (g_failures != 0)
    {
        std::cerr << g_failures << " free-fly camera test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "free-fly camera tests passed\n";
    return EXIT_SUCCESS;
}
