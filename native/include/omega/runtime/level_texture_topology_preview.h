#pragma once

#include "omega/content/level_texture_store.h"
#include "omega/runtime/asset_service.h"
#include "omega/runtime/debug_image.h"
#include "omega/runtime/packed24_transfer_debug_image.h"
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

struct LevelTextureDiagnosticPreviewLimits
{
    TextureStorageTopologyDebugImageLimits topology;
    Packed24TransferDebugImageLimits packed24_transfer;
};

// Every populated image is independently owned. Builder results always contain the topology
// image. Every Packed24 diagnostic failure is intentionally nonfatal, including invalid or
// unsupported storage, configured byte limits, arithmetic bounds, and allocation failure: successful
// builder results populate exactly one of packed24_transfer_image and packed24_transfer_error_code,
// so callers can retain the topology-only path without reloading while still observing the category.
// The optional image retains source-slot order with a synthetic fourth slot; it assigns no channel,
// row-order, swizzle, color-space, alpha, material, or display-correctness semantics.
struct LevelTextureDiagnosticPreview
{
    DebugImage topology_image;
    std::optional<DebugImage> packed24_transfer_image;
    std::optional<Packed24TransferDebugImageErrorCode> packed24_transfer_error_code;
};

// [game/main thread, startup; exclusive AssetService access] Blocking and non-hot-reloadable.
// Builds an independently owned topology image from canonical texture handle zero, then verifies
// exact aggregate AssetService snapshot restoration before returning success.
[[nodiscard]] std::expected<DebugImage, LevelTextureTopologyPreviewError>
BuildFirstLevelTextureTopologyPreview(AssetService& assets,
    const content::LevelTextureStore& texture_store,
    const TextureStorageTopologyDebugImageLimits& limits = {});

// [game/main thread, startup; exclusive AssetService access] Blocking and non-hot-reloadable.
// Uses one canonical handle-zero request to build the mandatory metadata topology image and attempt
// the strict payload-sensitive Packed24 diagnostic. Both results outlive the released source, and
// exact aggregate AssetService snapshot restoration is required before success is returned.
[[nodiscard]] std::expected<LevelTextureDiagnosticPreview,
    LevelTextureTopologyPreviewError>
BuildFirstLevelTextureDiagnosticPreview(AssetService& assets,
    const content::LevelTextureStore& texture_store,
    const LevelTextureDiagnosticPreviewLimits& limits = {});
} // namespace omega::runtime
