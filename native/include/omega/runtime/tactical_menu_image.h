#pragma once

#include "omega/runtime/debug_image.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

namespace omega::runtime {
inline constexpr std::uint32_t kTacticalMenuImageWidth = 960U;
inline constexpr std::uint32_t kTacticalMenuImageHeight = 540U;
inline constexpr std::size_t kTacticalMenuMaximumSavedAgents = 3U;
inline constexpr std::size_t kTacticalMenuMaximumLabelBytes = 24U;

enum class TacticalMenuScreen : std::uint8_t {
  Title,
  CreateAgent,
  LoadAgent,
  Briefing,
};

enum class CreateAgentPresentation : std::uint8_t {
  Empty,
  Ready,
  Confirmation,
};

// Renderer-only view data. The front end retains ownership of every string for
// the duration of BuildTacticalMenuImage; the resulting DebugImage owns all of
// its pixels.
struct TacticalMenuImageModel {
  TacticalMenuScreen screen = TacticalMenuScreen::Title;
  CreateAgentPresentation create_agent_presentation =
      CreateAgentPresentation::Empty;
  std::uint8_t selected_row = 0U;
  std::string_view agent_name;
  std::array<std::string_view, kTacticalMenuMaximumSavedAgents>
      saved_agent_labels{};
  std::uint8_t saved_agent_count = 0U;
  std::string_view mission_label;
};

// [any worker thread; reentrant] Builds one opaque, renderer-neutral RGBA8
// frame. All geometry, typography, colors, and coverage sampling are
// deterministic project-authored presentation; no platform font, native
// windowing API, third-party asset, or external data is consulted.
[[nodiscard]] std::expected<DebugImage, std::string>
BuildTacticalMenuImage(const TacticalMenuImageModel &model);
} // namespace omega::runtime
