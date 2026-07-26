#include "front_end_screen_survey_report.h"

#include <cstddef>

namespace omega::tool
{
namespace
{
void WriteBool(std::ostream& output, const bool value)
{
    output << (value ? "true" : "false");
}
} // namespace

void WriteFrontEndScreenSurveyReport(std::ostream& output, const FrontEndScreenSurveyReport& report)
{
    output << "{\"schema\":\"" << kFrontEndScreenSurveySchema << "\",\"screens\":[";
    for (std::size_t index = 0U; index < report.screens.size(); ++index)
    {
        if (index != 0U)
            output << ',';

        const auto& observation = report.screens[index];
        output << "{\"key\":\"" << content::FrontEndScreenSurveyKeyName(observation.key)
               << "\",\"loaded\":";
        WriteBool(output, observation.loaded);
        output << ",\"error\":";
        if (observation.loaded)
            output << "null";
        else
            output << '"' << content::GameDataErrorCodeName(observation.error) << '"';
        output << '}';
    }

    output << "],\"title_buttons\":{\"observed\":";
    WriteBool(output, report.title_observed);
    output << ",\"visible_button_count\":" << report.title_buttons.visible_button_count
           << ",\"unknown_identifier_count\":" << report.title_buttons.unknown_identifier_count
           << ",\"empty_identifier_count\":" << report.title_buttons.empty_identifier_count
           << ",\"duplicate_known_identifier_count\":"
           << report.title_buttons.duplicate_known_identifier_count
           << ",\"nodes_visited\":" << report.title_buttons.nodes_visited << ",\"truncated\":";
    WriteBool(output, report.title_buttons.truncated);
    output << ",\"known\":[";
    for (std::size_t index = 0U; index < report.title_buttons.known.size(); ++index)
    {
        if (index != 0U)
            output << ',';

        auto known = report.title_buttons.known[index];
        known.identifier = content::kKnownFrontEndButtonIdentifiers[index];
        output << "{\"identifier\":\"" << known.identifier << "\",\"present\":";
        WriteBool(output, known.present);
        output << ",\"ordinal\":";
        if (known.present)
            output << known.ordinal;
        else
            output << "null";
        output << '}';
    }
    output << "]}}\n";
}

int WriteFrontEndScreenSurveyOpenFailure(std::ostream& error_output,
                                         const content::GameDataError& error)
{
    error_output << "front-end screen survey: game data root unavailable ["
                 << content::GameDataErrorCodeName(error.code) << "]\n";
    return 1;
}
} // namespace omega::tool
