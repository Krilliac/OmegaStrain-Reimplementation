#pragma once

#include "omega/content/level_texture_store.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace omega::runtime
{
class JobService;

namespace detail
{
// Opaque process-local service identity. Handles observe it weakly and never retain the service,
// source store, source locator, or decoded asset.
struct AssetServiceIdentity;
} // namespace detail

inline constexpr std::size_t kMaximumAssetServiceSlotCapacity = 8192U;

struct AssetServiceConfig
{
    // Internal native-runtime policy, not a retail limit or user-facing setting.
    std::size_t slot_capacity = 64U;
    std::size_t maximum_in_flight_requests = 64U;
    // Logical owned output only; not allocator capacity, process RSS, GPU memory, or decoder
    // scratch/input residency.
    std::uint64_t maximum_resident_logical_bytes = 64ULL * 1024ULL * 1024ULL;
};

enum class TextureAssetState : std::uint8_t
{
    Queued,
    Loading,
    Ready,
    Failed,
};

enum class AssetServiceErrorCode
{
    InvalidConfiguration,
    InvalidDependency,
    InvalidSourceHandle,
    InvalidHandle,
    SlotCapacityExceeded,
    InFlightLimitExceeded,
    SubmissionRejected,
    NotReady,
    Busy,
    LoadFailed,
    ResidentBudgetExceeded,
    WorkerFailed,
    AllocationFailed,
};

[[nodiscard]] std::string_view AssetServiceErrorCodeName(
    AssetServiceErrorCode code) noexcept;

struct AssetServiceError
{
    AssetServiceErrorCode code = AssetServiceErrorCode::InvalidConfiguration;
    // Fixed category/stage text only. Never include paths, names, locators, hashes, offsets,
    // payloads, transformed keys, or other source identity.
    std::string message;
    std::optional<content::LevelTextureStoreError> level_texture_error;
};

class AssetService;

class TextureAssetHandle final
{
public:
    TextureAssetHandle() noexcept = default;

    // [any thread; thread-safe] Reports service-token lifetime only. State/Get/Release remain the
    // authority for slot membership and generation validation.
    [[nodiscard]] bool expired() const noexcept { return service_identity_.expired(); }

private:
    friend class AssetService;

    TextureAssetHandle(std::weak_ptr<const detail::AssetServiceIdentity> service_identity,
        std::size_t slot_index, std::uint64_t generation) noexcept
        : service_identity_(std::move(service_identity)), slot_index_(slot_index),
          generation_(generation)
    {
    }

    std::weak_ptr<const detail::AssetServiceIdentity> service_identity_;
    std::size_t slot_index_ = 0U;
    std::uint64_t generation_ = 0U;
};

struct TextureAssetView
{
    std::reference_wrapper<const asset::TextureStorageIR> storage;
    content::LevelTextureOperationUsage load_usage;
};

struct AssetServiceSnapshot
{
    std::size_t slot_capacity = 0U;
    std::size_t free_slots = 0U;
    std::size_t active_slots = 0U;
    std::size_t retired_slots = 0U;
    std::size_t queued = 0U;
    std::size_t loading = 0U;
    std::size_t ready = 0U;
    std::size_t failed = 0U;
    std::size_t in_flight_requests = 0U;
    std::uint64_t resident_logical_bytes = 0U;
};

// Non-hot-reloadable texture-storage cache. It schedules only LevelTextureStore::Load and
// publishes independently owned immutable TextureStorageIR values. It performs no catalog-name or
// material lookup, alias resolution, display expansion, GPU upload, placement, visibility, or draw
// work. All public methods except expired() are game-thread APIs; worker jobs use the internal
// synchronization boundary.
class AssetService final
{
public:
    // Dependencies are non-owning and must remain at stable addresses until after this service is
    // destroyed. Configuration is validated and the complete fixed slot pool is allocated before
    // publication.
    [[nodiscard]] static std::expected<std::unique_ptr<AssetService>, AssetServiceError>
    Create(JobService& jobs, const content::GameDataService& game_data,
        const content::LevelTextureStore& texture_store, AssetServiceConfig config = {});

    // Stops new requests and waits only for this service's accepted jobs before releasing storage.
    // It never invokes JobService::WaitForIdle().
    ~AssetService();
    AssetService(const AssetService&) = delete;
    AssetService& operator=(const AssetService&) = delete;
    AssetService(AssetService&&) = delete;
    AssetService& operator=(AssetService&&) = delete;

    [[nodiscard]] std::expected<TextureAssetHandle, AssetServiceError> Request(
        const content::LevelTextureHandle& source);
    [[nodiscard]] std::expected<TextureAssetState, AssetServiceError> State(
        const TextureAssetHandle& handle) const;
    // The view remains valid until its handle is successfully released or this service is
    // destroyed. Callers must not retain the reference across either event.
    [[nodiscard]] std::expected<TextureAssetView, AssetServiceError> Get(
        const TextureAssetHandle& handle) const;
    // Queued/Loading slots are busy and cannot be cancelled in v0. Ready and Failed slots recycle
    // explicitly; recycling increments the generation so every prior handle stays stale.
    [[nodiscard]] std::expected<void, AssetServiceError> Release(
        const TextureAssetHandle& handle);

    void WaitForIdle();
    [[nodiscard]] AssetServiceSnapshot Snapshot() const;

private:
    struct Impl;
    explicit AssetService(std::shared_ptr<Impl> impl) noexcept;
    // Accepted jobs capture this ownership independently. Destruction stops requests and waits for
    // publication, while the shared implementation lifetime also covers the final worker return.
    std::shared_ptr<Impl> impl_;
};
} // namespace omega::runtime
