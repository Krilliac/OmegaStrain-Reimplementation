#include "opening_movie_player.h"

#include "omega/asset/opening_movie_source.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {
using omega::app::OpeningMoviePlayer;
using omega::app::OpeningMoviePlayerErrorCode;
using omega::app::detail::CommitValidatedOpeningMovieFrameBatch;
using omega::app::detail::OpeningMovieDecodedFrameFacts;
using omega::app::detail::OpeningMovieFrameAdmission;
using omega::app::detail::OpeningMovieFrameQueueState;
using omega::app::detail::ValidateOpeningMovieFrameBatch;
using omega::asset::OpeningMovieSource;

int failures = 0;

void Check(const bool condition, const std::string_view message) {
  if (condition)
    return;
  std::cerr << "FAIL: " << message << '\n';
  ++failures;
}

void AppendByte(std::vector<std::byte> &bytes, const std::uint8_t value) {
  bytes.push_back(static_cast<std::byte>(value));
}

void AppendStartCode(std::vector<std::byte> &bytes,
                     const std::uint8_t stream_id) {
  AppendByte(bytes, 0U);
  AppendByte(bytes, 0U);
  AppendByte(bytes, 1U);
  AppendByte(bytes, stream_id);
}

void AppendU16BigEndian(std::vector<std::byte> &bytes,
                        const std::uint16_t value) {
  AppendByte(bytes, static_cast<std::uint8_t>(value >> 8U));
  AppendByte(bytes, static_cast<std::uint8_t>(value));
}

void AppendPackHeader(std::vector<std::byte> &bytes) {
  AppendStartCode(bytes, 0xBAU);
  constexpr std::array<std::uint8_t, 10> body{{
      0x44U, 0x00U, 0x04U, 0x00U, 0x04U,
      0x01U, 0x00U, 0x00U, 0x03U, 0xF8U,
  }};
  for (const std::uint8_t value : body)
    AppendByte(bytes, value);
}

void AppendVideoPes(std::vector<std::byte> &bytes,
                    const std::span<const std::uint8_t> payload) {
  AppendStartCode(bytes, 0xE0U);
  AppendU16BigEndian(
      bytes, static_cast<std::uint16_t>(3U + payload.size()));
  AppendByte(bytes, 0x80U);
  AppendByte(bytes, 0x00U);
  AppendByte(bytes, 0x00U);
  for (const std::uint8_t value : payload)
    AppendByte(bytes, value);
}

[[nodiscard]] std::vector<std::byte> PackOnlyProgram() {
  std::vector<std::byte> bytes;
  AppendPackHeader(bytes);
  AppendStartCode(bytes, 0xB9U);
  return bytes;
}

[[nodiscard]] std::vector<std::byte> VideoProgram(
    const std::span<const std::uint8_t> payload) {
  std::vector<std::byte> bytes;
  AppendPackHeader(bytes);
  AppendVideoPes(bytes, payload);
  AppendStartCode(bytes, 0xB9U);
  return bytes;
}

[[nodiscard]] bool WriteFixture(const std::filesystem::path &path,
                                const std::span<const std::byte> bytes) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output)
    return false;
  if (!bytes.empty()) {
    output.write(reinterpret_cast<const char *>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
  }
  return output.good();
}

void CheckCreateError(const std::filesystem::path &path,
                      const OpeningMoviePlayerErrorCode expected_code,
                      const std::string_view label) {
  auto result = OpeningMoviePlayer::Create(path);
  Check(!result, label);
  if (result)
    return;

  const auto expected_message =
      omega::app::OpeningMoviePlayerErrorMessage(expected_code);
  Check(result.error().code == expected_code, label);
  Check(result.error().message == expected_message,
        "opening movie failure uses its exact categorical message");
  Check(result.error().message.find('/') == std::string_view::npos &&
            result.error().message.find('\\') == std::string_view::npos,
        "opening movie failure contains no filesystem path");
  const std::string filename = path.filename().string();
  Check(filename.empty() || result.error().message.find(filename) ==
                                std::string_view::npos,
        "opening movie failure does not echo its source filename");
}

void CheckOwnedCreateError(std::vector<std::byte> bytes,
                           const OpeningMoviePlayerErrorCode expected_code,
                           const std::string_view label) {
  auto source = OpeningMovieSource::Create(std::move(bytes));
  Check(source.has_value(), "bounded owned movie fixture creates a source value");
  if (!source)
    return;
  auto result = OpeningMoviePlayer::Create(std::move(*source));
  Check(!result, label);
  if (result)
    return;

  const auto expected_message =
      omega::app::OpeningMoviePlayerErrorMessage(expected_code);
  Check(result.error().code == expected_code, label);
  Check(result.error().message == expected_message,
        "owned opening movie failure matches the path boundary category");
  Check(result.error().message.find('/') == std::string_view::npos &&
            result.error().message.find('\\') == std::string_view::npos &&
            result.error().message.find("PrivateOwner") == std::string_view::npos,
        "owned opening movie failure contains no path or member identity");
}

[[nodiscard]] std::filesystem::path CreateSyntheticDirectory() {
  std::error_code error;
  const auto seed = std::chrono::steady_clock::now().time_since_epoch().count();
  const std::filesystem::path parent = std::filesystem::temp_directory_path(error);
  if (error)
    return {};

  for (std::uint32_t attempt = 0U; attempt < 32U; ++attempt) {
    const auto candidate =
        parent / ("openomega-opening-boundary-" + std::to_string(seed) + "-" +
                  std::to_string(attempt));
    error.clear();
    if (std::filesystem::create_directory(candidate, error))
      return candidate;
    if (error)
      return {};
  }
  return {};
}

constexpr std::uint32_t kBatchWidth = 640U;
constexpr std::uint32_t kBatchHeight = 448U;

[[nodiscard]] OpeningMovieFrameQueueState
EmptyQueueState(const std::size_t maximum_queued_frames) {
  return OpeningMovieFrameQueueState{
      .width = kBatchWidth,
      .height = kBatchHeight,
      .queued_frame_count = 0U,
      .maximum_queued_frames = maximum_queued_frames,
      .first_decoder_timestamp_100ns = std::nullopt,
      .last_decoded_timestamp_100ns = std::nullopt,
  };
}

[[nodiscard]] constexpr OpeningMovieDecodedFrameFacts
DecodedFrame(const std::int64_t timestamp_100ns,
             const std::int64_t duration_100ns) {
  return OpeningMovieDecodedFrameFacts{
      .width = kBatchWidth,
      .height = kBatchHeight,
      .timestamp_100ns = timestamp_100ns,
      .duration_100ns = duration_100ns,
  };
}

void CheckBatchRejectionIsWhole(
    const std::span<const OpeningMovieDecodedFrameFacts> batch,
    const OpeningMovieFrameQueueState &state,
    const OpeningMoviePlayerErrorCode expected_code,
    const std::string_view label) {
  const OpeningMovieFrameQueueState before = state;
  const auto rejected = ValidateOpeningMovieFrameBatch(state, batch);
  Check(!rejected, label);
  if (rejected)
    return;
  Check(rejected.error() == expected_code, label);
  Check(state == before,
        "rejected opening movie batch advances no offered queue state");

  // The leading frames remain admissible against the exact seeded state, which
  // demonstrates that the typed rejection consumed no prefix.
  const auto prefix = batch.first(batch.size() - 1U);
  const auto admitted = ValidateOpeningMovieFrameBatch(state, prefix);
  Check(admitted.has_value(),
        "opening movie batch rejection leaves its valid prefix admissible");
  if (!admitted)
    return;
  Check(admitted->frames.size() == prefix.size(),
        "opening movie prefix admission covers every prefix frame");
  Check(admitted->state.queued_frame_count ==
            before.queued_frame_count + prefix.size(),
        "opening movie prefix admission advances the seeded queue count");

  using FakeQueueEntry =
      std::pair<std::size_t, OpeningMovieFrameAdmission>;
  const FakeQueueEntry sentinel{
      99U,
      OpeningMovieFrameAdmission{
          .timestamp_100ns = 77U,
          .duration_100ns = 88U,
      },
  };
  std::vector<FakeQueueEntry> fake_queue{sentinel};
  std::size_t callback_count = 0U;
  const auto commit_rejected = CommitValidatedOpeningMovieFrameBatch(
      state, batch,
      [&](const std::size_t index,
          const OpeningMovieFrameAdmission &frame_admission) {
        ++callback_count;
        fake_queue.emplace_back(index, frame_admission);
      });
  Check(!commit_rejected && commit_rejected.error() == expected_code,
        "opening movie commit seam returns the typed batch rejection");
  Check(callback_count == 0U,
        "typed opening movie batch rejection invokes no commit callback");
  Check(fake_queue.size() == 1U && fake_queue.front() == sentinel,
        "typed opening movie batch rejection preserves the sentinel queue");
  Check(state == before,
        "typed opening movie commit rejection preserves seeded input state");
}

void CheckFrameBatchAdmissionContract() {
  const std::array admitted_batch{
      DecodedFrame(1'000, 400),
      DecodedFrame(1'400, 400),
  };
  const auto admitted =
      ValidateOpeningMovieFrameBatch(EmptyQueueState(4U), admitted_batch);
  Check(admitted.has_value(),
        "opening movie admits an in-extent strictly advancing batch");
  if (admitted) {
    Check(admitted->frames.size() == admitted_batch.size(),
          "opening movie admission reports one entry per batch frame");
    Check(admitted->frames[0].timestamp_100ns == 0U &&
              admitted->frames[0].duration_100ns == 400U,
          "opening movie normalizes the first admitted frame to zero");
    Check(admitted->frames[1].timestamp_100ns == 400U,
          "opening movie normalizes against the first decoder timestamp");
    Check(admitted->state.first_decoder_timestamp_100ns == 1'000U &&
              admitted->state.last_decoded_timestamp_100ns == 400U &&
              admitted->state.queued_frame_count == admitted_batch.size(),
          "opening movie admission advances every queue cursor exactly once");
  }

  const std::array extent_batch{
      DecodedFrame(0, 400),
      OpeningMovieDecodedFrameFacts{
          .width = kBatchWidth,
          .height = kBatchHeight + 1U,
          .timestamp_100ns = 400,
          .duration_100ns = 400,
      },
  };
  CheckBatchRejectionIsWhole(
      extent_batch, EmptyQueueState(4U),
      OpeningMoviePlayerErrorCode::FrameExtentChanged,
      "opening movie rejects a whole batch whose frame extent changes");

  const std::array timestamp_batch{
      DecodedFrame(0, 400),
      DecodedFrame(0, 400),
  };
  CheckBatchRejectionIsWhole(
      timestamp_batch, EmptyQueueState(4U),
      OpeningMoviePlayerErrorCode::InvalidTimestamp,
      "opening movie rejects a whole batch whose timestamps do not advance");

  const std::array queue_batch{
      DecodedFrame(0, 400),
      DecodedFrame(400, 400),
  };
  CheckBatchRejectionIsWhole(
      queue_batch, EmptyQueueState(1U),
      OpeningMoviePlayerErrorCode::FrameQueueExceeded,
      "opening movie rejects a whole batch that would exceed the frame queue");

  const OpeningMovieFrameQueueState seeded_state{
      .width = kBatchWidth,
      .height = kBatchHeight,
      .queued_frame_count = 2U,
      .maximum_queued_frames = 4U,
      .first_decoder_timestamp_100ns = 1'000U,
      .last_decoded_timestamp_100ns = 400U,
  };
  const std::array seeded_batch{
      DecodedFrame(1'600, 400),
      DecodedFrame(2'000, 400),
  };
  const auto seeded_admission =
      ValidateOpeningMovieFrameBatch(seeded_state, seeded_batch);
  Check(seeded_admission.has_value(),
        "opening movie admits a batch against seeded queue cursors");
  if (seeded_admission) {
    Check(seeded_state.queued_frame_count == 2U &&
              seeded_state.first_decoder_timestamp_100ns == 1'000U &&
              seeded_state.last_decoded_timestamp_100ns == 400U,
          "opening movie successful admission does not mutate seeded input");
    Check(seeded_admission->frames[0].timestamp_100ns == 600U &&
              seeded_admission->frames[1].timestamp_100ns == 1'000U &&
              seeded_admission->state.queued_frame_count == 4U &&
              seeded_admission->state.first_decoder_timestamp_100ns == 1'000U &&
              seeded_admission->state.last_decoded_timestamp_100ns == 1'000U,
          "opening movie admission advances from the seeded queue state");
  }

  using FakeQueueEntry =
      std::pair<std::size_t, OpeningMovieFrameAdmission>;
  const FakeQueueEntry sentinel{
      99U,
      OpeningMovieFrameAdmission{
          .timestamp_100ns = 77U,
          .duration_100ns = 88U,
      },
  };
  std::vector<FakeQueueEntry> fake_queue{sentinel};
  const auto seeded_commit = CommitValidatedOpeningMovieFrameBatch(
      seeded_state, seeded_batch,
      [&](const std::size_t index,
          const OpeningMovieFrameAdmission &frame_admission) {
        fake_queue.emplace_back(index, frame_admission);
      });
  Check(seeded_commit.has_value(),
        "opening movie commit seam accepts a seeded batch");
  if (seeded_commit) {
    Check(fake_queue.size() == 3U && fake_queue[0] == sentinel,
          "opening movie seeded commit preserves its sentinel entry");
    Check(fake_queue[1].first == 0U &&
              fake_queue[1].second.timestamp_100ns == 600U &&
              fake_queue[2].first == 1U &&
              fake_queue[2].second.timestamp_100ns == 1'000U,
          "opening movie seeded commit publishes ordered normalized frames");
    Check(seeded_commit->queued_frame_count == 4U &&
              seeded_commit->first_decoder_timestamp_100ns == 1'000U &&
              seeded_commit->last_decoded_timestamp_100ns == 1'000U,
          "opening movie seeded commit returns its advanced queue state");
  }

  const std::array seeded_rejection{
      DecodedFrame(1'600, 400),
      DecodedFrame(1'500, 400),
  };
  CheckBatchRejectionIsWhole(
      seeded_rejection, seeded_state,
      OpeningMoviePlayerErrorCode::InvalidTimestamp,
      "opening movie rejects a whole batch against seeded timestamp cursors");

  OpeningMovieFrameQueueState seeded_queue_limit = seeded_state;
  seeded_queue_limit.maximum_queued_frames = 3U;
  CheckBatchRejectionIsWhole(
      seeded_batch, seeded_queue_limit,
      OpeningMoviePlayerErrorCode::FrameQueueExceeded,
      "opening movie rejects a whole batch against a seeded queue count");
}
} // namespace

int main() {
  CheckFrameBatchAdmissionContract();

  CheckCreateError({}, OpeningMoviePlayerErrorCode::InvalidPath,
                   "opening movie rejects an empty path");

  const std::filesystem::path root = CreateSyntheticDirectory();
  Check(!root.empty(), "opening movie boundary fixtures create a temporary directory");
  if (root.empty())
    return 1;

  const auto private_looking_missing =
      root / "private" / "owned-opening-movie.pss";
  CheckCreateError(private_looking_missing,
                   OpeningMoviePlayerErrorCode::FileOpenFailed,
                   "opening movie rejects a private-looking missing path categorically");

  const auto empty_path = root / "empty.pss";
  Check(WriteFixture(empty_path, {}), "opening movie writes an empty synthetic fixture");
  CheckCreateError(empty_path, OpeningMoviePlayerErrorCode::ProgramStreamRejected,
                   "opening movie rejects an empty program stream");
  CheckOwnedCreateError({}, OpeningMoviePlayerErrorCode::ProgramStreamRejected,
                        "owned opening movie rejects an empty program stream identically");

  const std::array malformed_bytes{std::byte{0x7FU}};
  const auto malformed_path = root / "malformed.pss";
  Check(WriteFixture(malformed_path, malformed_bytes),
        "opening movie writes a malformed synthetic fixture");
  CheckCreateError(malformed_path,
                   OpeningMoviePlayerErrorCode::ProgramStreamRejected,
                   "opening movie rejects malformed program-stream framing");
  CheckOwnedCreateError(std::vector<std::byte>(malformed_bytes.begin(),
                            malformed_bytes.end()),
                        OpeningMoviePlayerErrorCode::ProgramStreamRejected,
                        "owned opening movie rejects malformed framing identically");

  const auto no_video = PackOnlyProgram();
  const auto no_video_path = root / "no-video.pss";
  Check(WriteFixture(no_video_path, no_video),
        "opening movie writes a no-video synthetic fixture");
  CheckCreateError(no_video_path,
                   OpeningMoviePlayerErrorCode::VideoStreamRejected,
                   "opening movie rejects a program stream without video");
  CheckOwnedCreateError(no_video, OpeningMoviePlayerErrorCode::VideoStreamRejected,
                        "owned opening movie rejects missing video identically");

  constexpr std::array<std::uint8_t, 7> malformed_h262{{
      0x00U, 0x00U, 0x01U, 0xB3U, 0x2DU, 0x01U, 0xE0U,
  }};
  const auto bad_h262 = VideoProgram(malformed_h262);
  const auto bad_h262_path = root / "bad-h262.pss";
  Check(WriteFixture(bad_h262_path, bad_h262),
        "opening movie writes a malformed H.262 synthetic fixture");
  CheckCreateError(bad_h262_path,
                   OpeningMoviePlayerErrorCode::H262StreamRejected,
                   "opening movie rejects malformed H.262 before decoder creation");
  CheckOwnedCreateError(bad_h262, OpeningMoviePlayerErrorCode::H262StreamRejected,
                        "owned opening movie rejects malformed H.262 identically");

  constexpr std::array<std::uint8_t, 12> valid_sequence_header{{
      0x00U, 0x00U, 0x01U, 0xB3U, 0x2DU, 0x01U,
      0xE0U, 0x34U, 0x00U, 0x00U, 0x20U, 0x00U,
  }};
  const auto no_audio = VideoProgram(valid_sequence_header);
  const auto no_audio_path = root / "no-audio.pss";
  Check(WriteFixture(no_audio_path, no_audio),
        "opening movie writes a no-audio synthetic fixture");
  CheckCreateError(no_audio_path,
                   OpeningMoviePlayerErrorCode::AudioStreamRejected,
                   "opening movie rejects missing audio before decoder creation");
  CheckOwnedCreateError(no_audio, OpeningMoviePlayerErrorCode::AudioStreamRejected,
                        "owned opening movie rejects missing audio identically");

  std::error_code cleanup_error;
  std::filesystem::remove_all(root, cleanup_error);
  Check(!cleanup_error, "opening movie boundary fixtures clean up their temporary directory");

  if (failures == 0)
    std::cout << "omega_opening_movie_player_boundary_tests: all checks passed\n";
  return failures == 0 ? 0 : 1;
}
