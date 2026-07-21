#include "opening_movie_player.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {
using omega::app::OpeningMoviePlayer;
using omega::app::OpeningMoviePlayerErrorCode;

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
} // namespace

int main() {
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

  const std::array malformed_bytes{std::byte{0x7FU}};
  const auto malformed_path = root / "malformed.pss";
  Check(WriteFixture(malformed_path, malformed_bytes),
        "opening movie writes a malformed synthetic fixture");
  CheckCreateError(malformed_path,
                   OpeningMoviePlayerErrorCode::ProgramStreamRejected,
                   "opening movie rejects malformed program-stream framing");

  const auto no_video = PackOnlyProgram();
  const auto no_video_path = root / "no-video.pss";
  Check(WriteFixture(no_video_path, no_video),
        "opening movie writes a no-video synthetic fixture");
  CheckCreateError(no_video_path,
                   OpeningMoviePlayerErrorCode::VideoStreamRejected,
                   "opening movie rejects a program stream without video");

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

  std::error_code cleanup_error;
  std::filesystem::remove_all(root, cleanup_error);
  Check(!cleanup_error, "opening movie boundary fixtures clean up their temporary directory");

  if (failures == 0)
    std::cout << "omega_opening_movie_player_boundary_tests: all checks passed\n";
  return failures == 0 ? 0 : 1;
}
