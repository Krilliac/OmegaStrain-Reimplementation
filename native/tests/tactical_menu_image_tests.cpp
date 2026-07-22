#include "omega/runtime/tactical_menu_image.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>

namespace {
int failures = 0;

void Check(const bool condition, const std::string_view message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    ++failures;
  }
}

[[nodiscard]] bool IsFullOpaqueFrame(const omega::runtime::DebugImage &image) {
  if (image.width != omega::runtime::kTacticalMenuImageWidth ||
      image.height != omega::runtime::kTacticalMenuImageHeight ||
      image.rgba8_pixels.size() !=
          static_cast<std::size_t>(omega::runtime::kTacticalMenuImageWidth) *
              omega::runtime::kTacticalMenuImageHeight * 4U)
    return false;
  for (std::size_t offset = 3U; offset < image.rgba8_pixels.size();
       offset += 4U) {
    if (image.rgba8_pixels[offset] != std::byte{255U})
      return false;
  }
  return true;
}

[[nodiscard]] std::size_t
DifferenceCount(const omega::runtime::DebugImage &left,
                const omega::runtime::DebugImage &right) {
  if (left.rgba8_pixels.size() != right.rgba8_pixels.size())
    return std::max(left.rgba8_pixels.size(), right.rgba8_pixels.size());
  std::size_t differences = 0U;
  for (std::size_t index = 0U; index < left.rgba8_pixels.size(); ++index)
    differences +=
        left.rgba8_pixels[index] != right.rgba8_pixels[index] ? 1U : 0U;
  return differences;
}

[[nodiscard]] std::size_t
CountWarmCoverageTones(const omega::runtime::DebugImage &image) {
  std::array<bool, 256> observed_red{};
  for (std::size_t offset = 0U; offset < image.rgba8_pixels.size();
       offset += 4U) {
    const std::uint8_t red =
        std::to_integer<std::uint8_t>(image.rgba8_pixels[offset]);
    const std::uint8_t green =
        std::to_integer<std::uint8_t>(image.rgba8_pixels[offset + 1U]);
    const std::uint8_t blue =
        std::to_integer<std::uint8_t>(image.rgba8_pixels[offset + 2U]);
    if (red > green && green > blue && red > 70U)
      observed_red[red] = true;
  }
  return static_cast<std::size_t>(
      std::count(observed_red.begin(), observed_red.end(), true));
}

void CheckRejected(const omega::runtime::TacticalMenuImageModel &model,
                   const std::string_view message) {
  Check(!omega::runtime::BuildTacticalMenuImage(model), message);
}
} // namespace

int main() {
  using omega::runtime::BuildTacticalMenuImage;
  using omega::runtime::CreateAgentPresentation;
  using omega::runtime::TacticalMenuImageModel;
  using omega::runtime::TacticalMenuScreen;

  const TacticalMenuImageModel title{};
  auto first_title = BuildTacticalMenuImage(title);
  auto second_title = BuildTacticalMenuImage(title);
  Check(first_title && second_title && IsFullOpaqueFrame(*first_title),
        "title produces a fully owned opaque 960x540 RGBA frame");
  Check(first_title && second_title &&
            first_title->rgba8_pixels == second_title->rgba8_pixels,
        "title pixels are deterministic");
  Check(first_title &&
            first_title->pixels().size() == first_title->rgba8_pixels.size(),
        "the DebugImage view spans every owned byte");
  Check(first_title && CountWarmCoverageTones(*first_title) >= 4U,
        "selected vector text contains multiple supersampled amber coverage "
        "tones");

  TacticalMenuImageModel second_title_row = title;
  second_title_row.selected_row = 1U;
  auto moved_title_selection = BuildTacticalMenuImage(second_title_row);
  Check(first_title && moved_title_selection &&
            DifferenceCount(*first_title, *moved_title_selection) > 500U,
        "moving the title selection visibly changes the compact menu rows");

  TacticalMenuImageModel create_empty{
      .screen = TacticalMenuScreen::CreateAgent,
      .create_agent_presentation = CreateAgentPresentation::Empty,
  };
  auto empty_image = BuildTacticalMenuImage(create_empty);
  TacticalMenuImageModel create_ready{
      .screen = TacticalMenuScreen::CreateAgent,
      .create_agent_presentation = CreateAgentPresentation::Ready,
      .selected_row = 0U,
      .agent_name = "RAVEN-7",
  };
  auto ready_image = BuildTacticalMenuImage(create_ready);
  TacticalMenuImageModel create_confirmation = create_ready;
  create_confirmation.create_agent_presentation =
      CreateAgentPresentation::Confirmation;
  auto confirmation_image = BuildTacticalMenuImage(create_confirmation);
  Check(empty_image && ready_image && confirmation_image &&
            IsFullOpaqueFrame(*empty_image) &&
            IsFullOpaqueFrame(*ready_image) &&
            IsFullOpaqueFrame(*confirmation_image),
        "all create-agent presentations produce bounded opaque frames");
  Check(
      empty_image && ready_image && confirmation_image &&
          DifferenceCount(*empty_image, *ready_image) > 1000U &&
          DifferenceCount(*ready_image, *confirmation_image) > 1000U,
      "empty, ready, and confirmation create-agent presentations are distinct");

  std::string first_label = "RAVEN-7";
  TacticalMenuImageModel load{
      .screen = TacticalMenuScreen::LoadAgent,
      .selected_row = 1U,
      .saved_agent_labels = {first_label, "NOMAD", "ORION 3"},
      .saved_agent_count = 3U,
  };
  auto load_image = BuildTacticalMenuImage(load);
  Check(load_image && IsFullOpaqueFrame(*load_image),
        "three bounded load-agent labels render into one owned frame");
  if (load_image) {
    const auto owned_pixels = load_image->rgba8_pixels;
    first_label.assign("CHANGED");
    Check(load_image->rgba8_pixels == owned_pixels,
          "rendered pixels do not borrow caller-owned saved-agent text");
  }

  TacticalMenuImageModel empty_load{
      .screen = TacticalMenuScreen::LoadAgent,
      .selected_row = 0U,
  };
  auto empty_load_image = BuildTacticalMenuImage(empty_load);
  Check(
      empty_load_image && load_image &&
          DifferenceCount(*empty_load_image, *load_image) > 1000U,
      "an empty personnel archive is visibly distinct from three saved agents");

  TacticalMenuImageModel briefing{
      .screen = TacticalMenuScreen::Briefing,
      .selected_row = 0U,
      .mission_label = "CARTHAGE 1",
  };
  auto briefing_image = BuildTacticalMenuImage(briefing);
  Check(briefing_image && IsFullOpaqueFrame(*briefing_image),
        "the briefing entry placeholder is a bounded opaque frame");
  Check(first_title && briefing_image &&
            DifferenceCount(*first_title, *briefing_image) > 1000U,
        "title and briefing surfaces are structurally distinct");

  TacticalMenuImageModel invalid = title;
  invalid.selected_row = 3U;
  CheckRejected(invalid, "title rejects a fourth selection");

  invalid = create_empty;
  invalid.selected_row = 1U;
  CheckRejected(invalid,
                "empty create-agent presentation rejects a phantom row");

  invalid = create_ready;
  invalid.agent_name = {};
  CheckRejected(invalid, "ready create-agent presentation requires a name");

  invalid = create_ready;
  invalid.agent_name = "1234567890123456789012345";
  CheckRejected(invalid, "agent names reject more than 24 bytes");

  invalid = create_ready;
  invalid.agent_name = "BAD\nNAME";
  CheckRejected(invalid, "agent names reject non-printable bytes");

  invalid = load;
  invalid.saved_agent_count = 4U;
  CheckRejected(invalid,
                "load-agent presentation rejects more than three labels");

  invalid = empty_load;
  invalid.selected_row = 1U;
  CheckRejected(invalid,
                "empty load-agent presentation exposes only its back row");

  invalid = load;
  invalid.saved_agent_labels[1] = {};
  CheckRejected(invalid, "counted saved-agent labels must not be empty");

  invalid = briefing;
  invalid.screen = static_cast<TacticalMenuScreen>(255U);
  CheckRejected(invalid, "unknown screen enum values fail closed");

  invalid = create_ready;
  invalid.create_agent_presentation =
      static_cast<CreateAgentPresentation>(255U);
  CheckRejected(invalid, "unknown create-agent enum values fail closed");

  if (failures == 0)
    std::cout << "tactical menu image tests passed\n";
  return failures == 0 ? 0 : 1;
}
