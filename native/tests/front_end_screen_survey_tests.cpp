#include "front_end_screen_survey_report.h"

#include "omega/asset/frontend_ir.h"
#include "omega/content/front_end_screen_survey.h"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace
{
int failures = 0;

void Check(const bool condition, const char* const description)
{
    if (condition)
        return;
    ++failures;
    std::cerr << "FAIL: " << description << '\n';
}

[[nodiscard]] omega::asset::FrontendWidgetIR MakeButton(std::string identifier,
                                                        const bool visible = true)
{
    omega::asset::FrontendWidgetIR widget;
    widget.kind = omega::asset::FrontendWidgetKind::Button;
    widget.identifier = std::move(identifier);
    widget.visible = visible;
    return widget;
}

[[nodiscard]] omega::asset::FrontendWidgetIR MakeContainer(
    std::vector<omega::asset::FrontendWidgetIR> children, const bool visible = true)
{
    omega::asset::FrontendWidgetIR widget;
    widget.kind = omega::asset::FrontendWidgetKind::Container;
    widget.visible = visible;
    widget.children = std::move(children);
    return widget;
}
} // namespace

int main()
{
    using omega::content::FrontEndButtonSurvey;
    using omega::content::FrontEndScreenKey;
    using omega::content::FrontEndScreenSurveyKeyName;
    using omega::content::kAllFrontEndScreenKeys;
    using omega::content::kFrontEndSurveyMaximumDepth;
    using omega::content::kFrontEndSurveyMaximumNodes;
    using omega::content::kKnownFrontEndButtonIdentifiers;
    using omega::content::SurveyFrontEndVisibleButtons;

    static_assert(kAllFrontEndScreenKeys.size() == 3U);
    static_assert(FrontEndScreenSurveyKeyName(FrontEndScreenKey::Title) == "title");
    static_assert(FrontEndScreenSurveyKeyName(FrontEndScreenKey::CreateAgent) == "create_agent");
    static_assert(FrontEndScreenSurveyKeyName(FrontEndScreenKey::LoadAgent) == "load_agent");

    // The fixed known array is present even when Title could not be observed.
    {
        const FrontEndButtonSurvey survey;
        Check(survey.known[0U].identifier == kKnownFrontEndButtonIdentifiers[0U] &&
                  survey.known[1U].identifier == kKnownFrontEndButtonIdentifiers[1U],
              "default report retains the fixed known-identifier schema");
    }

    {
        const auto survey = SurveyFrontEndVisibleButtons(MakeContainer({}));
        Check(survey.visible_button_count == 0U, "empty document has no visible buttons");
        Check(survey.nodes_visited == 1U, "empty document visits its root");
        Check(!survey.truncated, "empty document is not truncated");
    }

    // Ordinals follow visible preorder rather than allowlist order.
    {
        const auto survey = SurveyFrontEndVisibleButtons(MakeContainer({
            MakeContainer({MakeButton("loadagent")}),
            MakeButton("newagent"),
        }));
        Check(survey.visible_button_count == 2U, "both visible known buttons are counted");
        Check(survey.known[1U].present && survey.known[1U].ordinal == 0U,
              "nested earlier button receives ordinal zero");
        Check(survey.known[0U].present && survey.known[0U].ordinal == 1U,
              "later sibling receives ordinal one");
    }

    // Match the app's inherited visibility: a hidden parent suppresses its
    // entire subtree and consumes no descendant ordinal.
    {
        const auto survey = SurveyFrontEndVisibleButtons(MakeContainer({
            MakeContainer({MakeButton("newagent")}, false),
            MakeButton("loadagent"),
        }));
        Check(survey.visible_button_count == 1U, "hidden ancestor suppresses descendant buttons");
        Check(!survey.known[0U].present, "known button under hidden ancestor is not reported");
        Check(survey.known[1U].present && survey.known[1U].ordinal == 0U,
              "hidden subtree consumes no ordinal");
        Check(survey.nodes_visited == 3U, "walk does not enter a hidden subtree");
    }

    {
        auto text = MakeButton("newagent");
        text.kind = omega::asset::FrontendWidgetKind::Text;
        const auto survey = SurveyFrontEndVisibleButtons(MakeContainer({std::move(text)}));
        Check(survey.visible_button_count == 0U, "non-button widget does not participate");
        Check(!survey.known[0U].present, "non-button identifier cannot match a route");
    }

    // Unknown and empty identifiers are counts only. Matching remains exact
    // and case-sensitive, as in the current app route.
    {
        const auto survey = SurveyFrontEndVisibleButtons(MakeContainer({
            MakeButton("newagent"),
            MakeButton("owner-private-sentinel"),
            MakeButton(""),
            MakeButton("NewAgent"),
        }));
        Check(survey.visible_button_count == 4U, "all visible buttons are counted");
        Check(survey.unknown_identifier_count == 2U,
              "unknown and differently-cased identifiers are anonymous");
        Check(survey.empty_identifier_count == 1U, "empty identifier is counted separately");
        Check(survey.known[0U].present && survey.known[0U].ordinal == 0U,
              "exact known identifier is retained");
        Check(!survey.known[1U].present, "absent known identifier remains absent");
    }

    {
        const auto survey = SurveyFrontEndVisibleButtons(MakeContainer({
            MakeButton("newagent"),
            MakeButton("newagent"),
        }));
        Check(survey.known[0U].present && survey.known[0U].ordinal == 0U,
              "first duplicate occurrence keeps the ordinal");
        Check(survey.duplicate_known_identifier_count == 1U,
              "duplicate known identifier is counted");
        Check(survey.unknown_identifier_count == 0U, "known duplicate is not misclassified");
    }

    // Exactly 64 levels are admitted; the next level reports truncation before
    // it is visited.
    {
        auto chain = MakeButton("newagent");
        for (std::uint32_t depth = 1U; depth < kFrontEndSurveyMaximumDepth; ++depth)
        {
            chain = MakeContainer({std::move(chain)});
        }
        const auto at_limit = SurveyFrontEndVisibleButtons(chain);
        Check(!at_limit.truncated, "document at exact depth limit is admitted");
        Check(at_limit.nodes_visited == kFrontEndSurveyMaximumDepth,
              "exact depth limit visits all 64 nodes");
        Check(at_limit.known[0U].present, "button at exact depth limit remains observable");

        chain = MakeContainer({std::move(chain)});
        const auto over_limit = SurveyFrontEndVisibleButtons(chain);
        Check(over_limit.truncated, "document one level over depth limit is truncated");
        Check(over_limit.nodes_visited == kFrontEndSurveyMaximumDepth,
              "over-depth walk stops at exactly 64 visited nodes");
        Check(!over_limit.known[0U].present, "node beyond the depth limit is not observed");
    }

    // Exactly 65,536 nodes are admitted; the next node reports truncation.
    {
        std::vector<omega::asset::FrontendWidgetIR> children;
        children.reserve(kFrontEndSurveyMaximumNodes - 1U);
        for (std::uint32_t index = 1U; index < kFrontEndSurveyMaximumNodes; ++index)
        {
            children.push_back(MakeButton("newagent"));
        }
        const auto at_limit = SurveyFrontEndVisibleButtons(MakeContainer(std::move(children)));
        Check(!at_limit.truncated, "document at exact node limit is admitted");
        Check(at_limit.nodes_visited == kFrontEndSurveyMaximumNodes,
              "exact node limit visits all 65,536 nodes");
    }

    {
        std::vector<omega::asset::FrontendWidgetIR> children;
        children.reserve(kFrontEndSurveyMaximumNodes);
        for (std::uint32_t index = 0U; index < kFrontEndSurveyMaximumNodes; ++index)
        {
            children.push_back(MakeButton("newagent"));
        }
        const auto over_limit = SurveyFrontEndVisibleButtons(MakeContainer(std::move(children)));
        Check(over_limit.truncated, "document one node over limit reports truncation");
        Check(over_limit.nodes_visited == kFrontEndSurveyMaximumNodes,
              "over-wide walk stops at exactly 65,536 visited nodes");
    }

    // The result borrows allowlist entries, not document strings.
    {
        const auto survey = SurveyFrontEndVisibleButtons(MakeContainer({
            MakeButton("newagent"),
            MakeButton("loadagent"),
            MakeButton("owner-private-sentinel"),
        }));
        bool all_from_allowlist = true;
        for (std::size_t index = 0U; index < survey.known.size(); ++index)
        {
            if (survey.known[index].identifier.data() !=
                kKnownFrontEndButtonIdentifiers[index].data())
            {
                all_from_allowlist = false;
            }
        }
        Check(all_from_allowlist, "reported identifiers borrow repository allowlist entries");
        Check(survey.unknown_identifier_count == 1U,
              "owner-authored identifier survives only as a count");
    }

    // Exact serializer regression: the report type accepts only enum/fixed
    // strings, and an unknown document identifier cannot enter its JSON.
    {
        omega::tool::FrontEndScreenSurveyReport report;
        report.screens[0U].loaded = true;
        report.screens[1U].error = omega::content::GameDataErrorCode::MissingRequiredFile;
        report.screens[2U].loaded = true;
        report.title_observed = true;
        report.title_buttons = SurveyFrontEndVisibleButtons(MakeContainer({
            MakeButton("newagent"),
            MakeButton("owner-private-sentinel"),
            MakeButton(""),
        }));
        report.title_buttons.known[0U].identifier = "serializer-private-sentinel";

        std::ostringstream output;
        omega::tool::WriteFrontEndScreenSurveyReport(output, report);
        const std::string expected = "{\"schema\":\"omega-front-end-screen-survey-v1\","
                                     "\"screens\":["
                                     "{\"key\":\"title\",\"loaded\":true,\"error\":null},"
                                     "{\"key\":\"create_agent\",\"loaded\":false,"
                                     "\"error\":\"missing-required-file\"},"
                                     "{\"key\":\"load_agent\",\"loaded\":true,\"error\":null}],"
                                     "\"title_buttons\":{\"observed\":true,"
                                     "\"visible_button_count\":3,\"unknown_identifier_count\":1,"
                                     "\"empty_identifier_count\":1,"
                                     "\"duplicate_known_identifier_count\":0,\"nodes_visited\":4,"
                                     "\"truncated\":false,\"known\":["
                                     "{\"identifier\":\"newagent\",\"present\":true,\"ordinal\":0},"
                                     "{\"identifier\":\"loadagent\",\"present\":false,"
                                     "\"ordinal\":null}]}}\n";
        Check(output.str() == expected, "serializer emits the exact fixed v1 schema");
        Check(output.str().find("owner-private-sentinel") == std::string::npos &&
                  output.str().find("serializer-private-sentinel") == std::string::npos,
              "serializer cannot emit a document or caller-supplied identifier");
    }

    // A per-screen failure is an observation: all three rows are emitted and
    // the post-open command path returns success. Neither the typed error's
    // message nor the unknown document identifier can enter its output.
    {
        const auto probe =
            [](const FrontEndScreenKey key) -> omega::tool::FrontEndScreenProbeResult {
            if (key == FrontEndScreenKey::Title)
            {
                return std::optional<FrontEndButtonSurvey>{
                    SurveyFrontEndVisibleButtons(MakeContainer({
                        MakeButton("newagent"),
                        MakeButton("owner-private-sentinel"),
                        MakeButton(""),
                    })),
                };
            }
            if (key == FrontEndScreenKey::CreateAgent)
            {
                return std::unexpected(omega::content::GameDataError{
                    .code = omega::content::GameDataErrorCode::MissingRequiredFile,
                    .message = "owner-private-error-message",
                });
            }
            return std::optional<FrontEndButtonSurvey>{};
        };

        std::ostringstream output;
        const int result = omega::tool::RunFrontEndScreenSurveyProbes(output, probe);
        std::string expected =
            R"json({"schema":"omega-front-end-screen-survey-v1","screens":[)json"
            R"json({"key":"title","loaded":true,"error":null},)json"
            R"json({"key":"create_agent","loaded":false,"error":"missing-required-file"},)json"
            R"json({"key":"load_agent","loaded":true,"error":null}],)json"
            R"json("title_buttons":{"observed":true,"visible_button_count":3,)json"
            R"json("unknown_identifier_count":1,"empty_identifier_count":1,)json"
            R"json("duplicate_known_identifier_count":0,"nodes_visited":4,)json"
            R"json("truncated":false,"known":[)json"
            R"json({"identifier":"newagent","present":true,"ordinal":0},)json"
            R"json({"identifier":"loadagent","present":false,"ordinal":null}]}})json";
        expected.push_back('\n');
        Check(result == 0, "partial screen failure keeps the command successful");
        Check(output.str() == expected, "observed report emits all rows in exact v1 order");
        Check(output.str().find("owner-private-sentinel") == std::string::npos &&
                  output.str().find("owner-private-error-message") == std::string::npos,
              "screen report omits unknown identifiers and error messages");

        std::ostringstream repeated;
        const int repeated_result = omega::tool::RunFrontEndScreenSurveyProbes(repeated, probe);
        Check(repeated_result == 0 && repeated.str() == output.str(),
              "repeated synthetic survey is byte-for-byte deterministic");
    }

    // Title-unavailable output retains the complete fixed schema and known
    // repository literals, with no document-derived slots.
    {
        const auto unavailable_probe =
            [](const FrontEndScreenKey key) -> omega::tool::FrontEndScreenProbeResult {
            omega::content::GameDataErrorCode code =
                omega::content::GameDataErrorCode::DecodeFailed;
            if (key == FrontEndScreenKey::CreateAgent)
                code = omega::content::GameDataErrorCode::MissingRequiredFile;
            else if (key == FrontEndScreenKey::LoadAgent)
                code = omega::content::GameDataErrorCode::ReadFailed;
            return std::unexpected(omega::content::GameDataError{
                .code = code,
                .message = "owner-private-error-message",
            });
        };

        std::ostringstream output;
        const int result = omega::tool::RunFrontEndScreenSurveyProbes(output, unavailable_probe);
        std::string expected =
            R"json({"schema":"omega-front-end-screen-survey-v1","screens":[)json"
            R"json({"key":"title","loaded":false,"error":"decode-failed"},)json"
            R"json({"key":"create_agent","loaded":false,"error":"missing-required-file"},)json"
            R"json({"key":"load_agent","loaded":false,"error":"read-failed"}],)json"
            R"json("title_buttons":{"observed":false,"visible_button_count":0,)json"
            R"json("unknown_identifier_count":0,"empty_identifier_count":0,)json"
            R"json("duplicate_known_identifier_count":0,"nodes_visited":0,)json"
            R"json("truncated":false,"known":[)json"
            R"json({"identifier":"newagent","present":false,"ordinal":null},)json"
            R"json({"identifier":"loadagent","present":false,"ordinal":null}]}})json";
        expected.push_back('\n');
        Check(result == 0 && output.str() == expected,
              "unavailable Title emits exact stable v1 schema");
        Check(output.str().find("owner-private-error-message") == std::string::npos,
              "unavailable report omits every error message");
    }

    // Root-open failure is the only command-level failure. Its categorical
    // diagnostic omits both the owner path and the source error message.
    {
        const omega::content::GameDataError error{
            .code = omega::content::GameDataErrorCode::MountFailed,
            .message = "C:/owner/private/root owner-private-error-message",
        };
        std::ostringstream error_output;
        const int result = omega::tool::WriteFrontEndScreenSurveyOpenFailure(error_output, error);
        Check(result == 1, "root-open failure returns one");
        Check(error_output.str() == "front-end screen survey: game data root unavailable "
                                    "[mount-failed]\n",
              "root-open failure emits only the fixed error category");
        Check(error_output.str().find("C:/owner/private/root") == std::string::npos &&
                  error_output.str().find("owner-private-error-message") == std::string::npos,
              "root-open failure omits owner path and error message");
    }

    if (failures != 0)
    {
        std::cerr << failures << " front-end screen survey test(s) failed\n";
        return 1;
    }
    std::cout << "front-end screen survey tests passed\n";
    return 0;
}
