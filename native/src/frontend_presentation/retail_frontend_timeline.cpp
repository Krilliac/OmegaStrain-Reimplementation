#include "omega/frontend_presentation/retail_frontend_timeline.h"

#include <algorithm>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <iterator>
#include <new>
#include <utility>
#include <variant>

namespace omega::frontend::presentation {
namespace {
struct ValidatedTrackLayout final {
  const asset::FrontendVertexAnimationTrackIR *vertex = nullptr;
  const asset::FrontendScalarAnimationTrackIR *opacity = nullptr;
  const asset::FrontendScalarAnimationTrackIR *uv_offset_u = nullptr;
  const asset::FrontendScalarAnimationTrackIR *uv_offset_v = nullptr;
  std::uint64_t total_keys = 0U;
};

[[nodiscard]] bool
IsValidLimits(const RetailFrontendTimelineLimits limits) noexcept {
  return limits.maximum_positions != 0U &&
         limits.maximum_positions <= kRetailFrontendTimelineMaximumPositions &&
         limits.maximum_tracks != 0U &&
         limits.maximum_tracks <= kRetailFrontendTimelineMaximumTracks &&
         limits.maximum_total_keys != 0U &&
         limits.maximum_total_keys <= kRetailFrontendTimelineMaximumKeys;
}

[[nodiscard]] bool IsFinite(const asset::Float3IR &value) noexcept {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z);
}

[[nodiscard]] std::expected<void, RetailFrontendTimelineError>
DebitKeys(const std::size_t count, std::uint64_t &total,
          const RetailFrontendTimelineLimits limits) noexcept {
  const auto count_u64 = static_cast<std::uint64_t>(count);
  if (count_u64 > limits.maximum_total_keys - total)
    return std::unexpected(RetailFrontendTimelineError::LimitExceeded);
  total += count_u64;
  return {};
}

template <typename Key>
[[nodiscard]] std::expected<void, RetailFrontendTimelineError>
ValidateKeys(const std::vector<Key> &keys, std::uint64_t &total,
             const RetailFrontendTimelineLimits limits) noexcept {
  if (keys.empty())
    return std::unexpected(RetailFrontendTimelineError::MalformedTrack);
  const auto debited = DebitKeys(keys.size(), total, limits);
  if (!debited)
    return debited;

  float previous_tick = 0.0F;
  for (std::size_t index = 0U; index < keys.size(); ++index) {
    const auto &key = keys[index];
    if (!std::isfinite(key.timeline_tick))
      return std::unexpected(RetailFrontendTimelineError::NonFiniteValue);
    if constexpr (std::same_as<Key, asset::FrontendScalarAnimationKeyIR>) {
      if (!std::isfinite(key.value)) {
        return std::unexpected(RetailFrontendTimelineError::NonFiniteValue);
      }
    } else {
      if (!IsFinite(key.position)) {
        return std::unexpected(RetailFrontendTimelineError::NonFiniteValue);
      }
    }
    if (index != 0U && key.timeline_tick <= previous_tick) {
      return std::unexpected(RetailFrontendTimelineError::NonMonotonicKeys);
    }
    previous_tick = key.timeline_tick;
  }
  return {};
}

[[nodiscard]] std::expected<ValidatedTrackLayout, RetailFrontendTimelineError>
ValidateResource(const asset::FrontendVisualNodeIR &resource,
                 const RetailFrontendTimelineLimits limits) noexcept {
  if (!IsValidLimits(limits))
    return std::unexpected(RetailFrontendTimelineError::InvalidLimits);
  if (resource.positions.size() > limits.maximum_positions ||
      resource.animation_tracks.size() > limits.maximum_tracks) {
    return std::unexpected(RetailFrontendTimelineError::LimitExceeded);
  }
  for (const auto &position : resource.positions) {
    if (!IsFinite(position))
      return std::unexpected(RetailFrontendTimelineError::NonFiniteValue);
  }

  ValidatedTrackLayout layout;
  for (const auto &variant : resource.animation_tracks) {
    if (variant.valueless_by_exception())
      return std::unexpected(RetailFrontendTimelineError::MalformedTrack);

    if (const auto *const vertex =
            std::get_if<asset::FrontendVertexAnimationTrackIR>(&variant)) {
      if (layout.vertex != nullptr) {
        return std::unexpected(
            RetailFrontendTimelineError::AmbiguousTrackTarget);
      }
      if (vertex->position_subtracks.empty() ||
          vertex->position_subtracks.size() != resource.positions.size()) {
        return std::unexpected(RetailFrontendTimelineError::MalformedTrack);
      }
      for (const auto &subtrack : vertex->position_subtracks) {
        const auto valid =
            ValidateKeys(subtrack.keys, layout.total_keys, limits);
        if (!valid)
          return std::unexpected(valid.error());
      }
      layout.vertex = vertex;
      continue;
    }

    const auto *const scalar =
        std::get_if<asset::FrontendScalarAnimationTrackIR>(&variant);
    if (scalar == nullptr)
      return std::unexpected(RetailFrontendTimelineError::MalformedTrack);
    const auto valid = ValidateKeys(scalar->keys, layout.total_keys, limits);
    if (!valid)
      return std::unexpected(valid.error());

    const asset::FrontendScalarAnimationTrackIR **target = nullptr;
    switch (scalar->target) {
    case asset::FrontendScalarAnimationTarget::Opacity:
      target = &layout.opacity;
      break;
    case asset::FrontendScalarAnimationTarget::UvOffsetU:
      target = &layout.uv_offset_u;
      break;
    case asset::FrontendScalarAnimationTarget::UvOffsetV:
      target = &layout.uv_offset_v;
      break;
    default:
      return std::unexpected(RetailFrontendTimelineError::MalformedTrack);
    }
    if (*target != nullptr) {
      return std::unexpected(RetailFrontendTimelineError::AmbiguousTrackTarget);
    }
    *target = scalar;
  }
  return layout;
}

template <typename Key> struct KeyInterval final {
  const Key *lower = nullptr;
  const Key *upper = nullptr;
  double factor = 0.0;
};

template <typename Key>
[[nodiscard]] KeyInterval<Key>
FindKeyInterval(const std::vector<Key> &keys, const float tick) noexcept {
  if (tick <= keys.front().timeline_tick)
    return {.lower = &keys.front(), .upper = &keys.front()};
  if (tick >= keys.back().timeline_tick)
    return {.lower = &keys.back(), .upper = &keys.back()};

  const auto upper =
      std::lower_bound(keys.begin(), keys.end(), tick,
                       [](const Key &key, const float requested_tick) {
                         return key.timeline_tick < requested_tick;
                       });
  if (upper->timeline_tick == tick)
    return {.lower = &*upper, .upper = &*upper};
  const auto lower = std::prev(upper);
  const double lower_tick = static_cast<double>(lower->timeline_tick);
  const double upper_tick = static_cast<double>(upper->timeline_tick);
  return {
      .lower = &*lower,
      .upper = &*upper,
      .factor = (static_cast<double>(tick) - lower_tick) /
                (upper_tick - lower_tick),
  };
}

[[nodiscard]] float InterpolateScalar(
    const KeyInterval<asset::FrontendScalarAnimationKeyIR> interval) noexcept {
  const double lower = static_cast<double>(interval.lower->value);
  const double upper = static_cast<double>(interval.upper->value);
  return static_cast<float>(lower + (upper - lower) * interval.factor);
}

[[nodiscard]] asset::Float3IR InterpolatePosition(
    const KeyInterval<asset::FrontendVertexAnimationKeyIR> interval) noexcept {
  const auto component = [factor = interval.factor](const float lower,
                                                     const float upper) {
    return static_cast<float>(static_cast<double>(lower) +
                              (static_cast<double>(upper) -
                               static_cast<double>(lower)) *
                                  factor);
  };
  return {
      .x = component(interval.lower->position.x, interval.upper->position.x),
      .y = component(interval.lower->position.y, interval.upper->position.y),
      .z = component(interval.lower->position.z, interval.upper->position.z),
  };
}

[[nodiscard]] std::expected<bool, RetailTitleStateError>
ResolveAvailability(const RetailTitleAvailability availability) noexcept {
  switch (availability) {
  case RetailTitleAvailability::Unavailable:
    return false;
  case RetailTitleAvailability::Available:
    return true;
  case RetailTitleAvailability::Indeterminate:
    return std::unexpected(RetailTitleStateError::IndeterminateAvailability);
  default:
    return std::unexpected(RetailTitleStateError::InvalidAvailability);
  }
}
} // namespace

RetailFrontendVisualInstanceResult CloneRetailFrontendVisualInstance(
    const asset::FrontendVisualNodeIR &resource,
    const RetailFrontendTimelineLimits limits) noexcept {
  const auto validated = ValidateResource(resource, limits);
  if (!validated)
    return std::unexpected(validated.error());

  try {
    return RetailFrontendVisualInstanceState(
        resource.positions,
        static_cast<std::uint32_t>(resource.animation_tracks.size()));
  } catch (const std::bad_alloc &) {
    return std::unexpected(RetailFrontendTimelineError::AllocationFailure);
  }
}

RetailFrontendTimelineEvaluationResult EvaluateRetailFrontendTimeline(
    RetailFrontendVisualInstanceState &instance,
    const asset::FrontendVisualNodeIR &resource,
    const RetailFrontendTimelineInput input,
    const RetailFrontendTimelineLimits limits) noexcept {
  if (!std::isfinite(input.authored_timeline_tick))
    return std::unexpected(RetailFrontendTimelineError::NonFiniteValue);

  const auto layout = ValidateResource(resource, limits);
  if (!layout)
    return std::unexpected(layout.error());
  if (instance.positions_.size() != resource.positions.size() ||
      instance.source_track_count_ !=
          static_cast<std::uint32_t>(resource.animation_tracks.size())) {
    return std::unexpected(
        RetailFrontendTimelineError::InstanceResourceMismatch);
  }
  if (instance.last_live_tick_) {
    if (input.live_tick < *instance.last_live_tick_) {
      return std::unexpected(RetailFrontendTimelineError::NonMonotonicLiveTick);
    }
    if (input.live_tick == *instance.last_live_tick_ &&
        input.authored_timeline_tick !=
            *instance.last_authored_timeline_tick_) {
      return std::unexpected(RetailFrontendTimelineError::InconsistentLiveTick);
    }
  }

  std::vector<asset::Float3IR> positions;
  try {
    positions = resource.positions;
  } catch (const std::bad_alloc &) {
    return std::unexpected(RetailFrontendTimelineError::AllocationFailure);
  }
  std::optional<float> opacity;
  std::optional<float> uv_offset_u;
  std::optional<float> uv_offset_v;
  RetailFrontendTimelineEvaluation evaluation{
      .evaluated_track_count =
          static_cast<std::uint32_t>(resource.animation_tracks.size()),
  };

  for (const auto &variant : resource.animation_tracks) {
    if (const auto *const vertex =
            std::get_if<asset::FrontendVertexAnimationTrackIR>(&variant)) {
      for (std::size_t index = 0U; index < vertex->position_subtracks.size();
           ++index) {
        const auto interval =
            FindKeyInterval(vertex->position_subtracks[index].keys,
                            input.authored_timeline_tick);
        positions[index] = InterpolatePosition(interval);
      }
      evaluation.vertex_track_resolved = true;
      evaluation.evaluated_position_count =
          static_cast<std::uint32_t>(positions.size());
      continue;
    }

    const auto &scalar =
        std::get<asset::FrontendScalarAnimationTrackIR>(variant);
    const auto interval =
        FindKeyInterval(scalar.keys, input.authored_timeline_tick);
    const float value = InterpolateScalar(interval);
    switch (scalar.target) {
    case asset::FrontendScalarAnimationTarget::Opacity:
      opacity = value;
      evaluation.opacity_track_resolved = true;
      break;
    case asset::FrontendScalarAnimationTarget::UvOffsetU:
      uv_offset_u = value;
      evaluation.uv_offset_u_track_resolved = true;
      break;
    case asset::FrontendScalarAnimationTarget::UvOffsetV:
      uv_offset_v = value;
      evaluation.uv_offset_v_track_resolved = true;
      break;
    default:
      return std::unexpected(RetailFrontendTimelineError::MalformedTrack);
    }
  }

  instance.positions_ = std::move(positions);
  instance.opacity_ = opacity;
  instance.uv_offset_u_ = uv_offset_u;
  instance.uv_offset_v_ = uv_offset_v;
  instance.last_live_tick_ = input.live_tick;
  instance.last_authored_timeline_tick_ = input.authored_timeline_tick;
  return evaluation;
}

RetailTitleStateResult
ResolveRetailTitleInstanceState(const RetailTitleStateInput input) noexcept {
  const auto create_enabled = ResolveAvailability(input.create_agent);
  if (!create_enabled)
    return std::unexpected(create_enabled.error());
  const auto load_enabled = ResolveAvailability(input.saved_profile);
  if (!load_enabled)
    return std::unexpected(load_enabled.error());

  RetailTitleInstanceState state{
      .create_agent = {.enabled = *create_enabled},
      .load_agent = {.enabled = *load_enabled},
  };
  switch (input.focus) {
  case RetailTitleFocusRequest::None:
    break;
  case RetailTitleFocusRequest::CreateAgent:
    if (!state.create_agent.enabled)
      return std::unexpected(RetailTitleStateError::UnavailableFocus);
    state.create_agent.focused = true;
    break;
  case RetailTitleFocusRequest::LoadAgent:
    if (!state.load_agent.enabled)
      return std::unexpected(RetailTitleStateError::UnavailableFocus);
    state.load_agent.focused = true;
    break;
  default:
    return std::unexpected(RetailTitleStateError::InvalidFocusRequest);
  }
  return state;
}
} // namespace omega::frontend::presentation
