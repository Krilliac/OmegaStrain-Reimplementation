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

// Runtime mapping applies to a decoder Create already accepted, so it must keep
// WrongThread reserved for the player's own non-mutating boundary check.
constexpr std::array kRuntimeCases{
    MappingCase{DecoderCode::UnsupportedPlatform, PlayerCode::DecoderFailed},
    MappingCase{DecoderCode::InvalidConfiguration, PlayerCode::DecoderFailed},
    MappingCase{DecoderCode::WrongThread, PlayerCode::DecoderFailed},
    MappingCase{DecoderCode::AlreadyDrained, PlayerCode::DecoderFailed},
    MappingCase{DecoderCode::DecoderPoisoned, PlayerCode::DecoderFailed},
    MappingCase{DecoderCode::InitializationFailed, PlayerCode::DecoderFailed},
    MappingCase{DecoderCode::DecoderUnavailable, PlayerCode::DecoderFailed},
    MappingCase{DecoderCode::MediaTypeRejected, PlayerCode::DecoderFailed},
    MappingCase{DecoderCode::OutputTypeUnavailable, PlayerCode::DecoderFailed},
    MappingCase{DecoderCode::InputLimitExceeded, PlayerCode::DecoderFailed},
    MappingCase{DecoderCode::OutputLimitExceeded, PlayerCode::DecoderFailed},
    MappingCase{DecoderCode::FrameLimitExceeded, PlayerCode::DecoderFailed},
    MappingCase{DecoderCode::UnsupportedOutputLayout, PlayerCode::DecoderFailed},
    MappingCase{DecoderCode::TransformFailure, PlayerCode::DecoderFailed},
    MappingCase{DecoderCode::AllocationFailed, PlayerCode::AllocationFailed},
};

template <std::size_t Count>
[[nodiscard]] consteval bool
CoversEveryDecoderCodeInOrder(const std::array<MappingCase, Count> &cases) {
  for (std::size_t index = 0; index < cases.size(); ++index) {
    if (static_cast<std::size_t>(cases[index].decoder_code) != index)
      return false;
  }
  return true;
}

[[nodiscard]] consteval bool EveryExpectedCreationMappingMatches() {
  for (const MappingCase &test_case : kCreationCases) {
    if (omega::app::detail::MapOpeningMovieDecoderCreationError(
            test_case.decoder_code) != test_case.player_code) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] consteval bool EveryExpectedRuntimeMappingMatches() {
  for (const MappingCase &test_case : kRuntimeCases) {
    if (omega::app::detail::MapOpeningMovieDecoderRuntimeError(
            test_case.decoder_code) != test_case.player_code) {
      return false;
    }
  }
  return true;
}

// The player's own boundary check is the only source of WrongThread, so no
// decoder code may reach that player code through either mapping.
[[nodiscard]] consteval bool NeitherMappingProducesABoundaryCode() {
  for (const MappingCase &test_case : kCreationCases) {
    const PlayerCode creation = omega::app::detail::
        MapOpeningMovieDecoderCreationError(test_case.decoder_code);
    const PlayerCode runtime = omega::app::detail::
        MapOpeningMovieDecoderRuntimeError(test_case.decoder_code);
    if (creation == PlayerCode::WrongThread ||
        creation == PlayerCode::MovedFrom ||
        runtime == PlayerCode::WrongThread || runtime == PlayerCode::MovedFrom) {
      return false;
    }
  }
  return true;
}

static_assert(kCreationCases.size() ==
              static_cast<std::size_t>(DecoderCode::AllocationFailed) + 1U);
static_assert(kRuntimeCases.size() == kCreationCases.size());
static_assert(CoversEveryDecoderCodeInOrder(kCreationCases));
static_assert(CoversEveryDecoderCodeInOrder(kRuntimeCases));
static_assert(EveryExpectedCreationMappingMatches());
static_assert(EveryExpectedRuntimeMappingMatches());
static_assert(NeitherMappingProducesABoundaryCode());
} // namespace

int main() {
  for (const MappingCase &test_case : kCreationCases) {
    if (omega::app::detail::MapOpeningMovieDecoderCreationError(
            test_case.decoder_code) != test_case.player_code) {
      std::cerr << "opening movie decoder creation mapping mismatch\n";
      return EXIT_FAILURE;
    }
  }
  for (const MappingCase &test_case : kRuntimeCases) {
    if (omega::app::detail::MapOpeningMovieDecoderRuntimeError(
            test_case.decoder_code) != test_case.player_code) {
      std::cerr << "opening movie decoder runtime mapping mismatch\n";
      return EXIT_FAILURE;
    }
  }
  std::cout << "omega_opening_movie_player_error_mapping_tests: all checks "
               "passed\n";
  return EXIT_SUCCESS;
}
