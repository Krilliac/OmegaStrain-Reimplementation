#include "omega/runtime/level_texture_topology_preview.h"

#include <utility>

namespace omega::runtime
{
namespace
{
[[nodiscard]] constexpr std::string_view ErrorMessage(
    const LevelTextureTopologyPreviewErrorCode code) noexcept
{
    switch (code)
    {
    case LevelTextureTopologyPreviewErrorCode::AssetServiceNotEmpty:
        return "level texture topology preview requires an empty asset service";
    case LevelTextureTopologyPreviewErrorCode::EmptyTextureInventory:
        return "level texture topology preview requires at least one texture";
    case LevelTextureTopologyPreviewErrorCode::SourceHandleFailed:
        return "level texture topology preview source handle failed";
    case LevelTextureTopologyPreviewErrorCode::AssetRequestFailed:
        return "level texture topology preview asset request failed";
    case LevelTextureTopologyPreviewErrorCode::AssetGetFailed:
        return "level texture topology preview asset get failed";
    case LevelTextureTopologyPreviewErrorCode::ImageBuildFailed:
        return "level texture topology preview image build failed";
    case LevelTextureTopologyPreviewErrorCode::AssetReleaseFailed:
        return "level texture topology preview asset release failed";
    case LevelTextureTopologyPreviewErrorCode::ResidualAssetState:
        return "level texture topology preview left residual asset state";
    }
    return "level texture topology preview error is unknown";
}

[[nodiscard]] constexpr LevelTextureTopologyPreviewError MakeError(
    const LevelTextureTopologyPreviewErrorCode code,
    const std::optional<content::LevelTextureStoreErrorCode> texture_store_error_code =
        std::nullopt,
    const std::optional<AssetServiceErrorCode> asset_error_code = std::nullopt,
    const std::optional<TextureStorageTopologyDebugImageErrorCode> image_error_code =
        std::nullopt) noexcept
{
    return LevelTextureTopologyPreviewError{
        .code = code,
        .message = ErrorMessage(code),
        .texture_store_error_code = texture_store_error_code,
        .asset_error_code = asset_error_code,
        .image_error_code = image_error_code,
    };
}

[[nodiscard]] constexpr bool SnapshotsEqual(
    const AssetServiceSnapshot& left, const AssetServiceSnapshot& right) noexcept
{
    return left.slot_capacity == right.slot_capacity &&
           left.free_slots == right.free_slots &&
           left.active_slots == right.active_slots &&
           left.retired_slots == right.retired_slots && left.queued == right.queued &&
           left.loading == right.loading && left.ready == right.ready &&
           left.failed == right.failed &&
           left.in_flight_requests == right.in_flight_requests &&
           left.resident_logical_bytes == right.resident_logical_bytes;
}

[[nodiscard]] constexpr bool IsAggregateEmpty(
    const AssetServiceSnapshot& snapshot) noexcept
{
    return snapshot.free_slots == snapshot.slot_capacity && snapshot.active_slots == 0U &&
           snapshot.retired_slots == 0U && snapshot.queued == 0U &&
           snapshot.loading == 0U && snapshot.ready == 0U && snapshot.failed == 0U &&
           snapshot.in_flight_requests == 0U &&
           snapshot.resident_logical_bytes == 0U;
}

template <typename Result, typename Builder>
[[nodiscard]] std::expected<Result, LevelTextureTopologyPreviewError>
BuildFirstLevelTexturePreviewTransaction(AssetService& assets,
    const content::LevelTextureStore& texture_store, Builder builder)
{
    const AssetServiceSnapshot entry_snapshot = assets.Snapshot();
    if (!IsAggregateEmpty(entry_snapshot))
    {
        return std::unexpected(
            MakeError(LevelTextureTopologyPreviewErrorCode::AssetServiceNotEmpty));
    }
    if (texture_store.size() == 0U)
    {
        return std::unexpected(
            MakeError(LevelTextureTopologyPreviewErrorCode::EmptyTextureInventory));
    }

    auto source_handle = texture_store.HandleAt(0U);
    if (!source_handle)
    {
        return std::unexpected(MakeError(
            LevelTextureTopologyPreviewErrorCode::SourceHandleFailed,
            source_handle.error().code));
    }

    auto requested = assets.Request(*source_handle);
    if (!requested)
    {
        if (!SnapshotsEqual(entry_snapshot, assets.Snapshot()))
        {
            return std::unexpected(
                MakeError(LevelTextureTopologyPreviewErrorCode::ResidualAssetState));
        }
        return std::unexpected(MakeError(
            LevelTextureTopologyPreviewErrorCode::AssetRequestFailed, std::nullopt,
            requested.error().code));
    }

    assets.WaitForIdle();
    auto preview = [&]() -> std::expected<Result, LevelTextureTopologyPreviewError> {
        auto loaded = assets.Get(*requested);
        if (!loaded)
        {
            std::optional<content::LevelTextureStoreErrorCode> texture_store_error_code;
            if (loaded.error().level_texture_error)
                texture_store_error_code = loaded.error().level_texture_error->code;
            return std::unexpected(MakeError(
                LevelTextureTopologyPreviewErrorCode::AssetGetFailed,
                texture_store_error_code, loaded.error().code));
        }
        return builder(loaded->storage.get());
    }();

    auto released = assets.Release(*requested);
    const AssetServiceSnapshot final_snapshot = assets.Snapshot();
    if (!released)
    {
        return std::unexpected(MakeError(
            LevelTextureTopologyPreviewErrorCode::AssetReleaseFailed, std::nullopt,
            released.error().code));
    }
    if (!SnapshotsEqual(entry_snapshot, final_snapshot))
    {
        return std::unexpected(
            MakeError(LevelTextureTopologyPreviewErrorCode::ResidualAssetState));
    }
    if (!preview)
        return std::unexpected(preview.error());
    return std::move(*preview);
}
} // namespace

std::expected<DebugImage, LevelTextureTopologyPreviewError>
BuildFirstLevelTextureTopologyPreview(AssetService& assets,
    const content::LevelTextureStore& texture_store,
    const TextureStorageTopologyDebugImageLimits& limits)
{
    return BuildFirstLevelTexturePreviewTransaction<DebugImage>(
        assets, texture_store,
        [&limits](const asset::TextureStorageIR& storage)
            -> std::expected<DebugImage, LevelTextureTopologyPreviewError> {
        auto built = BuildTextureStorageTopologyDebugImage(
            storage, limits);
        if (!built)
        {
            return std::unexpected(MakeError(
                LevelTextureTopologyPreviewErrorCode::ImageBuildFailed, std::nullopt,
                std::nullopt, built.error().code));
        }
        return std::move(*built);
    });
}

std::expected<LevelTextureDiagnosticPreview, LevelTextureTopologyPreviewError>
BuildFirstLevelTextureDiagnosticPreview(AssetService& assets,
    const content::LevelTextureStore& texture_store,
    const LevelTextureDiagnosticPreviewLimits& limits)
{
    return BuildFirstLevelTexturePreviewTransaction<LevelTextureDiagnosticPreview>(
        assets, texture_store,
        [&limits](const asset::TextureStorageIR& storage)
            -> std::expected<LevelTextureDiagnosticPreview,
                LevelTextureTopologyPreviewError> {
        auto topology = BuildTextureStorageTopologyDebugImage(storage, limits.topology);
        if (!topology)
        {
            return std::unexpected(MakeError(
                LevelTextureTopologyPreviewErrorCode::ImageBuildFailed, std::nullopt,
                std::nullopt, topology.error().code));
        }

        LevelTextureDiagnosticPreview preview{
            .topology_image = std::move(*topology),
        };
        auto packed24 = BuildPacked24TransferDebugImage(
            storage, limits.packed24_transfer);
        if (packed24)
            preview.packed24_transfer_image.emplace(std::move(*packed24));
        else
            preview.packed24_transfer_error_code = packed24.error().code;
        return preview;
    });
}
} // namespace omega::runtime
