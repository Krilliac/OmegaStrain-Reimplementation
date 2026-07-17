#pragma once

#include "omega/asset/level_ir.h"
#include "omega/asset/level_spatial_ir.h"
#include "omega/content/game_data_service.h"
#include "omega/runtime/launch_options.h"
#include "omega/runtime/manifest_debug_image.h"

#include <expected>
#include <optional>
#include <string>
#include <string_view>

namespace omega::runtime
{
enum class ContentStartupErrorCode
{
    InvalidOptions,
    GameData,
    DebugImage,
};

[[nodiscard]] std::string_view ContentStartupErrorCodeName(
    ContentStartupErrorCode code) noexcept;

struct ContentStartupError
{
    ContentStartupErrorCode code = ContentStartupErrorCode::InvalidOptions;
    std::string message;
    std::optional<content::GameDataError> game_data_error;
};

struct ContentStartupState
{
    std::optional<content::GameDataService> game_data;
    std::optional<asset::LevelManifestIR> level_manifest;
    std::optional<asset::LevelSpatialIR> level_spatial;
    std::optional<ManifestDebugImage> debug_image;
};

// [game thread, startup] Owns the validated data service and canonical level state. SDL and GPU
// initialization deliberately occur after this function succeeds.
[[nodiscard]] std::expected<ContentStartupState, ContentStartupError> StartContent(
    const LaunchOptions& options);
} // namespace omega::runtime
