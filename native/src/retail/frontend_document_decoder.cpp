#include "omega/retail/frontend_document_decoder.h"

#include "omega/retail/gui_envelope_descriptor.h"
#include "omega/retail/ie_envelope_descriptor.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace omega::retail {
namespace {
constexpr std::uint64_t kMemberAlignment = 16;
constexpr std::string_view kGuiWidget = "GuiWidget";
constexpr std::string_view kGuiTextWidget = "GuiTextWidget";
constexpr std::string_view kGuiButtonWidget = "GuiButtonWidget";
constexpr std::string_view kGuiCharacterDisplay = "GuiCharacterDisplay";
constexpr std::string_view kGuiInterfaceDecorator = "GuiInterfaceDecorator";
constexpr std::string_view kVertexTrack = "VERTEX";
constexpr std::string_view kOpacityTrack = "OPACITY";
constexpr std::string_view kUvOffsetUTrack = "UVOFF_U";
constexpr std::string_view kUvOffsetVTrack = "UVOFF_V";

[[nodiscard]] asset::DecodeError
Error(const asset::DecodeErrorCode code, std::string message,
      const std::optional<std::uint64_t> byte_offset = std::nullopt) {
  return asset::DecodeError{
      .code = code,
      .byte_offset = byte_offset,
      .message = std::move(message),
  };
}

[[nodiscard]] bool Add(const std::uint64_t left, const std::uint64_t right,
                       std::uint64_t &result) noexcept {
  if (right > std::numeric_limits<std::uint64_t>::max() - left)
    return false;
  result = left + right;
  return true;
}

[[nodiscard]] bool Multiply(const std::uint64_t left, const std::uint64_t right,
                            std::uint64_t &result) noexcept {
  if (left != 0 && right > std::numeric_limits<std::uint64_t>::max() / left)
    return false;
  result = left * right;
  return true;
}

class Reader {
public:
  Reader(const std::span<const std::byte> bytes,
         const asset::DecodeLimits limits, const std::size_t offset) noexcept
      : bytes_(bytes), limits_(limits), offset_(offset) {
    limits_.maximum_output_bytes = std::min(limits_.maximum_output_bytes,
                                            kFrontendMaximumLogicalOutputBytes);
    limits_.maximum_items =
        std::min(limits_.maximum_items, kFrontendMaximumDecodedItems);
    limits_.maximum_string_bytes =
        std::min(limits_.maximum_string_bytes, kFrontendMaximumStringBytes);
    limits_.maximum_nesting_depth =
        std::min(limits_.maximum_nesting_depth, kFrontendMaximumNestingDepth);
  }

  [[nodiscard]] std::size_t offset() const noexcept { return offset_; }
  [[nodiscard]] std::uint64_t items() const noexcept { return items_; }
  [[nodiscard]] std::uint64_t output_bytes() const noexcept {
    return output_bytes_;
  }
  [[nodiscard]] const asset::DecodeLimits &limits() const noexcept {
    return limits_;
  }

  [[nodiscard]] asset::DecodeResult<void>
  ConsumeItems(const std::uint64_t amount, const std::uint64_t byte_offset) {
    std::uint64_t next = 0;
    if (!Add(items_, amount, next))
      return std::unexpected(Error(asset::DecodeErrorCode::Overflow,
                                   "frontend decoded-item count overflows",
                                   byte_offset));
    if (next > limits_.maximum_items)
      return std::unexpected(
          Error(asset::DecodeErrorCode::LimitExceeded,
                "frontend decoded items exceed the caller limit", byte_offset));
    items_ = next;
    return {};
  }

  [[nodiscard]] asset::DecodeResult<void>
  CheckItems(const std::uint64_t amount,
             const std::uint64_t byte_offset) const {
    std::uint64_t next = 0;
    if (!Add(items_, amount, next))
      return std::unexpected(Error(asset::DecodeErrorCode::Overflow,
                                   "frontend decoded-item count overflows",
                                   byte_offset));
    if (next > limits_.maximum_items)
      return std::unexpected(
          Error(asset::DecodeErrorCode::LimitExceeded,
                "frontend decoded items exceed the caller limit", byte_offset));
    return {};
  }

  [[nodiscard]] asset::DecodeResult<void>
  ConsumeOutput(const std::uint64_t amount, const std::uint64_t byte_offset) {
    std::uint64_t next = 0;
    if (!Add(output_bytes_, amount, next))
      return std::unexpected(Error(asset::DecodeErrorCode::Overflow,
                                   "frontend logical output size overflows",
                                   byte_offset));
    if (next > limits_.maximum_output_bytes)
      return std::unexpected(Error(
          asset::DecodeErrorCode::LimitExceeded,
          "frontend logical output exceeds the caller limit", byte_offset));
    output_bytes_ = next;
    return {};
  }

  [[nodiscard]] asset::DecodeResult<void>
  CheckOutput(const std::uint64_t amount,
              const std::uint64_t byte_offset) const {
    std::uint64_t next = 0;
    if (!Add(output_bytes_, amount, next))
      return std::unexpected(Error(asset::DecodeErrorCode::Overflow,
                                   "frontend logical output size overflows",
                                   byte_offset));
    if (next > limits_.maximum_output_bytes)
      return std::unexpected(Error(
          asset::DecodeErrorCode::LimitExceeded,
          "frontend logical output exceeds the caller limit", byte_offset));
    return {};
  }

  [[nodiscard]] asset::DecodeResult<std::string_view> ReadStringView() {
    const std::size_t start = offset_;
    std::uint64_t length = 0;
    while (offset_ < bytes_.size() && bytes_[offset_] != std::byte{0}) {
      if (length >= limits_.maximum_string_bytes)
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
                                     "frontend string exceeds the caller limit",
                                     offset_));
      ++offset_;
      ++length;
    }
    if (offset_ == bytes_.size())
      return std::unexpected(Error(asset::DecodeErrorCode::Truncated,
                                   "frontend string is missing its terminator",
                                   bytes_.size()));
    ++offset_;
    return std::string_view(
        reinterpret_cast<const char *>(bytes_.data() + start),
        static_cast<std::size_t>(length));
  }

  [[nodiscard]] asset::DecodeResult<std::string> ReadOwnedString() {
    const std::size_t start = offset_;
    auto view = ReadStringView();
    if (!view)
      return std::unexpected(view.error());
    auto output = ConsumeOutput(view->size(), start);
    if (!output)
      return std::unexpected(output.error());
    return std::string(*view);
  }

  [[nodiscard]] asset::DecodeResult<void> Align4() {
    const std::size_t aligned = (offset_ + 3U) & ~std::size_t{3U};
    if (aligned > bytes_.size())
      return std::unexpected(Error(asset::DecodeErrorCode::Truncated,
                                   "frontend alignment reaches past the input",
                                   bytes_.size()));
    offset_ = aligned;
    return {};
  }

  [[nodiscard]] asset::DecodeResult<std::uint16_t> ReadU16() {
    if (bytes_.size() - offset_ < 2U)
      return std::unexpected(Error(asset::DecodeErrorCode::Truncated,
                                   "frontend 16-bit field is truncated",
                                   bytes_.size()));
    const std::uint16_t value =
        static_cast<std::uint16_t>(
            std::to_integer<std::uint8_t>(bytes_[offset_])) |
        static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(
                std::to_integer<std::uint8_t>(bytes_[offset_ + 1U]))
            << 8U);
    offset_ += 2U;
    return value;
  }

  [[nodiscard]] asset::DecodeResult<std::uint32_t> ReadU32() {
    if (bytes_.size() - offset_ < 4U)
      return std::unexpected(Error(asset::DecodeErrorCode::Truncated,
                                   "frontend 32-bit field is truncated",
                                   bytes_.size()));
    std::uint32_t value = 0;
    for (unsigned shift = 0; shift < 32; shift += 8) {
      value |= static_cast<std::uint32_t>(
                   std::to_integer<std::uint8_t>(bytes_[offset_ + shift / 8U]))
               << shift;
    }
    offset_ += 4U;
    return value;
  }

  [[nodiscard]] asset::DecodeResult<float> ReadF32() {
    auto word = ReadU32();
    if (!word)
      return std::unexpected(word.error());
    return std::bit_cast<float>(*word);
  }

  [[nodiscard]] asset::DecodeResult<void> Skip(const std::uint64_t amount) {
    auto available = CheckAvailable(amount, offset_);
    if (!available)
      return available;
    offset_ += static_cast<std::size_t>(amount);
    return {};
  }

  [[nodiscard]] asset::DecodeResult<void>
  CheckAvailable(const std::uint64_t amount,
                 const std::uint64_t byte_offset) const {
    if (amount > static_cast<std::uint64_t>(bytes_.size() - offset_))
      return std::unexpected(
          Error(asset::DecodeErrorCode::Truncated,
                "frontend counted field reaches past the input", byte_offset));
    return {};
  }

  [[nodiscard]] asset::DecodeResult<void>
  SkipItems(const std::uint64_t count, const std::uint64_t stride,
            const std::uint64_t count_offset) {
    auto item_result = ConsumeItems(count, count_offset);
    if (!item_result)
      return item_result;
    std::uint64_t bytes = 0;
    if (!Multiply(count, stride, bytes))
      return std::unexpected(Error(asset::DecodeErrorCode::Overflow,
                                   "frontend counted-field size overflows",
                                   count_offset));
    return Skip(bytes);
  }

  [[nodiscard]] asset::DecodeResult<std::uint8_t>
  ValidateTrailingAlignment() const {
    const std::uint64_t expected =
        (kMemberAlignment - (offset_ % kMemberAlignment)) % kMemberAlignment;
    const std::uint64_t remaining = bytes_.size() - offset_;
    if (remaining < expected)
      return std::unexpected(
          Error(asset::DecodeErrorCode::Truncated,
                "frontend member is missing terminal alignment bytes",
                bytes_.size()));
    if (remaining > expected)
      return std::unexpected(
          Error(asset::DecodeErrorCode::Malformed,
                "frontend member has bytes beyond its terminal alignment",
                offset_ + expected));
    for (std::size_t index = offset_; index < bytes_.size(); ++index) {
      if (bytes_[index] != std::byte{0})
        return std::unexpected(Error(
            asset::DecodeErrorCode::Malformed,
            "frontend terminal alignment contains a nonzero byte", index));
    }
    return static_cast<std::uint8_t>(remaining);
  }

private:
  std::span<const std::byte> bytes_;
  asset::DecodeLimits limits_;
  std::size_t offset_ = 0;
  std::uint64_t items_ = 0;
  std::uint64_t output_bytes_ = 0;
};

template <typename Value>
[[nodiscard]] asset::DecodeResult<void>
CheckVectorCapacity(Reader &reader, const std::uint64_t count,
                    const std::uint64_t count_offset) {
  auto items = reader.CheckItems(count, count_offset);
  if (!items)
    return items;
  std::uint64_t bytes = 0;
  if (!Multiply(count, sizeof(Value), bytes))
    return std::unexpected(Error(asset::DecodeErrorCode::Overflow,
                                 "frontend vector storage size overflows",
                                 count_offset));
  return reader.CheckOutput(bytes, count_offset);
}

template <typename Value>
[[nodiscard]] asset::DecodeResult<void>
PrepareOwnedVector(Reader &reader, const std::uint64_t count,
                   const std::uint64_t source_stride,
                   const std::uint64_t count_offset) {
  auto items = reader.ConsumeItems(count, count_offset);
  if (!items)
    return items;
  std::uint64_t source_bytes = 0;
  if (!Multiply(count, source_stride, source_bytes))
    return std::unexpected(Error(asset::DecodeErrorCode::Overflow,
                                 "frontend source-vector size overflows",
                                 count_offset));
  auto available = reader.CheckAvailable(source_bytes, count_offset);
  if (!available)
    return available;
  std::uint64_t output_bytes = 0;
  if (!Multiply(count, sizeof(Value), output_bytes))
    return std::unexpected(Error(asset::DecodeErrorCode::Overflow,
                                 "frontend owned-vector size overflows",
                                 count_offset));
  return reader.ConsumeOutput(output_bytes, count_offset);
}

[[nodiscard]] asset::DecodeResult<float> ReadFiniteF32(Reader &reader,
                                                       const char *field) {
  const std::uint64_t value_offset = reader.offset();
  auto value = reader.ReadF32();
  if (!value)
    return std::unexpected(value.error());
  if (!std::isfinite(*value))
    return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
                                 std::string(field) + " is not finite",
                                 value_offset));
  return *value;
}

[[nodiscard]] asset::DecodeResult<std::uint8_t>
QuantizeColorChannel(const float value, const std::uint64_t value_offset) {
  if (!std::isfinite(value))
    return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
                                 "IE color channel is not finite",
                                 value_offset));
  if (value < 0.0F || value > 1.0F)
    return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
                                 "IE color channel is outside [0,1]",
                                 value_offset));

  // The retail parser multiplies a normalized channel by 255 and uses the
  // R5900 CVT.W.S conversion, whose fixed rule is rounding toward zero. A
  // double holds the exact product of a binary32 value and 255, avoiding any
  // dependence on the host floating-point rounding mode.
  const double scaled = static_cast<double>(value) * 255.0;
  return static_cast<std::uint8_t>(static_cast<std::uint32_t>(scaled));
}

[[nodiscard]] asset::DecodeResult<asset::FrontendWidgetIR>
ParseGuiNode(Reader &reader, const std::uint64_t depth,
             const bool embedded_root) {
  const std::uint64_t node_offset = reader.offset();
  if (depth > reader.limits().maximum_nesting_depth)
    return std::unexpected(Error(
        asset::DecodeErrorCode::LimitExceeded,
        "GUI hierarchy exceeds the caller nesting-depth limit", node_offset));
  auto item_result = reader.ConsumeItems(1, node_offset);
  if (!item_result)
    return std::unexpected(item_result.error());
  if (!embedded_root) {
    auto output =
        reader.ConsumeOutput(sizeof(asset::FrontendWidgetIR), node_offset);
    if (!output)
      return std::unexpected(output.error());
  }

  const std::uint64_t factory_offset = reader.offset();
  auto factory = reader.ReadStringView();
  if (!factory)
    return std::unexpected(factory.error());
  const std::uint64_t decorator_offset = reader.offset();
  auto decorator = reader.ReadStringView();
  if (!decorator)
    return std::unexpected(decorator.error());

  asset::FrontendWidgetIR node;
  bool has_text_record = false;
  if (*factory == kGuiWidget)
    node.kind = asset::FrontendWidgetKind::Container;
  else if (*factory == kGuiTextWidget) {
    node.kind = asset::FrontendWidgetKind::Text;
    has_text_record = true;
  } else if (*factory == kGuiButtonWidget) {
    node.kind = asset::FrontendWidgetKind::Button;
    has_text_record = true;
  } else if (*factory == kGuiCharacterDisplay)
    node.kind = asset::FrontendWidgetKind::CharacterDisplay;
  else
    return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
                                 "GUI node uses an unsupported factory",
                                 factory_offset));

  auto identifier = reader.ReadOwnedString();
  if (!identifier)
    return std::unexpected(identifier.error());
  node.identifier = std::move(*identifier);
  auto aligned = reader.Align4();
  if (!aligned)
    return std::unexpected(aligned.error());
  for (float &value : node.layout_values) {
    auto decoded = reader.ReadF32();
    if (!decoded)
      return std::unexpected(decoded.error());
    value = *decoded;
  }
  for (unsigned index = 0; index < 2; ++index) {
    auto ignored = reader.ReadU16();
    if (!ignored)
      return std::unexpected(ignored.error());
  }

  if (has_text_record) {
    auto text = reader.ReadOwnedString();
    if (!text)
      return std::unexpected(text.error());
    node.text_reference.emplace(std::move(*text));
    auto font = reader.ReadOwnedString();
    if (!font)
      return std::unexpected(font.error());
    node.font_reference.emplace(std::move(*font));
    aligned = reader.Align4();
    if (!aligned)
      return std::unexpected(aligned.error());
    auto ignored_style = reader.Skip(16);
    if (!ignored_style)
      return std::unexpected(ignored_style.error());
  }

  if (!decorator->empty()) {
    if (*decorator != kGuiInterfaceDecorator)
      return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
                                   "GUI node uses an unsupported decorator",
                                   decorator_offset));
    item_result = reader.ConsumeItems(1, decorator_offset);
    if (!item_result)
      return std::unexpected(item_result.error());
    node.binding.emplace();
    auto scope = reader.ReadOwnedString();
    if (!scope)
      return std::unexpected(scope.error());
    node.binding->scope_reference = std::move(*scope);
    auto resource = reader.ReadOwnedString();
    if (!resource)
      return std::unexpected(resource.error());
    node.binding->resource_reference = std::move(*resource);
    aligned = reader.Align4();
    if (!aligned)
      return std::unexpected(aligned.error());
    for (float &value : node.binding->transform_values) {
      auto decoded = reader.ReadF32();
      if (!decoded)
        return std::unexpected(decoded.error());
      value = *decoded;
    }

    const std::uint64_t action_count_offset = reader.offset();
    auto action_count = reader.ReadU16();
    if (!action_count)
      return std::unexpected(action_count.error());
    item_result = reader.ConsumeItems(*action_count, action_count_offset);
    if (!item_result)
      return std::unexpected(item_result.error());
    std::uint64_t action_storage = 0;
    if (!Multiply(*action_count, sizeof(std::string), action_storage))
      return std::unexpected(Error(
          asset::DecodeErrorCode::Overflow,
          "GUI action-reference storage size overflows", action_count_offset));
    auto output = reader.ConsumeOutput(action_storage, action_count_offset);
    if (!output)
      return std::unexpected(output.error());
    node.binding->action_references.reserve(*action_count);
    for (std::uint16_t index = 0; index < *action_count; ++index) {
      auto action = reader.ReadOwnedString();
      if (!action)
        return std::unexpected(action.error());
      node.binding->action_references.push_back(std::move(*action));
      aligned = reader.Align4();
      if (!aligned)
        return std::unexpected(aligned.error());
      auto ignored_words = reader.Skip(6);
      if (!ignored_words)
        return std::unexpected(ignored_words.error());
    }
  }

  const std::uint64_t child_count_offset = reader.offset();
  auto child_count = reader.ReadU16();
  if (!child_count)
    return std::unexpected(child_count.error());
  auto capacity = CheckVectorCapacity<asset::FrontendWidgetIR>(
      reader, *child_count, child_count_offset);
  if (!capacity)
    return std::unexpected(capacity.error());
  node.children.reserve(*child_count);
  for (std::uint16_t index = 0; index < *child_count; ++index) {
    auto child = ParseGuiNode(reader, depth + 1U, false);
    if (!child)
      return std::unexpected(child.error());
    node.children.push_back(std::move(*child));
  }
  return node;
}

[[nodiscard]] asset::DecodeResult<asset::FrontendVisualNodeIR>
ParseIeNode(Reader &reader, const std::uint64_t depth,
            const bool embedded_root) {
  const std::uint64_t node_offset = reader.offset();
  if (depth > reader.limits().maximum_nesting_depth)
    return std::unexpected(Error(
        asset::DecodeErrorCode::LimitExceeded,
        "IE hierarchy exceeds the caller nesting-depth limit", node_offset));
  auto item_result = reader.ConsumeItems(1, node_offset);
  if (!item_result)
    return std::unexpected(item_result.error());
  if (!embedded_root) {
    auto output =
        reader.ConsumeOutput(sizeof(asset::FrontendVisualNodeIR), node_offset);
    if (!output)
      return std::unexpected(output.error());
  }

  asset::FrontendVisualNodeIR node;
  auto identifier = reader.ReadOwnedString();
  if (!identifier)
    return std::unexpected(identifier.error());
  node.identifier = std::move(*identifier);
  const std::uint64_t texture_offset = reader.offset();
  auto texture_basename = reader.ReadStringView();
  if (!texture_basename)
    return std::unexpected(texture_basename.error());
  if (!texture_basename->empty()) {
    std::uint64_t member_bytes = 0;
    if (!Add(texture_basename->size(), 4U, member_bytes))
      return std::unexpected(Error(asset::DecodeErrorCode::Overflow,
                                   "IE texture-member size overflows",
                                   texture_offset));
    auto output = reader.ConsumeOutput(member_bytes, texture_offset);
    if (!output)
      return std::unexpected(output.error());
    node.texture_member.emplace(*texture_basename);
    node.texture_member->append(".TDX");
  }

  auto aligned = reader.Align4();
  if (!aligned)
    return std::unexpected(aligned.error());
  for (float &value : node.transform_values) {
    auto decoded = ReadFiniteF32(reader, "IE affine transform coefficient");
    if (!decoded)
      return std::unexpected(decoded.error());
    value = *decoded;
  }

  const std::uint64_t position_count_offset = reader.offset();
  auto position_count = reader.ReadU32();
  if (!position_count)
    return std::unexpected(position_count.error());
  auto prepared = PrepareOwnedVector<asset::Float3IR>(
      reader, *position_count, 12, position_count_offset);
  if (!prepared)
    return std::unexpected(prepared.error());
  node.positions.reserve(*position_count);
  for (std::uint32_t index = 0; index < *position_count; ++index) {
    asset::Float3IR position;
    auto x = ReadFiniteF32(reader, "IE position component");
    if (!x)
      return std::unexpected(x.error());
    auto y = ReadFiniteF32(reader, "IE position component");
    if (!y)
      return std::unexpected(y.error());
    auto z = ReadFiniteF32(reader, "IE position component");
    if (!z)
      return std::unexpected(z.error());
    position.x = *x;
    position.y = *y;
    position.z = *z;
    node.positions.push_back(position);
  }

  const std::uint64_t uv_count_offset = reader.offset();
  auto uv_count = reader.ReadU32();
  if (!uv_count)
    return std::unexpected(uv_count.error());
  prepared = PrepareOwnedVector<asset::FrontendUvIR>(reader, *uv_count, 8,
                                                     uv_count_offset);
  if (!prepared)
    return std::unexpected(prepared.error());
  node.uvs.reserve(*uv_count);
  for (std::uint32_t index = 0; index < *uv_count; ++index) {
    asset::FrontendUvIR uv;
    auto u = ReadFiniteF32(reader, "IE UV component");
    if (!u)
      return std::unexpected(u.error());
    auto v = ReadFiniteF32(reader, "IE UV component");
    if (!v)
      return std::unexpected(v.error());
    uv.u = *u;
    uv.v = *v;
    node.uvs.push_back(uv);
  }

  const std::uint64_t color_count_offset = reader.offset();
  auto color_count = reader.ReadU32();
  if (!color_count)
    return std::unexpected(color_count.error());
  prepared = PrepareOwnedVector<asset::FrontendColorRgba8IR>(
      reader, *color_count, 16, color_count_offset);
  if (!prepared)
    return std::unexpected(prepared.error());
  node.colors.reserve(*color_count);
  for (std::uint32_t index = 0; index < *color_count; ++index) {
    std::array<std::uint8_t, 4> channels{};
    for (std::uint8_t &channel : channels) {
      const std::uint64_t channel_offset = reader.offset();
      auto value = reader.ReadF32();
      if (!value)
        return std::unexpected(value.error());
      auto quantized = QuantizeColorChannel(*value, channel_offset);
      if (!quantized)
        return std::unexpected(quantized.error());
      channel = *quantized;
    }
    node.colors.push_back(asset::FrontendColorRgba8IR{
        .red = channels[0],
        .green = channels[1],
        .blue = channels[2],
        .alpha = channels[3],
    });
  }

  const std::uint64_t triangle_count_offset = reader.offset();
  auto triangle_count = reader.ReadU32();
  if (!triangle_count)
    return std::unexpected(triangle_count.error());
  prepared = PrepareOwnedVector<asset::FrontendTriangleIR>(
      reader, *triangle_count, 36, triangle_count_offset);
  if (!prepared)
    return std::unexpected(prepared.error());
  node.triangles.reserve(*triangle_count);
  for (std::uint32_t triangle_index = 0; triangle_index < *triangle_count;
       ++triangle_index) {
    std::array<std::uint32_t, 9> source_indices{};
    std::array<std::uint64_t, 9> source_offsets{};
    for (std::size_t index = 0; index < source_indices.size(); ++index) {
      source_offsets[index] = reader.offset();
      auto source_index = reader.ReadU32();
      if (!source_index)
        return std::unexpected(source_index.error());
      source_indices[index] = *source_index;
      if (*source_index > std::numeric_limits<std::uint16_t>::max())
        return std::unexpected(Error(
            asset::DecodeErrorCode::Malformed,
            "IE triangle index cannot be represented as an unsigned 16-bit "
            "value",
            source_offsets[index]));
    }

    asset::FrontendTriangleIR triangle;
    for (std::size_t corner = 0; corner < 3; ++corner) {
      if (source_indices[corner] >= *position_count)
        return std::unexpected(
            Error(asset::DecodeErrorCode::Malformed,
                  "IE triangle position index is out of bounds",
                  source_offsets[corner]));
      if (source_indices[corner + 3U] >= *uv_count)
        return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
                                     "IE triangle UV index is out of bounds",
                                     source_offsets[corner + 3U]));
      if (source_indices[corner + 6U] >= *color_count)
        return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
                                     "IE triangle color index is out of bounds",
                                     source_offsets[corner + 6U]));
      triangle.position_indices[corner] =
          static_cast<std::uint16_t>(source_indices[corner]);
      triangle.uv_indices[corner] =
          static_cast<std::uint16_t>(source_indices[corner + 3U]);
      triangle.color_indices[corner] =
          static_cast<std::uint16_t>(source_indices[corner + 6U]);
    }
    node.triangles.push_back(triangle);
  }

  const std::uint64_t track_count_offset = reader.offset();
  auto track_count = reader.ReadU32();
  if (!track_count)
    return std::unexpected(track_count.error());
  item_result = reader.ConsumeItems(*track_count, track_count_offset);
  if (!item_result)
    return std::unexpected(item_result.error());
  for (std::uint32_t track_index = 0; track_index < *track_count;
       ++track_index) {
    const std::uint64_t kind_offset = reader.offset();
    auto kind = reader.ReadStringView();
    if (!kind)
      return std::unexpected(kind.error());
    aligned = reader.Align4();
    if (!aligned)
      return std::unexpected(aligned.error());
    const std::uint64_t entry_count_offset = reader.offset();
    auto entry_count = reader.ReadU32();
    if (!entry_count)
      return std::unexpected(entry_count.error());
    if (*kind == kVertexTrack) {
      if (*entry_count != *position_count)
        return std::unexpected(
            Error(asset::DecodeErrorCode::Malformed,
                  "IE vertex-track target count contradicts its fixed stream",
                  entry_count_offset));
      item_result = reader.ConsumeItems(*entry_count, entry_count_offset);
      if (!item_result)
        return std::unexpected(item_result.error());
      for (std::uint32_t entry_index = 0; entry_index < *entry_count;
           ++entry_index) {
        const std::uint64_t key_count_offset = reader.offset();
        auto key_count = reader.ReadU32();
        if (!key_count)
          return std::unexpected(key_count.error());
        auto skipped = reader.SkipItems(*key_count, 16, key_count_offset);
        if (!skipped)
          return std::unexpected(skipped.error());
      }
    } else if (*kind == kOpacityTrack || *kind == kUvOffsetUTrack ||
               *kind == kUvOffsetVTrack) {
      auto skipped = reader.SkipItems(*entry_count, 8, entry_count_offset);
      if (!skipped)
        return std::unexpected(skipped.error());
    } else
      return std::unexpected(Error(
          asset::DecodeErrorCode::UnsupportedVariant,
          "IE node uses an unsupported animation-track family", kind_offset));
  }

  const std::uint64_t secondary_count_offset = reader.offset();
  auto secondary_count = reader.ReadU32();
  if (!secondary_count)
    return std::unexpected(secondary_count.error());
  if (*secondary_count != 0)
    return std::unexpected(
        Error(asset::DecodeErrorCode::UnsupportedVariant,
              "IE node uses an unsupported secondary-entry family",
              secondary_count_offset));

  const std::uint64_t child_count_offset = reader.offset();
  auto child_count = reader.ReadU32();
  if (!child_count)
    return std::unexpected(child_count.error());
  auto capacity = CheckVectorCapacity<asset::FrontendVisualNodeIR>(
      reader, *child_count, child_count_offset);
  if (!capacity)
    return std::unexpected(capacity.error());
  node.children.reserve(*child_count);
  for (std::uint32_t index = 0; index < *child_count; ++index) {
    auto child = ParseIeNode(reader, depth + 1U, false);
    if (!child)
      return std::unexpected(child.error());
    node.children.push_back(std::move(*child));
  }
  return node;
}

[[nodiscard]] asset::DecodeResult<void>
CheckInput(const std::span<const std::byte> bytes,
           const asset::DecodeLimits limits, const std::uint64_t fixed_limit,
           const char *family) {
  if (bytes.size() > fixed_limit)
    return std::unexpected(Error(
        asset::DecodeErrorCode::LimitExceeded,
        std::string(family) + " input exceeds the fixed decoder byte limit"));
  if (bytes.size() > limits.maximum_input_bytes)
    return std::unexpected(
        Error(asset::DecodeErrorCode::LimitExceeded,
              std::string(family) + " input exceeds the caller byte limit"));
  return {};
}
} // namespace

asset::DecodeResult<DecodedGuiFrontend>
DecodeGuiFrontendMeasured(const std::span<const std::byte> bytes,
                          const asset::DecodeLimits limits) {
  auto input = CheckInput(bytes, limits, kGuiMaximumInputBytes, "GUI");
  if (!input)
    return std::unexpected(input.error());
  if (bytes.size() < kGuiRootBoundaryOffset)
    return std::unexpected(Error(asset::DecodeErrorCode::Truncated,
                                 "GUI fixed prefix is truncated",
                                 bytes.size()));
  for (std::size_t index = 0; index < kGuiObservedTag.size(); ++index) {
    if (std::to_integer<std::uint8_t>(bytes[index]) != kGuiObservedTag[index])
      return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
                                   "GUI tag is outside the supported family",
                                   index));
  }

  Reader reader(bytes, limits,
                static_cast<std::size_t>(kGuiRootBoundaryOffset));
  auto root_item = reader.ConsumeItems(1, 0);
  if (!root_item)
    return std::unexpected(root_item.error());
  auto root_output =
      reader.ConsumeOutput(sizeof(asset::FrontendWidgetDocumentIR), 0);
  if (!root_output)
    return std::unexpected(root_output.error());
  auto root = ParseGuiNode(reader, 0, true);
  if (!root)
    return std::unexpected(root.error());
  auto padding = reader.ValidateTrailingAlignment();
  if (!padding)
    return std::unexpected(padding.error());

  return DecodedGuiFrontend{
      .document = {.root = std::move(*root)},
      .decoded_items = reader.items(),
      .logical_output_bytes = reader.output_bytes(),
      .trailing_zero_bytes = *padding,
  };
}

asset::DecodeResult<asset::FrontendWidgetDocumentIR>
DecodeGuiFrontend(const std::span<const std::byte> bytes,
                  const asset::DecodeLimits limits) {
  auto decoded = DecodeGuiFrontendMeasured(bytes, limits);
  if (!decoded)
    return std::unexpected(decoded.error());
  return std::move(decoded->document);
}

asset::DecodeResult<DecodedIeFrontend>
DecodeIeFrontendMeasured(const std::span<const std::byte> bytes,
                         const asset::DecodeLimits limits) {
  auto input = CheckInput(bytes, limits, kIeMaximumInputBytes, "IE");
  if (!input)
    return std::unexpected(input.error());
  if (bytes.size() < kIeRootBoundaryOffset)
    return std::unexpected(Error(asset::DecodeErrorCode::Truncated,
                                 "IE fixed prefix is truncated", bytes.size()));

  Reader reader(bytes, limits, static_cast<std::size_t>(kIeRootBoundaryOffset));
  auto root_item = reader.ConsumeItems(1, 0);
  if (!root_item)
    return std::unexpected(root_item.error());
  auto root_output =
      reader.ConsumeOutput(sizeof(asset::FrontendVisualDocumentIR), 0);
  if (!root_output)
    return std::unexpected(root_output.error());
  auto root = ParseIeNode(reader, 0, true);
  if (!root)
    return std::unexpected(root.error());
  auto padding = reader.ValidateTrailingAlignment();
  if (!padding)
    return std::unexpected(padding.error());

  return DecodedIeFrontend{
      .document = {.root = std::move(*root)},
      .decoded_items = reader.items(),
      .logical_output_bytes = reader.output_bytes(),
      .trailing_zero_bytes = *padding,
  };
}

asset::DecodeResult<asset::FrontendVisualDocumentIR>
DecodeIeFrontend(const std::span<const std::byte> bytes,
                 const asset::DecodeLimits limits) {
  auto decoded = DecodeIeFrontendMeasured(bytes, limits);
  if (!decoded)
    return std::unexpected(decoded.error());
  return std::move(decoded->document);
}
} // namespace omega::retail
