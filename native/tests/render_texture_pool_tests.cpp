#include "omega/runtime/render_frame_packet.h"
#include "omega/runtime/render_texture_pool.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <iostream>
#include <limits>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

namespace omega::runtime::detail
{
struct RenderTexturePoolTestAccess final
{
    static bool SetFreeGeneration(RenderTexturePool& pool, const std::size_t slot_index,
        const std::uint64_t generation) noexcept
    {
        if (generation == 0U || slot_index >= pool.slots_.size() ||
            pool.slots_[slot_index].state != RenderTexturePool::SlotState::Free)
            return false;
        pool.slots_[slot_index].generation = generation;
        return true;
    }
};
} // namespace omega::runtime::detail

namespace
{
using omega::runtime::RenderClearColorRgba8;
using omega::runtime::RenderFramePacket;
using omega::runtime::RenderTextureError;
using omega::runtime::RenderTextureErrorCode;
using omega::runtime::RenderTextureHandle;
using omega::runtime::RenderTexturePool;
using omega::runtime::RenderTexturePoolConfig;
using omega::runtime::RenderTexturePoolSnapshot;
using omega::runtime::Rgba8TextureUploadView;

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
bool CheckError(const std::expected<Value, RenderTextureError>& result,
    const RenderTextureErrorCode code, const std::string_view message)
{
    const bool matches = !result && result.error().code == code &&
                         result.error().message == omega::runtime::RenderTextureErrorMessage(code);
    Check(matches, message);
    return matches;
}

bool IsConsistent(const RenderTexturePoolSnapshot& snapshot)
{
    return snapshot.slot_capacity == snapshot.free_slots + snapshot.reserved_slots +
                                         snapshot.resident_slots + snapshot.retired_slots;
}

RenderTexturePoolConfig Config(
    const std::size_t slots, const std::uint64_t resident_bytes)
{
    return RenderTexturePoolConfig{
        .slot_capacity = slots,
        .maximum_resident_logical_bytes = resident_bytes,
    };
}

template <std::size_t Size>
Rgba8TextureUploadView Upload(
    const std::array<std::byte, Size>& pixels, const std::uint32_t width,
    const std::uint32_t height)
{
    return Rgba8TextureUploadView{
        .width = width,
        .height = height,
        .pixels = pixels,
    };
}

void CheckContractAndConfiguration()
{
    static_assert(std::is_trivially_copyable_v<RenderTextureHandle>);
    static_assert(std::is_standard_layout_v<RenderTextureHandle>);
    static_assert(sizeof(RenderClearColorRgba8) == 4U);
    static_assert(std::is_trivially_copyable_v<RenderClearColorRgba8>);
    static_assert(std::is_standard_layout_v<RenderClearColorRgba8>);
    static_assert(std::is_trivially_copyable_v<RenderFramePacket>);
    static_assert(std::is_standard_layout_v<RenderFramePacket>);
    static_assert(!std::is_copy_constructible_v<RenderTexturePool>);
    static_assert(!std::is_copy_assignable_v<RenderTexturePool>);
    static_assert(std::is_nothrow_move_constructible_v<RenderTexturePool>);
    static_assert(!std::is_move_assignable_v<RenderTexturePool>);
    static_assert(noexcept(std::declval<RenderTexturePool&>().Reserve(
        std::declval<Rgba8TextureUploadView>())));
    static_assert(noexcept(std::declval<RenderTexturePool&>().Publish(
        std::declval<const omega::runtime::RenderTextureReservation&>())));
    static_assert(noexcept(std::declval<RenderTexturePool&>().Rollback(
        std::declval<const omega::runtime::RenderTextureReservation&>())));
    static_assert(noexcept(std::declval<const RenderTexturePool&>().Get(
        std::declval<const RenderTextureHandle&>())));
    static_assert(noexcept(std::declval<RenderTexturePool&>().Release(
        std::declval<const RenderTextureHandle&>())));
    static_assert(noexcept(std::declval<const RenderTexturePool&>().Snapshot()));

    const RenderTextureHandle empty;
    const RenderFramePacket packet;
    const RenderClearColorRgba8 zero_clear;
    Check(zero_clear == RenderClearColorRgba8{},
        "generic render clear color defaults to zero");
    Check(omega::runtime::kDefaultRenderClearColor ==
              RenderClearColorRgba8{
                  .red = 4U,
                  .green = 5U,
                  .blue = 10U,
                  .alpha = 255U,
              },
        "named frame clear color has the exact synthetic default");
    Check(!empty.valid() &&
              packet.clear_color == omega::runtime::kDefaultRenderClearColor &&
              packet.draw_list.empty(),
        "default handle is invalid and frame-packet clear/draw values are explicit");
    Check(omega::runtime::kMaximumRenderTextureSlotCapacity == 8192U,
        "the render texture pool hard maximum is fixed at 8192 slots");
    const RenderTexturePoolConfig defaults;
    Check(defaults.slot_capacity == 64U &&
              defaults.maximum_resident_logical_bytes == 64ULL * 1024ULL * 1024ULL,
        "default render texture limits are explicit synthetic policy");

    using Pair = std::pair<RenderTextureErrorCode, std::string_view>;
    constexpr std::array names{
        Pair{RenderTextureErrorCode::InvalidConfiguration, "invalid-configuration"},
        Pair{RenderTextureErrorCode::AllocationFailed, "allocation-failed"},
        Pair{RenderTextureErrorCode::PoolIdentityExhausted, "pool-identity-exhausted"},
        Pair{RenderTextureErrorCode::InvalidImage, "invalid-image"},
        Pair{RenderTextureErrorCode::SlotCapacityExceeded, "slot-capacity-exceeded"},
        Pair{RenderTextureErrorCode::ResidentBudgetExceeded, "resident-budget-exceeded"},
        Pair{RenderTextureErrorCode::InvalidReservation, "invalid-reservation"},
        Pair{RenderTextureErrorCode::InvalidHandle, "invalid-handle"},
    };
    for (const auto& [code, name] : names)
    {
        Check(omega::runtime::RenderTextureErrorCodeName(code) == name,
            "every render texture error code has a fixed name");
        Check(!omega::runtime::RenderTextureErrorMessage(code).empty(),
            "every render texture error code has fixed message text");
    }

    CheckError(RenderTexturePool::Create(Config(0U, 1U)),
        RenderTextureErrorCode::InvalidConfiguration, "zero slot capacity is rejected");
    CheckError(RenderTexturePool::Create(Config(1U, 0U)),
        RenderTextureErrorCode::InvalidConfiguration, "zero resident budget is rejected");
    CheckError(RenderTexturePool::Create(
                   Config(omega::runtime::kMaximumRenderTextureSlotCapacity + 1U, 1U)),
        RenderTextureErrorCode::InvalidConfiguration,
        "slot capacity above the hard maximum is rejected");

    auto maximum = RenderTexturePool::Create(
        Config(omega::runtime::kMaximumRenderTextureSlotCapacity, 1U));
    Check(maximum && maximum->Snapshot().slot_capacity ==
                         omega::runtime::kMaximumRenderTextureSlotCapacity &&
              maximum->Snapshot().free_slots ==
                  omega::runtime::kMaximumRenderTextureSlotCapacity &&
              IsConsistent(maximum->Snapshot()),
        "the exact slot hard maximum is fully preallocated and empty");
}

void CheckImagePreflightAndIdentity()
{
    const std::array<std::byte, 4> rgba{};
    const std::array<std::byte, 8> two_pixels{};
    auto first_created = RenderTexturePool::Create(Config(1U, 8U));
    auto second_created = RenderTexturePool::Create(Config(1U, 8U));
    Check(first_created && second_created, "independent identity pools are created");
    if (!first_created || !second_created)
        return;
    RenderTexturePool first = std::move(*first_created);
    RenderTexturePool second = std::move(*second_created);

    CheckError(first.Reserve(Upload(rgba, 0U, 1U)), RenderTextureErrorCode::InvalidImage,
        "zero width is rejected");
    CheckError(first.Reserve(Upload(rgba, 1U, 0U)), RenderTextureErrorCode::InvalidImage,
        "zero height is rejected");
    CheckError(first.Reserve(Rgba8TextureUploadView{
                   .width = 1U, .height = 1U,
                   .pixels = std::span<const std::byte>(two_pixels.data(), 3U)}),
        RenderTextureErrorCode::InvalidImage, "a short RGBA8 extent is rejected");
    CheckError(first.Reserve(Upload(two_pixels, 1U, 1U)),
        RenderTextureErrorCode::InvalidImage, "a long RGBA8 extent is rejected");
    CheckError(first.Reserve(Rgba8TextureUploadView{
                   .width = std::numeric_limits<std::uint32_t>::max(),
                   .height = std::numeric_limits<std::uint32_t>::max(),
                   .pixels = {}}),
        RenderTextureErrorCode::InvalidImage, "RGBA8 extent multiplication fails closed");
    Check(first.Snapshot() == RenderTexturePoolSnapshot{
                                  .slot_capacity = 1U,
                                  .free_slots = 1U,
                              },
        "invalid image preflight consumes no slot or resident budget");

    auto first_reservation = first.Reserve(Upload(rgba, 1U, 1U));
    auto second_reservation = second.Reserve(Upload(rgba, 1U, 1U));
    Check(first_reservation && second_reservation &&
              first_reservation->handle.valid() && second_reservation->handle.valid() &&
              first_reservation->handle.pool_identity !=
                  second_reservation->handle.pool_identity,
        "live pools issue distinct nonzero process-local identities");
    if (first_reservation && second_reservation)
    {
        CheckError(first.Publish(*second_reservation),
            RenderTextureErrorCode::InvalidReservation,
            "a foreign reservation cannot publish in another pool");
        Check(first.Rollback(*first_reservation) && second.Rollback(*second_reservation),
            "independent reservations roll back without publishing");
    }
}

void CheckTransactionsAndDuplicates()
{
    const std::array<std::byte, 4> rgba{};
    auto created = RenderTexturePool::Create(Config(2U, 8U));
    Check(created.has_value(), "transaction pool is created");
    if (!created)
        return;
    RenderTexturePool pool = std::move(*created);

    auto first = pool.Reserve(Upload(rgba, 1U, 1U));
    auto duplicate = pool.Reserve(Upload(rgba, 1U, 1U));
    Check(first && duplicate && first->handle != duplicate->handle &&
              first->handle.slot_index != duplicate->handle.slot_index,
        "duplicate uploads reserve independent fixed slots");
    const auto reserved = pool.Snapshot();
    Check(reserved.free_slots == 0U && reserved.reserved_slots == 2U &&
              reserved.resident_slots == 0U && reserved.reserved_logical_bytes == 8U &&
              reserved.resident_logical_bytes == 0U && IsConsistent(reserved),
        "reservation charges exact aggregate logical bytes");
    CheckError(pool.Reserve(Upload(rgba, 1U, 1U)),
        RenderTextureErrorCode::SlotCapacityExceeded,
        "a full pool rejects another transaction without mutation");
    if (!first || !duplicate)
        return;

    CheckError(pool.Get(first->handle), RenderTextureErrorCode::InvalidHandle,
        "reserved metadata is not published resident state");
    CheckError(pool.Release(first->handle), RenderTextureErrorCode::InvalidHandle,
        "a backend cannot release an unpublished reservation as resident");

    auto forged = *first;
    ++forged.width;
    const auto before_forgery = pool.Snapshot();
    CheckError(pool.Publish(forged), RenderTextureErrorCode::InvalidReservation,
        "forged reservation metadata cannot publish");
    Check(pool.Snapshot() == before_forgery,
        "forged publication leaves the transaction unchanged");

    auto published = pool.Publish(*first);
    Check(published && *published == first->handle,
        "successful backend work publishes the reserved generation");
    CheckError(pool.Publish(*first), RenderTextureErrorCode::InvalidReservation,
        "one transaction cannot publish twice");
    CheckError(pool.Rollback(*first), RenderTextureErrorCode::InvalidReservation,
        "published resident metadata cannot be rolled back");
    Check(pool.Rollback(*duplicate).has_value(),
        "backend failure rolls back its independent duplicate transaction");
    CheckError(pool.Rollback(*duplicate), RenderTextureErrorCode::InvalidReservation,
        "one failed backend transaction cannot roll back twice");

    const auto live = pool.Get(*published);
    Check(live && live->width == 1U && live->height == 1U && live->logical_bytes == 4U,
        "resident lookup returns only portable metadata");
    const auto resident = pool.Snapshot();
    Check(resident.free_slots == 1U && resident.reserved_slots == 0U &&
              resident.resident_slots == 1U && resident.reserved_logical_bytes == 0U &&
              resident.resident_logical_bytes == 4U && IsConsistent(resident),
        "publish and rollback transfer exact slot and byte accounting");

    const auto released = pool.Release(*published);
    Check(released && released->handle == *published && released->logical_bytes == 4U,
        "explicit resident release returns metadata for the parallel backend slot");
    CheckError(pool.Get(*published), RenderTextureErrorCode::InvalidHandle,
        "released generation is permanently stale");
    CheckError(pool.Release(*published), RenderTextureErrorCode::InvalidHandle,
        "released generation cannot release twice");
    const auto empty = pool.Snapshot();
    Check(empty.free_slots == 2U && empty.reserved_slots == 0U &&
              empty.resident_slots == 0U && empty.retired_slots == 0U &&
              empty.reserved_logical_bytes == 0U && empty.resident_logical_bytes == 0U &&
              IsConsistent(empty),
        "transaction completion returns every reusable slot and logical byte");
}

void CheckResidentBudgetAndHandleValidation()
{
    const std::array<std::byte, 4> rgba{};
    const std::array<std::byte, 8> two_pixels{};
    auto exact_created = RenderTexturePool::Create(Config(3U, 8U));
    auto below_created = RenderTexturePool::Create(Config(1U, 7U));
    Check(exact_created && below_created, "resident-boundary pools are created");
    if (!exact_created || !below_created)
        return;
    RenderTexturePool exact = std::move(*exact_created);
    RenderTexturePool below = std::move(*below_created);

    auto exact_reservation = exact.Reserve(Upload(two_pixels, 2U, 1U));
    Check(exact_reservation && exact.Publish(*exact_reservation),
        "an upload exactly at the resident boundary publishes");
    CheckError(exact.Reserve(Upload(rgba, 1U, 1U)),
        RenderTextureErrorCode::ResidentBudgetExceeded,
        "resident bytes prevent reservation overcommit despite a free slot");
    CheckError(below.Reserve(Upload(two_pixels, 2U, 1U)),
        RenderTextureErrorCode::ResidentBudgetExceeded,
        "one byte below exact RGBA8 residency fails atomically");
    Check(below.Snapshot().free_slots == 1U &&
              below.Snapshot().reserved_logical_bytes == 0U &&
              below.Snapshot().resident_logical_bytes == 0U,
        "resident rejection consumes no slot or byte");

    auto foreign_created = RenderTexturePool::Create(Config(1U, 8U));
    Check(foreign_created.has_value(), "foreign-handle pool is created");
    if (!foreign_created || !exact_reservation)
        return;
    RenderTexturePool foreign = std::move(*foreign_created);
    auto foreign_reservation = foreign.Reserve(Upload(rgba, 1U, 1U));
    auto foreign_handle = foreign_reservation ? foreign.Publish(*foreign_reservation)
                                              : std::expected<RenderTextureHandle,
                                                    RenderTextureError>{std::unexpected(
                                                    RenderTextureError{})};
    Check(foreign_handle.has_value(), "foreign resident fixture publishes");
    CheckError(exact.Get(RenderTextureHandle{}), RenderTextureErrorCode::InvalidHandle,
        "a default handle is rejected");
    if (foreign_handle)
        CheckError(exact.Get(*foreign_handle), RenderTextureErrorCode::InvalidHandle,
            "a foreign live handle is rejected");

    auto forged = exact_reservation->handle;
    ++forged.generation;
    CheckError(exact.Get(forged), RenderTextureErrorCode::InvalidHandle,
        "a forged generation is rejected");
    forged = exact_reservation->handle;
    forged.slot_index = std::numeric_limits<std::uint32_t>::max();
    CheckError(exact.Get(forged), RenderTextureErrorCode::InvalidHandle,
        "an out-of-range slot is rejected");

    Check(exact.Release(exact_reservation->handle).has_value(),
        "the exact-boundary resident generation releases");
    if (foreign_handle)
        Check(foreign.Release(*foreign_handle).has_value(),
            "the independent foreign resident generation releases");
}

void CheckMoveAndSequentialGenerations()
{
    const std::array<std::byte, 4> first_pixels{std::byte{1}, std::byte{2},
        std::byte{3}, std::byte{4}};
    const std::array<std::byte, 4> second_pixels{std::byte{4}, std::byte{3},
        std::byte{2}, std::byte{1}};
    auto created = RenderTexturePool::Create(Config(1U, 4U));
    Check(created.has_value(), "capacity-one generation pool is created");
    if (!created)
        return;
    RenderTexturePool source = std::move(*created);

    auto initial_reservation = source.Reserve(Upload(first_pixels, 1U, 1U));
    auto initial_handle = initial_reservation ? source.Publish(*initial_reservation)
                                              : std::expected<RenderTextureHandle,
                                                    RenderTextureError>{std::unexpected(
                                                    RenderTextureError{})};
    Check(initial_handle.has_value(), "move fixture publishes before ownership transfer");
    RenderTexturePool pool = std::move(source);
    Check(initial_handle && pool.Get(*initial_handle).has_value(),
        "move construction transfers identity and resident metadata");
    Check(source.Snapshot().slot_capacity == 0U &&
              CheckError(source.Reserve(Upload(first_pixels, 1U, 1U)),
                  RenderTextureErrorCode::InvalidConfiguration,
                  "a moved-from pool rejects new transactions"),
        "moved-from state is inert and aggregate-empty");
    if (!initial_handle)
        return;
    Check(pool.Release(*initial_handle).has_value(),
        "the pre-move resident generation releases through its moved-to pool");

    RenderTextureHandle previous = *initial_handle;
    for (std::uint64_t iteration = 0U; iteration < 128U; ++iteration)
    {
        CheckError(pool.Get(previous), RenderTextureErrorCode::InvalidHandle,
            "every prior capacity-one generation stays stale");
        const auto& pixels = iteration % 2U == 0U ? first_pixels : second_pixels;
        auto reservation = pool.Reserve(Upload(pixels, 1U, 1U));
        Check(reservation && reservation->handle.generation == iteration + 2U,
            "capacity-one reuse advances generation exactly once");
        if (!reservation)
            break;
        auto handle = pool.Publish(*reservation);
        Check(handle && pool.Get(*handle).has_value(),
            "capacity-one transaction publishes before release");
        if (!handle)
            break;
        Check(pool.Release(*handle).has_value(),
            "capacity-one resident generation explicitly releases");
        const auto snapshot = pool.Snapshot();
        Check(snapshot.free_slots == 1U && snapshot.reserved_slots == 0U &&
                  snapshot.resident_slots == 0U && snapshot.retired_slots == 0U &&
                  snapshot.reserved_logical_bytes == 0U &&
                  snapshot.resident_logical_bytes == 0U && IsConsistent(snapshot),
            "every capacity-one iteration restores the exact empty aggregate");
        previous = *handle;
    }
}

void CheckGenerationRetirement()
{
    const std::array<std::byte, 4> rgba{};
    auto release_created = RenderTexturePool::Create(Config(1U, 4U));
    auto rollback_created = RenderTexturePool::Create(Config(1U, 4U));
    Check(release_created && rollback_created, "retirement pools are created");
    if (!release_created || !rollback_created)
        return;
    RenderTexturePool release_pool = std::move(*release_created);
    RenderTexturePool rollback_pool = std::move(*rollback_created);

    Check(omega::runtime::detail::RenderTexturePoolTestAccess::SetFreeGeneration(
              release_pool, 0U, std::numeric_limits<std::uint64_t>::max()),
        "release-retirement fixture reaches the maximum generation");
    auto reservation = release_pool.Reserve(Upload(rgba, 1U, 1U));
    auto handle = reservation ? release_pool.Publish(*reservation)
                              : std::expected<RenderTextureHandle, RenderTextureError>{
                                    std::unexpected(RenderTextureError{})};
    Check(handle && release_pool.Release(*handle),
        "maximum resident generation releases without wrapping");
    const auto released = release_pool.Snapshot();
    Check(released.free_slots == 0U && released.retired_slots == 1U &&
              released.resident_slots == 0U && released.resident_logical_bytes == 0U &&
              IsConsistent(released),
        "maximum released generation retires its slot permanently");
    CheckError(release_pool.Reserve(Upload(rgba, 1U, 1U)),
        RenderTextureErrorCode::SlotCapacityExceeded,
        "retired capacity cannot issue a wrapped handle");
    if (handle)
        CheckError(release_pool.Get(*handle), RenderTextureErrorCode::InvalidHandle,
            "the terminal released handle is stale");

    Check(omega::runtime::detail::RenderTexturePoolTestAccess::SetFreeGeneration(
              rollback_pool, 0U, std::numeric_limits<std::uint64_t>::max()),
        "rollback-retirement fixture reaches the maximum generation");
    auto failed_backend = rollback_pool.Reserve(Upload(rgba, 1U, 1U));
    Check(failed_backend && rollback_pool.Rollback(*failed_backend),
        "maximum unpublished generation rolls back without wrapping");
    const auto rolled_back = rollback_pool.Snapshot();
    Check(rolled_back.free_slots == 0U && rolled_back.retired_slots == 1U &&
              rolled_back.reserved_slots == 0U && rolled_back.reserved_logical_bytes == 0U &&
              IsConsistent(rolled_back),
        "maximum rolled-back generation retires its slot permanently");
}
} // namespace

int main()
{
    CheckContractAndConfiguration();
    CheckImagePreflightAndIdentity();
    CheckTransactionsAndDuplicates();
    CheckResidentBudgetAndHandleValidation();
    CheckMoveAndSequentialGenerations();
    CheckGenerationRetirement();

    if (failures == 0)
        std::cout << "omega_render_texture_pool_tests: all checks passed\n";
    return failures == 0 ? 0 : 1;
}
