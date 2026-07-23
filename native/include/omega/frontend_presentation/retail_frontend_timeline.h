#pragma once

#include "omega/asset/frontend_ir.h"

#include <cstdint>
#include <expected>
#include <optional>
#include <utility>
#include <vector>

namespace omega::frontend::presentation {
// Project safety ceilings, not retail limits. Callers may tighten but cannot
// raise them.
inline constexpr std::uint32_t kRetailFrontendTimelineMaximumPositions =
    65'536U;
inline constexpr std::uint32_t kRetailFrontendTimelineMaximumTracks = 4U;
inline constexpr std::uint64_t kRetailFrontendTimelineMaximumKeys = 1ULL << 20U;

struct RetailFrontendTimelineLimits final {
  std::uint32_t maximum_positions = kRetailFrontendTimelineMaximumPositions;
  std::uint32_t maximum_tracks = kRetailFrontendTimelineMaximumTracks;
  std::uint64_t maximum_total_keys = kRetailFrontendTimelineMaximumKeys;

  bool operator==(const RetailFrontendTimelineLimits &) const = default;
};

// The caller supplies both clock domains explicitly. This boundary assigns no
// frame rate, playback rate, wrap rule, or conversion between a live runtime
// tick and an asset-authored floating-point timeline tick.
struct RetailFrontendTimelineInput final {
  std::uint64_t live_tick = 0U;
  float authored_timeline_tick = 0.0F;

  bool operator==(const RetailFrontendTimelineInput &) const = default;
};

enum class RetailFrontendTimelineError : std::uint8_t {
  InvalidLimits = 0U,
  LimitExceeded,
  MalformedTrack,
  NonFiniteValue,
  NonMonotonicKeys,
  AmbiguousTrackTarget,
  // Retained for source/ordinal compatibility with the earlier exact-key
  // evaluator. The established clamped-linear path no longer emits these.
  TickOutsideTrackRange,
  UnsupportedInterpolation,
  NonMonotonicLiveTick,
  InconsistentLiveTick,
  InstanceResourceMismatch,
  AllocationFailure,
};

struct RetailFrontendTimelineEvaluation final {
  std::uint32_t evaluated_track_count = 0U;
  std::uint32_t evaluated_position_count = 0U;
  bool vertex_track_resolved = false;
  bool opacity_track_resolved = false;
  bool uv_offset_u_track_resolved = false;
  bool uv_offset_v_track_resolved = false;

  bool operator==(const RetailFrontendTimelineEvaluation &) const = default;
};

// Per-widget-instance mutable state. It owns a clone of the resource positions
// and only resolved scalar channel values; it never owns, aliases, or mutates
// the immutable visual resource. OPACITY/UVOFF values remain separate because
// their eventual color/UV application and sampling rules are not yet proven.
class RetailFrontendVisualInstanceState final {
public:
  RetailFrontendVisualInstanceState(const RetailFrontendVisualInstanceState &) =
      default;
  RetailFrontendVisualInstanceState &
  operator=(const RetailFrontendVisualInstanceState &) = default;
  RetailFrontendVisualInstanceState(
      RetailFrontendVisualInstanceState &&) noexcept = default;
  RetailFrontendVisualInstanceState &
  operator=(RetailFrontendVisualInstanceState &&) noexcept = default;

  [[nodiscard]] const std::vector<asset::Float3IR> &positions() const noexcept {
    return positions_;
  }
  [[nodiscard]] const std::optional<float> &opacity() const noexcept {
    return opacity_;
  }
  [[nodiscard]] const std::optional<float> &uv_offset_u() const noexcept {
    return uv_offset_u_;
  }
  [[nodiscard]] const std::optional<float> &uv_offset_v() const noexcept {
    return uv_offset_v_;
  }
  [[nodiscard]] const std::optional<std::uint64_t> &
  last_live_tick() const noexcept {
    return last_live_tick_;
  }
  [[nodiscard]] const std::optional<float> &
  last_authored_timeline_tick() const noexcept {
    return last_authored_timeline_tick_;
  }

  bool operator==(const RetailFrontendVisualInstanceState &) const = default;

private:
  friend std::expected<RetailFrontendVisualInstanceState,
                       RetailFrontendTimelineError>
  CloneRetailFrontendVisualInstance(const asset::FrontendVisualNodeIR &,
                                    RetailFrontendTimelineLimits) noexcept;
  friend std::expected<RetailFrontendTimelineEvaluation,
                       RetailFrontendTimelineError>
  EvaluateRetailFrontendTimeline(RetailFrontendVisualInstanceState &,
                                 const asset::FrontendVisualNodeIR &,
                                 RetailFrontendTimelineInput,
                                 RetailFrontendTimelineLimits) noexcept;

  explicit RetailFrontendVisualInstanceState(
      std::vector<asset::Float3IR> positions,
      const std::uint32_t source_track_count) noexcept
      : positions_(std::move(positions)),
        source_track_count_(source_track_count) {}

  std::vector<asset::Float3IR> positions_;
  std::optional<float> opacity_;
  std::optional<float> uv_offset_u_;
  std::optional<float> uv_offset_v_;
  std::optional<std::uint64_t> last_live_tick_;
  std::optional<float> last_authored_timeline_tick_;
  std::uint32_t source_track_count_ = 0U;
};

using RetailFrontendVisualInstanceResult =
    std::expected<RetailFrontendVisualInstanceState,
                  RetailFrontendTimelineError>;
using RetailFrontendTimelineEvaluationResult =
    std::expected<RetailFrontendTimelineEvaluation,
                  RetailFrontendTimelineError>;

// [any thread; stateless/reentrant] Borrows one immutable visual resource for
// this call and returns one fully owned clone. The returned value stores no
// source view or pointer. This is a pure value boundary and is hot-reload-safe.
[[nodiscard]] RetailFrontendVisualInstanceResult
CloneRetailFrontendVisualInstance(
    const asset::FrontendVisualNodeIR &resource,
    RetailFrontendTimelineLimits limits = {}) noexcept;

// [any thread for distinct instances; externally synchronized per instance]
// Evaluates the established retail timeline rule: clamp to the first/last key
// outside the authored range and linearly interpolate adjacent keys inside it.
// Vertex positions are interpolated componentwise. Evaluation is transactional:
// every track resolves into temporary owned state before the instance is
// changed, so an error can never publish a partial successor.
[[nodiscard]] RetailFrontendTimelineEvaluationResult
EvaluateRetailFrontendTimeline(
    RetailFrontendVisualInstanceState &instance,
    const asset::FrontendVisualNodeIR &resource,
    RetailFrontendTimelineInput input,
    RetailFrontendTimelineLimits limits = {}) noexcept;

enum class RetailTitleAvailability : std::uint8_t {
  Unavailable = 0U,
  Available,
  Indeterminate,
};

enum class RetailTitleFocusRequest : std::uint8_t {
  None = 0U,
  CreateAgent,
  LoadAgent,
};

struct RetailTitleStateInput final {
  RetailTitleAvailability create_agent = RetailTitleAvailability::Indeterminate;
  RetailTitleAvailability saved_profile =
      RetailTitleAvailability::Indeterminate;
  RetailTitleFocusRequest focus = RetailTitleFocusRequest::None;

  bool operator==(const RetailTitleStateInput &) const = default;
};

struct RetailTitleControlState final {
  bool enabled = false;
  bool focused = false;

  bool operator==(const RetailTitleControlState &) const = default;
};

struct RetailTitleInstanceState final {
  RetailTitleControlState create_agent;
  RetailTitleControlState load_agent;

  bool operator==(const RetailTitleInstanceState &) const = default;
};

enum class RetailTitleStateError : std::uint8_t {
  IndeterminateAvailability = 0U,
  InvalidAvailability,
  InvalidFocusRequest,
  UnavailableFocus,
};

using RetailTitleStateResult =
    std::expected<RetailTitleInstanceState, RetailTitleStateError>;

// [any thread; stateless/reentrant] Reduces explicit native availability and
// focus inputs to owned Title control state. It does not inspect persistence,
// infer a default focus, dispatch retail actions, or map roles to asset names.
// A requested unavailable control fails closed instead of choosing a fallback.
[[nodiscard]] RetailTitleStateResult
ResolveRetailTitleInstanceState(RetailTitleStateInput input) noexcept;
} // namespace omega::frontend::presentation
