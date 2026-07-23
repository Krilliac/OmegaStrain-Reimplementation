#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>

namespace omega::content
{
class FrontEndScreenBundle;
}

namespace omega::frontend::presentation
{
// Project safety ceilings, not retail-format claims. Callers may tighten but
// cannot raise them.
inline constexpr std::uint32_t
    kRetailPresentationInspectionMaximumWidgetNodes = 4'096U;
inline constexpr std::uint32_t
    kRetailPresentationInspectionMaximumVisualNodes = 4'096U;
inline constexpr std::uint32_t
    kRetailPresentationInspectionMaximumWidgetDepth = 256U;
inline constexpr std::uint32_t
    kRetailPresentationInspectionMaximumVisualDepth = 256U;
inline constexpr std::uint32_t
    kRetailPresentationInspectionMaximumCandidates = 4'096U;
inline constexpr std::uint64_t
    kRetailPresentationInspectionMaximumPositions = 1ULL << 20U;
inline constexpr std::uint64_t
    kRetailPresentationInspectionMaximumAnimationTracks = 1ULL << 16U;
inline constexpr std::uint64_t
    kRetailPresentationInspectionMaximumLookupEntries = 1ULL << 16U;
inline constexpr std::uint64_t
    kRetailPresentationInspectionMaximumIdentityBytes = 8ULL << 20U;
inline constexpr std::uint64_t
    kRetailPresentationInspectionMaximumCandidateNodeVisits = 1ULL << 20U;
inline constexpr std::uint64_t
    kRetailPresentationInspectionMaximumLookupSteps = 1ULL << 24U;
inline constexpr std::uint64_t
    kRetailPresentationInspectionMaximumTriangleVisits = 1ULL << 20U;
inline constexpr std::uint64_t
    kRetailPresentationInspectionMaximumCoveredSamples = 1ULL << 24U;
inline constexpr std::uint64_t
    kRetailPresentationInspectionMaximumScratchBytes = 8ULL << 20U;
inline constexpr std::uint64_t
    kRetailPresentationInspectionMaximumOutputBytes = 1ULL << 20U;
// Architecture-independent budget charges used by the inspector. The fixed
// coverage charge is the project raster's one-byte union mask plus two
// four-byte candidate work arrays. Each visual record is charged at this
// conservative fixed width even when the host representation is smaller.
inline constexpr std::uint64_t
    kRetailPresentationInspectionFixedCoverageScratchBytes = 2'580'480ULL;
inline constexpr std::uint64_t
    kRetailPresentationInspectionVisualRecordScratchBytes = 128ULL;

struct RetailPresentationInspectionLimits final
{
    std::uint32_t maximum_widget_nodes =
        kRetailPresentationInspectionMaximumWidgetNodes;
    std::uint32_t maximum_visual_nodes =
        kRetailPresentationInspectionMaximumVisualNodes;
    std::uint32_t maximum_widget_depth =
        kRetailPresentationInspectionMaximumWidgetDepth;
    std::uint32_t maximum_visual_depth =
        kRetailPresentationInspectionMaximumVisualDepth;
    std::uint32_t maximum_candidates =
        kRetailPresentationInspectionMaximumCandidates;
    std::uint64_t maximum_positions =
        kRetailPresentationInspectionMaximumPositions;
    std::uint64_t maximum_animation_tracks =
        kRetailPresentationInspectionMaximumAnimationTracks;
    std::uint64_t maximum_lookup_entries =
        kRetailPresentationInspectionMaximumLookupEntries;
    std::uint64_t maximum_identity_bytes =
        kRetailPresentationInspectionMaximumIdentityBytes;
    std::uint64_t maximum_candidate_node_visits =
        kRetailPresentationInspectionMaximumCandidateNodeVisits;
    std::uint64_t maximum_lookup_steps =
        kRetailPresentationInspectionMaximumLookupSteps;
    std::uint64_t maximum_triangle_visits =
        kRetailPresentationInspectionMaximumTriangleVisits;
    std::uint64_t maximum_covered_samples =
        kRetailPresentationInspectionMaximumCoveredSamples;
    std::uint64_t maximum_scratch_bytes =
        kRetailPresentationInspectionMaximumScratchBytes;
    std::uint64_t maximum_output_bytes =
        kRetailPresentationInspectionMaximumOutputBytes;

    bool operator==(const RetailPresentationInspectionLimits&) const = default;
};

// These are unresolved presentation requirements, not inferred retail
// semantics. Presence means that a native compositor still needs independent
// evidence or an explicit caller decision before it may merge the observed
// candidates into a retail presentation.
enum class RetailPresentationRequirement : std::uint8_t
{
    ParentVisibility = 0U,
    CandidateOrderingUnresolved,
    VisualWidgetLaneMerge,
    TextEncoding,
    TextLayout,
    TextInterleave,
    ActionLifecycle,
    AnimationTickSelection,
    AnimationOpacityApplication,
    AnimationUvOffsetApplication,
    CandidateOverlap,
    IncompleteCandidateUnion,
    MissingVisualBinding,
    MissingTextureBinding,
    TextureVisibility,
    VertexOpacityPolicy,
    DepthPolicy,
    UnsupportedWidgetKind,
    InvalidGeometry,
    NonFiniteMath,
    TransformFailure,
    ProjectionFailure,
    Count,
};

class RetailPresentationRequirementSet final
{
public:
    [[nodiscard]] constexpr bool Contains(
        const RetailPresentationRequirement requirement) const noexcept
    {
        const auto index = static_cast<std::uint8_t>(requirement);
        return index < static_cast<std::uint8_t>(
                           RetailPresentationRequirement::Count) &&
               present_[index];
    }

    bool operator==(const RetailPresentationRequirementSet&) const = default;

private:
    constexpr void Add(
        const RetailPresentationRequirement requirement) noexcept
    {
        const auto index = static_cast<std::uint8_t>(requirement);
        if (index < static_cast<std::uint8_t>(
                        RetailPresentationRequirement::Count))
        {
            present_[index] = true;
        }
    }

    std::array<bool,
        static_cast<std::size_t>(RetailPresentationRequirement::Count)>
        present_{};

    friend struct RetailPresentationInspectionContext;
};

// Candidate kind is structural only. It assigns neither role nor ordering.
enum class RetailLayerCandidateKind : std::uint8_t
{
    VisualGeometry = 0U,
    Text,
    UnresolvedWidget,
    UnresolvedBinding,
    Count,
};

// This is coverage under the project-owned canonical scan-conversion kernel,
// not retail GS coverage and never presentation completeness.
enum class RetailProjectedGeometryCoverage : std::uint8_t
{
    NoCanonicalSamples = 0U,
    PartialCanonicalSamples,
    AllCanonicalSamples,
    Indeterminate,
    Count,
};

struct RetailLayerCandidateCensus final
{
    [[nodiscard]] constexpr std::uint32_t Count(
        const RetailLayerCandidateKind kind) const noexcept
    {
        const auto kind_index = static_cast<std::uint8_t>(kind);
        if (kind_index >=
            static_cast<std::uint8_t>(RetailLayerCandidateKind::Count))
        {
            return 0U;
        }
        return counts_[kind_index];
    }

    [[nodiscard]] constexpr std::uint32_t total() const noexcept
    {
        return total_;
    }

    bool operator==(const RetailLayerCandidateCensus&) const = default;

private:
    constexpr void Add(const RetailLayerCandidateKind kind) noexcept
    {
        ++counts_[static_cast<std::uint8_t>(kind)];
        ++total_;
    }

    std::array<std::uint32_t,
        static_cast<std::size_t>(RetailLayerCandidateKind::Count)>
        counts_{};
    std::uint32_t total_ = 0U;

    friend struct RetailPresentationInspectionContext;
};

// Separate non-retail diagnostic for visual-geometry candidates only.
struct RetailProjectedGeometryCoverageCensus final
{
    [[nodiscard]] constexpr std::uint32_t Count(
        const RetailProjectedGeometryCoverage coverage) const noexcept
    {
        const auto index = static_cast<std::uint8_t>(coverage);
        return index < static_cast<std::uint8_t>(
                           RetailProjectedGeometryCoverage::Count)
            ? counts_[index]
            : 0U;
    }

    [[nodiscard]] constexpr std::uint32_t total() const noexcept
    {
        return total_;
    }

    bool operator==(
        const RetailProjectedGeometryCoverageCensus&) const = default;

private:
    constexpr void Add(
        const RetailProjectedGeometryCoverage coverage) noexcept
    {
        ++counts_[static_cast<std::uint8_t>(coverage)];
        ++total_;
    }

    std::array<std::uint32_t,
        static_cast<std::size_t>(RetailProjectedGeometryCoverage::Count)>
        counts_{};
    std::uint32_t total_ = 0U;

    friend struct RetailPresentationInspectionContext;
};

// Fully owned categorical output. It contains no source views, identifiers,
// paths, names, strings, hashes, payload values, pixels, rectangles, or frame
// capability. Projected-geometry union fields are meaningful only when
// projected_geometry_union_evaluated is true. Raster coverage means only that
// all known, visibility-resolved static candidate masks cover the canonical
// project raster under the project scan-conversion kernel. It is not a retail
// GS claim or a claim about draw order, opacity, text, or presentation
// completeness.
struct RetailPresentationRequirements final
{
    RetailLayerCandidateCensus candidate_census;
    RetailProjectedGeometryCoverageCensus projected_geometry_coverage;
    RetailPresentationRequirementSet blockers;
    bool projected_geometry_union_evaluated = false;
    bool projected_geometry_union_covers_canonical_raster = false;

    bool operator==(const RetailPresentationRequirements&) const = default;
};

// Limit failures are split by class so a bounded inspection never returns a
// misleading partial report.
enum class RetailPresentationInspectionError : std::uint8_t
{
    InvalidRetailCapability = 0U,
    InvalidLimits,
    WidgetNodeLimitExceeded,
    VisualNodeLimitExceeded,
    WidgetDepthLimitExceeded,
    VisualDepthLimitExceeded,
    CandidateLimitExceeded,
    PositionLimitExceeded,
    AnimationTrackLimitExceeded,
    LookupEntryLimitExceeded,
    IdentityByteLimitExceeded,
    CandidateNodeVisitLimitExceeded,
    LookupStepLimitExceeded,
    TriangleVisitLimitExceeded,
    CoveredSampleLimitExceeded,
    ScratchLimitExceeded,
    OutputLimitExceeded,
    AllocationFailure,
};

using RetailPresentationInspectionResult =
    std::expected<RetailPresentationRequirements,
        RetailPresentationInspectionError>;

// [any thread; stateless/reentrant] Borrows one live immutable Title,
// CreateAgent, or LoadAgent bundle for this synchronous call. The caller must
// not concurrently move or destroy it.
//
// Widget and visual trees are visited only to enumerate anonymous structural
// layer candidates and unresolved presentation requirements. Established
// parent*local affine composition and interface projection may be used for
// static coverage classification; traversal order is never promoted to draw
// order. No frame, pixels, retail capability, source view, or identifier can
// escape this boundary.
[[nodiscard]] RetailPresentationInspectionResult
InspectRetailPresentationRequirements(
    const content::FrontEndScreenBundle& bundle,
    RetailPresentationInspectionLimits limits = {}) noexcept;
} // namespace omega::frontend::presentation

static_assert(
    static_cast<std::uint8_t>(
        omega::frontend::presentation::RetailPresentationRequirement::Count) <=
    64U);
static_assert(
    sizeof(omega::frontend::presentation::RetailPresentationRequirements) <=
    omega::frontend::presentation::
        kRetailPresentationInspectionMaximumOutputBytes);
