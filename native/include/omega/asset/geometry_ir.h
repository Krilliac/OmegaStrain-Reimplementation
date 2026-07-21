#pragma once

#include <array>
#include <type_traits>

namespace omega::asset
{
// Project-owned three-component geometry value. It carries no coordinate-system, unit, retail-
// format, or serialization meaning by itself.
struct Float3IR
{
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;

    bool operator==(const Float3IR&) const = default;
};

// Project-owned row-major 4x4 matrix value. The multiplication and clip-volume policy used by
// SceneIR is recorded in ADR 0005; storage order alone does not imply a retail convention.
struct Matrix4x4IR
{
    std::array<float, 16> row_major{};

    bool operator==(const Matrix4x4IR&) const = default;
};

inline constexpr Matrix4x4IR kIdentityMatrix4x4IR{
    .row_major = {
        1.0F, 0.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 1.0F, 0.0F,
        0.0F, 0.0F, 0.0F, 1.0F,
    },
};

static_assert(std::is_trivially_copyable_v<Float3IR>);
static_assert(std::is_standard_layout_v<Float3IR>);
static_assert(std::is_trivially_copyable_v<Matrix4x4IR>);
static_assert(std::is_standard_layout_v<Matrix4x4IR>);
} // namespace omega::asset
