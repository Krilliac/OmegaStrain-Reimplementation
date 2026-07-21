#include "opening_movie_player_error_mapping.h"

#include <array>
#include <cstddef>
#include <cstdlib>
#include <iostream>

namespace {
using DecoderCode = omega::media::MediaFoundationH262DecoderErrorCode;
using PlayerCode = omega::app::OpeningMoviePlayerErrorCode;

struct MappingCase {
  DecoderCode decoder_code;
  PlayerCode player_code;
};

constexpr std::array kCreationCases{
    MappingCase{DecoderCode::UnsupportedPlatform,
                PlayerCode::DecoderUnavailable},
    MappingCase{DecoderCode::InvalidConfiguration,
                PlayerCode::H262StreamRejected},
    MappingCase{DecoderCode::WrongThread, PlayerCode::DecoderFailed},
    MappingCase{DecoderCode::AlreadyDrained, PlayerCode::DecoderFailed},
    MappingCase{DecoderCode::DecoderPoisoned, PlayerCode::DecoderFailed},
    MappingCase{DecoderCode::InitializationFailed,
                PlayerCode::DecoderUnavailable},
    MappingCase{DecoderCode::DecoderUnavailable,
                PlayerCode::DecoderUnavailable},
    MappingCase{DecoderCode::MediaTypeRejected, PlayerCode::DecoderUnavailable},
    MappingCase{DecoderCode::OutputTypeUnavailable,
                PlayerCode::DecoderUnavailable},
    MappingCase{DecoderCode::InputLimitExceeded, PlayerCode::DecoderFailed},
    MappingCase{DecoderCode::OutputLimitExceeded, PlayerCode::DecoderFailed},
    MappingCase{DecoderCode::FrameLimitExceeded, PlayerCode::DecoderFailed},
    MappingCase{DecoderCode::UnsupportedOutputLayout,
                PlayerCode::DecoderFailed},
    MappingCase{DecoderCode::TransformFailure, PlayerCode::DecoderFailed},
    MappingCase{DecoderCode::AllocationFailed, PlayerCode::AllocationFailed},
};

[[nodiscard]] consteval bool CoversEveryDecoderCodeInOrder() {
  for (std::size_t index = 0; index < kCreationCases.size(); ++index) {
    if (static_cast<std::size_t>(kCreationCases[index].decoder_code) != index)
      return false;
  }
  return true;
}

[[nodiscard]] consteval bool EveryExpectedMappingMatches() {
  for (const MappingCase &test_case : kCreationCases) {
    if (omega::app::detail::MapOpeningMovieDecoderCreationError(
            test_case.decoder_code) != test_case.player_code) {
      return false;
    }
  }
  return true;
}

static_assert(kCreationCases.size() ==
              static_cast<std::size_t>(DecoderCode::AllocationFailed) + 1U);
static_assert(CoversEveryDecoderCodeInOrder());
static_assert(EveryExpectedMappingMatches());
} // namespace

int main() {
  for (const MappingCase &test_case : kCreationCases) {
    if (omega::app::detail::MapOpeningMovieDecoderCreationError(
            test_case.decoder_code) != test_case.player_code) {
      std::cerr << "opening movie decoder creation mapping mismatch\n";
      return EXIT_FAILURE;
    }
  }
  std::cout << "omega_opening_movie_player_error_mapping_tests: all checks "
               "passed\n";
  return EXIT_SUCCESS;
}
