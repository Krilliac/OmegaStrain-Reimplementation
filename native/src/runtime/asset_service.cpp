#include "omega/runtime/asset_service.h"

#include "omega/runtime/job_service.h"

#include <condition_variable>
#include <exception>
#include <limits>
#include <mutex>
#include <new>
#include <utility>
#include <vector>

namespace omega::runtime
{
namespace detail
{
struct AssetServiceIdentity
{
};
} // namespace detail

namespace
{
[[nodiscard]] AssetServiceError Error(
    const AssetServiceErrorCode code, const std::string_view message)
{
    return AssetServiceError{
        .code = code,
        .message = std::string(message),
        .level_texture_error = std::nullopt,
    };
}

[[nodiscard]] AssetServiceError StoredFailure(const AssetServiceErrorCode code,
    const std::optional<content::LevelTextureStoreError>& level_texture_error)
{
    switch (code)
    {
    case AssetServiceErrorCode::LoadFailed:
        return AssetServiceError{
            .code = code,
            .message = "level texture load failed",
            .level_texture_error = level_texture_error,
        };
    case AssetServiceErrorCode::ResidentBudgetExceeded:
        return Error(code, "asset resident budget exceeded");
    case AssetServiceErrorCode::WorkerFailed:
        return Error(code, "asset worker failed");
    default:
        return Error(AssetServiceErrorCode::WorkerFailed, "asset worker failed");
    }
}
} // namespace

struct AssetService::Impl
{
    struct Slot
    {
        std::uint64_t generation = 1U;
        bool occupied = false;
        bool retired = false;
        TextureAssetState state = TextureAssetState::Queued;
        std::optional<asset::TextureStorageIR> storage;
        content::LevelTextureOperationUsage load_usage;
        AssetServiceErrorCode failure_code = AssetServiceErrorCode::WorkerFailed;
        std::optional<content::LevelTextureStoreError> level_texture_error;
    };

    Impl(JobService& jobs_value, const content::GameDataService& game_data_value,
        const content::LevelTextureStore& texture_store_value,
        const AssetServiceConfig config_value)
        : jobs(&jobs_value), game_data(&game_data_value), texture_store(&texture_store_value),
          config(config_value), identity(std::make_shared<detail::AssetServiceIdentity>()),
          slots(config.slot_capacity)
    {
        free_slots.reserve(config.slot_capacity);
        for (std::size_t index = config.slot_capacity; index != 0U; --index)
            free_slots.push_back(index - 1U);
    }

    [[nodiscard]] bool RecycleLocked(const std::size_t index)
    {
        Slot& slot = slots[index];
        if (slot.storage)
        {
            const std::uint64_t bytes = slot.load_usage.logical_output_bytes;
            if (bytes > resident_logical_bytes)
                return false;
            resident_logical_bytes -= bytes;
        }

        slot.storage.reset();
        slot.load_usage = {};
        slot.level_texture_error.reset();
        slot.failure_code = AssetServiceErrorCode::WorkerFailed;
        slot.occupied = false;

        if (slot.generation == std::numeric_limits<std::uint64_t>::max())
        {
            slot.retired = true;
            ++retired_slots;
        }
        else
        {
            ++slot.generation;
            free_slots.push_back(index);
        }
        return true;
    }

    void BeginLoad(const std::size_t index, const std::uint64_t generation)
    {
        const std::lock_guard<std::mutex> lock(mutex);
        Slot& slot = slots[index];
        if (!slot.occupied || slot.generation != generation ||
            slot.state != TextureAssetState::Queued)
            std::terminate();
        slot.state = TextureAssetState::Loading;
    }

    void PublishFailure(const std::size_t index, const std::uint64_t generation,
        const AssetServiceErrorCode code,
        std::optional<content::LevelTextureStoreError> level_texture_error = std::nullopt)
    {
        {
            const std::lock_guard<std::mutex> lock(mutex);
            Slot& slot = slots[index];
            if (!slot.occupied || slot.generation != generation ||
                (slot.state != TextureAssetState::Queued &&
                    slot.state != TextureAssetState::Loading))
                std::terminate();

            slot.storage.reset();
            slot.load_usage = {};
            slot.failure_code = code;
            if (level_texture_error && level_texture_error->decode_error)
                level_texture_error->decode_error->byte_offset.reset();
            slot.level_texture_error = std::move(level_texture_error);
            slot.state = TextureAssetState::Failed;
            if (in_flight_requests == 0U)
                std::terminate();
            --in_flight_requests;
        }
        idle.notify_all();
    }

    void PublishLoaded(const std::size_t index, const std::uint64_t generation,
        content::LoadedLevelTexture loaded)
    {
        bool notify = false;
        {
            const std::lock_guard<std::mutex> lock(mutex);
            Slot& slot = slots[index];
            if (!slot.occupied || slot.generation != generation ||
                (slot.state != TextureAssetState::Queued &&
                    slot.state != TextureAssetState::Loading))
                std::terminate();

            const std::uint64_t bytes = loaded.usage.logical_output_bytes;
            if (resident_logical_bytes > config.maximum_resident_logical_bytes ||
                bytes > config.maximum_resident_logical_bytes - resident_logical_bytes)
            {
                slot.failure_code = AssetServiceErrorCode::ResidentBudgetExceeded;
                slot.level_texture_error.reset();
                slot.state = TextureAssetState::Failed;
            }
            else
            {
                slot.storage.emplace(std::move(loaded.storage));
                slot.load_usage = loaded.usage;
                resident_logical_bytes += bytes;
                slot.state = TextureAssetState::Ready;
            }
            if (in_flight_requests == 0U)
                std::terminate();
            --in_flight_requests;
            notify = true;
        }
        if (notify)
            idle.notify_all();
    }

    void PublishWorkerFailureNoexcept(
        const std::size_t index, const std::uint64_t generation) noexcept
    {
        try
        {
            {
                const std::lock_guard<std::mutex> lock(mutex);
                Slot& slot = slots[index];
                if (!slot.occupied || slot.generation != generation ||
                    (slot.state != TextureAssetState::Queued &&
                        slot.state != TextureAssetState::Loading) ||
                    in_flight_requests == 0U)
                    std::terminate();

                if (slot.storage)
                {
                    const std::uint64_t bytes = slot.load_usage.logical_output_bytes;
                    if (bytes > resident_logical_bytes)
                        std::terminate();
                    resident_logical_bytes -= bytes;
                }
                slot.storage.reset();
                slot.load_usage = {};
                slot.level_texture_error.reset();
                slot.failure_code = AssetServiceErrorCode::WorkerFailed;
                slot.state = TextureAssetState::Failed;
                --in_flight_requests;
            }
            idle.notify_all();
        }
        catch (...)
        {
            std::terminate();
        }
    }

    void RunLoad(const std::size_t index, const std::uint64_t generation,
        const content::LevelTextureHandle source) noexcept
    {
        try
        {
            BeginLoad(index, generation);
            auto loaded = texture_store->Load(*game_data, source);
            if (!loaded)
            {
                PublishFailure(index, generation, AssetServiceErrorCode::LoadFailed,
                    std::move(loaded.error()));
                return;
            }
            PublishLoaded(index, generation, std::move(*loaded));
        }
        catch (...)
        {
            PublishWorkerFailureNoexcept(index, generation);
        }
    }

    JobService* jobs = nullptr;
    const content::GameDataService* game_data = nullptr;
    const content::LevelTextureStore* texture_store = nullptr;
    AssetServiceConfig config;
    std::shared_ptr<const detail::AssetServiceIdentity> identity;
    std::vector<Slot> slots;
    std::vector<std::size_t> free_slots;
    mutable std::mutex mutex;
    std::condition_variable idle;
    bool accepting = true;
    std::size_t in_flight_requests = 0U;
    std::size_t retired_slots = 0U;
    std::uint64_t resident_logical_bytes = 0U;
};

std::string_view AssetServiceErrorCodeName(const AssetServiceErrorCode code) noexcept
{
    switch (code)
    {
    case AssetServiceErrorCode::InvalidConfiguration:
        return "invalid-configuration";
    case AssetServiceErrorCode::InvalidDependency:
        return "invalid-dependency";
    case AssetServiceErrorCode::InvalidSourceHandle:
        return "invalid-source-handle";
    case AssetServiceErrorCode::InvalidHandle:
        return "invalid-handle";
    case AssetServiceErrorCode::SlotCapacityExceeded:
        return "slot-capacity-exceeded";
    case AssetServiceErrorCode::InFlightLimitExceeded:
        return "in-flight-limit-exceeded";
    case AssetServiceErrorCode::SubmissionRejected:
        return "submission-rejected";
    case AssetServiceErrorCode::NotReady:
        return "not-ready";
    case AssetServiceErrorCode::Busy:
        return "busy";
    case AssetServiceErrorCode::LoadFailed:
        return "load-failed";
    case AssetServiceErrorCode::ResidentBudgetExceeded:
        return "resident-budget-exceeded";
    case AssetServiceErrorCode::WorkerFailed:
        return "worker-failed";
    case AssetServiceErrorCode::AllocationFailed:
        return "allocation-failed";
    }
    return "unknown";
}

AssetService::AssetService(std::shared_ptr<Impl> impl) noexcept : impl_(std::move(impl))
{
}

std::expected<std::unique_ptr<AssetService>, AssetServiceError> AssetService::Create(
    JobService& jobs, const content::GameDataService& game_data,
    const content::LevelTextureStore& texture_store, const AssetServiceConfig config)
{
    if (config.slot_capacity == 0U ||
        config.slot_capacity > kMaximumAssetServiceSlotCapacity ||
        config.maximum_in_flight_requests == 0U ||
        config.maximum_in_flight_requests > config.slot_capacity ||
        config.maximum_resident_logical_bytes == 0U)
        return std::unexpected(Error(AssetServiceErrorCode::InvalidConfiguration,
            "asset service configuration is invalid"));
    if (jobs.worker_count() == 0U)
        return std::unexpected(Error(
            AssetServiceErrorCode::InvalidDependency, "asset job service is unavailable"));

    try
    {
        auto impl = std::make_shared<Impl>(jobs, game_data, texture_store, config);
        return std::unique_ptr<AssetService>(new AssetService(std::move(impl)));
    }
    catch (...)
    {
        return std::unexpected(Error(
            AssetServiceErrorCode::AllocationFailed, "asset service allocation failed"));
    }
}

AssetService::~AssetService()
{
    if (!impl_)
        return;
    std::unique_lock<std::mutex> lock(impl_->mutex);
    impl_->accepting = false;
    impl_->idle.wait(lock, [this] { return impl_->in_flight_requests == 0U; });
    // A worker-owned shared Impl may outlive this destructor for the final callable return. The
    // public service identity must not: handles expire deterministically at the service boundary.
    impl_->identity.reset();
}

std::expected<TextureAssetHandle, AssetServiceError> AssetService::Request(
    const content::LevelTextureHandle& source)
{
    if (source.expired())
        return std::unexpected(Error(
            AssetServiceErrorCode::InvalidSourceHandle, "source texture handle is unavailable"));

    std::size_t index = 0U;
    std::uint64_t generation = 0U;
    {
        const std::lock_guard<std::mutex> lock(impl_->mutex);
        if (!impl_->accepting)
            return std::unexpected(Error(
                AssetServiceErrorCode::InvalidDependency, "asset service is unavailable"));
        if (impl_->free_slots.empty())
            return std::unexpected(Error(
                AssetServiceErrorCode::SlotCapacityExceeded, "asset slot capacity is exhausted"));
        if (impl_->in_flight_requests >= impl_->config.maximum_in_flight_requests)
            return std::unexpected(Error(AssetServiceErrorCode::InFlightLimitExceeded,
                "asset in-flight request limit is exhausted"));

        index = impl_->free_slots.back();
        impl_->free_slots.pop_back();
        Impl::Slot& slot = impl_->slots[index];
        slot.occupied = true;
        slot.state = TextureAssetState::Queued;
        slot.storage.reset();
        slot.load_usage = {};
        slot.failure_code = AssetServiceErrorCode::WorkerFailed;
        slot.level_texture_error.reset();
        generation = slot.generation;
        ++impl_->in_flight_requests;
    }

    bool accepted = false;
    try
    {
        auto submitted = impl_->jobs->Submit(
            [implementation = impl_, index, generation, source]() mutable
            { implementation->RunLoad(index, generation, std::move(source)); });
        accepted = submitted.has_value();
    }
    catch (...)
    {
        accepted = false;
    }

    if (!accepted)
    {
        const std::lock_guard<std::mutex> lock(impl_->mutex);
        if (impl_->in_flight_requests == 0U)
            std::terminate();
        --impl_->in_flight_requests;
        if (!impl_->RecycleLocked(index))
            return std::unexpected(Error(
                AssetServiceErrorCode::WorkerFailed, "asset slot rollback failed"));
        impl_->idle.notify_all();
        return std::unexpected(Error(
            AssetServiceErrorCode::SubmissionRejected, "asset job submission was rejected"));
    }

    return TextureAssetHandle(impl_->identity, index, generation);
}

std::expected<TextureAssetState, AssetServiceError> AssetService::State(
    const TextureAssetHandle& handle) const
{
    const std::lock_guard<std::mutex> lock(impl_->mutex);
    const auto identity = handle.service_identity_.lock();
    if (!identity || identity != impl_->identity || handle.slot_index_ >= impl_->slots.size())
        return std::unexpected(
            Error(AssetServiceErrorCode::InvalidHandle, "asset handle is invalid"));
    const Impl::Slot& slot = impl_->slots[handle.slot_index_];
    if (!slot.occupied || slot.retired || slot.generation != handle.generation_)
        return std::unexpected(
            Error(AssetServiceErrorCode::InvalidHandle, "asset handle is invalid"));
    return slot.state;
}

std::expected<TextureAssetView, AssetServiceError> AssetService::Get(
    const TextureAssetHandle& handle) const
{
    const std::lock_guard<std::mutex> lock(impl_->mutex);
    const auto identity = handle.service_identity_.lock();
    if (!identity || identity != impl_->identity || handle.slot_index_ >= impl_->slots.size())
        return std::unexpected(
            Error(AssetServiceErrorCode::InvalidHandle, "asset handle is invalid"));
    const Impl::Slot& slot = impl_->slots[handle.slot_index_];
    if (!slot.occupied || slot.retired || slot.generation != handle.generation_)
        return std::unexpected(
            Error(AssetServiceErrorCode::InvalidHandle, "asset handle is invalid"));
    if (slot.state == TextureAssetState::Failed)
        return std::unexpected(StoredFailure(slot.failure_code, slot.level_texture_error));
    if (slot.state != TextureAssetState::Ready || !slot.storage)
        return std::unexpected(
            Error(AssetServiceErrorCode::NotReady, "asset texture is not ready"));
    return TextureAssetView{
        .storage = std::cref(*slot.storage),
        .load_usage = slot.load_usage,
    };
}

std::expected<void, AssetServiceError> AssetService::Release(
    const TextureAssetHandle& handle)
{
    const std::lock_guard<std::mutex> lock(impl_->mutex);
    const auto identity = handle.service_identity_.lock();
    if (!identity || identity != impl_->identity || handle.slot_index_ >= impl_->slots.size())
        return std::unexpected(
            Error(AssetServiceErrorCode::InvalidHandle, "asset handle is invalid"));
    const Impl::Slot& slot = impl_->slots[handle.slot_index_];
    if (!slot.occupied || slot.retired || slot.generation != handle.generation_)
        return std::unexpected(
            Error(AssetServiceErrorCode::InvalidHandle, "asset handle is invalid"));
    if (slot.state == TextureAssetState::Queued || slot.state == TextureAssetState::Loading)
        return std::unexpected(
            Error(AssetServiceErrorCode::Busy, "asset texture request is still in flight"));
    if (!impl_->RecycleLocked(handle.slot_index_))
        return std::unexpected(
            Error(AssetServiceErrorCode::WorkerFailed, "asset resident accounting failed"));
    return {};
}

void AssetService::WaitForIdle()
{
    std::unique_lock<std::mutex> lock(impl_->mutex);
    impl_->idle.wait(lock, [this] { return impl_->in_flight_requests == 0U; });
}

AssetServiceSnapshot AssetService::Snapshot() const
{
    const std::lock_guard<std::mutex> lock(impl_->mutex);
    AssetServiceSnapshot result{
        .slot_capacity = impl_->slots.size(),
        .free_slots = impl_->free_slots.size(),
        .active_slots = 0U,
        .retired_slots = impl_->retired_slots,
        .queued = 0U,
        .loading = 0U,
        .ready = 0U,
        .failed = 0U,
        .in_flight_requests = impl_->in_flight_requests,
        .resident_logical_bytes = impl_->resident_logical_bytes,
    };
    for (const Impl::Slot& slot : impl_->slots)
    {
        if (!slot.occupied)
            continue;
        ++result.active_slots;
        switch (slot.state)
        {
        case TextureAssetState::Queued:
            ++result.queued;
            break;
        case TextureAssetState::Loading:
            ++result.loading;
            break;
        case TextureAssetState::Ready:
            ++result.ready;
            break;
        case TextureAssetState::Failed:
            ++result.failed;
            break;
        }
    }
    return result;
}
} // namespace omega::runtime
