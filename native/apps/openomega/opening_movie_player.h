#pragma once

#include "omega/media/nv12_to_rgba8.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace omega::asset {
// The app boundary may not depend on omega_assets directly; the owned source
// arrives through the runtime/content composition edge. Declaring it is enough
// for the by-value overload below, and the defining translation unit pins this
// ceiling to the asset-owned one.
class OpeningMovieSource;
} // namespace omega::asset

namespace omega::app {
// Project-owned ceiling for one explicitly selected opening movie, external or
// archive-backed. The player never searches for media and never persists its
// source path or member name.
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
  AudioStreamRejected,
  AudioDecodeFailed,
  AudioNotReady,
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
  case OpeningMoviePlayerErrorCode::AudioStreamRejected:
    return "opening movie PCM stream was rejected";
  case OpeningMoviePlayerErrorCode::AudioDecodeFailed:
    return "opening movie PCM decode failed";
  case OpeningMoviePlayerErrorCode::AudioNotReady:
    return "opening movie PCM is not ready before first video presentation";
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

namespace detail {
// The four decoder-published scalars the future-frame queue admits a frame on.
// Copying them out of the platform frame keeps the admission rules free of any
// platform decoder type, so the batch contract below is stated and exercised
// without a decoder, a movie source, or a pixel byte.
struct OpeningMovieDecodedFrameFacts {
  std::uint32_t width = 0U;
  std::uint32_t height = 0U;
  std::int64_t timestamp_100ns = 0;
  std::int64_t duration_100ns = 0;

  [[nodiscard]] constexpr bool
  operator==(const OpeningMovieDecodedFrameFacts &) const noexcept = default;
};

// Everything the admission rules read and advance across decoded batches. The
// player fills maximum_queued_frames from the decoder's per-call frame ceiling;
// it is carried here rather than read from that ceiling so the rules stay pure.
struct OpeningMovieFrameQueueState {
  std::uint32_t width = 0U;
  std::uint32_t height = 0U;
  std::size_t queued_frame_count = 0U;
  std::size_t maximum_queued_frames = 0U;
  std::optional<std::uint64_t> first_decoder_timestamp_100ns;
  std::optional<std::uint64_t> last_decoded_timestamp_100ns;

  [[nodiscard]] bool
  operator==(const OpeningMovieFrameQueueState &) const = default;
};

// One admitted frame, with its timestamp normalized against the first timestamp
// the decoder ever published.
struct OpeningMovieFrameAdmission {
  std::uint64_t timestamp_100ns = 0U;
  std::uint64_t duration_100ns = 0U;

  [[nodiscard]] constexpr bool
  operator==(const OpeningMovieFrameAdmission &) const noexcept = default;
};

struct OpeningMovieFrameBatchAdmission {
  // Exactly one entry per input frame, in input order.
  std::vector<OpeningMovieFrameAdmission> frames;
  // Queue state as it stands once the whole batch has been queued.
  OpeningMovieFrameQueueState state;
};

// All-or-nothing admission for one decoded batch: either every frame passes the
// typed decoder checks and the caller receives the normalized timestamps and
// state to adopt, or the typed rejection leaves the input state untouched.
// Allocation while staging the result may throw; the player maps that exception
// to its terminal AllocationFailed state.
//
// Rejection reasons, checked per frame in this order: FrameExtentChanged when
// the decoder changes the validated display extent; InvalidTimestamp for a
// negative timestamp, a non-positive duration, a timestamp below the first one
// published, a normalized timestamp that does not strictly advance, or a
// duration that would overflow the normalized end; FrameQueueExceeded when the
// batch would push the queue past maximum_queued_frames.
[[nodiscard]] std::expected<OpeningMovieFrameBatchAdmission,
                            OpeningMoviePlayerErrorCode>
ValidateOpeningMovieFrameBatch(
    const OpeningMovieFrameQueueState &state,
    std::span<const OpeningMovieDecodedFrameFacts> batch);
} // namespace detail

struct OpeningMoviePlayerUpdate {
  OpeningMoviePlayerStatus status = OpeningMoviePlayerStatus::Ready;
  bool frame_updated = false;
  // Borrowed from the player. Valid through the next non-const player call,
  // move, or destruction. The intended caller uploads it synchronously.
  const media::Rgba8VideoFrame *current_frame = nullptr;
};

// Non-hot-reloadable app boundary for one synchronous opening-movie source.
// OmegaApp owns exactly one implementation and invokes and destroys it on the
// creating game/main thread. Published frame storage is borrowed only through
// the next non-const call and is consumed synchronously by the app.
class OpeningMoviePlayback {
public:
  OpeningMoviePlayback(const OpeningMoviePlayback &) = delete;
  OpeningMoviePlayback &operator=(const OpeningMoviePlayback &) = delete;
  virtual ~OpeningMoviePlayback() noexcept = default;

  [[nodiscard]] virtual std::expected<OpeningMoviePlayerUpdate,
                                      OpeningMoviePlayerError>
  Advance(std::chrono::nanoseconds elapsed) = 0;
  [[nodiscard]] virtual std::expected<std::uint64_t,
                                      OpeningMoviePlayerError>
  ReadAudioFrames(std::span<std::int16_t> interleaved_samples) = 0;
  [[nodiscard]] virtual bool audio_finished() const noexcept = 0;
  [[nodiscard]] virtual std::uint32_t width() const noexcept = 0;
  [[nodiscard]] virtual std::uint32_t height() const noexcept = 0;
  [[nodiscard]] virtual std::uint64_t
  safety_duration_ticks() const noexcept = 0;

protected:
  OpeningMoviePlayback() = default;
  OpeningMoviePlayback(OpeningMoviePlayback &&) noexcept = default;
  OpeningMoviePlayback &operator=(OpeningMoviePlayback &&) noexcept = default;
};

// Non-hot-reloadable, synchronous opening-movie owner. Source inspection,
// Media Foundation decoding, and frame conversion all run on the creating
// game/main thread. The player stores no source path and exposes no encoded
// input bytes or platform decoder object.
class OpeningMoviePlayer final : public OpeningMoviePlayback {
public:
  OpeningMoviePlayer(const OpeningMoviePlayer &) = delete;
  OpeningMoviePlayer &operator=(const OpeningMoviePlayer &) = delete;
  OpeningMoviePlayer(OpeningMoviePlayer &&) noexcept;
  OpeningMoviePlayer &operator=(OpeningMoviePlayer &&) noexcept = delete;
  // [creating game/main thread]
  ~OpeningMoviePlayer() noexcept override;

  // [creating game/main thread, startup] Privately reads exactly the explicit
  // external path under kOpeningMovieMaximumSourceBytes, validates its MPEG-PS,
  // H.262 video, and block-interleaved PCM structure, and creates the platform decoder. Errors are
  // categorical and never echo the path.
  [[nodiscard]] static std::expected<OpeningMoviePlayer,
                                     OpeningMoviePlayerError>
  Create(const std::filesystem::path &path);

  // [creating game/main thread, startup] Consumes one bounded, identity-free owned source from the
  // content boundary. Validation and decoder behavior are identical to the explicit-path route.
  [[nodiscard]] static std::expected<OpeningMoviePlayer,
                                     OpeningMoviePlayerError>
  Create(asset::OpeningMovieSource source);

  // [creating game/main thread] Advances the project-owned playback clock,
  // incrementally feeds bounded PES payload slices, and publishes the newest
  // decoded frame due at that clock. Advance(0ns) transitions Ready to Playing
  // and may publish the first frame. WrongThread and MovedFrom are non-mutating
  // boundary errors; errors reached after those checks permanently mark the
  // player Failed, and later calls return the identical categorical error.
  //
  // FrameExtentChanged, InvalidTimestamp and FrameQueueExceeded are decided for
  // a complete decoded batch before any of its frames are queued, so those typed
  // rejections leave the future-frame queue, published frame and normalization
  // cursors as the batch found them. Allocation failure while staging or
  // committing a batch is terminal instead of transactional.
  [[nodiscard]] std::expected<OpeningMoviePlayerUpdate, OpeningMoviePlayerError>
  Advance(std::chrono::nanoseconds elapsed) override;

  // [creating game/main thread] After the first video frame has been published, decodes up to the
  // caller's frame-aligned stereo capacity into host-endian signed PCM16. The returned count is in
  // stereo frames and may be smaller only at stream end. The player advances its audio cursor only
  // for samples written successfully; no allocation occurs. WrongThread and MovedFrom are the same
  // non-mutating boundary errors described for Advance.
  [[nodiscard]] std::expected<std::uint64_t, OpeningMoviePlayerError>
  ReadAudioFrames(std::span<std::int16_t> interleaved_samples) override;
  // [creating game/main thread] True once every validated PCM frame has been returned.
  [[nodiscard]] bool audio_finished() const noexcept override;

  // [creating game/main thread]
  [[nodiscard]] OpeningMoviePlayerStatus status() const noexcept;

  // [creating game/main thread] Borrowed through the next non-const player
  // call, move, or destruction. Null until a frame has been published.
  [[nodiscard]] const media::Rgba8VideoFrame *current_frame() const noexcept;

  // [creating game/main thread] Validated H.262 display extent available
  // immediately after Create. A moved-from player reports zero.
  [[nodiscard]] std::uint32_t width() const noexcept override;
  [[nodiscard]] std::uint32_t height() const noexcept override;

  // [creating game/main thread] Nonzero MPEG-PTS-derived fail-open guard in
  // boot-sequence ticks. Completion itself is driven only by decoder EOS and
  // the final decoded frame duration.
  [[nodiscard]] std::uint64_t safety_duration_ticks() const noexcept override;

private:
  struct Impl;

  explicit OpeningMoviePlayer(std::unique_ptr<Impl> impl) noexcept;
  std::unique_ptr<Impl> impl_;
};
} // namespace omega::app
