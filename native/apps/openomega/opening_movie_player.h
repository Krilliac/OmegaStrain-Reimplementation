#pragma once

#include "omega/media/nv12_to_rgba8.h"

#include <chrono>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <string_view>

namespace omega::app {
// Project-owned ceiling for one explicitly selected external opening movie.
// The player never searches for media and never persists its source path.
inline constexpr std::uint64_t kOpeningMovieMaximumSourceBytes =
    512ULL * 1024ULL * 1024ULL;

enum class OpeningMoviePlayerStatus : std::uint8_t {
  Ready = 0U,
  Playing = 1U,
  Completed = 2U,
  Failed = 3U,
};

enum class OpeningMoviePlayerErrorCode : std::uint8_t {
  InvalidPath,
  FileOpenFailed,
  FileSizeUnavailable,
  InputLimitExceeded,
  FileReadFailed,
  ProgramStreamRejected,
  VideoStreamRejected,
  H262StreamRejected,
  DecoderUnavailable,
  DecoderFailed,
  FrameExtentChanged,
  FrameConversionFailed,
  InvalidTimestamp,
  FrameQueueExceeded,
  InvalidElapsed,
  WrongThread,
  EmptyVideo,
  AllocationFailed,
  MovedFrom,
};

[[nodiscard]] constexpr std::string_view OpeningMoviePlayerErrorMessage(
    const OpeningMoviePlayerErrorCode code) noexcept {
  switch (code) {
  case OpeningMoviePlayerErrorCode::InvalidPath:
    return "opening movie path is empty";
  case OpeningMoviePlayerErrorCode::FileOpenFailed:
    return "opening movie source could not be opened";
  case OpeningMoviePlayerErrorCode::FileSizeUnavailable:
    return "opening movie source size could not be determined";
  case OpeningMoviePlayerErrorCode::InputLimitExceeded:
    return "opening movie source exceeds the input limit";
  case OpeningMoviePlayerErrorCode::FileReadFailed:
    return "opening movie source could not be read exactly";
  case OpeningMoviePlayerErrorCode::ProgramStreamRejected:
    return "opening movie program stream was rejected";
  case OpeningMoviePlayerErrorCode::VideoStreamRejected:
    return "opening movie video stream was rejected";
  case OpeningMoviePlayerErrorCode::H262StreamRejected:
    return "opening movie H.262 stream was rejected";
  case OpeningMoviePlayerErrorCode::DecoderUnavailable:
    return "opening movie decoder is unavailable";
  case OpeningMoviePlayerErrorCode::DecoderFailed:
    return "opening movie decoder failed";
  case OpeningMoviePlayerErrorCode::FrameExtentChanged:
    return "opening movie decoder changed the validated frame extent";
  case OpeningMoviePlayerErrorCode::FrameConversionFailed:
    return "opening movie frame conversion failed";
  case OpeningMoviePlayerErrorCode::InvalidTimestamp:
    return "opening movie decoder published an invalid timestamp";
  case OpeningMoviePlayerErrorCode::FrameQueueExceeded:
    return "opening movie future-frame queue limit was exceeded";
  case OpeningMoviePlayerErrorCode::InvalidElapsed:
    return "opening movie elapsed time is invalid";
  case OpeningMoviePlayerErrorCode::WrongThread:
    return "opening movie player was called from the wrong thread";
  case OpeningMoviePlayerErrorCode::EmptyVideo:
    return "opening movie decoder produced no video frame";
  case OpeningMoviePlayerErrorCode::AllocationFailed:
    return "opening movie allocation failed";
  case OpeningMoviePlayerErrorCode::MovedFrom:
    return "opening movie player is moved-from";
  }
  return "opening movie player failed";
}

struct OpeningMoviePlayerError {
  OpeningMoviePlayerErrorCode code = OpeningMoviePlayerErrorCode::DecoderFailed;
  // Fixed categorical text only. It never contains a path, media byte,
  // platform status, decoder identity, or other proprietary input detail.
  std::string_view message = OpeningMoviePlayerErrorMessage(code);

  [[nodiscard]] constexpr bool
  operator==(const OpeningMoviePlayerError &) const noexcept = default;
};

struct OpeningMoviePlayerUpdate {
  OpeningMoviePlayerStatus status = OpeningMoviePlayerStatus::Ready;
  bool frame_updated = false;
  // Borrowed from the player. Valid through the next non-const player call,
  // move, or destruction. The intended caller uploads it synchronously.
  const media::Rgba8VideoFrame *current_frame = nullptr;
};

// Non-hot-reloadable, synchronous opening-movie owner. Source inspection,
// Media Foundation decoding, and frame conversion all run on the creating
// game/main thread. The player stores no source path and exposes no encoded
// input bytes or platform decoder object.
class OpeningMoviePlayer final {
public:
  OpeningMoviePlayer(const OpeningMoviePlayer &) = delete;
  OpeningMoviePlayer &operator=(const OpeningMoviePlayer &) = delete;
  OpeningMoviePlayer(OpeningMoviePlayer &&) noexcept;
  OpeningMoviePlayer &operator=(OpeningMoviePlayer &&) noexcept = delete;
  // [creating game/main thread]
  ~OpeningMoviePlayer();

  // [creating game/main thread, startup] Privately reads exactly the explicit
  // external path under kOpeningMovieMaximumSourceBytes, validates its MPEG-PS
  // and H.262 structure, and creates the platform decoder. Errors are
  // categorical and never echo the path.
  [[nodiscard]] static std::expected<OpeningMoviePlayer,
                                     OpeningMoviePlayerError>
  Create(const std::filesystem::path &path);

  // [creating game/main thread] Advances the project-owned playback clock,
  // incrementally feeds bounded PES payload slices, and publishes the newest
  // decoded frame due at that clock. Advance(0ns) transitions Ready to Playing
  // and may publish the first frame. Any unexpected result permanently marks
  // the player Failed; later calls return the identical categorical error.
  [[nodiscard]] std::expected<OpeningMoviePlayerUpdate, OpeningMoviePlayerError>
  Advance(std::chrono::nanoseconds elapsed);

  // [creating game/main thread]
  [[nodiscard]] OpeningMoviePlayerStatus status() const noexcept;

  // [creating game/main thread] Borrowed through the next non-const player
  // call, move, or destruction. Null until a frame has been published.
  [[nodiscard]] const media::Rgba8VideoFrame *current_frame() const noexcept;

  // [creating game/main thread] Validated H.262 display extent available
  // immediately after Create. A moved-from player reports zero.
  [[nodiscard]] std::uint32_t width() const noexcept;
  [[nodiscard]] std::uint32_t height() const noexcept;

  // [creating game/main thread] Nonzero MPEG-PTS-derived fail-open guard in
  // boot-sequence ticks. Completion itself is driven only by decoder EOS and
  // the final decoded frame duration.
  [[nodiscard]] std::uint64_t safety_duration_ticks() const noexcept;

private:
  struct Impl;

  explicit OpeningMoviePlayer(std::unique_ptr<Impl> impl) noexcept;
  std::unique_ptr<Impl> impl_;
};
} // namespace omega::app
