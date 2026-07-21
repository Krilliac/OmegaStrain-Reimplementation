#pragma once

#include "opening_movie_player.h"

#include "omega/media/media_foundation_h262_decoder.h"

namespace omega::app::detail {
// Creation-time InvalidConfiguration can be derived from the inspected H.262
// sequence dimensions, so classify it as a rejected source. The same decoder
// code at runtime represents a player/decoder invariant failure and is mapped
// separately by OpeningMoviePlayer.
[[nodiscard]] constexpr OpeningMoviePlayerErrorCode
MapOpeningMovieDecoderCreationError(
    const media::MediaFoundationH262DecoderErrorCode code) noexcept {
  using DecoderCode = media::MediaFoundationH262DecoderErrorCode;
  switch (code) {
  case DecoderCode::InvalidConfiguration:
    return OpeningMoviePlayerErrorCode::H262StreamRejected;
  case DecoderCode::UnsupportedPlatform:
  case DecoderCode::InitializationFailed:
  case DecoderCode::DecoderUnavailable:
  case DecoderCode::MediaTypeRejected:
  case DecoderCode::OutputTypeUnavailable:
    return OpeningMoviePlayerErrorCode::DecoderUnavailable;
  case DecoderCode::AllocationFailed:
    return OpeningMoviePlayerErrorCode::AllocationFailed;
  case DecoderCode::WrongThread:
  case DecoderCode::AlreadyDrained:
  case DecoderCode::DecoderPoisoned:
  case DecoderCode::InputLimitExceeded:
  case DecoderCode::OutputLimitExceeded:
  case DecoderCode::FrameLimitExceeded:
  case DecoderCode::UnsupportedOutputLayout:
  case DecoderCode::TransformFailure:
    return OpeningMoviePlayerErrorCode::DecoderFailed;
  }
  return OpeningMoviePlayerErrorCode::DecoderFailed;
}

// Runtime mapping, applied only to a decoder that Create already accepted.
//
// OpeningMoviePlayerErrorCode::WrongThread is reserved for the player's own
// non-mutating boundary check, which runs before any decoder call and reports
// without touching player state. The player and its decoder capture the same
// creating thread, so a decoder that disagrees at runtime is a broken pair
// invariant, not a caller mistake - and the player must poison itself, which
// the WrongThread contract forbids. Classify it as DecoderFailed instead so the
// two codes keep disjoint meanings. AllocationFailed stays distinct because it
// is a genuine resource condition rather than an invariant failure.
[[nodiscard]] constexpr OpeningMoviePlayerErrorCode
MapOpeningMovieDecoderRuntimeError(
    const media::MediaFoundationH262DecoderErrorCode code) noexcept {
  using DecoderCode = media::MediaFoundationH262DecoderErrorCode;
  switch (code) {
  case DecoderCode::AllocationFailed:
    return OpeningMoviePlayerErrorCode::AllocationFailed;
  case DecoderCode::UnsupportedPlatform:
  case DecoderCode::InvalidConfiguration:
  case DecoderCode::WrongThread:
  case DecoderCode::AlreadyDrained:
  case DecoderCode::DecoderPoisoned:
  case DecoderCode::InitializationFailed:
  case DecoderCode::DecoderUnavailable:
  case DecoderCode::MediaTypeRejected:
  case DecoderCode::OutputTypeUnavailable:
  case DecoderCode::InputLimitExceeded:
  case DecoderCode::OutputLimitExceeded:
  case DecoderCode::FrameLimitExceeded:
  case DecoderCode::UnsupportedOutputLayout:
  case DecoderCode::TransformFailure:
    return OpeningMoviePlayerErrorCode::DecoderFailed;
  }
  return OpeningMoviePlayerErrorCode::DecoderFailed;
}
} // namespace omega::app::detail
