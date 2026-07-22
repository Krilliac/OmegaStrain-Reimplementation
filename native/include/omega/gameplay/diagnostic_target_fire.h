#pragma once

#include <cstdint>
#include <expected>
#include <optional>

namespace omega::gameplay
{
// Inclusive extent of the project-owned diagnostic aim plane. This normalized
// plane is host policy only; it establishes no retail camera, ray, weapon, or
// collision coordinate contract.
inline constexpr std::uint32_t kDiagnosticAimExtent = 65'536U;

struct DiagnosticAimPointQ16
{
    std::uint32_t x = 0U;
    std::uint32_t y = 0U;

    [[nodiscard]] friend constexpr bool operator==(
        const DiagnosticAimPointQ16&, const DiagnosticAimPointQ16&) noexcept = default;
};

// Inclusive target bounds in the same project-owned normalized aim plane.
struct DiagnosticAimTargetQ16
{
    std::uint32_t left = 0U;
    std::uint32_t top = 0U;
    std::uint32_t right = 0U;
    std::uint32_t bottom = 0U;

    [[nodiscard]] friend constexpr bool operator==(
        const DiagnosticAimTargetQ16&, const DiagnosticAimTargetQ16&) noexcept = default;
};

inline constexpr DiagnosticAimTargetQ16 kProjectDiagnosticAimTarget{
    .left = 47'104U,
    .top = 30'720U,
    .right = 51'200U,
    .bottom = 34'816U,
};

struct DiagnosticTargetFireInput
{
    std::optional<DiagnosticAimPointQ16> pointer;
    bool enabled = false;
    bool target_held = false;
    bool fire_pressed = false;

    [[nodiscard]] friend constexpr bool operator==(
        const DiagnosticTargetFireInput&, const DiagnosticTargetFireInput&) noexcept = default;
};

struct DiagnosticTargetFireState
{
    bool acquired = false;
    bool target_complete = false;

    [[nodiscard]] friend constexpr bool operator==(
        const DiagnosticTargetFireState&, const DiagnosticTargetFireState&) noexcept = default;
};

struct DiagnosticTargetFireStep
{
    DiagnosticTargetFireState state{};
    bool attempted_now = false;
    bool hit_now = false;

    [[nodiscard]] friend constexpr bool operator==(
        const DiagnosticTargetFireStep&, const DiagnosticTargetFireStep&) noexcept = default;
};

enum class DiagnosticTargetFireError : std::uint8_t
{
    InvalidTarget,
    PointerOutOfRange,
};

// [any thread; reentrant] Advances one project-owned diagnostic target from
// owned values. Target and pointer validation always precede disabled or
// completed-state handling. The reducer allocates nothing and retains no state
// or references.
[[nodiscard]] std::expected<DiagnosticTargetFireStep,
    DiagnosticTargetFireError>
AdvanceDiagnosticTargetFire(DiagnosticAimTargetQ16 target,
    DiagnosticTargetFireState prior_state,
    DiagnosticTargetFireInput input) noexcept;
} // namespace omega::gameplay
