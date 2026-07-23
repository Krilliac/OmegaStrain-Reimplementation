#include "omega/frontend_presentation/retail_frontend_timeline.h"

#include <cstdint>
#include <iostream>
#include <limits>
#include <string_view>
#include <utility>
#include <variant>

namespace {
namespace presentation = omega::frontend::presentation;
using omega::asset::Float3IR;
using omega::asset::FrontendScalarAnimationKeyIR;
using omega::asset::FrontendScalarAnimationTarget;
using omega::asset::FrontendScalarAnimationTrackIR;
using omega::asset::FrontendVertexAnimationKeyIR;
using omega::asset::FrontendVertexAnimationSubtrackIR;
using omega::asset::FrontendVertexAnimationTrackIR;
using omega::asset::FrontendVisualNodeIR;
using presentation::RetailFrontendTimelineError;

int failures = 0;

void Check(const bool condition, const std::string_view message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    ++failures;
  }
}

template <typename Result>
void CheckError(const Result &result, const RetailFrontendTimelineError error,
                const std::string_view message) {
  Check(!result && result.error() == error, message);
}

[[nodiscard]] FrontendScalarAnimationTrackIR
MakeScalarTrack(const FrontendScalarAnimationTarget target, const float first,
                const float last) {
  return FrontendScalarAnimationTrackIR{
      .target = target,
      .keys =
          {
              FrontendScalarAnimationKeyIR{
                  .timeline_tick = 0.0F,
                  .value = first,
              },
              FrontendScalarAnimationKeyIR{
                  .timeline_tick = 30.0F,
                  .value = last,
              },
          },
  };
}

[[nodiscard]] FrontendVisualNodeIR MakeAnimatedResource() {
  FrontendVertexAnimationTrackIR vertex{
      .position_subtracks =
          {
              FrontendVertexAnimationSubtrackIR{
                  .keys =
                      {
                          FrontendVertexAnimationKeyIR{
                              .timeline_tick = 0.0F,
                              .position = {.x = 10.0F, .y = 20.0F, .z = 30.0F},
                          },
                          FrontendVertexAnimationKeyIR{
                              .timeline_tick = 30.0F,
                              .position = {.x = 40.0F, .y = 50.0F, .z = 60.0F},
                          },
                      },
              },
              FrontendVertexAnimationSubtrackIR{
                  .keys =
                      {
                          FrontendVertexAnimationKeyIR{
                              .timeline_tick = 0.0F,
                              .position =
                                  {.x = -10.0F, .y = -20.0F, .z = -30.0F},
                          },
                          FrontendVertexAnimationKeyIR{
                              .timeline_tick = 30.0F,
                              .position =
                                  {.x = -40.0F, .y = -50.0F, .z = -60.0F},
                          },
                      },
              },
          },
  };
  FrontendVisualNodeIR resource{
      .identifier = "SYNTHETIC",
      .positions =
          {
              Float3IR{.x = 1.0F, .y = 2.0F, .z = 3.0F},
              Float3IR{.x = 4.0F, .y = 5.0F, .z = 6.0F},
          },
  };
  resource.animation_tracks.emplace_back(std::move(vertex));
  resource.animation_tracks.emplace_back(
      MakeScalarTrack(FrontendScalarAnimationTarget::Opacity, 1.0F, 0.25F));
  resource.animation_tracks.emplace_back(
      MakeScalarTrack(FrontendScalarAnimationTarget::UvOffsetU, 0.0F, 0.5F));
  resource.animation_tracks.emplace_back(
      MakeScalarTrack(FrontendScalarAnimationTarget::UvOffsetV, 0.0F, -0.5F));
  return resource;
}

void TestIndependentOwnedInstancesAndFourTrackFamilies() {
  auto resource = MakeAnimatedResource();
  const auto immutable_copy = resource;
  auto first_result = presentation::CloneRetailFrontendVisualInstance(resource);
  auto second_result =
      presentation::CloneRetailFrontendVisualInstance(resource);
  Check(first_result.has_value() && second_result.has_value(),
        "two instances clone from one immutable visual resource");
  if (!first_result || !second_result)
    return;

  auto first = std::move(*first_result);
  auto second = std::move(*second_result);
  const auto base_positions = resource.positions;
  const auto first_evaluation = presentation::EvaluateRetailFrontendTimeline(
      first, resource, {.live_tick = 700U, .authored_timeline_tick = 0.0F});
  Check(first_evaluation && first_evaluation->evaluated_track_count == 4U &&
            first_evaluation->evaluated_position_count == 2U &&
            first_evaluation->vertex_track_resolved &&
            first_evaluation->opacity_track_resolved &&
            first_evaluation->uv_offset_u_track_resolved &&
            first_evaluation->uv_offset_v_track_resolved,
        "one boundary evaluation resolves all four retained track families");
  Check(first.positions()[0] == Float3IR{.x = 10.0F, .y = 20.0F, .z = 30.0F} &&
            first.positions()[1] ==
                Float3IR{.x = -10.0F, .y = -20.0F, .z = -30.0F} &&
            first.opacity() == 1.0F && first.uv_offset_u() == 0.0F &&
            first.uv_offset_v() == 0.0F,
        "resolved values are retained in the instance without scalar "
        "application");
  Check(second.positions() == base_positions && !second.opacity() &&
            !second.uv_offset_u() && !second.uv_offset_v() &&
            !second.last_live_tick(),
        "evaluating one clone does not mutate a sibling clone");
  Check(resource == immutable_copy,
        "timeline evaluation never mutates the immutable visual resource");

  const auto second_evaluation = presentation::EvaluateRetailFrontendTimeline(
      second, resource, {.live_tick = 2U, .authored_timeline_tick = 30.0F});
  Check(second_evaluation &&
            second.positions()[0] ==
                Float3IR{.x = 40.0F, .y = 50.0F, .z = 60.0F} &&
            second.opacity() == 0.25F && second.uv_offset_u() == 0.5F &&
            second.uv_offset_v() == -0.5F && second.last_live_tick() == 2U &&
            second.last_authored_timeline_tick() == 30.0F,
        "the final exact key is an independently selectable boundary");

  const auto owned_after_source_destruction = [] {
    const auto temporary = MakeAnimatedResource();
    return presentation::CloneRetailFrontendVisualInstance(temporary);
  }();
  Check(owned_after_source_destruction &&
            owned_after_source_destruction->positions() == base_positions,
        "a clone owns its state after source-resource destruction");
}

void TestClampedLinearInterpolationAndLiveTickContract() {
  const auto resource = MakeAnimatedResource();
  auto created = presentation::CloneRetailFrontendVisualInstance(resource);
  Check(created.has_value(), "boundary fixture clones");
  if (!created)
    return;
  auto instance = std::move(*created);

  Check(presentation::EvaluateRetailFrontendTimeline(
            instance, resource,
            {.live_tick = 100U, .authored_timeline_tick = 0.0F})
            .has_value(),
        "the first exact key evaluates");
  Check(presentation::EvaluateRetailFrontendTimeline(
            instance, resource,
            {.live_tick = 101U, .authored_timeline_tick = 15.0F})
            .has_value() &&
            instance.positions()[0] ==
                Float3IR{.x = 25.0F, .y = 35.0F, .z = 45.0F} &&
            instance.positions()[1] ==
                Float3IR{.x = -25.0F, .y = -35.0F, .z = -45.0F} &&
            instance.opacity() == 0.625F &&
            instance.uv_offset_u() == 0.25F &&
            instance.uv_offset_v() == -0.25F,
        "a between-key tick linearly interpolates scalar and vertex channels");

  Check(presentation::EvaluateRetailFrontendTimeline(
            instance, resource,
            {.live_tick = 102U, .authored_timeline_tick = -1.0F})
            .has_value() &&
            instance.positions()[0] ==
                Float3IR{.x = 10.0F, .y = 20.0F, .z = 30.0F} &&
            instance.opacity() == 1.0F,
        "a tick before the authored range clamps to the first key");
  Check(presentation::EvaluateRetailFrontendTimeline(
            instance, resource,
            {.live_tick = 103U, .authored_timeline_tick = 31.0F})
            .has_value() &&
            instance.positions()[0] ==
                Float3IR{.x = 40.0F, .y = 50.0F, .z = 60.0F} &&
            instance.opacity() == 0.25F,
        "a tick after the authored range clamps to the last key");
  const auto clamped_boundary = instance;

  Check(presentation::EvaluateRetailFrontendTimeline(
            instance, resource,
            {.live_tick = 103U, .authored_timeline_tick = 31.0F})
            .has_value(),
        "repeating one identical live/authored input is deterministic");
  CheckError(presentation::EvaluateRetailFrontendTimeline(
                 instance, resource,
                 {.live_tick = 103U, .authored_timeline_tick = 30.0F}),
             RetailFrontendTimelineError::InconsistentLiveTick,
             "one live tick cannot be rebound to a different authored tick");
  CheckError(presentation::EvaluateRetailFrontendTimeline(
                 instance, resource,
                 {.live_tick = 102U, .authored_timeline_tick = 0.0F}),
             RetailFrontendTimelineError::NonMonotonicLiveTick,
             "live tick regression is categorical");
  CheckError(
      presentation::EvaluateRetailFrontendTimeline(
          instance, resource,
          {.live_tick = 104U,
           .authored_timeline_tick = std::numeric_limits<float>::infinity()}),
      RetailFrontendTimelineError::NonFiniteValue,
      "a nonfinite authored tick is rejected before evaluation");
  Check(instance == clamped_boundary,
        "all invalid clock inputs preserve the prior state");

  FrontendVisualNodeIR interior_key_resource;
  interior_key_resource.animation_tracks.emplace_back(
      FrontendScalarAnimationTrackIR{
          .target = FrontendScalarAnimationTarget::Opacity,
          .keys = {
              {.timeline_tick = 0.0F, .value = 0.25F},
              {.timeline_tick = 10.0F, .value = 0.75F},
              {.timeline_tick = 20.0F, .value = 0.5F},
          },
      });
  auto interior_instance =
      presentation::CloneRetailFrontendVisualInstance(interior_key_resource);
  Check(interior_instance &&
            presentation::EvaluateRetailFrontendTimeline(
                *interior_instance, interior_key_resource,
                {.live_tick = 1U, .authored_timeline_tick = 10.0F}) &&
            interior_instance->opacity() == 0.75F,
        "an exact interior key publishes its authored value without blending");

  FrontendVisualNodeIR single_key_resource;
  single_key_resource.animation_tracks.emplace_back(
      FrontendScalarAnimationTrackIR{
          .target = FrontendScalarAnimationTarget::Opacity,
          .keys = {{.timeline_tick = 4.0F, .value = 0.375F}},
      });
  auto single_key_instance =
      presentation::CloneRetailFrontendVisualInstance(single_key_resource);
  Check(single_key_instance &&
            presentation::EvaluateRetailFrontendTimeline(
                *single_key_instance, single_key_resource,
                {.live_tick = 1U, .authored_timeline_tick = -100.0F}) &&
            single_key_instance->opacity() == 0.375F &&
            presentation::EvaluateRetailFrontendTimeline(
                *single_key_instance, single_key_resource,
                {.live_tick = 2U, .authored_timeline_tick = 100.0F}) &&
            single_key_instance->opacity() == 0.375F,
        "a one-key track clamps to its sole authored value on both sides");
}

void TestMalformedAmbiguousAndLimitedResources() {
  auto malformed = MakeAnimatedResource();
  auto &vertex =
      std::get<FrontendVertexAnimationTrackIR>(malformed.animation_tracks[0]);
  vertex.position_subtracks[0].keys.clear();
  CheckError(presentation::CloneRetailFrontendVisualInstance(malformed),
             RetailFrontendTimelineError::MalformedTrack,
             "an empty vertex subtrack is malformed");

  malformed = MakeAnimatedResource();
  std::get<FrontendVertexAnimationTrackIR>(malformed.animation_tracks[0])
      .position_subtracks.pop_back();
  CheckError(presentation::CloneRetailFrontendVisualInstance(malformed),
             RetailFrontendTimelineError::MalformedTrack,
             "vertex subtrack count must match the source positions exactly");

  malformed = MakeAnimatedResource();
  auto &scalar =
      std::get<FrontendScalarAnimationTrackIR>(malformed.animation_tracks[1]);
  scalar.keys[1].timeline_tick = scalar.keys[0].timeline_tick;
  CheckError(presentation::CloneRetailFrontendVisualInstance(malformed),
             RetailFrontendTimelineError::NonMonotonicKeys,
             "duplicate scalar key ticks are not accepted as an ordering rule");
  scalar.keys[1].timeline_tick = -1.0F;
  CheckError(presentation::CloneRetailFrontendVisualInstance(malformed),
             RetailFrontendTimelineError::NonMonotonicKeys,
             "decreasing scalar key ticks are rejected");

  malformed = MakeAnimatedResource();
  auto &vertex_keys =
      std::get<FrontendVertexAnimationTrackIR>(malformed.animation_tracks[0])
          .position_subtracks[0]
          .keys;
  vertex_keys[1].timeline_tick = vertex_keys[0].timeline_tick;
  CheckError(presentation::CloneRetailFrontendVisualInstance(malformed),
             RetailFrontendTimelineError::NonMonotonicKeys,
             "duplicate vertex key ticks are rejected");

  malformed = MakeAnimatedResource();
  std::get<FrontendScalarAnimationTrackIR>(malformed.animation_tracks[1])
      .keys.clear();
  CheckError(presentation::CloneRetailFrontendVisualInstance(malformed),
             RetailFrontendTimelineError::MalformedTrack,
             "an empty scalar track is malformed");

  malformed = MakeAnimatedResource();
  std::get<FrontendVertexAnimationTrackIR>(malformed.animation_tracks[0])
      .position_subtracks[0]
      .keys[0]
      .position.x = std::numeric_limits<float>::quiet_NaN();
  CheckError(presentation::CloneRetailFrontendVisualInstance(malformed),
             RetailFrontendTimelineError::NonFiniteValue,
             "nonfinite vertex values are rejected");
  malformed = MakeAnimatedResource();
  std::get<FrontendScalarAnimationTrackIR>(malformed.animation_tracks[1])
      .keys[0]
      .value = std::numeric_limits<float>::infinity();
  CheckError(presentation::CloneRetailFrontendVisualInstance(malformed),
             RetailFrontendTimelineError::NonFiniteValue,
             "nonfinite scalar values are rejected");

  auto ambiguous = MakeAnimatedResource();
  ambiguous.animation_tracks[3] =
      MakeScalarTrack(FrontendScalarAnimationTarget::Opacity, 0.75F, 0.5F);
  CheckError(presentation::CloneRetailFrontendVisualInstance(ambiguous),
             RetailFrontendTimelineError::AmbiguousTrackTarget,
             "duplicate scalar targets fail instead of choosing a lane order");

  auto invalid_target = MakeAnimatedResource();
  std::get<FrontendScalarAnimationTrackIR>(invalid_target.animation_tracks[1])
      .target = static_cast<FrontendScalarAnimationTarget>(255U);
  CheckError(presentation::CloneRetailFrontendVisualInstance(invalid_target),
             RetailFrontendTimelineError::MalformedTrack,
             "an invalid scalar target cannot masquerade as a known family");

  const auto resource = MakeAnimatedResource();
  CheckError(presentation::CloneRetailFrontendVisualInstance(
                 resource, {.maximum_positions = 1U,
                            .maximum_tracks = 4U,
                            .maximum_total_keys = 10U}),
             RetailFrontendTimelineError::LimitExceeded,
             "the caller can tighten the position limit");
  CheckError(presentation::CloneRetailFrontendVisualInstance(
                 resource, {.maximum_positions = 2U,
                            .maximum_tracks = 3U,
                            .maximum_total_keys = 10U}),
             RetailFrontendTimelineError::LimitExceeded,
             "the caller can tighten the track limit");
  CheckError(presentation::CloneRetailFrontendVisualInstance(
                 resource, {.maximum_positions = 2U,
                            .maximum_tracks = 4U,
                            .maximum_total_keys = 9U}),
             RetailFrontendTimelineError::LimitExceeded,
             "cumulative keys are bounded before state allocation");
  Check(presentation::CloneRetailFrontendVisualInstance(
            resource, {.maximum_positions = 2U,
                       .maximum_tracks = 4U,
                       .maximum_total_keys = 10U})
            .has_value(),
        "exact caller limits accept the complete resource");
  CheckError(presentation::CloneRetailFrontendVisualInstance(
                 resource, {.maximum_positions = 0U,
                            .maximum_tracks = 4U,
                            .maximum_total_keys = 10U}),
             RetailFrontendTimelineError::InvalidLimits,
             "zero limits are invalid rather than unbounded");
  CheckError(
      presentation::CloneRetailFrontendVisualInstance(
          resource,
          {.maximum_positions =
               presentation::kRetailFrontendTimelineMaximumPositions,
           .maximum_tracks = presentation::kRetailFrontendTimelineMaximumTracks,
           .maximum_total_keys = std::numeric_limits<std::uint64_t>::max()}),
      RetailFrontendTimelineError::InvalidLimits,
      "a hostile raised key ceiling cannot create overflow headroom");

  FrontendVisualNodeIR static_resource{
      .positions = {Float3IR{.x = 3.0F, .y = 2.0F, .z = 1.0F}},
  };
  auto static_instance =
      presentation::CloneRetailFrontendVisualInstance(static_resource);
  Check(static_instance.has_value(), "an animation-free visual still clones");
  if (static_instance) {
    const auto static_evaluation = presentation::EvaluateRetailFrontendTimeline(
        *static_instance, static_resource,
        {.live_tick = 9U, .authored_timeline_tick = 17.5F});
    Check(static_evaluation && static_evaluation->evaluated_track_count == 0U &&
              !static_evaluation->vertex_track_resolved &&
              !static_instance->opacity() && !static_instance->uv_offset_u() &&
              !static_instance->uv_offset_v(),
          "an animation-free visual publishes no invented scalar defaults");
  }
}

void TestResourceMismatchAndTransactionalFailure() {
  const auto resource = MakeAnimatedResource();
  auto created = presentation::CloneRetailFrontendVisualInstance(resource);
  Check(created.has_value(), "transaction fixture clones");
  if (!created)
    return;
  auto instance = std::move(*created);
  Check(
      presentation::EvaluateRetailFrontendTimeline(
          instance, resource, {.live_tick = 8U, .authored_timeline_tick = 0.0F})
          .has_value(),
      "transaction fixture establishes prior state");
  const auto prior = instance;

  auto mismatched = resource;
  mismatched.positions.push_back({.x = 7.0F, .y = 8.0F, .z = 9.0F});
  std::get<FrontendVertexAnimationTrackIR>(mismatched.animation_tracks[0])
      .position_subtracks.push_back(FrontendVertexAnimationSubtrackIR{
          .keys = {
              {.timeline_tick = 0.0F, .position = {}},
              {.timeline_tick = 30.0F, .position = {}},
          }});
  CheckError(
      presentation::EvaluateRetailFrontendTimeline(
          instance, mismatched,
          {.live_tick = 9U, .authored_timeline_tick = 30.0F}),
      RetailFrontendTimelineError::InstanceResourceMismatch,
      "a state cannot be rebound to a differently shaped visual resource");
  Check(instance == prior, "resource mismatch publishes no state");

  auto differing_ranges = resource;
  auto &opacity = std::get<FrontendScalarAnimationTrackIR>(
      differing_ranges.animation_tracks[1]);
  opacity.keys[1].timeline_tick = 20.0F;
  const auto clamped = presentation::EvaluateRetailFrontendTimeline(
      instance, differing_ranges,
      {.live_tick = 9U, .authored_timeline_tick = 25.0F});
  Check(clamped && instance.positions()[0] ==
                       Float3IR{.x = 35.0F, .y = 45.0F, .z = 55.0F} &&
            instance.opacity() == 0.25F,
        "each track clamps against its own authored key range");
  Check(instance != prior,
        "a complete clamped evaluation publishes one atomic successor");
}

void TestExplicitTitleAvailability() {
  using presentation::RetailTitleAvailability;
  using presentation::RetailTitleFocusRequest;
  using presentation::RetailTitleStateError;

  const auto empty = presentation::ResolveRetailTitleInstanceState({
      .create_agent = RetailTitleAvailability::Available,
      .saved_profile = RetailTitleAvailability::Unavailable,
      .focus = RetailTitleFocusRequest::CreateAgent,
  });
  Check(empty && empty->create_agent.enabled && empty->create_agent.focused &&
            !empty->load_agent.enabled && !empty->load_agent.focused,
        "an explicit empty-save input disables Load Agent and permits Create "
        "focus");

  const auto saved = presentation::ResolveRetailTitleInstanceState({
      .create_agent = RetailTitleAvailability::Available,
      .saved_profile = RetailTitleAvailability::Available,
      .focus = RetailTitleFocusRequest::LoadAgent,
  });
  Check(
      saved && saved->create_agent.enabled && !saved->create_agent.focused &&
          saved->load_agent.enabled && saved->load_agent.focused,
      "available saved-profile state enables explicitly requested Load focus");

  const auto no_default = presentation::ResolveRetailTitleInstanceState({
      .create_agent = RetailTitleAvailability::Available,
      .saved_profile = RetailTitleAvailability::Available,
      .focus = RetailTitleFocusRequest::None,
  });
  Check(no_default && !no_default->create_agent.focused &&
            !no_default->load_agent.focused,
        "absence of a focus request does not guess the retail default");

  const auto unavailable_focus = presentation::ResolveRetailTitleInstanceState({
      .create_agent = RetailTitleAvailability::Available,
      .saved_profile = RetailTitleAvailability::Unavailable,
      .focus = RetailTitleFocusRequest::LoadAgent,
  });
  Check(!unavailable_focus && unavailable_focus.error() ==
                                  RetailTitleStateError::UnavailableFocus,
        "an unavailable Load focus fails instead of falling back");

  const auto unknown = presentation::ResolveRetailTitleInstanceState({
      .create_agent = RetailTitleAvailability::Available,
      .saved_profile = RetailTitleAvailability::Indeterminate,
      .focus = RetailTitleFocusRequest::None,
  });
  Check(!unknown &&
            unknown.error() == RetailTitleStateError::IndeterminateAvailability,
        "indeterminate profile availability cannot publish Title state");
  const auto invalid_availability =
      presentation::ResolveRetailTitleInstanceState({
          .create_agent = static_cast<RetailTitleAvailability>(255U),
          .saved_profile = RetailTitleAvailability::Available,
          .focus = RetailTitleFocusRequest::None,
      });
  Check(!invalid_availability && invalid_availability.error() ==
                                     RetailTitleStateError::InvalidAvailability,
        "invalid availability is categorical");
  const auto invalid_focus = presentation::ResolveRetailTitleInstanceState({
      .create_agent = RetailTitleAvailability::Available,
      .saved_profile = RetailTitleAvailability::Available,
      .focus = static_cast<RetailTitleFocusRequest>(255U),
  });
  Check(!invalid_focus &&
            invalid_focus.error() == RetailTitleStateError::InvalidFocusRequest,
        "invalid focus input cannot publish state");
}
} // namespace

int main() {
  TestIndependentOwnedInstancesAndFourTrackFamilies();
  TestClampedLinearInterpolationAndLiveTickContract();
  TestMalformedAmbiguousAndLimitedResources();
  TestResourceMismatchAndTransactionalFailure();
  TestExplicitTitleAvailability();

  if (failures != 0) {
    std::cerr << failures << " test(s) failed\n";
    return 1;
  }
  return 0;
}
