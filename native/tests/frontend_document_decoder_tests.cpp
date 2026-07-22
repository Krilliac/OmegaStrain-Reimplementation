#include "omega/retail/frontend_document_decoder.h"
#include "omega/retail/gui_envelope_descriptor.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
void AppendU16(std::vector<std::byte> &bytes, const std::uint16_t value) {
  bytes.push_back(static_cast<std::byte>(value & 0xFFU));
  bytes.push_back(static_cast<std::byte>((value >> 8U) & 0xFFU));
}

void AppendU32(std::vector<std::byte> &bytes, const std::uint32_t value) {
  for (unsigned shift = 0; shift < 32; shift += 8)
    bytes.push_back(static_cast<std::byte>((value >> shift) & 0xFFU));
}

void WriteU32(std::vector<std::byte> &bytes, const std::size_t offset,
              const std::uint32_t value) {
  for (unsigned shift = 0; shift < 32; shift += 8)
    bytes[offset + shift / 8U] =
        static_cast<std::byte>((value >> shift) & 0xFFU);
}

void AppendF32(std::vector<std::byte> &bytes, const float value) {
  AppendU32(bytes, std::bit_cast<std::uint32_t>(value));
}

void WriteF32(std::vector<std::byte> &bytes, const std::size_t offset,
              const float value) {
  WriteU32(bytes, offset, std::bit_cast<std::uint32_t>(value));
}

void AppendString(std::vector<std::byte> &bytes, const std::string_view value) {
  for (const char character : value)
    bytes.push_back(static_cast<std::byte>(character));
  bytes.push_back(std::byte{0});
}

void AppendZeros(std::vector<std::byte> &bytes, const std::size_t count) {
  bytes.insert(bytes.end(), count, std::byte{0});
}

void Align4(std::vector<std::byte> &bytes) {
  while ((bytes.size() & 3U) != 0)
    bytes.push_back(std::byte{0xA5});
}

[[nodiscard]] std::uint8_t Align16(std::vector<std::byte> &bytes) {
  const std::size_t logical_size = bytes.size();
  while ((bytes.size() & 15U) != 0)
    bytes.push_back(std::byte{0});
  return static_cast<std::uint8_t>(bytes.size() - logical_size);
}

struct GuiNodeSpec {
  std::string_view factory;
  std::string_view identifier;
  std::array<float, 4> layout_values{};
  bool has_text_record = false;
  std::string_view text_reference;
  std::string_view font_reference;
  bool decorated = true;
  std::string_view scope_reference;
  std::string_view resource_reference;
  std::array<float, 12> transform_values{};
  std::vector<std::string_view> actions;
  std::vector<GuiNodeSpec> children;
};

void AppendGuiNode(std::vector<std::byte> &bytes, const GuiNodeSpec &node) {
  AppendString(bytes, node.factory);
  AppendString(bytes, node.decorated ? "GuiInterfaceDecorator" : "");
  AppendString(bytes, node.identifier);
  Align4(bytes);
  for (const float value : node.layout_values)
    AppendF32(bytes, value);
  AppendU16(bytes, 1);
  AppendU16(bytes, 0);
  if (node.has_text_record) {
    AppendString(bytes, node.text_reference);
    AppendString(bytes, node.font_reference);
    Align4(bytes);
    AppendF32(bytes, 0.25F);
    AppendF32(bytes, 0.5F);
    AppendF32(bytes, 0.75F);
    AppendU32(bytes, 0x12345678U);
  }
  if (node.decorated) {
    AppendString(bytes, node.scope_reference);
    AppendString(bytes, node.resource_reference);
    Align4(bytes);
    for (const float value : node.transform_values)
      AppendF32(bytes, value);
    AppendU16(bytes, static_cast<std::uint16_t>(node.actions.size()));
    for (const std::string_view action : node.actions) {
      AppendString(bytes, action);
      Align4(bytes);
      AppendU16(bytes, 3);
      AppendU16(bytes, 0);
      AppendU16(bytes, 42);
    }
  }
  AppendU16(bytes, static_cast<std::uint16_t>(node.children.size()));
  for (const GuiNodeSpec &child : node.children)
    AppendGuiNode(bytes, child);
}

struct GuiFixture {
  std::vector<std::byte> bytes;
  std::uint8_t padding = 0;
};

[[nodiscard]] GuiFixture EncodeGuiFixture(const GuiNodeSpec &root) {
  GuiFixture fixture;
  fixture.bytes = {std::byte{'G'},  std::byte{'U'},  std::byte{'I'},
                   std::byte{0x7E}, std::byte{0x34}, std::byte{0x12}};
  AppendGuiNode(fixture.bytes, root);
  fixture.padding = Align16(fixture.bytes);
  return fixture;
}

[[nodiscard]] GuiFixture MakeGuiFixture() {
  GuiNodeSpec character{
      .factory = "GuiCharacterDisplay",
      .identifier = "agent_preview",
      .layout_values = {9.0F, 10.0F, 11.0F, 12.0F},
      .decorated = false,
  };
  GuiNodeSpec button{
      .factory = "GuiButtonWidget",
      .identifier = "create_agent",
      .layout_values = {5.0F, 6.0F, 7.0F, 8.0F},
      .has_text_record = true,
      .text_reference = "$CreateAgent",
      .font_reference = "Default",
      .scope_reference = "shell",
      .resource_reference = "button_visual",
      .transform_values = {20.0F, 21.0F, 22.0F, 23.0F, 24.0F, 25.0F, 26.0F,
                           27.0F, 28.0F, 29.0F, 30.0F, 31.0F},
      .actions = {"gain_focus", "activate"},
  };
  GuiNodeSpec text{
      .factory = "GuiTextWidget",
      .identifier = "agent_status",
      .layout_values = {13.0F, 14.0F, 15.0F, 16.0F},
      .has_text_record = true,
      .text_reference = "$AgentStatus",
      .font_reference = "Small",
      .scope_reference = "shell",
      .resource_reference = "status_visual",
      .transform_values = {30.0F, 31.0F, 32.0F, 33.0F, 34.0F, 35.0F, 36.0F,
                           37.0F, 38.0F, 39.0F, 40.0F, 41.0F},
      .actions = {"refresh"},
  };
  GuiNodeSpec root{
      .factory = "GuiWidget",
      .identifier = "title_root",
      .layout_values = {1.0F, 2.0F, 3.0F, 4.0F},
      .scope_reference = "",
      .resource_reference = "root_visual",
      .transform_values = {10.0F, 11.0F, 12.0F, 13.0F, 14.0F, 15.0F, 16.0F,
                           17.0F, 18.0F, 19.0F, 20.0F, 21.0F},
      .actions = {"wake"},
      .children = {button, text, character},
  };

  return EncodeGuiFixture(root);
}

struct IeFixture {
  std::vector<std::byte> bytes;
  std::size_t root_transform_offset = 0;
  std::size_t position_count_offset = 0;
  std::size_t position_data_offset = 0;
  std::size_t uv_data_offset = 0;
  std::size_t color_data_offset = 0;
  std::size_t triangle_data_offset = 0;
  std::size_t vertex_kind_offset = 0;
  std::size_t vertex_count_offset = 0;
  std::size_t animation_payload_offset = 0;
  std::size_t secondary_count_offset = 0;
  std::size_t root_child_count_offset = 0;
  std::uint8_t padding = 0;
};

void AppendIeEmptyNode(std::vector<std::byte> &bytes,
                       const std::string_view identifier,
                       const std::array<float, 12> &transform_values) {
  AppendString(bytes, identifier);
  AppendString(bytes, "");
  Align4(bytes);
  for (const float value : transform_values)
    AppendF32(bytes, value);
  for (unsigned index = 0; index < 4; ++index)
    AppendU32(bytes, 0);
  AppendU32(bytes, 0);
  AppendU32(bytes, 0);
  AppendU32(bytes, 0);
}

[[nodiscard]] IeFixture MakeIeFixture() {
  IeFixture fixture;
  fixture.bytes = {std::byte{0x11}, std::byte{0x22}, std::byte{0x33},
                   std::byte{0x44}};

  AppendString(fixture.bytes, "visual_root");
  AppendString(fixture.bytes, "");
  Align4(fixture.bytes);
  fixture.root_transform_offset = fixture.bytes.size();
  for (unsigned index = 0; index < 12; ++index)
    AppendF32(fixture.bytes, static_cast<float>(index));
  for (unsigned index = 0; index < 4; ++index)
    AppendU32(fixture.bytes, 0);
  AppendU32(fixture.bytes, 0);
  AppendU32(fixture.bytes, 0);
  fixture.root_child_count_offset = fixture.bytes.size();
  AppendU32(fixture.bytes, 1);

  AppendString(fixture.bytes, "panel");
  AppendString(fixture.bytes, "panel_skin");
  Align4(fixture.bytes);
  for (unsigned index = 0; index < 12; ++index)
    AppendF32(fixture.bytes, 100.0F + static_cast<float>(index));

  fixture.position_count_offset = fixture.bytes.size();
  AppendU32(fixture.bytes, 2);
  fixture.position_data_offset = fixture.bytes.size();
  for (const float value :
       std::array<float, 6>{1.0F, 2.0F, 3.0F, 4.0F, 5.0F, 6.0F})
    AppendF32(fixture.bytes, value);
  AppendU32(fixture.bytes, 1);
  fixture.uv_data_offset = fixture.bytes.size();
  AppendF32(fixture.bytes, 0.25F);
  AppendF32(fixture.bytes, 0.75F);
  AppendU32(fixture.bytes, 1);
  fixture.color_data_offset = fixture.bytes.size();
  AppendF32(fixture.bytes, 0.0F);
  AppendF32(fixture.bytes, 0.5F);
  AppendF32(fixture.bytes, 1.0F);
  AppendF32(fixture.bytes, 0.1F);
  AppendU32(fixture.bytes, 1);
  fixture.triangle_data_offset = fixture.bytes.size();
  for (const std::uint32_t index :
       std::array<std::uint32_t, 9>{0, 1, 0, 0, 0, 0, 0, 0, 0})
    AppendU32(fixture.bytes, index);

  AppendU32(fixture.bytes, 4);
  fixture.vertex_kind_offset = fixture.bytes.size();
  AppendString(fixture.bytes, "VERTEX");
  Align4(fixture.bytes);
  fixture.vertex_count_offset = fixture.bytes.size();
  AppendU32(fixture.bytes, 2);
  for (unsigned entry = 0; entry < 2; ++entry) {
    AppendU32(fixture.bytes, 2);
    if (entry == 0)
      fixture.animation_payload_offset = fixture.bytes.size();
    AppendZeros(fixture.bytes, 2U * 16U);
  }
  AppendString(fixture.bytes, "OPACITY");
  Align4(fixture.bytes);
  AppendU32(fixture.bytes, 2);
  AppendZeros(fixture.bytes, 2U * 8U);
  AppendString(fixture.bytes, "UVOFF_U");
  Align4(fixture.bytes);
  AppendU32(fixture.bytes, 1);
  AppendZeros(fixture.bytes, 8);
  AppendString(fixture.bytes, "UVOFF_V");
  Align4(fixture.bytes);
  AppendU32(fixture.bytes, 1);
  AppendZeros(fixture.bytes, 8);

  fixture.secondary_count_offset = fixture.bytes.size();
  AppendU32(fixture.bytes, 0);
  AppendU32(fixture.bytes, 1);
  AppendIeEmptyNode(fixture.bytes, "panel_child",
                    {200.0F, 201.0F, 202.0F, 203.0F, 204.0F, 205.0F, 206.0F,
                     207.0F, 208.0F, 209.0F, 210.0F, 211.0F});
  fixture.padding = Align16(fixture.bytes);
  return fixture;
}

[[nodiscard]] std::size_t FindString(const std::vector<std::byte> &bytes,
                                     const std::string_view value) {
  std::vector<std::byte> needle;
  AppendString(needle, value);
  const auto found =
      std::search(bytes.begin(), bytes.end(), needle.begin(), needle.end());
  return found == bytes.end() ? std::string::npos
                              : static_cast<std::size_t>(found - bytes.begin());
}

int failures = 0;

void Check(const bool condition, const std::string_view message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    ++failures;
  }
}

template <typename Value>
void CheckError(const omega::asset::DecodeResult<Value> &result,
                const omega::asset::DecodeErrorCode code,
                const std::string_view message) {
  if (result) {
    Check(false, message);
    return;
  }
  Check(result.error().code == code, message);
  Check(!result.error().message.empty(),
        "frontend errors own a nonempty diagnostic");
  Check(result.error().message.find('/') == std::string::npos &&
            result.error().message.find('\\') == std::string::npos,
        "frontend errors contain no filesystem path");
}
} // namespace

int main() {
  const GuiFixture gui_fixture = MakeGuiFixture();
  Check(gui_fixture.padding > 0 && gui_fixture.padding <= 15,
        "generated GUI fixture exercises bounded terminal alignment");
  const auto gui_measured =
      omega::retail::DecodeGuiFrontendMeasured(gui_fixture.bytes);
  Check(gui_measured.has_value(),
        "GUI decodes a generated recursive frontend document");
  if (gui_measured) {
    const auto &root = gui_measured->document.root;
    Check(root.kind == omega::asset::FrontendWidgetKind::Container &&
              root.identifier == "title_root" &&
              root.layout_values ==
                  std::array<float, 4>{1.0F, 2.0F, 3.0F, 4.0F} &&
              root.binding && root.binding->scope_reference.empty() &&
              root.binding->resource_reference == "root_visual" &&
              root.binding->transform_values[0] == 10.0F &&
              root.binding->transform_values[11] == 21.0F &&
              root.binding->action_references ==
                  std::vector<std::string>{"wake"},
          "GUI retains only proven ordered layout, binding, transform, and "
          "action fields");
    Check(root.children.size() == 3 &&
              root.children[0].kind ==
                  omega::asset::FrontendWidgetKind::Button &&
              root.children[0].identifier == "create_agent" &&
              root.children[0].text_reference == "$CreateAgent" &&
              root.children[0].font_reference == "Default" &&
              root.children[0].binding &&
              root.children[0].binding->action_references ==
                  std::vector<std::string>{"gain_focus", "activate"} &&
              root.children[1].kind == omega::asset::FrontendWidgetKind::Text &&
              root.children[1].identifier == "agent_status" &&
              root.children[1].text_reference == "$AgentStatus" &&
              root.children[1].font_reference == "Small" &&
              root.children[1].binding &&
              root.children[1].binding->resource_reference == "status_visual" &&
              root.children[1].binding->action_references ==
                  std::vector<std::string>{"refresh"} &&
              root.children[2].kind ==
                  omega::asset::FrontendWidgetKind::CharacterDisplay &&
              !root.children[2].binding,
          "GUI preserves source-order child topology and all four supported "
          "widget kinds");
    Check(gui_measured->decoded_items > 0 &&
              gui_measured->logical_output_bytes >=
                  sizeof(omega::asset::FrontendWidgetDocumentIR) &&
              gui_measured->trailing_zero_bytes == gui_fixture.padding,
          "GUI measured decode reports exact budgets and terminal zero "
          "alignment");
  }

  const auto gui = omega::retail::DecodeGuiFrontend(gui_fixture.bytes);
  Check(gui && gui_measured && *gui == gui_measured->document,
        "GUI convenience decode returns the measured canonical document");
  auto gui_owned_bytes = gui_fixture.bytes;
  auto owned_gui = omega::retail::DecodeGuiFrontend(gui_owned_bytes);
  std::fill(gui_owned_bytes.begin(), gui_owned_bytes.end(), std::byte{0xFF});
  Check(owned_gui && owned_gui->root.children[0].font_reference == "Default",
        "GUI output remains valid after source storage replacement");

  auto changed_gui_prefix = gui_fixture.bytes;
  changed_gui_prefix[3] = std::byte{0x01};
  changed_gui_prefix[4] = std::byte{0xFE};
  changed_gui_prefix[5] = std::byte{0xCA};
  const auto changed_prefix_gui =
      omega::retail::DecodeGuiFrontend(changed_gui_prefix);
  Check(gui && changed_prefix_gui && *gui == *changed_prefix_gui,
        "GUI does not assign semantics to the skipped prefix byte or observed "
        "word");

  auto bad_gui = gui_fixture.bytes;
  bad_gui[0] = std::byte{'X'};
  CheckError(omega::retail::DecodeGuiFrontend(bad_gui),
             omega::asset::DecodeErrorCode::UnsupportedVariant,
             "GUI rejects an unsupported tag");
  bad_gui = gui_fixture.bytes;
  const std::size_t button_factory = FindString(bad_gui, "GuiButtonWidget");
  Check(button_factory != std::string::npos,
        "generated GUI contains a button factory token");
  bad_gui[button_factory] = std::byte{'X'};
  CheckError(omega::retail::DecodeGuiFrontend(bad_gui),
             omega::asset::DecodeErrorCode::UnsupportedVariant,
             "GUI rejects an unsupported widget factory");
  bad_gui = gui_fixture.bytes;
  const std::size_t decorator = FindString(bad_gui, "GuiInterfaceDecorator");
  Check(decorator != std::string::npos,
        "generated GUI contains a decorator token");
  bad_gui[decorator] = std::byte{'X'};
  CheckError(omega::retail::DecodeGuiFrontend(bad_gui),
             omega::asset::DecodeErrorCode::UnsupportedVariant,
             "GUI rejects an unsupported decorator family");
  bad_gui = gui_fixture.bytes;
  bad_gui.back() = std::byte{1};
  CheckError(omega::retail::DecodeGuiFrontend(bad_gui),
             omega::asset::DecodeErrorCode::Malformed,
             "GUI rejects nonzero terminal alignment bytes");
  bad_gui = gui_fixture.bytes;
  bad_gui.pop_back();
  CheckError(omega::retail::DecodeGuiFrontend(bad_gui),
             omega::asset::DecodeErrorCode::Truncated,
             "GUI rejects a missing terminal alignment byte");
  bad_gui = gui_fixture.bytes;
  AppendZeros(bad_gui, 16);
  CheckError(omega::retail::DecodeGuiFrontend(bad_gui),
             omega::asset::DecodeErrorCode::Malformed,
             "GUI rejects bytes beyond one terminal-alignment boundary");

  if (gui_measured) {
    auto limits = omega::asset::DecodeLimits{};
    limits.maximum_input_bytes = gui_fixture.bytes.size();
    limits.maximum_items = gui_measured->decoded_items;
    limits.maximum_output_bytes = gui_measured->logical_output_bytes;
    limits.maximum_scratch_bytes = 0;
    Check(
        omega::retail::DecodeGuiFrontend(gui_fixture.bytes, limits).has_value(),
        "GUI accepts exact input, item, output, and zero-scratch budgets");
    --limits.maximum_items;
    CheckError(omega::retail::DecodeGuiFrontend(gui_fixture.bytes, limits),
               omega::asset::DecodeErrorCode::LimitExceeded,
               "GUI rejects one item below its exact decoded-item budget");
    limits.maximum_items = gui_measured->decoded_items;
    --limits.maximum_output_bytes;
    CheckError(omega::retail::DecodeGuiFrontend(gui_fixture.bytes, limits),
               omega::asset::DecodeErrorCode::LimitExceeded,
               "GUI rejects one byte below its exact logical-output budget");
  }
  auto gui_limits = omega::asset::DecodeLimits{};
  gui_limits.maximum_nesting_depth = 0;
  CheckError(omega::retail::DecodeGuiFrontend(gui_fixture.bytes, gui_limits),
             omega::asset::DecodeErrorCode::LimitExceeded,
             "GUI enforces the caller's root-at-zero nesting-depth limit");
  gui_limits = omega::asset::DecodeLimits{};
  gui_limits.maximum_string_bytes = 0;
  CheckError(omega::retail::DecodeGuiFrontend(gui_fixture.bytes, gui_limits),
             omega::asset::DecodeErrorCode::LimitExceeded,
             "GUI enforces the caller's per-string byte limit");

  const std::string overlong_identifier(
      static_cast<std::size_t>(omega::retail::kFrontendMaximumStringBytes) + 1U,
      'X');
  const GuiFixture overlong_string_fixture = EncodeGuiFixture(GuiNodeSpec{
      .factory = "GuiWidget",
      .identifier = overlong_identifier,
      .decorated = false,
  });
  auto raised_string_limits = omega::asset::DecodeLimits{};
  raised_string_limits.maximum_string_bytes =
      std::numeric_limits<std::uint32_t>::max();
  CheckError(
      omega::retail::DecodeGuiFrontend(overlong_string_fixture.bytes,
                                       raised_string_limits),
      omega::asset::DecodeErrorCode::LimitExceeded,
      "GUI caller limits cannot raise the fixed per-string byte ceiling");

  GuiNodeSpec nested_node{
      .factory = "GuiWidget",
      .identifier = "nested",
      .decorated = false,
  };
  for (std::uint32_t depth = 0;
       depth <= omega::retail::kFrontendMaximumNestingDepth; ++depth) {
    GuiNodeSpec parent{
        .factory = "GuiWidget",
        .identifier = "nested",
        .decorated = false,
    };
    parent.children.push_back(std::move(nested_node));
    nested_node = std::move(parent);
  }
  const GuiFixture excessive_depth_fixture = EncodeGuiFixture(nested_node);
  auto raised_depth_limits = omega::asset::DecodeLimits{};
  raised_depth_limits.maximum_nesting_depth =
      std::numeric_limits<std::uint32_t>::max();
  CheckError(omega::retail::DecodeGuiFrontend(excessive_depth_fixture.bytes,
                                              raised_depth_limits),
             omega::asset::DecodeErrorCode::LimitExceeded,
             "GUI caller limits cannot raise the fixed nesting-depth ceiling");

  const IeFixture ie_fixture = MakeIeFixture();
  Check(ie_fixture.padding > 0 && ie_fixture.padding <= 15,
        "generated IE fixture exercises bounded terminal alignment");
  const auto ie_measured =
      omega::retail::DecodeIeFrontendMeasured(ie_fixture.bytes);
  Check(ie_measured.has_value(),
        "IE decodes a generated recursive visual document");
  if (ie_measured) {
    const auto &root = ie_measured->document.root;
    Check(root.identifier == "visual_root" && !root.texture_member &&
              root.transform_values[0] == 0.0F &&
              root.transform_values[11] == 11.0F && root.children.size() == 1 &&
              root.children[0].identifier == "panel" &&
              root.children[0].texture_member == "panel_skin.TDX" &&
              root.children[0].transform_values[0] == 100.0F &&
              root.children[0].transform_values[11] == 111.0F &&
              root.children[0].children.size() == 1 &&
              root.children[0].children[0].identifier == "panel_child",
          "IE retains proven identifiers, texture members, ordered transforms, "
          "and topology");
    const auto &panel = root.children[0];
    Check(panel.positions.size() == 2 &&
              panel.positions[0] ==
                  omega::asset::Float3IR{.x = 1.0F, .y = 2.0F, .z = 3.0F} &&
              panel.positions[1] ==
                  omega::asset::Float3IR{.x = 4.0F, .y = 5.0F, .z = 6.0F} &&
              panel.uvs ==
                  std::vector<omega::asset::FrontendUvIR>{
                      {.u = 0.25F, .v = 0.75F}} &&
              panel.colors == std::vector<omega::asset::FrontendColorRgba8IR>{{
                                  .red = 0,
                                  .green = 127,
                                  .blue = 255,
                                  .alpha = 25,
                              }} &&
              panel.triangles == std::vector<omega::asset::FrontendTriangleIR>{{
                                     .position_indices = {0, 1, 0},
                                     .uv_indices = {0, 0, 0},
                                     .color_indices = {0, 0, 0},
                                 }},
          "IE retains owned positions, UVs, toward-zero RGBA8 colors, and "
          "separate index triples");
    Check(
        ie_measured->decoded_items > 0 &&
            ie_measured->logical_output_bytes >=
                sizeof(omega::asset::FrontendVisualDocumentIR) &&
            ie_measured->trailing_zero_bytes == ie_fixture.padding,
        "IE measured decode reports exact budgets and terminal zero alignment");
  }

  const auto ie = omega::retail::DecodeIeFrontend(ie_fixture.bytes);
  Check(ie && ie_measured && *ie == ie_measured->document,
        "IE convenience decode returns the measured canonical document");
  auto changed_ie_prefix = ie_fixture.bytes;
  changed_ie_prefix[0] = std::byte{0xAA};
  changed_ie_prefix[1] = std::byte{0xBB};
  changed_ie_prefix[2] = std::byte{0xCC};
  changed_ie_prefix[3] = std::byte{0xDD};
  const auto alternate_ie = omega::retail::DecodeIeFrontend(changed_ie_prefix);
  Check(ie && alternate_ie && *ie == *alternate_ie,
        "IE keeps the skipped prefix and observed word outside canonical "
        "semantics");
  auto changed_animation = ie_fixture.bytes;
  changed_animation[ie_fixture.animation_payload_offset] = std::byte{0x7F};
  const auto opaque_ie = omega::retail::DecodeIeFrontend(changed_animation);
  Check(ie && opaque_ie && *ie == *opaque_ie,
        "IE validates but does not expose unproven animation payload values");
  auto ie_owned_bytes = ie_fixture.bytes;
  auto owned_ie = omega::retail::DecodeIeFrontend(ie_owned_bytes);
  std::fill(ie_owned_bytes.begin(), ie_owned_bytes.end(), std::byte{0xFF});
  Check(owned_ie && owned_ie->root.children.size() == 1 &&
            owned_ie->root.children[0].texture_member == "panel_skin.TDX" &&
            owned_ie->root.children[0].positions.size() == 2 &&
            owned_ie->root.children[0].colors.size() == 1 &&
            owned_ie->root.children[0].triangles.size() == 1 &&
            owned_ie->root.children[0].positions[1].z == 6.0F &&
            owned_ie->root.children[0].colors[0].green == 127 &&
            owned_ie->root.children[0].triangles[0].position_indices[1] == 1,
        "IE render streams remain owned after source storage replacement");

  auto raised_limits = omega::asset::DecodeLimits{};
  raised_limits.maximum_items = std::numeric_limits<std::uint64_t>::max();
  raised_limits.maximum_output_bytes =
      std::numeric_limits<std::uint64_t>::max();
  raised_limits.maximum_nesting_depth =
      std::numeric_limits<std::uint32_t>::max();

  auto bad_ie = ie_fixture.bytes;
  WriteF32(bad_ie, ie_fixture.root_transform_offset,
           std::numeric_limits<float>::quiet_NaN());
  CheckError(omega::retail::DecodeIeFrontend(bad_ie),
             omega::asset::DecodeErrorCode::Malformed,
             "IE rejects a nonfinite affine-transform coefficient");
  bad_ie = ie_fixture.bytes;
  WriteF32(bad_ie, ie_fixture.position_data_offset,
           std::numeric_limits<float>::infinity());
  CheckError(omega::retail::DecodeIeFrontend(bad_ie),
             omega::asset::DecodeErrorCode::Malformed,
             "IE rejects a nonfinite position component");
  bad_ie = ie_fixture.bytes;
  WriteF32(bad_ie, ie_fixture.uv_data_offset,
           std::numeric_limits<float>::quiet_NaN());
  CheckError(omega::retail::DecodeIeFrontend(bad_ie),
             omega::asset::DecodeErrorCode::Malformed,
             "IE rejects a nonfinite UV component");
  bad_ie = ie_fixture.bytes;
  WriteF32(bad_ie, ie_fixture.color_data_offset,
           std::numeric_limits<float>::infinity());
  CheckError(omega::retail::DecodeIeFrontend(bad_ie),
             omega::asset::DecodeErrorCode::Malformed,
             "IE rejects a nonfinite normalized color channel");
  bad_ie = ie_fixture.bytes;
  WriteF32(bad_ie, ie_fixture.color_data_offset, 1.01F);
  CheckError(omega::retail::DecodeIeFrontend(bad_ie),
             omega::asset::DecodeErrorCode::Malformed,
             "IE rejects a normalized color channel above one");
  bad_ie = ie_fixture.bytes;
  WriteF32(bad_ie, ie_fixture.color_data_offset, -0.01F);
  CheckError(omega::retail::DecodeIeFrontend(bad_ie),
             omega::asset::DecodeErrorCode::Malformed,
             "IE rejects a normalized color channel below zero");
  bad_ie = ie_fixture.bytes;
  WriteU32(bad_ie, ie_fixture.position_count_offset,
           std::numeric_limits<std::uint32_t>::max());
  CheckError(omega::retail::DecodeIeFrontend(bad_ie, raised_limits),
             omega::asset::DecodeErrorCode::LimitExceeded,
             "IE bounds a hostile retained-stream count before allocation");
  bad_ie = ie_fixture.bytes;
  WriteU32(bad_ie, ie_fixture.triangle_data_offset, 0x00010000U);
  CheckError(omega::retail::DecodeIeFrontend(bad_ie),
             omega::asset::DecodeErrorCode::Malformed,
             "IE rejects triangle indices with nonzero upper 16 bits");
  bad_ie = ie_fixture.bytes;
  WriteU32(bad_ie, ie_fixture.triangle_data_offset, 2);
  CheckError(omega::retail::DecodeIeFrontend(bad_ie),
             omega::asset::DecodeErrorCode::Malformed,
             "IE rejects an out-of-bounds position index");
  bad_ie = ie_fixture.bytes;
  WriteU32(bad_ie, ie_fixture.triangle_data_offset + 3U * sizeof(std::uint32_t),
           1);
  CheckError(omega::retail::DecodeIeFrontend(bad_ie),
             omega::asset::DecodeErrorCode::Malformed,
             "IE rejects an out-of-bounds UV index");
  bad_ie = ie_fixture.bytes;
  WriteU32(bad_ie, ie_fixture.triangle_data_offset + 6U * sizeof(std::uint32_t),
           1);
  CheckError(omega::retail::DecodeIeFrontend(bad_ie),
             omega::asset::DecodeErrorCode::Malformed,
             "IE rejects an out-of-bounds color index");
  bad_ie = ie_fixture.bytes;
  bad_ie[ie_fixture.vertex_kind_offset] = std::byte{'X'};
  CheckError(omega::retail::DecodeIeFrontend(bad_ie),
             omega::asset::DecodeErrorCode::UnsupportedVariant,
             "IE rejects an unsupported animation-track family");
  bad_ie = ie_fixture.bytes;
  WriteU32(bad_ie, ie_fixture.vertex_count_offset, 1);
  CheckError(
      omega::retail::DecodeIeFrontend(bad_ie),
      omega::asset::DecodeErrorCode::Malformed,
      "IE rejects a vertex-track count that contradicts its fixed stream");
  bad_ie = ie_fixture.bytes;
  WriteU32(bad_ie, ie_fixture.secondary_count_offset, 1);
  CheckError(omega::retail::DecodeIeFrontend(bad_ie),
             omega::asset::DecodeErrorCode::UnsupportedVariant,
             "IE fails closed on the unproven secondary-entry family");
  bad_ie = ie_fixture.bytes;
  WriteU32(bad_ie, ie_fixture.root_child_count_offset,
           std::numeric_limits<std::uint32_t>::max());
  CheckError(omega::retail::DecodeIeFrontend(bad_ie, raised_limits),
             omega::asset::DecodeErrorCode::LimitExceeded,
             "IE caller limits cannot raise the fixed decoded-item ceiling");
  bad_ie = ie_fixture.bytes;
  const std::uint64_t output_child_count =
      omega::retail::kFrontendMaximumLogicalOutputBytes /
          sizeof(omega::asset::FrontendVisualNodeIR) +
      1U;
  Check(output_child_count + 16U <
                omega::retail::kFrontendMaximumDecodedItems &&
            output_child_count <= std::numeric_limits<std::uint32_t>::max(),
        "generated IE count isolates the fixed output ceiling from the item "
        "ceiling");
  WriteU32(bad_ie, ie_fixture.root_child_count_offset,
           static_cast<std::uint32_t>(output_child_count));
  CheckError(omega::retail::DecodeIeFrontend(bad_ie, raised_limits),
             omega::asset::DecodeErrorCode::LimitExceeded,
             "IE caller limits cannot raise the fixed logical-output ceiling");
  bad_ie = ie_fixture.bytes;
  bad_ie.back() = std::byte{1};
  CheckError(omega::retail::DecodeIeFrontend(bad_ie),
             omega::asset::DecodeErrorCode::Malformed,
             "IE rejects nonzero terminal alignment bytes");
  bad_ie = ie_fixture.bytes;
  bad_ie.pop_back();
  CheckError(omega::retail::DecodeIeFrontend(bad_ie),
             omega::asset::DecodeErrorCode::Truncated,
             "IE rejects a missing terminal alignment byte");

  if (ie_measured) {
    auto limits = omega::asset::DecodeLimits{};
    limits.maximum_input_bytes = ie_fixture.bytes.size();
    limits.maximum_items = ie_measured->decoded_items;
    limits.maximum_output_bytes = ie_measured->logical_output_bytes;
    limits.maximum_scratch_bytes = 0;
    Check(omega::retail::DecodeIeFrontend(ie_fixture.bytes, limits).has_value(),
          "IE accepts exact input, item, output, and zero-scratch budgets");
    --limits.maximum_items;
    CheckError(omega::retail::DecodeIeFrontend(ie_fixture.bytes, limits),
               omega::asset::DecodeErrorCode::LimitExceeded,
               "IE rejects one item below its exact decoded-item budget");
    limits.maximum_items = ie_measured->decoded_items;
    --limits.maximum_output_bytes;
    CheckError(omega::retail::DecodeIeFrontend(ie_fixture.bytes, limits),
               omega::asset::DecodeErrorCode::LimitExceeded,
               "IE rejects one byte below its exact logical-output budget");
  }
  auto ie_limits = omega::asset::DecodeLimits{};
  ie_limits.maximum_nesting_depth = 0;
  CheckError(omega::retail::DecodeIeFrontend(ie_fixture.bytes, ie_limits),
             omega::asset::DecodeErrorCode::LimitExceeded,
             "IE enforces the caller's root-at-zero nesting-depth limit");

  std::vector<std::byte> oversized(
      static_cast<std::size_t>(omega::retail::kGuiMaximumInputBytes) + 1U,
      std::byte{0});
  auto permissive_limits = omega::asset::DecodeLimits{};
  permissive_limits.maximum_input_bytes =
      std::numeric_limits<std::uint64_t>::max();
  CheckError(omega::retail::DecodeGuiFrontend(oversized, permissive_limits),
             omega::asset::DecodeErrorCode::LimitExceeded,
             "GUI caller limits cannot raise the fixed decoder byte ceiling");
  CheckError(omega::retail::DecodeIeFrontend(oversized, permissive_limits),
             omega::asset::DecodeErrorCode::LimitExceeded,
             "IE caller limits cannot raise the fixed decoder byte ceiling");

  if (failures != 0)
    std::cerr << failures << " frontend document decoder test(s) failed\n";
  return failures == 0 ? 0 : 1;
}
