#include "omega/simulation/simulation_world.h"

#include <chrono>

int main()
{
    const auto world = omega::simulation::SimulationWorld::Create(
        {.fixed_step = std::chrono::milliseconds{16}});
    return world.has_value() ? 0 : 1;
}
