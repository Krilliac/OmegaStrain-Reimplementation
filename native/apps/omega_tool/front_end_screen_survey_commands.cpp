#include "front_end_screen_survey_commands.h"

#include "omega/content/front_end_screen_survey.h"
#include "omega/content/game_data_service.h"

#include <array>
#include <cstddef>
#include <exception>
#include <iostream>
#include <string_view>

namespace omega::tool
{
namespace
{
constexpr std::string_view kSchema = "omega-front-end-screen-survey-v1";

// One observation per declared key. `error` carries only the repository's own
// GameDataErrorCode NAME. The error's `message` field is deliberately never
// emitted: its own contract allows it to name validated project-relative
// identifiers, and this report's contract is that it names nothing at all.
struct ScreenObservation final
{
    content::FrontEndScreenKey key = content::FrontEndScreenKey::Title;
    bool loaded = false;
    std::string_view error;
};

void PrintBool(const bool value)
{
    std::cout << (value ? "true" : "false");
}
} // namespace

int FrontEndScreenSurvey(const std::filesystem::path& root)
{
    // The path is used to open the root and is never echoed: an error message
    // that quotes the path would put an owner filesystem location into a report
    // meant to be shareable.
    auto service = content::GameDataService::Open(
        content::GameDataServiceConfig{.root = root});
    if (!service)
    {
        std::cerr << "front-end screen survey: game data root unavailable ["
                  << content::GameDataErrorCodeName(service.error().code) << "]\n";
        return 1;
    }

    std::array<ScreenObservation, content::kAllFrontEndScreenKeys.size()>
        observations{};
    content::FrontEndButtonSurvey title_survey;
    bool title_observed = false;

    for (std::size_t index = 0U; index < content::kAllFrontEndScreenKeys.size();
         ++index)
    {
        const content::FrontEndScreenKey key =
            content::kAllFrontEndScreenKeys[index];
        observations[index].key = key;

        // LoadFrontEndScreen is not noexcept. A decoder or allocation throw is an
        // observation about that screen, not a reason to abandon the remaining
        // ones, so it is contained per screen and reported as a decode failure.
        try
        {
            auto bundle = service->LoadFrontEndScreen(key);
            if (!bundle)
            {
                observations[index].error =
                    content::GameDataErrorCodeName(bundle.error().code);
                continue;
            }
            observations[index].loaded = true;
            if (key == content::FrontEndScreenKey::Title)
            {
                title_survey = content::SurveyFrontEndVisibleButtons(
                    bundle->widget_document().root);
                title_observed = true;
            }
        }
        catch (const std::exception&)
        {
            observations[index].error =
                content::GameDataErrorCodeName(content::GameDataErrorCode::DecodeFailed);
        }
        catch (...)
        {
            observations[index].error =
                content::GameDataErrorCodeName(content::GameDataErrorCode::DecodeFailed);
        }
    }

    std::cout << "{\"schema\":\"" << kSchema << "\",\"screens\":[";
    for (std::size_t index = 0U; index < observations.size(); ++index)
    {
        if (index != 0U)
            std::cout << ',';
        const auto& observation = observations[index];
        std::cout << "{\"key\":\""
                  << content::FrontEndScreenSurveyKeyName(observation.key)
                  << "\",\"loaded\":";
        PrintBool(observation.loaded);
        std::cout << ",\"error\":";
        if (observation.error.empty())
            std::cout << "null";
        else
            std::cout << '"' << observation.error << '"';
        std::cout << '}';
    }

    std::cout << "],\"title_buttons\":{\"observed\":";
    PrintBool(title_observed);
    std::cout << ",\"visible_button_count\":" << title_survey.visible_button_count
              << ",\"unknown_identifier_count\":"
              << title_survey.unknown_identifier_count
              << ",\"empty_identifier_count\":" << title_survey.empty_identifier_count
              << ",\"duplicate_known_identifier_count\":"
              << title_survey.duplicate_known_identifier_count
              << ",\"nodes_visited\":" << title_survey.nodes_visited
              << ",\"truncated\":";
    PrintBool(title_survey.truncated);
    std::cout << ",\"known\":[";
    for (std::size_t index = 0U; index < title_survey.known.size(); ++index)
    {
        if (index != 0U)
            std::cout << ',';
        const auto& known = title_survey.known[index];
        std::cout << "{\"identifier\":\"" << known.identifier << "\",\"present\":";
        PrintBool(known.present);
        std::cout << ",\"ordinal\":";
        if (known.present)
            std::cout << known.ordinal;
        else
            std::cout << "null";
        std::cout << '}';
    }
    std::cout << "]}}\n";
    return 0;
}
} // namespace omega::tool
