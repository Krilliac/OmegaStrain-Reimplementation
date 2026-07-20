#pragma once

#include "omega/content/level_texture_store.h"
#include "omega/runtime/asset_service.h"
#include "omega/runtime/debug_image.h"
#include "omega/runtime/texture_storage_topology_debug_image.h"

#include <cstdint>
#include <expected>
#include <optional>
#include <string_view>

namespace omega::runtime
{
enum class LevelTextureTopologyPreviewErrorCode : std::uint8_t
{
    AssetServiceNotEmpty = 0U,
    EmptyTextureInventory,
    SourceHandleFailed,
    AssetRequestFailed,
    AssetGetFailed,
    ImageBuildFailed,
    AssetReleaseFailed,
    ResidualAssetState,
};

struct LevelTextureTopologyPreviewError
{
    LevelTextureTopologyPreviewErrorCode code;
    // Fixed category text only. Never includes paths, names, locators, payloads, hashes, offsets,
    // or nested diagnostic text.
    std::string_view message;
    std::optional<content::LevelTextureStoreErrorCode> texture_store_error_code;
    std::optional<AssetServiceErrorCode> asset_error_code;
    std::optional<TextureStorageTopologyDebugImageErrorCode> image_error_code;
};

// [game/main thread, startup; exclusive AssetService access] Blocking and non-hot-reloadable.
// Builds an independently owned topology image from canonical texture handle zero, then verifies
// exact aggregate AssetService snapshot restoration before returning success.
[[nodiscard]] std::expected<DebugImage, LevelTextureTopologyPreviewError>
BuildFirstLevelTextureTopologyPreview(AssetService& assets,
    const content::LevelTextureStore& texture_store,
    const TextureStorageTopologyDebugImageLimits& limits = {});
} // namespace omega::runtime
