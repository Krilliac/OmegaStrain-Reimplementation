#include "omega/runtime/content_startup_diagnostic.h"

namespace omega::runtime
{
std::expected<ContentStartupDiagnosticView, ContentStartupDiagnosticErrorCode>
DescribeContentStartupError(const ContentStartupError& error) noexcept
{
    if (error.message.empty())
        return std::unexpected(ContentStartupDiagnosticErrorCode::InconsistentRepresentation);

    const bool has_game_data_error = error.game_data_error.has_value();
    const bool has_level_texture_error = error.level_texture_error.has_value();

    switch (error.code)
    {
    case ContentStartupErrorCode::InvalidOptions:
    case ContentStartupErrorCode::DebugImage:
        if (has_game_data_error || has_level_texture_error)
            break;
        return ContentStartupDiagnosticView{
            .category = ContentStartupErrorCodeName(error.code),
            .message = error.message,
        };
    case ContentStartupErrorCode::GameData:
    {
        if (!has_game_data_error || has_level_texture_error)
            break;
        const std::string_view category =
            content::GameDataErrorCodeName(error.game_data_error->code);
        if (category == "unknown")
            break;
        return ContentStartupDiagnosticView{
            .category = category,
            .message = error.message,
        };
    }
    case ContentStartupErrorCode::LevelTextures:
    {
        if (has_game_data_error || !has_level_texture_error)
            break;
        const std::string_view category =
            content::LevelTextureStoreErrorCodeName(error.level_texture_error->code);
        if (category == "unknown")
            break;
        return ContentStartupDiagnosticView{
            .category = category,
            .message = error.message,
        };
    }
    }

    return std::unexpected(ContentStartupDiagnosticErrorCode::InconsistentRepresentation);
}
} // namespace omega::runtime
