#pragma once

#include <array>
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

// Canonical widget hierarchy. Retail factory names, flags, unproven style
// values, byte offsets, and source storage are intentionally absent.
struct FrontendWidgetIR {
  FrontendWidgetKind kind = FrontendWidgetKind::Container;
  std::string identifier;
  std::array<float, 4> layout_values{};
  std::optional<std::string> text_reference;
  std::optional<std::string> font_reference;
  std::optional<FrontendWidgetBindingIR> binding;
  std::vector<FrontendWidgetIR> children;

  bool operator==(const FrontendWidgetIR &) const = default;
};

struct FrontendWidgetDocumentIR {
  FrontendWidgetIR root;

  bool operator==(const FrontendWidgetDocumentIR &) const = default;
};

// Canonical visual-resource hierarchy paired with a widget document. Fixed
// source streams and animation records are validated by the retail decoder but
// remain absent until their semantic roles are proven. Nonempty texture members
// include the proven .TDX suffix.
struct FrontendVisualNodeIR {
  std::string identifier;
  std::optional<std::string> texture_member;
  std::array<float, 12> transform_values{};
  std::vector<FrontendVisualNodeIR> children;

  bool operator==(const FrontendVisualNodeIR &) const = default;
};

struct FrontendVisualDocumentIR {
  FrontendVisualNodeIR root;

  bool operator==(const FrontendVisualDocumentIR &) const = default;
};
} // namespace omega::asset
