#include "omega/runtime/front_end_presentation_gate.h"

#include <array>
#include <iostream>
#include <string_view>
#include <type_traits>
#include <utility>

namespace omega::content::detail
{
struct RetailFrontEndPresentationCapabilityTestAccess final
{
    [[nodiscard]] static omega::content::RetailFrontEndPresentationCapability
    MakeRetailCapability() noexcept
    {
        return omega::content::RetailFrontEndPresentationCapability{
            omega::content::RetailFrontEndPresentationCapability::
                ConstructionKey{}};
    }
};
} // namespace omega::content::detail

namespace
{
int failures = 0;

void Check(const bool condition, const std::string_view message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

void CheckError(
    const std::expected<void, omega::runtime::FrontEndPresentationGateError>& result,
    const omega::runtime::FrontEndPresentationGateErrorCode code,
    const std::string_view message,
    const std::string_view context)
{
    Check(!result && result.error().code == code &&
              result.error().message == message,
        context);
}
} // namespace

int main()
{
    using omega::runtime::AuthorizeFrontEndPresentation;
    using omega::runtime::DeveloperDiagnosticFrontEndPresentationCapability;
    using omega::runtime::FrontEndPresentationGateErrorCode;
    using omega::runtime::FrontEndPresentationMode;
    using omega::content::RetailFrontEndPresentationCapability;

    static_assert(!std::is_default_constructible_v<
                  RetailFrontEndPresentationCapability>);
    static_assert(!std::is_copy_constructible_v<
                  RetailFrontEndPresentationCapability>);
    static_assert(!std::is_copy_assignable_v<
                  RetailFrontEndPresentationCapability>);
    static_assert(std::is_nothrow_move_constructible_v<
                  RetailFrontEndPresentationCapability>);
    static_assert(std::is_nothrow_move_assignable_v<
                  RetailFrontEndPresentationCapability>);
    static_assert(!std::is_default_constructible_v<
                  DeveloperDiagnosticFrontEndPresentationCapability>);
    static_assert(!std::is_copy_constructible_v<
                  DeveloperDiagnosticFrontEndPresentationCapability>);

    auto retail = omega::content::detail::
        RetailFrontEndPresentationCapabilityTestAccess::MakeRetailCapability();
    auto developer = DeveloperDiagnosticFrontEndPresentationCapability::
        CreateForExplicitDeveloperMode();

    Check(AuthorizeFrontEndPresentation(
              FrontEndPresentationMode::RetailRequired, retail)
              .has_value(),
        "retail-required accepts the unforgeable game-data capability");
    Check(AuthorizeFrontEndPresentation(
              FrontEndPresentationMode::DeveloperDiagnostics, developer)
              .has_value(),
        "the explicit developer mode accepts only its separate capability");

    CheckError(AuthorizeFrontEndPresentation(
                   FrontEndPresentationMode::RetailRequired, std::nullopt),
        FrontEndPresentationGateErrorCode::PresentationUnavailable,
        "retail front-end presentation is unavailable",
        "normal mode fails closed while the retail decoder bundle is unavailable");
    CheckError(AuthorizeFrontEndPresentation(
                   FrontEndPresentationMode::DeveloperDiagnostics, std::nullopt),
        FrontEndPresentationGateErrorCode::PresentationUnavailable,
        "developer diagnostic presentation is unavailable",
        "developer mode still requires an explicit presentation capability");
    CheckError(AuthorizeFrontEndPresentation(
                   FrontEndPresentationMode::RetailRequired, developer),
        FrontEndPresentationGateErrorCode::ProvenanceRejected,
        "front-end presentation provenance is not authorized for this mode",
        "project-authored presentation cannot satisfy normal retail mode");
    CheckError(AuthorizeFrontEndPresentation(
                   FrontEndPresentationMode::DeveloperDiagnostics, retail),
        FrontEndPresentationGateErrorCode::ProvenanceRejected,
        "front-end presentation provenance is not authorized for this mode",
        "developer diagnostics cannot relabel a retail presentation");

    auto moved_retail = std::move(retail);
    Check(AuthorizeFrontEndPresentation(
              FrontEndPresentationMode::RetailRequired, moved_retail)
              .has_value(),
        "retail capability authority follows its move");
    CheckError(AuthorizeFrontEndPresentation(
                   FrontEndPresentationMode::RetailRequired, retail),
        FrontEndPresentationGateErrorCode::InvalidCapability,
        "front-end presentation capability is invalid",
        "a moved-from retail capability cannot duplicate authority");

    auto moved_developer = std::move(developer);
    Check(AuthorizeFrontEndPresentation(
              FrontEndPresentationMode::DeveloperDiagnostics, moved_developer)
              .has_value(),
        "developer capability authority follows its move");
    CheckError(AuthorizeFrontEndPresentation(
                   FrontEndPresentationMode::DeveloperDiagnostics, developer),
        FrontEndPresentationGateErrorCode::InvalidCapability,
        "front-end presentation capability is invalid",
        "a moved-from developer capability cannot duplicate authority");

    CheckError(AuthorizeFrontEndPresentation(
                   static_cast<FrontEndPresentationMode>(0xffU), moved_retail),
        FrontEndPresentationGateErrorCode::InvalidMode,
        "front-end presentation mode is invalid",
        "invalid launch-mode bytes fail before capability inspection");
    CheckError(AuthorizeFrontEndPresentation(
                   static_cast<FrontEndPresentationMode>(0xffU), std::nullopt),
        FrontEndPresentationGateErrorCode::InvalidMode,
        "front-end presentation mode is invalid",
        "invalid launch-mode bytes also fail on the unavailable path");

    constexpr std::array mode_names{
        omega::runtime::FrontEndPresentationModeName(
            FrontEndPresentationMode::RetailRequired),
        omega::runtime::FrontEndPresentationModeName(
            FrontEndPresentationMode::DeveloperDiagnostics),
    };
    Check(mode_names == std::array<std::string_view, 2U>{
                            "retail-required", "developer-diagnostics"} &&
              omega::runtime::FrontEndPresentationProvenanceName(moved_retail) ==
                  "retail-game-data" &&
              omega::runtime::FrontEndPresentationProvenanceName(moved_developer) ==
                  "project-developer-diagnostics",
        "mode and capability type names remain explicit and identity-free");

    constexpr std::array error_names{
        omega::runtime::FrontEndPresentationGateErrorCodeName(
            FrontEndPresentationGateErrorCode::InvalidMode),
        omega::runtime::FrontEndPresentationGateErrorCodeName(
            FrontEndPresentationGateErrorCode::PresentationUnavailable),
        omega::runtime::FrontEndPresentationGateErrorCodeName(
            FrontEndPresentationGateErrorCode::InvalidCapability),
        omega::runtime::FrontEndPresentationGateErrorCodeName(
            FrontEndPresentationGateErrorCode::ProvenanceRejected),
    };
    Check(error_names == std::array<std::string_view, 4U>{
                             "invalid-mode", "presentation-unavailable",
                             "invalid-capability", "provenance-rejected"},
        "every presentation-gate failure has a stable category");

    if (failures != 0)
    {
        std::cerr << failures << " front-end presentation gate test(s) failed\n";
        return 1;
    }
    std::cout << "front-end presentation gate tests passed\n";
    return 0;
}
