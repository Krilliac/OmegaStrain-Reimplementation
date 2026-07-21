#include "omega/runtime/render_mesh_pool.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <iostream>
#include <limits>
#include <string_view>
#include <type_traits>
#include <utility>

namespace omega::runtime::detail
{
struct RenderMeshPoolTestAccess final
{
    static bool SetFreeGeneration(RenderMeshPool& pool, const std::size_t slot_index,
        const std::uint64_t generation) noexcept
    {
        if (generation == 0U || slot_index >= pool.slots_.size() ||
            pool.slots_[slot_index].state != RenderMeshPool::SlotState::Free)
            return false;
        pool.slots_[slot_index].generation = generation;
        return true;
    }
};
} // namespace omega::runtime::detail

namespace
{
using omega::asset::Float3IR;
using omega::runtime::RenderMeshError;
using omega::runtime::RenderMeshErrorCode;
using omega::runtime::RenderMeshHandle;
using omega::runtime::RenderMeshPool;
using omega::runtime::RenderMeshPoolConfig;
using omega::runtime::RenderMeshPoolSnapshot;
using omega::runtime::RenderMeshUploadView;

constexpr std::uint64_t kTriangleLogicalBytes =
    3U * sizeof(Float3IR) + 3U * sizeof(std::uint32_t);

int failures = 0;

void Check(const bool condition, const std::string_view message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

template <typename Value>
bool CheckError(const std::expected<Value, RenderMeshError>& result,
    const RenderMeshErrorCode code, const std::string_view message)
{
    const bool matches = !result && result.error().code == code &&
                         result.error().message == omega::runtime::RenderMeshErrorMessage(code);
    Check(matches, message);
    return matches;
}

[[nodiscard]] bool IsConsistent(const RenderMeshPoolSnapshot& snapshot) noexcept
{
    return snapshot.slot_capacity == snapshot.free_slots + snapshot.reserved_slots +
                                         snapshot.resident_slots + snapshot.retired_slots;
}

[[nodiscard]] RenderMeshPoolConfig Config(const std::size_t slots,
    const std::uint64_t positions, const std::uint64_t indices,
    const std::uint64_t logical_bytes) noexcept
{
    return RenderMeshPoolConfig{
        .slot_capacity = slots,
        .maximum_resident_positions = positions,
        .maximum_resident_triangle_indices = indices,
        .maximum_resident_logical_bytes = logical_bytes,
    };
}

struct TriangleFixture
{
    std::array<Float3IR, 3> positions{
        Float3IR{.x = -1.0F, .y = -1.0F, .z = 0.5F},
        Float3IR{.x = 1.0F, .y = -1.0F, .z = 0.5F},
        Float3IR{.x = 0.0F, .y = 1.0F, .z = 0.5F},
    };
    std::array<std::uint32_t, 3> indices{0U, 1U, 2U};

    [[nodiscard]] RenderMeshUploadView View() const noexcept
    {
        return RenderMeshUploadView{.positions = positions, .triangle_indices = indices};
    }
};

void CheckContractAndConfiguration()
{
    static_assert(std::is_trivially_copyable_v<RenderMeshHandle>);
    static_assert(std::is_standard_layout_v<RenderMeshHandle>);
    static_assert(!std::is_copy_constructible_v<RenderMeshPool>);
    static_assert(!std::is_copy_assignable_v<RenderMeshPool>);
    static_assert(std::is_nothrow_move_constructible_v<RenderMeshPool>);
    static_assert(!std::is_move_assignable_v<RenderMeshPool>);
    static_assert(noexcept(std::declval<RenderMeshPool&>().Reserve(
        std::declval<RenderMeshUploadView>())));
    static_assert(noexcept(std::declval<RenderMeshPool&>().Publish(
        std::declval<const omega::runtime::RenderMeshReservation&>())));
    static_assert(noexcept(std::declval<RenderMeshPool&>().Rollback(
        std::declval<const omega::runtime::RenderMeshReservation&>())));
    static_assert(noexcept(std::declval<const RenderMeshPool&>().Get(
        std::declval<const RenderMeshHandle&>())));
    static_assert(noexcept(std::declval<RenderMeshPool&>().Release(
        std::declval<const RenderMeshHandle&>())));
    static_assert(noexcept(std::declval<const RenderMeshPool&>().Snapshot()));

    const RenderMeshHandle empty;
    Check(!empty.valid(), "a value-initialized mesh handle is invalid");
    Check(omega::runtime::kMaximumRenderMeshSlotCapacity == 8192U,
        "the render mesh pool hard slot maximum is fixed");
    const RenderMeshPoolConfig defaults;
    Check(defaults.slot_capacity == 64U &&
              defaults.maximum_resident_positions == 4ULL << 20U &&
              defaults.maximum_resident_triangle_indices == 12ULL << 20U &&
              defaults.maximum_resident_logical_bytes == 128ULL * 1024ULL * 1024ULL,
        "default mesh residency limits are explicit project policy");

    using Pair = std::pair<RenderMeshErrorCode, std::string_view>;
    constexpr std::array names{
        Pair{RenderMeshErrorCode::InvalidConfiguration, "invalid-configuration"},
        Pair{RenderMeshErrorCode::AllocationFailed, "allocation-failed"},
        Pair{RenderMeshErrorCode::PoolIdentityExhausted, "pool-identity-exhausted"},
        Pair{RenderMeshErrorCode::InvalidMesh, "invalid-mesh"},
        Pair{RenderMeshErrorCode::SlotCapacityExceeded, "slot-capacity-exceeded"},
        Pair{RenderMeshErrorCode::PositionBudgetExceeded, "position-budget-exceeded"},
        Pair{RenderMeshErrorCode::TriangleIndexBudgetExceeded,
            "triangle-index-budget-exceeded"},
        Pair{RenderMeshErrorCode::LogicalByteBudgetExceeded,
            "logical-byte-budget-exceeded"},
        Pair{RenderMeshErrorCode::InvalidReservation, "invalid-reservation"},
        Pair{RenderMeshErrorCode::InvalidHandle, "invalid-handle"},
    };
    for (const auto& [code, name] : names)
    {
        Check(omega::runtime::RenderMeshErrorCodeName(code) == name,
            "every mesh error has a fixed name");
        Check(!omega::runtime::RenderMeshErrorMessage(code).empty(),
            "every mesh error has fixed category text");
    }

    CheckError(RenderMeshPool::Create(Config(0U, 1U, 1U, 1U)),
        RenderMeshErrorCode::InvalidConfiguration, "zero slot capacity is rejected");
    CheckError(RenderMeshPool::Create(Config(1U, 0U, 1U, 1U)),
        RenderMeshErrorCode::InvalidConfiguration, "zero position budget is rejected");
    CheckError(RenderMeshPool::Create(Config(1U, 1U, 0U, 1U)),
        RenderMeshErrorCode::InvalidConfiguration, "zero index budget is rejected");
    CheckError(RenderMeshPool::Create(Config(1U, 1U, 1U, 0U)),
        RenderMeshErrorCode::InvalidConfiguration, "zero logical-byte budget is rejected");
    CheckError(RenderMeshPool::Create(
                   Config(omega::runtime::kMaximumRenderMeshSlotCapacity + 1U, 1U, 1U, 1U)),
        RenderMeshErrorCode::InvalidConfiguration,
        "slot capacity above the hard maximum is rejected");

    auto maximum = RenderMeshPool::Create(
        Config(omega::runtime::kMaximumRenderMeshSlotCapacity, 1U, 1U, 1U));
    Check(maximum && maximum->Snapshot().free_slots ==
                         omega::runtime::kMaximumRenderMeshSlotCapacity &&
              IsConsistent(maximum->Snapshot()),
        "the exact hard slot maximum is preallocated and empty");
}

void CheckUploadValidation()
{
    TriangleFixture fixture;
    auto created = RenderMeshPool::Create(Config(1U, 3U, 3U, kTriangleLogicalBytes));
    Check(created.has_value(), "validation pool is created");
    if (!created)
        return;
    RenderMeshPool pool = std::move(*created);

    const std::array<std::uint32_t, 2> two_indices{0U, 1U};
    const std::array<std::uint32_t, 4> four_indices{0U, 1U, 2U, 0U};
    CheckError(pool.Reserve({}), RenderMeshErrorCode::InvalidMesh,
        "empty position and index spans are rejected");
    CheckError(pool.Reserve({.triangle_indices = fixture.indices}),
        RenderMeshErrorCode::InvalidMesh, "triangle indices without positions are rejected");
    CheckError(pool.Reserve({.positions = fixture.positions}), RenderMeshErrorCode::InvalidMesh,
        "a mesh without triangle indices is rejected");
    CheckError(pool.Reserve(
                   {.positions = fixture.positions, .triangle_indices = two_indices}),
        RenderMeshErrorCode::InvalidMesh, "an incomplete triangle is rejected");
    CheckError(pool.Reserve(
                   {.positions = fixture.positions, .triangle_indices = four_indices}),
        RenderMeshErrorCode::InvalidMesh, "a non-triple index count is rejected");

    auto nonfinite_positions = fixture.positions;
    nonfinite_positions[0].x = std::numeric_limits<float>::infinity();
    CheckError(pool.Reserve(
                   {.positions = nonfinite_positions, .triangle_indices = fixture.indices}),
        RenderMeshErrorCode::InvalidMesh, "an infinite position is rejected");
    nonfinite_positions = fixture.positions;
    nonfinite_positions[2].z = std::numeric_limits<float>::quiet_NaN();
    CheckError(pool.Reserve(
                   {.positions = nonfinite_positions, .triangle_indices = fixture.indices}),
        RenderMeshErrorCode::InvalidMesh, "a NaN position is rejected");

    auto out_of_range = fixture.indices;
    out_of_range[2] = 3U;
    CheckError(pool.Reserve(
                   {.positions = fixture.positions, .triangle_indices = out_of_range}),
        RenderMeshErrorCode::InvalidMesh, "an out-of-range triangle index is rejected");
    Check(pool.Snapshot() == RenderMeshPoolSnapshot{
                                 .slot_capacity = 1U,
                                 .free_slots = 1U,
                             },
        "invalid uploads consume no slot or residency budget");
}

void CheckBudgetsAndTransactions()
{
    TriangleFixture fixture;
    auto exact_created = RenderMeshPool::Create(Config(2U, 6U, 6U, 2U * kTriangleLogicalBytes));
    Check(exact_created.has_value(), "exact aggregate-budget pool is created");
    if (!exact_created)
        return;
    RenderMeshPool pool = std::move(*exact_created);

    auto first = pool.Reserve(fixture.View());
    auto second = pool.Reserve(fixture.View());
    Check(first && second && first->handle != second->handle &&
              first->handle.pool_identity == second->handle.pool_identity,
        "duplicate borrowed uploads reserve independent generations in one pool");
    const auto reserved = pool.Snapshot();
    Check(reserved.free_slots == 0U && reserved.reserved_slots == 2U &&
              reserved.reserved_positions == 6U && reserved.reserved_triangle_indices == 6U &&
              reserved.reserved_logical_bytes == 2U * kTriangleLogicalBytes &&
              reserved.resident_slots == 0U && IsConsistent(reserved),
        "reservations charge exact aggregate position, index, and logical-byte counts");
    CheckError(pool.Reserve(fixture.View()), RenderMeshErrorCode::SlotCapacityExceeded,
        "a full slot table rejects another reservation before budget mutation");
    if (!first || !second)
        return;

    auto forged = *first;
    ++forged.position_count;
    const auto before_forgery = pool.Snapshot();
    CheckError(pool.Publish(forged), RenderMeshErrorCode::InvalidReservation,
        "forged reservation metadata cannot publish");
    Check(pool.Snapshot() == before_forgery, "forged publication leaves accounting unchanged");

    auto handle = pool.Publish(*first);
    Check(handle && *handle == first->handle, "successful backend work publishes one reservation");
    CheckError(pool.Publish(*first), RenderMeshErrorCode::InvalidReservation,
        "one reservation cannot publish twice");
    CheckError(pool.Rollback(*first), RenderMeshErrorCode::InvalidReservation,
        "resident metadata cannot roll back as an unpublished transaction");
    Check(pool.Rollback(*second).has_value(), "backend failure rolls back the other reservation");
    CheckError(pool.Rollback(*second), RenderMeshErrorCode::InvalidReservation,
        "one reservation cannot roll back twice");

    if (!handle)
        return;
    auto metadata = pool.Get(*handle);
    Check(metadata && metadata->position_count == 3U &&
              metadata->triangle_index_count == 3U &&
              metadata->logical_bytes == kTriangleLogicalBytes,
        "resident lookup returns exact portable mesh metadata");
    const auto resident = pool.Snapshot();
    Check(resident.free_slots == 1U && resident.resident_slots == 1U &&
              resident.reserved_slots == 0U && resident.resident_positions == 3U &&
              resident.resident_triangle_indices == 3U &&
              resident.resident_logical_bytes == kTriangleLogicalBytes && IsConsistent(resident),
        "publish and rollback transfer all aggregate counters atomically");

    auto released = pool.Release(*handle);
    Check(released && released->handle == *handle,
        "release returns metadata for the backend's parallel slot");
    CheckError(pool.Get(*handle), RenderMeshErrorCode::InvalidHandle,
        "a released generation is stale");
    CheckError(pool.Release(*handle), RenderMeshErrorCode::InvalidHandle,
        "a released generation cannot release twice");
    Check(pool.Snapshot() == RenderMeshPoolSnapshot{
                                 .slot_capacity = 2U,
                                 .free_slots = 2U,
                             },
        "completed transactions restore every slot and budget counter");

    auto next = pool.Reserve(fixture.View());
    Check(next && next->handle.slot_index == handle->slot_index &&
              next->handle.generation == handle->generation + 1U,
        "slot reuse advances its generation exactly once");
    if (next)
        Check(pool.Rollback(*next).has_value(), "reused reservation rolls back cleanly");

    const auto CheckRejectedBudget = [&](const RenderMeshPoolConfig config,
                                         const RenderMeshErrorCode code,
                                         const std::string_view message)
    {
        auto bounded = RenderMeshPool::Create(config);
        Check(bounded && CheckError(bounded->Reserve(fixture.View()), code, message), message);
        if (bounded)
            Check(bounded->Snapshot().free_slots == 1U &&
                      bounded->Snapshot().reserved_slots == 0U,
                "budget rejection leaves its pool empty");
    };
    CheckRejectedBudget(Config(1U, 2U, 3U, kTriangleLogicalBytes),
        RenderMeshErrorCode::PositionBudgetExceeded,
        "one below the exact position budget is rejected");
    CheckRejectedBudget(Config(1U, 3U, 2U, kTriangleLogicalBytes),
        RenderMeshErrorCode::TriangleIndexBudgetExceeded,
        "one below the exact triangle-index budget is rejected");
    CheckRejectedBudget(Config(1U, 3U, 3U, kTriangleLogicalBytes - 1U),
        RenderMeshErrorCode::LogicalByteBudgetExceeded,
        "one below the exact logical-byte budget is rejected");
}

void CheckForeignMoveAndRetirement()
{
    TriangleFixture fixture;
    auto first_created = RenderMeshPool::Create(Config(1U, 3U, 3U, kTriangleLogicalBytes));
    auto foreign_created = RenderMeshPool::Create(Config(1U, 3U, 3U, kTriangleLogicalBytes));
    Check(first_created && foreign_created, "independent mesh pools are created");
    if (!first_created || !foreign_created)
        return;
    RenderMeshPool source = std::move(*first_created);
    RenderMeshPool foreign = std::move(*foreign_created);
    auto reservation = source.Reserve(fixture.View());
    auto foreign_reservation = foreign.Reserve(fixture.View());
    Check(reservation && foreign_reservation &&
              reservation->handle.pool_identity != foreign_reservation->handle.pool_identity,
        "independent pools issue distinct nonzero identities");
    if (!reservation || !foreign_reservation)
        return;
    CheckError(source.Publish(*foreign_reservation), RenderMeshErrorCode::InvalidReservation,
        "a foreign reservation cannot publish");
    CheckError(source.Rollback(*foreign_reservation), RenderMeshErrorCode::InvalidReservation,
        "a foreign reservation cannot roll back");

    auto handle = source.Publish(*reservation);
    Check(handle.has_value(), "move fixture publishes one resident generation");
    RenderMeshPool moved = std::move(source);
    Check(handle && moved.Get(*handle).has_value(),
        "move construction transfers identity and resident metadata");
    Check(source.Snapshot().slot_capacity == 0U &&
              CheckError(source.Reserve(fixture.View()),
                  RenderMeshErrorCode::InvalidConfiguration,
                  "a moved-from pool rejects new transactions"),
        "moved-from pool is inert and aggregate-empty");
    if (handle)
    {
        CheckError(foreign.Get(*handle), RenderMeshErrorCode::InvalidHandle,
            "a foreign pool rejects a live handle");
        auto forged = *handle;
        ++forged.generation;
        CheckError(moved.Get(forged), RenderMeshErrorCode::InvalidHandle,
            "a forged generation is rejected");
        forged = *handle;
        forged.slot_index = std::numeric_limits<std::uint32_t>::max();
        CheckError(moved.Get(forged), RenderMeshErrorCode::InvalidHandle,
            "an out-of-range slot is rejected");
        Check(moved.Release(*handle).has_value(), "moved resident generation releases");
    }
    Check(foreign.Rollback(*foreign_reservation).has_value(),
        "independent foreign reservation rolls back");

    auto release_created = RenderMeshPool::Create(Config(1U, 3U, 3U, kTriangleLogicalBytes));
    auto rollback_created = RenderMeshPool::Create(Config(1U, 3U, 3U, kTriangleLogicalBytes));
    Check(release_created && rollback_created, "terminal-generation pools are created");
    if (!release_created || !rollback_created)
        return;
    RenderMeshPool release_pool = std::move(*release_created);
    RenderMeshPool rollback_pool = std::move(*rollback_created);
    constexpr std::uint64_t terminal = std::numeric_limits<std::uint64_t>::max();
    Check(omega::runtime::detail::RenderMeshPoolTestAccess::SetFreeGeneration(
              release_pool, 0U, terminal),
        "release fixture reaches the terminal generation");
    auto terminal_reservation = release_pool.Reserve(fixture.View());
    auto terminal_handle = terminal_reservation ? release_pool.Publish(*terminal_reservation)
                                                : std::expected<RenderMeshHandle,
                                                      RenderMeshError>{std::unexpected(
                                                      RenderMeshError{})};
    Check(terminal_handle && release_pool.Release(*terminal_handle),
        "terminal resident generation releases without wrapping");
    Check(release_pool.Snapshot().retired_slots == 1U &&
              release_pool.Snapshot().free_slots == 0U &&
              release_pool.Snapshot().resident_positions == 0U &&
              release_pool.Snapshot().resident_triangle_indices == 0U &&
              release_pool.Snapshot().resident_logical_bytes == 0U &&
              IsConsistent(release_pool.Snapshot()),
        "release permanently retires terminal capacity and clears counters");
    if (terminal_handle)
        CheckError(release_pool.Get(*terminal_handle), RenderMeshErrorCode::InvalidHandle,
            "the terminal released handle is stale");
    CheckError(release_pool.Reserve(fixture.View()),
        RenderMeshErrorCode::SlotCapacityExceeded,
        "retired release capacity cannot issue a wrapped handle");

    Check(omega::runtime::detail::RenderMeshPoolTestAccess::SetFreeGeneration(
              rollback_pool, 0U, terminal),
        "rollback fixture reaches the terminal generation");
    auto terminal_rollback = rollback_pool.Reserve(fixture.View());
    Check(terminal_rollback && rollback_pool.Rollback(*terminal_rollback),
        "terminal reservation rolls back without wrapping");
    Check(rollback_pool.Snapshot().retired_slots == 1U &&
              rollback_pool.Snapshot().free_slots == 0U &&
              rollback_pool.Snapshot().reserved_positions == 0U &&
              rollback_pool.Snapshot().reserved_triangle_indices == 0U &&
              rollback_pool.Snapshot().reserved_logical_bytes == 0U &&
              IsConsistent(rollback_pool.Snapshot()),
        "rollback permanently retires terminal capacity and clears counters");
}
} // namespace

int main()
{
    CheckContractAndConfiguration();
    CheckUploadValidation();
    CheckBudgetsAndTransactions();
    CheckForeignMoveAndRetirement();
    if (failures == 0)
        std::cout << "omega_render_mesh_pool_tests: all checks passed\n";
    return failures == 0 ? 0 : 1;
}
