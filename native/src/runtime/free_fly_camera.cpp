#include "omega/runtime/free_fly_camera.h"

#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>

namespace omega::runtime
{
namespace
{
constexpr float kPitchLimit = 1.55334F; // ~89 degrees, keeps the view from flipping

[[nodiscard]] asset::Float3IR Cross(
    const asset::Float3IR& a, const asset::Float3IR& b) noexcept
{
    return asset::Float3IR{
        .x = a.y * b.z - a.z * b.y,
        .y = a.z * b.x - a.x * b.z,
        .z = a.x * b.y - a.y * b.x,
    };
}

[[nodiscard]] float Dot(const asset::Float3IR& a, const asset::Float3IR& b) noexcept
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

[[nodiscard]] asset::Float3IR Normalize(const asset::Float3IR& v) noexcept
{
    const float length = std::sqrt(Dot(v, v));
    if (!(length > 0.0F) || !std::isfinite(length))
        return asset::Float3IR{.x = 0.0F, .y = 0.0F, .z = 1.0F};
    const float inverse = 1.0F / length;
    return asset::Float3IR{.x = v.x * inverse, .y = v.y * inverse, .z = v.z * inverse};
}

[[nodiscard]] bool Finite(const float value) noexcept { return std::isfinite(value); }
} // namespace

asset::Float3IR FreeFlyForward(const FreeFlyPose& pose) noexcept
{
    const float cos_pitch = std::cos(pose.pitch);
    // Left-handed, +Z forward at yaw=0/pitch=0.
    return Normalize(asset::Float3IR{
        .x = std::sin(pose.yaw) * cos_pitch,
        .y = std::sin(pose.pitch),
        .z = std::cos(pose.yaw) * cos_pitch,
    });
}

asset::Float3IR FreeFlyRight(const FreeFlyPose& pose) noexcept
{
    // Left-handed basis: right = worldUp x forward.
    return Normalize(Cross(asset::Float3IR{.x = 0.0F, .y = 1.0F, .z = 0.0F},
        FreeFlyForward(pose)));
}

FreeFlyPose AdvanceFreeFly(
    FreeFlyPose pose, const FreeFlyInput& input, const float move_speed) noexcept
{
    if (Finite(input.yaw_delta))
        pose.yaw += input.yaw_delta;
    if (Finite(input.pitch_delta))
        pose.pitch += input.pitch_delta;
    if (pose.pitch > kPitchLimit)
        pose.pitch = kPitchLimit;
    if (pose.pitch < -kPitchLimit)
        pose.pitch = -kPitchLimit;

    const asset::Float3IR forward = FreeFlyForward(pose);
    const asset::Float3IR right = FreeFlyRight(pose);
    const float f = Finite(input.forward) ? input.forward : 0.0F;
    const float s = Finite(input.strafe) ? input.strafe : 0.0F;
    const float v = Finite(input.vertical) ? input.vertical : 0.0F;
    const float speed = Finite(move_speed) ? move_speed : 0.0F;

    pose.position.x += (forward.x * f + right.x * s) * speed;
    pose.position.y += (forward.y * f + right.y * s + v) * speed;
    pose.position.z += (forward.z * f + right.z * s) * speed;
    return pose;
}

asset::Matrix4x4IR FreeFlyViewMatrix(const FreeFlyPose& pose) noexcept
{
    const asset::Float3IR forward = FreeFlyForward(pose);
    const asset::Float3IR right = FreeFlyRight(pose);
    const asset::Float3IR up = Cross(forward, right); // left-handed, unit-length

    // Row-major, column-vector world_to_view: rows are the basis, last column is
    // the translation -dot(basis, position). view = M * world_point.
    return asset::Matrix4x4IR{
        .row_major = {
            right.x, right.y, right.z, -Dot(right, pose.position),
            up.x, up.y, up.z, -Dot(up, pose.position),
            forward.x, forward.y, forward.z, -Dot(forward, pose.position),
            0.0F, 0.0F, 0.0F, 1.0F,
        },
    };
}

asset::Matrix4x4IR LookAtViewMatrix(const asset::Float3IR& eye,
    const asset::Float3IR& target, const asset::Float3IR& world_up) noexcept
{
    const asset::Float3IR forward = Normalize(asset::Float3IR{
        .x = target.x - eye.x,
        .y = target.y - eye.y,
        .z = target.z - eye.z,
    });
    // Left-handed basis: right = world_up x forward, up = forward x right.
    asset::Float3IR right = Cross(world_up, forward);
    if (!(Dot(right, right) > 0.0F))
    {
        // world_up parallel to forward: pick an arbitrary perpendicular axis.
        right = Cross(asset::Float3IR{.x = 1.0F, .y = 0.0F, .z = 0.0F}, forward);
        if (!(Dot(right, right) > 0.0F))
            right = Cross(asset::Float3IR{.x = 0.0F, .y = 1.0F, .z = 0.0F}, forward);
    }
    right = Normalize(right);
    const asset::Float3IR up = Cross(forward, right);

    return asset::Matrix4x4IR{
        .row_major = {
            right.x, right.y, right.z, -Dot(right, eye),
            up.x, up.y, up.z, -Dot(up, eye),
            forward.x, forward.y, forward.z, -Dot(forward, eye),
            0.0F, 0.0F, 0.0F, 1.0F,
        },
    };
}

asset::Matrix4x4IR PerspectiveProjection(const float vertical_field_of_view_radians,
    const float aspect_ratio, const float near_plane, const float far_plane) noexcept
{
    const float tan_half = std::tan(vertical_field_of_view_radians * 0.5F);
    const float f = (tan_half > 0.0F && std::isfinite(tan_half)) ? 1.0F / tan_half : 1.0F;
    const float aspect = (aspect_ratio > 0.0F && std::isfinite(aspect_ratio))
                             ? aspect_ratio
                             : 1.0F;
    const float depth = far_plane - near_plane;
    const float z_scale = (depth > 0.0F) ? far_plane / depth : 1.0F;
    const float z_bias = (depth > 0.0F) ? -(far_plane * near_plane) / depth : 0.0F;

    // Row-major, column-vector, left-handed (+Z forward), clip depth [0,1].
    return asset::Matrix4x4IR{
        .row_major = {
            f / aspect, 0.0F, 0.0F, 0.0F,
            0.0F, f, 0.0F, 0.0F,
            0.0F, 0.0F, z_scale, z_bias,
            0.0F, 0.0F, 1.0F, 0.0F,
        },
    };
}

std::optional<FreeFlyPose> ParseFreeFlyPose(const std::string_view spec) noexcept
{
    std::array<float, 5> values{};
    std::size_t field = 0U;
    std::size_t cursor = 0U;
    while (field < values.size())
    {
        std::size_t comma = spec.find(',', cursor);
        const std::string_view token = spec.substr(
            cursor, comma == std::string_view::npos ? std::string_view::npos
                                                    : comma - cursor);
        if (token.empty())
            return std::nullopt;
        float parsed = 0.0F;
        const char* const begin = token.data();
        const char* const end = begin + token.size();
        const auto result = std::from_chars(begin, end, parsed);
        if (result.ec != std::errc{} || result.ptr != end || !std::isfinite(parsed))
            return std::nullopt;
        values[field] = parsed;
        ++field;
        if (field == values.size())
        {
            // All fields read: any trailing comma means too many fields.
            if (comma != std::string_view::npos)
                return std::nullopt;
            break;
        }
        if (comma == std::string_view::npos)
            break;
        cursor = comma + 1U;
    }
    if (field != values.size())
        return std::nullopt;

    return FreeFlyPose{
        .position = {.x = values[0], .y = values[1], .z = values[2]},
        .yaw = values[3],
        .pitch = values[4],
    };
}

FreeFlyInput ParseFreeFlyScript(
    const std::string_view spec, const float look_step) noexcept
{
    const float step = std::isfinite(look_step) ? look_step : 0.0F;
    FreeFlyInput input{};
    std::size_t cursor = 0U;
    while (cursor <= spec.size())
    {
        std::size_t next = spec.find_first_of(", ;", cursor);
        const std::string_view token = spec.substr(
            cursor, next == std::string_view::npos ? std::string_view::npos
                                                   : next - cursor);
        if (!token.empty())
        {
            if (token == "forward")
                input.forward += 1.0F;
            else if (token == "back")
                input.forward -= 1.0F;
            // "left"/"right" are aliases for yaw turning; strafing has its own tokens.
            else if (token == "left" || token == "yawleft")
                input.yaw_delta -= step;
            else if (token == "right" || token == "yawright")
                input.yaw_delta += step;
            else if (token == "strafeleft")
                input.strafe -= 1.0F;
            else if (token == "straferight")
                input.strafe += 1.0F;
            else if (token == "up")
                input.vertical += 1.0F;
            else if (token == "down")
                input.vertical -= 1.0F;
            else if (token == "pitchup")
                input.pitch_delta += step;
            else if (token == "pitchdown")
                input.pitch_delta -= step;
            // Unknown tokens are ignored.
        }
        if (next == std::string_view::npos)
            break;
        cursor = next + 1U;
    }
    return input;
}
} // namespace omega::runtime
