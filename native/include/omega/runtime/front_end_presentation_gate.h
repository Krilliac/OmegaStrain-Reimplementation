#pragma once

#include "omega/content/retail_front_end_presentation_capability.h"

#include <cstdint>
#include <expected>
#include <optional>
#include <string_view>
#include <utility>

namespace omega::runtime
{
// Launch policy for every presentation submitted after the opening movie. The
// launcher never enables DeveloperDiagnostics; that exact command-line mode
// exists only for visibly labelled project-authored native diagnostics.
enum class FrontEndPresentationMode : std::uint8_t
{
    RetailRequired = 0U,
    DeveloperDiagnostics = 1U,
};

enum class FrontEndPresentationGateErrorCode : std::uint8_t
{
    InvalidMode = 0U,
    PresentationUnavailable = 1U,
    InvalidCapability = 2U,
    ProvenanceRejected = 3U,
};

[[nodiscard]] constexpr std::string_view FrontEndPresentationModeName(
    const FrontEndPresentationMode mode) noexcept
{
    switch (mode)
    {
    case FrontEndPresentationMode::RetailRequired:
        return "retail-required";
    case FrontEndPresentationMode::DeveloperDiagnostics:
        return "developer-diagnostics";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view FrontEndPresentationGateErrorCodeName(
    const FrontEndPresentationGateErrorCode code) noexcept
{
    switch (code)
    {
    case FrontEndPresentationGateErrorCode::InvalidMode:
        return "invalid-mode";
    case FrontEndPresentationGateErrorCode::PresentationUnavailable:
        return "presentation-unavailable";
    case FrontEndPresentationGateErrorCode::InvalidCapability:
        return "invalid-capability";
    case FrontEndPresentationGateErrorCode::ProvenanceRejected:
        return "provenance-rejected";
    }
    return "unknown";
}

struct FrontEndPresentationGateError
{
    FrontEndPresentationGateErrorCode code =
        FrontEndPresentationGateErrorCode::InvalidMode;
    // Fixed identity-free diagnostic. It never includes a game path, archive
    // member, payload value, profile identifier, or backend resource identity.
    std::string_view message;

    friend constexpr bool operator==(
        const FrontEndPresentationGateError&,
        const FrontEndPresentationGateError&) noexcept = default;
};

// Separate capability for the project-authored developer presentation. Its
// named factory makes the opt-in explicit, while the gate prevents this type
// from ever satisfying RetailRequired.
class DeveloperDiagnosticFrontEndPresentationCapability final
{
public:
    [[nodiscard]] static DeveloperDiagnosticFrontEndPresentationCapability
    CreateForExplicitDeveloperMode() noexcept
    {
        return DeveloperDiagnosticFrontEndPresentationCapability{
            ConstructionKey{}};
    }

    DeveloperDiagnosticFrontEndPresentationCapability() = delete;
    DeveloperDiagnosticFrontEndPresentationCapability(
        const DeveloperDiagnosticFrontEndPresentationCapability&) = delete;
    DeveloperDiagnosticFrontEndPresentationCapability& operator=(
        const DeveloperDiagnosticFrontEndPresentationCapability&) = delete;
    DeveloperDiagnosticFrontEndPresentationCapability(
        DeveloperDiagnosticFrontEndPresentationCapability&& other) noexcept
        : valid_(std::exchange(other.valid_, false))
    {
    }
    DeveloperDiagnosticFrontEndPresentationCapability& operator=(
        DeveloperDiagnosticFrontEndPresentationCapability&& other) noexcept
    {
        if (this != &other)
            valid_ = std::exchange(other.valid_, false);
        return *this;
    }

private:
    struct ConstructionKey final
    {
    };

    explicit DeveloperDiagnosticFrontEndPresentationCapability(
        ConstructionKey) noexcept
        : valid_(true)
    {
    }

    bool valid_ = false;

    friend std::expected<void, FrontEndPresentationGateError>
    AuthorizeFrontEndPresentation(
        FrontEndPresentationMode,
        const DeveloperDiagnosticFrontEndPresentationCapability&) noexcept;
};

[[nodiscard]] constexpr std::string_view FrontEndPresentationProvenanceName(
    const content::RetailFrontEndPresentationCapability&) noexcept
{
    return "retail-game-data";
}

[[nodiscard]] constexpr std::string_view FrontEndPresentationProvenanceName(
    const DeveloperDiagnosticFrontEndPresentationCapability&) noexcept
{
    return "project-developer-diagnostics";
}

// [any thread; reentrant] Type-directed authorization prevents a diagnostic
// presentation from being relabelled as retail by naming an enum. A moved-from
// capability is invalid and cannot be reused to duplicate authority.
[[nodiscard]] std::expected<void, FrontEndPresentationGateError>
AuthorizeFrontEndPresentation(
    FrontEndPresentationMode mode,
    const content::RetailFrontEndPresentationCapability& capability) noexcept;
[[nodiscard]] std::expected<void, FrontEndPresentationGateError>
AuthorizeFrontEndPresentation(
    FrontEndPresentationMode mode,
    const DeveloperDiagnosticFrontEndPresentationCapability& capability) noexcept;

// Explicit absence path. Missing retail presentation fails closed; it exists
// until the evidenced retail FNT/GUI/IE plus display-conversion pipeline owns
// a capability-bearing canonical presentation.
[[nodiscard]] std::expected<void, FrontEndPresentationGateError>
AuthorizeFrontEndPresentation(
    FrontEndPresentationMode mode, std::nullopt_t) noexcept;
} // namespace omega::runtime
