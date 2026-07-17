#include "omega/runtime/content_startup.h"

#include <utility>

namespace omega::runtime
{
std::string_view ContentStartupErrorCodeName(const ContentStartupErrorCode code) noexcept
{
    switch (code)
    {
    case ContentStartupErrorCode::InvalidOptions:
        return "invalid-options";
    case ContentStartupErrorCode::GameData:
        return "game-data";
    case ContentStartupErrorCode::DebugImage:
        return "debug-image";
    }
    return "unknown";
}

std::expected<ContentStartupState, ContentStartupError> StartContent(
    const LaunchOptions& options)
{
    if ((options.level_code || options.probe_only) && !options.data_root)
    {
        return std::unexpected(ContentStartupError{
            .code = ContentStartupErrorCode::InvalidOptions,
            .message = "content startup requires a data root",
            .game_data_error = std::nullopt,
        });
    }

    ContentStartupState state;
    if (!options.data_root)
        return state;

    auto opened = content::GameDataService::Open({.root = *options.data_root});
    if (!opened)
    {
        return std::unexpected(ContentStartupError{
            .code = ContentStartupErrorCode::GameData,
            .message = opened.error().message,
            .game_data_error = std::move(opened.error()),
        });
    }
    state.game_data = std::move(*opened);

    if (!options.level_code)
        return state;
    auto loaded = state.game_data->LoadLevelManifest(*options.level_code);
    if (!loaded)
    {
        return std::unexpected(ContentStartupError{
            .code = ContentStartupErrorCode::GameData,
            .message = loaded.error().message,
            .game_data_error = std::move(loaded.error()),
        });
    }
    state.level_manifest = std::move(*loaded);

    auto spatial = state.game_data->LoadLevelSpatial(*state.level_manifest);
    if (!spatial)
    {
        return std::unexpected(ContentStartupError{
            .code = ContentStartupErrorCode::GameData,
            .message = spatial.error().message,
            .game_data_error = std::move(spatial.error()),
        });
    }
    state.level_spatial = std::move(*spatial);

    auto built_image = BuildManifestDebugImage(*state.level_manifest);
    if (!built_image)
    {
        return std::unexpected(ContentStartupError{
            .code = ContentStartupErrorCode::DebugImage,
            .message = std::move(built_image.error()),
            .game_data_error = std::nullopt,
        });
    }
    state.debug_image = std::move(*built_image);
    return state;
}
} // namespace omega::runtime
