#pragma once

#include "omega/asset/geometry_ir.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace omega::asset {
enum class FrontendWidgetKind {
  Container,
  Text,
  Button,
  CharacterDisplay,
};

// Retail GUI rectangles are stored as a left/top origin with X increasing to
// the right and Y increasing upward. The lower edge is therefore top-height.
struct FrontendWidgetRectangleIR {
  float left = 0.0F;
  float top = 0.0F;
  float width = 0.0F;
  float height = 0.0F;

  bool operator==(const FrontendWidgetRectangleIR &) const = default;
};

// Selected retail records store normalized RGB and supply an implicit opaque
// alpha. No color-space, premultiplication, or blend meaning is inferred.
struct FrontendTextColorIR {
  float red = 0.0F;
  float green = 0.0F;
  float blue = 0.0F;
  float alpha = 1.0F;

  bool operator==(const FrontendTextColorIR &) const = default;
};

enum class FrontendTextAlignment : std::uint8_t {
  Left = 0,
  Right = 1,
  Center = 2,
};

// Canonical, fully owned references used by a widget's interface binding. The
// ordered values are intentionally neutral until their individual coordinate
// and matrix roles are independently established.
struct FrontendWidgetBindingIR {
  std::string scope_reference;
  std::string resource_reference;
  std::array<float, 12> transform_values{};
  std::vector<std::string> action_references;

  bool operator==(const FrontendWidgetBindingIR &) const = default;
};

// Canonical widget hierarchy. Retail factory names, raw flags/style values,
// byte offsets, and source storage are intentionally absent. Text-specific
// optionals are populated together by the decoder only for Text and Button
// widgets.
struct FrontendWidgetIR {
  FrontendWidgetKind kind = FrontendWidgetKind::Container;
  std::string identifier;
  FrontendWidgetRectangleIR rectangle;
  // Asset-authored render gate.
  bool visible = false;
  // Asset-authored focus/activation and disabled-text gate.
  bool enabled = false;
  std::optional<std::string> text_reference;
  std::optional<std::string> font_reference;
  std::optional<FrontendTextColorIR> text_color;
  std::optional<FrontendTextAlignment> text_alignment;
  std::optional<FrontendWidgetBindingIR> binding;
  std::vector<FrontendWidgetIR> children;

  bool operator==(const FrontendWidgetIR &) const = default;
};

struct FrontendWidgetDocumentIR {
  FrontendWidgetIR root;

  bool operator==(const FrontendWidgetDocumentIR &) const = default;
};

struct FrontendUvIR {
  float u = 0.0F;
  float v = 0.0F;

  bool operator==(const FrontendUvIR &) const = default;
};

// Project-owned result of the retail normalized-float-to-byte conversion.
// Channels carry no inferred color-space, premultiplication, or blend meaning.
struct FrontendColorRgba8IR {
  std::uint8_t red = 0;
  std::uint8_t green = 0;
  std::uint8_t blue = 0;
  std::uint8_t alpha = 0;

  bool operator==(const FrontendColorRgba8IR &) const = default;
};

struct FrontendTriangleIR {
  std::array<std::uint16_t, 3> position_indices{};
  std::array<std::uint16_t, 3> uv_indices{};
  std::array<std::uint16_t, 3> color_indices{};

  bool operator==(const FrontendTriangleIR &) const = default;
};

// Canonical visual-resource hierarchy paired with a widget document. Nonempty
// texture members include the proven .TDX suffix. transform_values preserves
// the twelve source coefficients of a proven affine transform; whether the
// retail bridge treats those coefficients as rows or columns remains unknown.
// Animation records remain outside canonical IR until their semantics are
// independently established.
struct FrontendVisualNodeIR {
  std::string identifier;
  std::optional<std::string> texture_member;
  std::array<float, 12> transform_values{};
  std::vector<Float3IR> positions;
  std::vector<FrontendUvIR> uvs;
  std::vector<FrontendColorRgba8IR> colors;
  std::vector<FrontendTriangleIR> triangles;
  std::vector<FrontendVisualNodeIR> children;

  bool operator==(const FrontendVisualNodeIR &) const = default;
};

struct FrontendVisualDocumentIR {
  FrontendVisualNodeIR root;

  bool operator==(const FrontendVisualDocumentIR &) const = default;
};

static_assert(sizeof(FrontendUvIR) == 8U);
static_assert(sizeof(FrontendColorRgba8IR) == 4U);
static_assert(sizeof(FrontendTriangleIR) == 18U);
static_assert(sizeof(FrontendWidgetRectangleIR) == 16U);
static_assert(sizeof(FrontendTextColorIR) == 16U);
} // namespace omega::asset
