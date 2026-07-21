#pragma once

#include "omega/media/mpeg_video_elementary_stream.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace omega::media {
inline constexpr std::uint32_t kMediaFoundationH262MaximumWidth = 1920U;
inline constexpr std::uint32_t kMediaFoundationH262MaximumHeight = 1088U;
inline constexpr std::uint64_t kMediaFoundationH262MaximumTotalInputBytes =
    512ULL * 1024ULL * 1024ULL;
inline constexpr std::uint64_t kMediaFoundationH262MaximumFrameBytes =
    static_cast<std::uint64_t>(kMediaFoundationH262MaximumWidth) *
    kMediaFoundationH262MaximumHeight * 3U / 2U;

namespace detail {
inline constexpr std::uint64_t kMediaFoundationTicksPerSecond = 10'000'000ULL;
inline constexpr std::uint32_t
    kMediaFoundationH262MaximumConsecutiveFormatChanges = 16U;

// Platform-neutral seam for the live-instance lifetime contract. A mismatch
// requires fail-fast handling before any COM/MF state is moved or released.
[[nodiscard]] constexpr bool MediaFoundationH262LifetimeThreadMatches(
    const std::uint64_t creator_thread_id,
    const std::uint64_t current_thread_id) noexcept {
  return creator_thread_id == current_thread_id;
}

// Internal policy helpers kept here so the platform-independent test target can
// verify the exact cadence and output-progress invariants without requiring an
// installed MPEG-2 Media Foundation transform.
[[nodiscard]] constexpr bool MediaFoundationH262FrameRateHasPositiveCadence(
    const std::uint32_t numerator, const std::uint32_t denominator) noexcept {
  return numerator != 0U && denominator != 0U &&
         static_cast<std::uint64_t>(numerator) <=
             kMediaFoundationTicksPerSecond * denominator;
}

class MediaFoundationH262OutputProgress final {
public:
  [[nodiscard]] constexpr bool RecordFormatChangeWithoutOutput() noexcept {
    if (consecutive_format_changes_ >=
        kMediaFoundationH262MaximumConsecutiveFormatChanges) {
      return false;
    }
    ++consecutive_format_changes_;
    return true;
  }

  constexpr void RecordOutput() noexcept { consecutive_format_changes_ = 0U; }

  [[nodiscard]] constexpr std::uint32_t
  consecutive_format_changes() const noexcept {
    return consecutive_format_changes_;
  }

private:
  std::uint32_t consecutive_format_changes_ = 0U;
};
} // namespace detail

struct MediaFoundationH262DecoderLimits {
  std::uint64_t maximum_input_chunk_bytes = 1ULL * 1024ULL * 1024ULL;
  std::uint64_t maximum_total_input_bytes =
      kMediaFoundationH262MaximumTotalInputBytes;
  std::uint64_t maximum_output_bytes_per_call = 64ULL * 1024ULL * 1024ULL;
  std::uint64_t maximum_frames_per_call = 512U;
  std::uint64_t maximum_total_frames = 1ULL << 20U;
  std::uint32_t maximum_width = kMediaFoundationH262MaximumWidth;
  std::uint32_t maximum_height = kMediaFoundationH262MaximumHeight;

  [[nodiscard]] bool
  operator==(const MediaFoundationH262DecoderLimits &) const = default;
};

[[nodiscard]] constexpr MediaFoundationH262DecoderLimits
DefaultMediaFoundationH262DecoderLimits() noexcept {
  return {};
}

enum class MediaFoundationH262DecoderErrorCode {
  UnsupportedPlatform,
  InvalidConfiguration,
  WrongThread,
  AlreadyDrained,
  DecoderPoisoned,
  InitializationFailed,
  DecoderUnavailable,
  MediaTypeRejected,
  OutputTypeUnavailable,
  InputLimitExceeded,
  OutputLimitExceeded,
  FrameLimitExceeded,
  UnsupportedOutputLayout,
  TransformFailure,
  AllocationFailed,
};

struct MediaFoundationH262DecoderError {
  MediaFoundationH262DecoderErrorCode code =
      MediaFoundationH262DecoderErrorCode::TransformFailure;
  // HRESULT or other platform status. This is diagnostic metadata only and
  // never contains a path, media byte, decoder name, CLSID, or DLL identity.
  std::int32_t platform_code = 0;
  std::string message;

  [[nodiscard]] bool
  operator==(const MediaFoundationH262DecoderError &) const = default;
};

// One owned, top-down, tightly packed NV12 frame. The Y plane starts at byte
// zero and contains luma_stride * height bytes. Interleaved UV follows and
// contains chroma_stride * (height / 2) bytes. Both strides equal width today;
// they remain explicit so consumers never infer a platform buffer layout.
struct OwnedNv12VideoFrame {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t luma_stride = 0;
  std::uint32_t chroma_stride = 0;
  std::int64_t timestamp_100ns = 0;
  std::int64_t duration_100ns = 0;
  std::vector<std::byte> pixels;
};

using MediaFoundationH262DecodeResult =
    std::expected<std::vector<OwnedNv12VideoFrame>,
                  MediaFoundationH262DecoderError>;

// Stateful synchronous Windows Media Foundation H.262 decoder.
//
// Ownership: the instance exclusively owns COM/MF transform state. Push copies
// the supplied bytes into an MF sample before returning and never retains the
// caller span. Returned frames own every pixel byte and are independent of the
// decoder and input storage.
//
// Thread affinity: Create, Push, Drain, move construction, and destruction of a
// live instance must all occur on the creating OS thread. The instance is not
// thread-safe. Moving the C++ object does not transfer that affinity to another
// thread. Push and Drain report WrongThread without touching platform state;
// wrong-thread move construction or destruction cannot return an error and
// therefore terminates before touching owned COM/MF state. A moved-from object
// owns no platform state and may be destroyed on any thread. The creating
// thread must remain alive until every live instance it created is destroyed.
//
// Hot reload: non-hot-reloadable while an instance is alive because its opaque
// implementation owns COM interfaces. Drain/destroy every instance before a
// module reload.
class MediaFoundationH262Decoder final {
public:
  MediaFoundationH262Decoder(const MediaFoundationH262Decoder &) = delete;
  MediaFoundationH262Decoder &
  operator=(const MediaFoundationH262Decoder &) = delete;
  // [creator thread, live source] Transfers ownership without changing thread
  // affinity. A wrong-thread attempt terminates before ownership is moved.
  MediaFoundationH262Decoder(MediaFoundationH262Decoder &&) noexcept;
  MediaFoundationH262Decoder &operator=(MediaFoundationH262Decoder &&) = delete;
  // [creator thread, live instance] Releases COM/MF state. Wrong-thread
  // destruction terminates before any platform cleanup is attempted.
  ~MediaFoundationH262Decoder() noexcept;

  // [creator thread] Configures a synchronous software/system MPEG-2 MFT and
  // negotiates NV12. The sequence facts describe the elementary stream; no
  // source span or proprietary metadata is retained.
  [[nodiscard]] static std::expected<MediaFoundationH262Decoder,
                                     MediaFoundationH262DecoderError>
  Create(const H262SequenceHeaderFacts &sequence,
         MediaFoundationH262DecoderLimits limits =
             DefaultMediaFoundationH262DecoderLimits());

  // [creator thread] Synchronously copies one generic H.262 elementary-stream
  // chunk, submits it, and pulls all currently available frames. timestamp is
  // an optional already-normalized signed 100-nanosecond presentation time.
  // On a limit/runtime failure the decoder becomes poisoned and accepts no
  // further Push or Drain call.
  [[nodiscard]] MediaFoundationH262DecodeResult
  Push(std::span<const std::byte> elementary_bytes,
       std::optional<std::int64_t> timestamp_100ns = std::nullopt);

  // [creator thread] Sends end-of-stream and drain exactly once, then returns
  // all remaining owned frames. A second call returns AlreadyDrained.
  [[nodiscard]] MediaFoundationH262DecodeResult Drain();

private:
  struct Impl;

  explicit MediaFoundationH262Decoder(std::unique_ptr<Impl> impl) noexcept;
  std::unique_ptr<Impl> impl_;
};
} // namespace omega::media
