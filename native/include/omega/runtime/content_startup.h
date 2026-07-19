#pragma once

#include "omega/asset/level_content_ir.h"
#include "omega/asset/level_ir.h"
#include "omega/content/game_data_service.h"
#include "omega/content/level_texture_store.h"
#include "omega/runtime/debug_image.h"
#include "omega/runtime/launch_options.h"

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>

namespace omega::runtime
{
enum class ContentStartupStage : std::uint8_t
{
    NoContent = 0U,
    DataMounted = 1U,
    LevelContent = 2U,
};

enum class ContentStartupStateErrorCode : std::uint8_t
{
    InconsistentOwnership = 0U,
};

enum class ContentStartupErrorCode
{
    InvalidOptions,
    GameData,
    LevelTextures,
    DebugImage,
};

[[nodiscard]] std::string_view ContentStartupErrorCodeName(
    ContentStartupErrorCode code) noexcept;

struct ContentStartupError
{
    ContentStartupErrorCode code = ContentStartupErrorCode::InvalidOptions;
    std::string message;
    std::optional<content::GameDataError> game_data_error;
    std::optional<content::LevelTextureStoreError> level_texture_error;
};

struct ContentStartupState
{
    std::optional<content::GameDataService> game_data;
    // Declared after the bound service so reverse destruction releases the store first.
    std::optional<content::LevelTextureStore> level_texture_store;
    std::optional<asset::LevelManifestIR> level_manifest;
    // Keeps the loader-established spatial and material collections present and owned together.
    std::optional<asset::LevelContentIR> level_content;
    std::optional<DebugImage> debug_image;
};

// [any thread; reentrant] Classifies only the three complete ownership shapes published by
// StartContent. The check performs no allocation and rejects every partial or mixed state.
[[nodiscard]] std::expected<ContentStartupStage, ContentStartupStateErrorCode>
ClassifyContentStartupState(const ContentStartupState& state) noexcept;

// [game thread, startup] Owns the validated data service, canonical level state, and optional
// texture-locator inventory. SDL and GPU initialization deliberately occur after this succeeds.
[[nodiscard]] std::expected<ContentStartupState, ContentStartupError> StartContent(
    const LaunchOptions& options);
} // namespace omega::runtime
