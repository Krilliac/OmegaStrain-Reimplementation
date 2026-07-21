#include "opening_movie_player.h"
#include "opening_movie_safety.h"

#include "omega/media/media_foundation_h262_decoder.h"
#include "omega/media/mpeg_program_stream_descriptor.h"
#include "omega/media/mpeg_video_elementary_stream.h"
#include "omega/media/pss_pcm_audio_stream.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <fstream>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <thread>
#include <utility>
#include <vector>

namespace omega::app {
namespace {
using Error = OpeningMoviePlayerError;
using ErrorCode = OpeningMoviePlayerErrorCode;

constexpr media::MediaFoundationH262DecoderLimits kDecoderLimits =
    media::DefaultMediaFoundationH262DecoderLimits();
constexpr std::size_t kMaximumQueuedFrames =
    static_cast<std::size_t>(kDecoderLimits.maximum_frames_per_call);
constexpr std::size_t kMaximumDecoderFeedsPerAdvance = 64U;
constexpr std::uint64_t kMpegTimestampTicksPerSecond = 90'000U;
constexpr std::uint64_t kMediaFoundationTicksPerSecond = 10'000'000U;
constexpr std::uint32_t kOpeningMovieAudioSampleRateHz = 48'000U;
constexpr std::uint32_t kOpeningMovieAudioChannelCount = 2U;

static_assert(kOpeningMovieMaximumSourceBytes ==
              media::kMpegProgramStreamMaximumInputBytes);
static_assert(kMaximumQueuedFrames != 0U);
static_assert(kDecoderLimits.maximum_frames_per_call <=
              std::numeric_limits<std::size_t>::max());

[[nodiscard]] constexpr Error MakeError(const ErrorCode code) noexcept {
  return Error{.code = code, .message = OpeningMoviePlayerErrorMessage(code)};
}

[[nodiscard]] std::expected<std::vector<std::byte>, Error>
ReadSource(const std::filesystem::path &path) {
  if (path.empty())
    return std::unexpected(MakeError(ErrorCode::InvalidPath));

  try {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream.is_open())
      return std::unexpected(MakeError(ErrorCode::FileOpenFailed));

    const std::ifstream::pos_type end = stream.tellg();
    if (end < std::ifstream::pos_type{0})
      return std::unexpected(MakeError(ErrorCode::FileSizeUnavailable));
    const auto byte_count = static_cast<std::uint64_t>(end);
    if (byte_count > kOpeningMovieMaximumSourceBytes ||
        byte_count > static_cast<std::uint64_t>(
                         std::numeric_limits<std::size_t>::max()) ||
        byte_count > static_cast<std::uint64_t>(
                         std::numeric_limits<std::streamsize>::max())) {
      return std::unexpected(MakeError(ErrorCode::InputLimitExceeded));
    }

    std::vector<std::byte> bytes(static_cast<std::size_t>(byte_count));
    stream.seekg(0, std::ios::beg);
    if (!stream)
      return std::unexpected(MakeError(ErrorCode::FileReadFailed));
    if (!bytes.empty()) {
      stream.read(reinterpret_cast<char *>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
      if (!stream ||
          stream.gcount() != static_cast<std::streamsize>(bytes.size())) {
        return std::unexpected(MakeError(ErrorCode::FileReadFailed));
      }
    }

    // Detect growth after the bounded size probe without reading another
    // source byte into owned storage.
    if (stream.peek() != std::char_traits<char>::eof())
      return std::unexpected(MakeError(ErrorCode::FileReadFailed));
    return bytes;
  } catch (const std::bad_alloc &) {
    return std::unexpected(MakeError(ErrorCode::AllocationFailed));
  } catch (...) {
    return std::unexpected(MakeError(ErrorCode::FileReadFailed));
  }
}

[[nodiscard]] constexpr std::optional<std::int64_t>
Convert90KhzTo100ns(const std::optional<std::uint64_t> timestamp) noexcept {
  if (!timestamp)
    return std::nullopt;
  const std::uint64_t whole_seconds = *timestamp / kMpegTimestampTicksPerSecond;
  const std::uint64_t remainder = *timestamp % kMpegTimestampTicksPerSecond;
  const std::uint64_t converted =
      whole_seconds * kMediaFoundationTicksPerSecond +
      remainder * kMediaFoundationTicksPerSecond / kMpegTimestampTicksPerSecond;
  return static_cast<std::int64_t>(converted);
}

[[nodiscard]] Error MapDecoderCreationError(
    const media::MediaFoundationH262DecoderError &error) noexcept {
  using DecoderCode = media::MediaFoundationH262DecoderErrorCode;
  switch (error.code) {
  case DecoderCode::UnsupportedPlatform:
  case DecoderCode::InitializationFailed:
  case DecoderCode::DecoderUnavailable:
  case DecoderCode::MediaTypeRejected:
  case DecoderCode::OutputTypeUnavailable:
    return MakeError(ErrorCode::DecoderUnavailable);
  case DecoderCode::AllocationFailed:
    return MakeError(ErrorCode::AllocationFailed);
  default:
    return MakeError(ErrorCode::DecoderFailed);
  }
}

[[nodiscard]] Error MapDecoderRuntimeError(
    const media::MediaFoundationH262DecoderError &error) noexcept {
  if (error.code == media::MediaFoundationH262DecoderErrorCode::WrongThread)
    return MakeError(ErrorCode::WrongThread);
  if (error.code ==
      media::MediaFoundationH262DecoderErrorCode::AllocationFailed)
    return MakeError(ErrorCode::AllocationFailed);
  return MakeError(ErrorCode::DecoderFailed);
}

struct QueuedNv12Frame {
  std::uint64_t timestamp_100ns = 0U;
  std::uint64_t duration_100ns = 0U;
  media::OwnedNv12VideoFrame frame;
};
} // namespace

struct OpeningMoviePlayer::Impl {
  Impl(std::vector<std::byte> source_bytes,
       media::MpegVideoElementaryStreamPlan stream_plan,
       media::PssPcmAudioStreamPlan pcm_plan,
       media::MediaFoundationH262Decoder video_decoder,
       const std::uint32_t video_width, const std::uint32_t video_height,
       const std::uint64_t safety_ticks) noexcept
      : source(std::move(source_bytes)), plan(std::move(stream_plan)),
        audio_plan(std::move(pcm_plan)), decoder(std::move(video_decoder)),
        width(video_width), height(video_height),
        safety_duration_ticks(safety_ticks),
        creator_thread(std::this_thread::get_id()) {}

  void RememberFailure(const Error error) noexcept {
    if (!failure)
      failure = error;
    status = OpeningMoviePlayerStatus::Failed;
  }

  [[nodiscard]] std::expected<OpeningMoviePlayerUpdate, Error>
  Fail(const Error error) noexcept {
    RememberFailure(error);
    return std::unexpected(*failure);
  }

  [[nodiscard]] std::optional<Error>
  AdvanceClock(const std::chrono::nanoseconds elapsed) noexcept {
    if (elapsed.count() < 0)
      return MakeError(ErrorCode::InvalidElapsed);

    const std::uint64_t nanoseconds =
        static_cast<std::uint64_t>(elapsed.count());
    std::uint64_t delta_100ns = nanoseconds / 100U;
    const std::uint64_t remainder = nanoseconds % 100U + nanosecond_remainder;
    if (remainder >= 100U)
      ++delta_100ns;
    nanosecond_remainder = remainder % 100U;
    if (delta_100ns >
        std::numeric_limits<std::uint64_t>::max() - playback_time_100ns) {
      return MakeError(ErrorCode::InvalidElapsed);
    }
    playback_time_100ns += delta_100ns;
    return std::nullopt;
  }

  [[nodiscard]] std::optional<Error>
  QueueDecodedFrames(std::vector<media::OwnedNv12VideoFrame> &&decoded) {
    for (media::OwnedNv12VideoFrame &frame : decoded) {
      if (frame.width != width || frame.height != height)
        return MakeError(ErrorCode::FrameExtentChanged);
      if (frame.timestamp_100ns < 0 || frame.duration_100ns <= 0)
        return MakeError(ErrorCode::InvalidTimestamp);
      const std::uint64_t timestamp =
          static_cast<std::uint64_t>(frame.timestamp_100ns);
      if (!first_decoder_timestamp_100ns)
        first_decoder_timestamp_100ns = timestamp;
      if (timestamp < *first_decoder_timestamp_100ns)
        return MakeError(ErrorCode::InvalidTimestamp);
      const std::uint64_t normalized =
          timestamp - *first_decoder_timestamp_100ns;
      if (last_decoded_timestamp_100ns &&
          normalized <= *last_decoded_timestamp_100ns) {
        return MakeError(ErrorCode::InvalidTimestamp);
      }
      const std::uint64_t duration =
          static_cast<std::uint64_t>(frame.duration_100ns);
      if (duration > std::numeric_limits<std::uint64_t>::max() - normalized)
        return MakeError(ErrorCode::InvalidTimestamp);
      if (queued_frames.size() >= kMaximumQueuedFrames)
        return MakeError(ErrorCode::FrameQueueExceeded);

      queued_frames.push_back(QueuedNv12Frame{
          .timestamp_100ns = normalized,
          .duration_100ns = duration,
          .frame = std::move(frame),
      });
      last_decoded_timestamp_100ns = normalized;
    }
    return std::nullopt;
  }

  [[nodiscard]] std::expected<bool, Error> PublishDueFrame() {
    std::optional<QueuedNv12Frame> newest_due;
    while (!queued_frames.empty() &&
           queued_frames.front().timestamp_100ns <= playback_time_100ns) {
      newest_due = std::move(queued_frames.front());
      queued_frames.pop_front();
    }
    if (!newest_due)
      return false;

    auto converted = media::ConvertNv12ToRgba8(media::Nv12FrameView{
        .width = newest_due->frame.width,
        .height = newest_due->frame.height,
        .luma_stride = newest_due->frame.luma_stride,
        .chroma_stride = newest_due->frame.chroma_stride,
        .bytes = newest_due->frame.pixels,
    });
    if (!converted)
      return std::unexpected(MakeError(ErrorCode::FrameConversionFailed));
    current_frame = std::move(*converted);
    current_frame_end_100ns =
        newest_due->timestamp_100ns + newest_due->duration_100ns;
    return true;
  }

  [[nodiscard]] std::expected<std::vector<media::OwnedNv12VideoFrame>, Error>
  FeedDecoderOnce() {
    while (payload_index < plan.payloads.size() &&
           payload_byte_offset == plan.payloads[payload_index].byte_count) {
      ++payload_index;
      payload_byte_offset = 0U;
    }

    if (payload_index >= plan.payloads.size()) {
      auto drained = decoder.Drain();
      if (!drained)
        return std::unexpected(MapDecoderRuntimeError(drained.error()));
      decoder_drained = true;
      return std::move(*drained);
    }

    const media::MpegVideoElementaryStreamPayloadRange &payload =
        plan.payloads[payload_index];
    if (payload.source_offset > source.size() ||
        payload.byte_count > source.size() - payload.source_offset ||
        payload_byte_offset > payload.byte_count) {
      return std::unexpected(MakeError(ErrorCode::VideoStreamRejected));
    }

    const std::uint64_t remaining = payload.byte_count - payload_byte_offset;
    const std::uint64_t chunk_bytes =
        std::min<std::uint64_t>(remaining, decoder_input_chunk_bytes);
    if (chunk_bytes == 0U)
      return std::unexpected(MakeError(ErrorCode::VideoStreamRejected));
    const std::uint64_t source_offset =
        payload.source_offset + payload_byte_offset;
    const auto bytes = std::span<const std::byte>(source).subspan(
        static_cast<std::size_t>(source_offset),
        static_cast<std::size_t>(chunk_bytes));
    const std::optional<std::int64_t> timestamp =
        payload_byte_offset == 0U
            ? Convert90KhzTo100ns(payload.presentation_timestamp_90khz)
            : std::nullopt;

    auto pushed = decoder.Push(bytes, timestamp);
    if (!pushed)
      return std::unexpected(MapDecoderRuntimeError(pushed.error()));
    payload_byte_offset += chunk_bytes;
    return std::move(*pushed);
  }

  // Destruction is reverse declaration order: decoder first, then both plans
  // and source. This preserves every borrowed-input owner through decoder
  // shutdown even though the decoder retains none of them.
  std::vector<std::byte> source;
  media::MpegVideoElementaryStreamPlan plan;
  media::PssPcmAudioStreamPlan audio_plan;
  media::MediaFoundationH262Decoder decoder;
  std::size_t payload_index = 0U;
  std::uint64_t payload_byte_offset = 0U;
  std::uint64_t decoder_input_chunk_bytes =
      media::DefaultMediaFoundationH262DecoderLimits()
          .maximum_input_chunk_bytes;
  std::deque<QueuedNv12Frame> queued_frames;
  std::optional<std::uint64_t> first_decoder_timestamp_100ns;
  std::optional<std::uint64_t> last_decoded_timestamp_100ns;
  std::optional<media::Rgba8VideoFrame> current_frame;
  std::uint32_t width = 0U;
  std::uint32_t height = 0U;
  std::uint64_t current_frame_end_100ns = 0U;
  std::uint64_t playback_time_100ns = 0U;
  std::uint64_t nanosecond_remainder = 0U;
  std::uint64_t audio_frame_cursor = 0U;
  std::uint64_t safety_duration_ticks = kOpeningMovieMaximumSafetyTicks;
  OpeningMoviePlayerStatus status = OpeningMoviePlayerStatus::Ready;
  std::optional<Error> failure;
  bool decoder_drained = false;
  std::thread::id creator_thread;
};

OpeningMoviePlayer::OpeningMoviePlayer(std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl)) {}

OpeningMoviePlayer::OpeningMoviePlayer(OpeningMoviePlayer &&) noexcept =
    default;
OpeningMoviePlayer::~OpeningMoviePlayer() = default;

std::expected<OpeningMoviePlayer, OpeningMoviePlayerError>
OpeningMoviePlayer::Create(const std::filesystem::path &path) {
  try {
    auto source = ReadSource(path);
    if (!source)
      return std::unexpected(source.error());

    auto descriptor = media::InspectMpegProgramStream(*source);
    if (!descriptor)
      return std::unexpected(MakeError(ErrorCode::ProgramStreamRejected));
    auto plan = media::BuildMpegVideoElementaryStreamPlan(*source, *descriptor);
    if (!plan)
      return std::unexpected(MakeError(ErrorCode::VideoStreamRejected));
    media::H262SequenceHeaderFacts sequence;
    {
      auto view = media::BorrowMpegVideoElementaryStream(*plan, *source);
      if (!view)
        return std::unexpected(MakeError(ErrorCode::VideoStreamRejected));
      auto inspected_sequence = media::InspectH262SequenceHeaderFacts(*view);
      if (!inspected_sequence)
        return std::unexpected(MakeError(ErrorCode::H262StreamRejected));
      sequence = *inspected_sequence;
    }
    auto audio_plan = media::BuildPssPcmAudioStreamPlan(*source, *descriptor);
    if (!audio_plan ||
        audio_plan->sample_rate_hz != kOpeningMovieAudioSampleRateHz ||
        audio_plan->channel_count != kOpeningMovieAudioChannelCount)
      return std::unexpected(MakeError(ErrorCode::AudioStreamRejected));

    if (plan->total_payload_bytes == 0U ||
        plan->total_payload_bytes > kDecoderLimits.maximum_total_input_bytes) {
      return std::unexpected(MakeError(ErrorCode::VideoStreamRejected));
    }
    auto decoder_limits = kDecoderLimits;
    decoder_limits.maximum_total_input_bytes = plan->total_payload_bytes;
    auto decoder =
        media::MediaFoundationH262Decoder::Create(sequence, decoder_limits);
    if (!decoder)
      return std::unexpected(MapDecoderCreationError(decoder.error()));

    const std::uint64_t safety_ticks =
        CalculateOpeningMovieSafetyDurationTicks(*plan);
    auto impl = std::make_unique<Impl>(
        std::move(*source), std::move(*plan), std::move(*audio_plan),
        std::move(*decoder), sequence.width, sequence.height, safety_ticks);
    impl->decoder_input_chunk_bytes = decoder_limits.maximum_input_chunk_bytes;
    return OpeningMoviePlayer(std::move(impl));
  } catch (const std::bad_alloc &) {
    return std::unexpected(MakeError(ErrorCode::AllocationFailed));
  } catch (...) {
    return std::unexpected(MakeError(ErrorCode::DecoderFailed));
  }
}

std::expected<OpeningMoviePlayerUpdate, OpeningMoviePlayerError>
OpeningMoviePlayer::Advance(const std::chrono::nanoseconds elapsed) {
  if (!impl_)
    return std::unexpected(MakeError(ErrorCode::MovedFrom));
  if (std::this_thread::get_id() != impl_->creator_thread)
    return std::unexpected(MakeError(ErrorCode::WrongThread));
  if (impl_->failure)
    return std::unexpected(*impl_->failure);
  if (impl_->status == OpeningMoviePlayerStatus::Completed) {
    return OpeningMoviePlayerUpdate{
        .status = impl_->status,
        .frame_updated = false,
        .current_frame =
            impl_->current_frame ? &*impl_->current_frame : nullptr,
    };
  }

  try {
    impl_->status = OpeningMoviePlayerStatus::Playing;
    if (auto error = impl_->AdvanceClock(elapsed))
      return impl_->Fail(*error);

    bool frame_updated = false;
    auto published = impl_->PublishDueFrame();
    if (!published)
      return impl_->Fail(published.error());
    frame_updated = *published;

    std::size_t feed_count = 0U;
    while (impl_->queued_frames.empty() && !impl_->decoder_drained &&
           feed_count < kMaximumDecoderFeedsPerAdvance) {
      auto decoded = impl_->FeedDecoderOnce();
      if (!decoded)
        return impl_->Fail(decoded.error());
      if (auto error = impl_->QueueDecodedFrames(std::move(*decoded)))
        return impl_->Fail(*error);

      published = impl_->PublishDueFrame();
      if (!published)
        return impl_->Fail(published.error());
      frame_updated = frame_updated || *published;
      ++feed_count;
    }

    if (impl_->decoder_drained && impl_->queued_frames.empty()) {
      if (!impl_->current_frame)
        return impl_->Fail(MakeError(ErrorCode::EmptyVideo));
      if (impl_->playback_time_100ns >= impl_->current_frame_end_100ns)
        impl_->status = OpeningMoviePlayerStatus::Completed;
    }

    return OpeningMoviePlayerUpdate{
        .status = impl_->status,
        .frame_updated = frame_updated,
        .current_frame =
            impl_->current_frame ? &*impl_->current_frame : nullptr,
    };
  } catch (const std::bad_alloc &) {
    return impl_->Fail(MakeError(ErrorCode::AllocationFailed));
  } catch (...) {
    return impl_->Fail(MakeError(ErrorCode::DecoderFailed));
  }
}

std::expected<std::uint64_t, OpeningMoviePlayerError>
OpeningMoviePlayer::ReadAudioFrames(
    const std::span<std::int16_t> interleaved_samples) {
  if (!impl_)
    return std::unexpected(MakeError(ErrorCode::MovedFrom));
  if (std::this_thread::get_id() != impl_->creator_thread)
    return std::unexpected(MakeError(ErrorCode::WrongThread));
  if (impl_->failure)
    return std::unexpected(*impl_->failure);
  if (!impl_->current_frame) {
    impl_->RememberFailure(MakeError(ErrorCode::AudioNotReady));
    return std::unexpected(*impl_->failure);
  }
  if (interleaved_samples.size() % kOpeningMovieAudioChannelCount != 0U) {
    impl_->RememberFailure(MakeError(ErrorCode::AudioDecodeFailed));
    return std::unexpected(*impl_->failure);
  }

  const std::uint64_t requested_frames =
      interleaved_samples.size() / kOpeningMovieAudioChannelCount;
  const std::uint64_t remaining_frames =
      impl_->audio_plan.total_frame_count - impl_->audio_frame_cursor;
  const std::uint64_t frame_count =
      std::min(requested_frames, remaining_frames);
  if (frame_count == 0U)
    return 0U;
  const auto output = interleaved_samples.first(
      static_cast<std::size_t>(frame_count * kOpeningMovieAudioChannelCount));
  auto decoded = media::DecodePssPcm16Interleaved(
      impl_->audio_plan, impl_->source, impl_->audio_frame_cursor, output);
  if (!decoded || *decoded != frame_count) {
    impl_->RememberFailure(MakeError(ErrorCode::AudioDecodeFailed));
    return std::unexpected(*impl_->failure);
  }
  impl_->audio_frame_cursor += frame_count;
  return frame_count;
}

bool OpeningMoviePlayer::audio_finished() const noexcept {
  return !impl_ ||
         impl_->audio_frame_cursor >= impl_->audio_plan.total_frame_count;
}

OpeningMoviePlayerStatus OpeningMoviePlayer::status() const noexcept {
  return impl_ ? impl_->status : OpeningMoviePlayerStatus::Failed;
}

const media::Rgba8VideoFrame *
OpeningMoviePlayer::current_frame() const noexcept {
  return impl_ && impl_->current_frame ? &*impl_->current_frame : nullptr;
}

std::uint32_t OpeningMoviePlayer::width() const noexcept {
  return impl_ ? impl_->width : 0U;
}

std::uint32_t OpeningMoviePlayer::height() const noexcept {
  return impl_ ? impl_->height : 0U;
}

std::uint64_t OpeningMoviePlayer::safety_duration_ticks() const noexcept {
  return impl_ ? impl_->safety_duration_ticks : 0U;
}
} // namespace omega::app
