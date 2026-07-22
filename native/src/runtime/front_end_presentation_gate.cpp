#include "omega/runtime/front_end_presentation_gate.h"

namespace omega::runtime
{
namespace
{
[[nodiscard]] constexpr FrontEndPresentationGateError Error(
    const FrontEndPresentationGateErrorCode code,
    const std::string_view message) noexcept
{
    return FrontEndPresentationGateError{.code = code, .message = message};
}

[[nodiscard]] constexpr bool IsValidMode(
    const FrontEndPresentationMode mode) noexcept
{
    return mode == FrontEndPresentationMode::RetailRequired ||
           mode == FrontEndPresentationMode::DeveloperDiagnostics;
}

[[nodiscard]] std::expected<void, FrontEndPresentationGateError>
RejectInvalidMode() noexcept
{
    return std::unexpected(Error(
        FrontEndPresentationGateErrorCode::InvalidMode,
        "front-end presentation mode is invalid"));
}

[[nodiscard]] std::expected<void, FrontEndPresentationGateError>
RejectInvalidCapability() noexcept
{
    return std::unexpected(Error(
        FrontEndPresentationGateErrorCode::InvalidCapability,
        "front-end presentation capability is invalid"));
}

[[nodiscard]] std::expected<void, FrontEndPresentationGateError>
RejectProvenance() noexcept
{
    return std::unexpected(Error(
        FrontEndPresentationGateErrorCode::ProvenanceRejected,
        "front-end presentation provenance is not authorized for this mode"));
}
} // namespace

std::expected<void, FrontEndPresentationGateError>
AuthorizeFrontEndPresentation(
    const FrontEndPresentationMode mode,
    const content::RetailFrontEndPresentationCapability& capability) noexcept
{
    if (!IsValidMode(mode))
        return RejectInvalidMode();
    if (!capability.valid())
        return RejectInvalidCapability();
    if (mode != FrontEndPresentationMode::RetailRequired)
        return RejectProvenance();
    return {};
}

std::expected<void, FrontEndPresentationGateError>
AuthorizeFrontEndPresentation(
    const FrontEndPresentationMode mode,
    const DeveloperDiagnosticFrontEndPresentationCapability& capability) noexcept
{
    if (!IsValidMode(mode))
        return RejectInvalidMode();
    if (!capability.valid_)
        return RejectInvalidCapability();
    if (mode != FrontEndPresentationMode::DeveloperDiagnostics)
        return RejectProvenance();
    return {};
}

std::expected<void, FrontEndPresentationGateError>
AuthorizeFrontEndPresentation(
    const FrontEndPresentationMode mode, const std::nullopt_t) noexcept
{
    if (!IsValidMode(mode))
        return RejectInvalidMode();
    return std::unexpected(Error(
        FrontEndPresentationGateErrorCode::PresentationUnavailable,
        mode == FrontEndPresentationMode::RetailRequired
            ? "retail front-end presentation is unavailable"
            : "developer diagnostic presentation is unavailable"));
}
} // namespace omega::runtime
