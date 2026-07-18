#pragma once

#include <chrono>
#include <cstdint>
#include <expected>
#include <string>

namespace omega::simulation
{
struct SimulationWorldConfig
{
    // Project-owned fixed step supplied by the composition root. It must be positive; its value is
    // a synthetic-shell choice until retail timing is established from evidence.
    std::chrono::nanoseconds fixed_step{0};
};

struct SimulationState
{
    std::uint64_t completed_steps = 0;
    std::chrono::nanoseconds simulated_time{0};
};

enum class SimulationStepResult : std::uint8_t
{
    Advanced,
    RepresentationExhausted,
};

// Deterministic game-thread state boundary. This first implementation owns only the canonical
// simulation clock; components and systems will enter behind this API without making the SDL host
// or frame scheduler depend on gameplay state.
class SimulationWorld final
{
public:
    // [any thread; reentrant] Validates a positive fixed step and returns an empty world.
    [[nodiscard]] static std::expected<SimulationWorld, std::string> Create(
        const SimulationWorldConfig& config);

    // [game thread] Advances exactly one fixed step. If either diagnostic representation is full,
    // returns RepresentationExhausted and leaves the complete state unchanged.
    [[nodiscard]] SimulationStepResult AdvanceOneStep() noexcept;

    // [game thread] Returns an owned immutable value suitable for render/debug packet assembly.
    [[nodiscard]] SimulationState Snapshot() const noexcept;

    // [game thread; immutable after Create()]
    [[nodiscard]] const SimulationWorldConfig& config() const noexcept;

private:
    explicit SimulationWorld(const SimulationWorldConfig& config) noexcept;

    SimulationWorldConfig config_{};
    SimulationState state_{};
};
} // namespace omega::simulation
