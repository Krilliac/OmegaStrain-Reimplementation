#pragma once

#include "omega/content/front_end_screen_survey.h"
#include "omega/content/game_data_service.h"

#include <array>
#include <cstddef>
#include <exception>
#include <expected>
#include <new>
#include <optional>
#include <ostream>
#include <string_view>

namespace omega::tool
{
inline constexpr std::string_view kFrontEndScreenSurveySchema = "omega-front-end-screen-survey-v1";

struct FrontEndScreenLoadObservation final
{
    content::FrontEndScreenKey key = content::FrontEndScreenKey::Title;
    bool loaded = false;
    // Serialized only through GameDataErrorCodeName; no diagnostic message or
    // owner-authored string can enter the report model.
    content::GameDataErrorCode error = content::GameDataErrorCode::DecodeFailed;
};

struct FrontEndScreenSurveyReport final
{
    constexpr FrontEndScreenSurveyReport() noexcept
    {
        for (std::size_t index = 0U; index < content::kAllFrontEndScreenKeys.size(); ++index)
        {
            screens[index].key = content::kAllFrontEndScreenKeys[index];
        }
    }

    std::array<FrontEndScreenLoadObservation, content::kAllFrontEndScreenKeys.size()> screens{};
    bool title_observed = false;
    content::FrontEndButtonSurvey title_buttons;
};

// Emits one fixed-schema JSON line. Every string emitted comes from an in-tree
// constant or enum formatter. Known identifiers are reindexed through the
// repository allowlist rather than trusting a caller-supplied string_view.
void WriteFrontEndScreenSurveyReport(std::ostream& output,
                                     const FrontEndScreenSurveyReport& report);

// The probe result deliberately retains a typed error rather than a message in
// the final report. A successful Title probe carries its aggregate button
// survey; successful non-Title probes carry std::nullopt.
using FrontEndScreenProbeResult =
    std::expected<std::optional<content::FrontEndButtonSurvey>, content::GameDataError>;

// Stable root-open failure path. The typed error is accepted so tests can prove
// that its potentially identifier-bearing message is ignored.
[[nodiscard]] int WriteFrontEndScreenSurveyOpenFailure(std::ostream& error_output,
                                                       const content::GameDataError& error);

// Runs all declared roles after the owner root has opened. Per-screen failures
// remain observations, so this always returns 0 after writing every row.
template <typename Probe>
[[nodiscard]] int RunFrontEndScreenSurveyProbes(std::ostream& output, Probe&& probe)
{
    FrontEndScreenSurveyReport report;
    for (auto& observation : report.screens)
    {
        try
        {
            FrontEndScreenProbeResult result = probe(observation.key);
            if (!result)
            {
                observation.error = result.error().code;
                continue;
            }

            observation.loaded = true;
            if (observation.key == content::FrontEndScreenKey::Title && result->has_value())
            {
                report.title_buttons = **result;
                report.title_observed = true;
            }
        }
        catch (const std::bad_alloc&)
        {
            throw;
        }
        catch (const std::exception&)
        {
            observation.error = content::GameDataErrorCode::DecodeFailed;
        }
        catch (...)
        {
            observation.error = content::GameDataErrorCode::DecodeFailed;
        }
    }

    WriteFrontEndScreenSurveyReport(output, report);
    return 0;
}
} // namespace omega::tool
