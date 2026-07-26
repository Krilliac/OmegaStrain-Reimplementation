#include "front_end_screen_survey_commands.h"

#include "front_end_screen_survey_report.h"

#include "omega/content/game_data_service.h"

#include <iostream>
#include <optional>

namespace omega::tool
{
int FrontEndScreenSurvey(const std::filesystem::path& root)
{
    // GameDataService holds the owner path for this command's lifetime so it
    // can mount and read the source. This command does not serialize, persist,
    // or copy the path or decoded inputs into project storage.
    auto service = content::GameDataService::Open(content::GameDataServiceConfig{.root = root});
    if (!service)
        return WriteFrontEndScreenSurveyOpenFailure(std::cerr, service.error());

    return RunFrontEndScreenSurveyProbes(
        std::cout, [&service](const content::FrontEndScreenKey key) -> FrontEndScreenProbeResult {
            auto bundle = service->LoadFrontEndScreen(key);
            if (!bundle)
                return std::unexpected(bundle.error());

            if (key == content::FrontEndScreenKey::Title)
            {
                return std::optional<content::FrontEndButtonSurvey>{
                    content::SurveyFrontEndVisibleButtons(bundle->widget_document().root),
                };
            }
            return std::optional<content::FrontEndButtonSurvey>{};
        });
}
} // namespace omega::tool
