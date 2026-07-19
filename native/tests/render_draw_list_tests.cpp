#include "omega/runtime/render_draw_list.h"
#include "omega/runtime/render_frame_packet.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <iostream>
#include <limits>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

namespace omega::runtime::detail
{
struct RenderDrawListTestAccess final
{
    static const auto& Storage(const RenderDrawList& list) noexcept
    {
        return list.commands_;
    }
};
} // namespace omega::runtime::detail

namespace
{
using omega::runtime::RenderDrawList;
using omega::runtime::RenderDrawListError;
using omega::runtime::RenderDrawListErrorCode;
using omega::runtime::RenderFramePacket;
using omega::runtime::RenderSourceRectPixels;
using omega::runtime::RenderSourceRectQ16;
using omega::runtime::RenderTargetRectPixels;
using omega::runtime::RenderTargetRectQ16;
using omega::runtime::RenderTextureBlitCommand;
using omega::runtime::RenderTextureBlitPlan;
using omega::runtime::RenderTextureFilterMode;
using omega::runtime::RenderTextureFitMode;
using omega::runtime::RenderTextureHandle;

int failures = 0;

void Check(const bool condition, const std::string_view message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

template <typename Value>
void CheckError(const std::expected<Value, RenderDrawListError>& result,
    const RenderDrawListErrorCode code, const std::string_view message)
{
    Check(!result && result.error().code == code &&
              result.error().message == omega::runtime::RenderDrawListErrorMessage(code),
        message);
}

constexpr RenderTextureHandle Handle(const std::uint32_t index = 0U) noexcept
{
    return RenderTextureHandle{
        .pool_identity = 7U,
        .generation = 11U,
        .slot_index = index,
    };
}

constexpr RenderSourceRectQ16 FullSource() noexcept
{
    return RenderSourceRectQ16{
        .left = 0U,
        .top = 0U,
        .right = omega::runtime::kNormalizedRenderExtent,
        .bottom = omega::runtime::kNormalizedRenderExtent,
    };
}

constexpr RenderTargetRectQ16 FullTarget() noexcept
{
    return RenderTargetRectQ16{
        .left = 0U,
        .top = 0U,
        .right = omega::runtime::kNormalizedRenderExtent,
        .bottom = omega::runtime::kNormalizedRenderExtent,
    };
}

constexpr RenderTextureBlitCommand Command(const std::uint32_t index = 0U,
    const RenderSourceRectQ16 source = FullSource(),
    const RenderTargetRectQ16 destination = FullTarget(),
    const RenderTextureFitMode fit_mode = RenderTextureFitMode::Contain,
    const RenderTextureFilterMode filter_mode = RenderTextureFilterMode::Nearest) noexcept
{
    return RenderTextureBlitCommand{
        .texture = Handle(index),
        .source = source,
        .destination = destination,
        .fit_mode = fit_mode,
        .filter_mode = filter_mode,
    };
}

void CheckContractAndErrors()
{
    static_assert(std::is_trivially_copyable_v<RenderSourceRectQ16>);
    static_assert(std::is_standard_layout_v<RenderSourceRectQ16>);
    static_assert(std::is_trivially_copyable_v<RenderSourceRectPixels>);
    static_assert(std::is_standard_layout_v<RenderSourceRectPixels>);
    static_assert(std::is_trivially_copyable_v<RenderTargetRectQ16>);
    static_assert(std::is_standard_layout_v<RenderTargetRectQ16>);
    static_assert(std::is_trivially_copyable_v<RenderTargetRectPixels>);
    static_assert(std::is_standard_layout_v<RenderTargetRectPixels>);
    static_assert(sizeof(RenderTextureFitMode) == 1U);
    static_assert(sizeof(RenderTextureFilterMode) == 1U);
    static_assert(std::is_trivially_copyable_v<RenderTextureBlitCommand>);
    static_assert(std::is_standard_layout_v<RenderTextureBlitCommand>);
    static_assert(std::is_trivially_copyable_v<RenderTextureBlitPlan>);
    static_assert(std::is_standard_layout_v<RenderTextureBlitPlan>);
    static_assert(std::is_trivially_copyable_v<RenderDrawList>);
    static_assert(std::is_standard_layout_v<RenderDrawList>);
    static_assert(std::is_trivially_copyable_v<RenderFramePacket>);
    static_assert(std::is_standard_layout_v<RenderFramePacket>);
    static_assert(std::is_nothrow_copy_constructible_v<RenderDrawList>);
    static_assert(std::is_nothrow_copy_assignable_v<RenderDrawList>);
    static_assert(std::is_same_v<decltype(std::declval<const RenderDrawList&>().commands()),
        std::span<const RenderTextureBlitCommand>>);
    static_assert(noexcept(RenderDrawList::Create(
        std::declval<std::span<const RenderTextureBlitCommand>>())));
    static_assert(noexcept(omega::runtime::MapTextureSourceRect(
        FullSource(), 1U, 1U)));
    static_assert(noexcept(omega::runtime::PlanTextureBlit(
        RenderSourceRectPixels{0U, 0U, 1U, 1U}, FullTarget(),
        RenderTextureFitMode::Contain, 1U, 1U)));

    Check(omega::runtime::kMaximumRenderTextureBlitsPerFrame == 16U &&
              omega::runtime::kNormalizedRenderExtent == 65'536U,
        "draw-list constants are fixed");

    struct ExpectedError
    {
        RenderDrawListErrorCode code;
        std::string_view name;
        std::string_view message;
    };
    constexpr std::array errors{
        ExpectedError{RenderDrawListErrorCode::CapacityExceeded, "capacity-exceeded",
            "render draw list command capacity is exceeded"},
        ExpectedError{RenderDrawListErrorCode::InvalidTextureHandle,
            "invalid-texture-handle", "render draw list texture handle is invalid"},
        ExpectedError{RenderDrawListErrorCode::InvalidSourceRect, "invalid-source-rect",
            "render draw list source rectangle is invalid"},
        ExpectedError{RenderDrawListErrorCode::InvalidTargetRect, "invalid-target-rect",
            "render draw list target rectangle is invalid"},
        ExpectedError{RenderDrawListErrorCode::InvalidFitMode, "invalid-fit-mode",
            "render draw list fit mode is invalid"},
        ExpectedError{RenderDrawListErrorCode::InvalidFilterMode, "invalid-filter-mode",
            "render draw list filter mode is invalid"},
        ExpectedError{RenderDrawListErrorCode::InvalidSourceExtent, "invalid-source-extent",
            "render draw list source extent is invalid"},
        ExpectedError{RenderDrawListErrorCode::InvalidTargetExtent, "invalid-target-extent",
            "render draw list target extent is invalid"},
    };
    for (const ExpectedError& error : errors)
    {
        Check(omega::runtime::RenderDrawListErrorCodeName(error.code) == error.name,
            "every draw-list error has a fixed name");
        Check(omega::runtime::RenderDrawListErrorMessage(error.code) == error.message,
            "every draw-list error has a fixed message");
    }
}

void CheckDefaultCapacityAndOwnership()
{
    const RenderDrawList empty;
    Check(empty.empty() && empty.size() == 0U && empty.commands().empty(),
        "default draw list is empty");
    const auto& empty_storage = omega::runtime::detail::RenderDrawListTestAccess::Storage(empty);
    for (const auto& command : empty_storage)
        Check(command == RenderTextureBlitCommand{}, "default inactive commands are zero values");
    for (const std::byte byte : std::as_bytes(std::span{empty_storage}))
        Check(byte == std::byte{0}, "default inactive command bytes are zero");

    std::array<RenderTextureBlitCommand, 16U> maximum{};
    for (std::uint32_t index = 0U; index < maximum.size(); ++index)
        maximum[index] = Command(index);
    auto accepted = RenderDrawList::Create(maximum);
    Check(accepted && accepted->size() == maximum.size(), "exact capacity is accepted");

    std::array<RenderTextureBlitCommand, 17U> overflow{};
    overflow.fill(Command());
    overflow[0].texture = {};
    overflow[0].source = {};
    CheckError(RenderDrawList::Create(overflow), RenderDrawListErrorCode::CapacityExceeded,
        "capacity plus one is rejected before command validation");

    const RenderTextureBlitCommand first = Command(1U);
    const RenderTextureBlitCommand second = Command(2U,
        RenderSourceRectQ16{.left = 1U, .top = 2U, .right = 3U, .bottom = 4U},
        RenderTargetRectQ16{.left = 5U, .top = 6U, .right = 7U, .bottom = 8U},
        RenderTextureFitMode::Stretch, RenderTextureFilterMode::Linear);
    const std::array original{first, second, first};
    auto caller = original;
    auto created = RenderDrawList::Create(caller);
    caller.fill(RenderTextureBlitCommand{});
    Check(created && created->commands().size() == original.size() &&
              created->commands()[0] == original[0] &&
              created->commands()[1] == original[1] &&
              created->commands()[2] == original[2],
        "creation owns a copy and preserves order, duplicates, crop, fit, and filter");
    if (created)
    {
        const auto& storage = omega::runtime::detail::RenderDrawListTestAccess::Storage(*created);
        const auto tail = std::span{storage}.subspan(created->size());
        for (const auto& command : tail)
            Check(command == RenderTextureBlitCommand{}, "inactive tail commands remain zero");
        for (const std::byte byte : std::as_bytes(tail))
            Check(byte == std::byte{0}, "inactive tail bytes remain zero");
    }
}

void CheckCommandValidation()
{
    auto invalid_handle = Command();
    invalid_handle.texture = {};
    invalid_handle.source = {};
    CheckError(RenderDrawList::Create(std::span{&invalid_handle, 1U}),
        RenderDrawListErrorCode::InvalidTextureHandle,
        "invalid handle is rejected before later command fields");

    constexpr std::uint32_t limit = omega::runtime::kNormalizedRenderExtent;
    const std::array invalid_sources{
        RenderSourceRectQ16{.left = 0U, .top = 0U, .right = 0U, .bottom = 1U},
        RenderSourceRectQ16{.left = 0U, .top = 0U, .right = 1U, .bottom = 0U},
        RenderSourceRectQ16{.left = 2U, .top = 0U, .right = 1U, .bottom = 1U},
        RenderSourceRectQ16{.left = 0U, .top = 2U, .right = 1U, .bottom = 1U},
        RenderSourceRectQ16{.left = 0U, .top = 0U, .right = limit + 1U, .bottom = 1U},
        RenderSourceRectQ16{.left = 0U, .top = 0U, .right = 1U, .bottom = limit + 1U},
    };
    for (const auto& source : invalid_sources)
    {
        const RenderTextureBlitCommand command = Command(0U, source, {});
        CheckError(RenderDrawList::Create(std::span{&command, 1U}),
            RenderDrawListErrorCode::InvalidSourceRect,
            "zero, inverted, or out-of-range source rectangle is rejected before target");
    }

    const std::array invalid_targets{
        RenderTargetRectQ16{.left = 0U, .top = 0U, .right = 0U, .bottom = 1U},
        RenderTargetRectQ16{.left = 0U, .top = 0U, .right = 1U, .bottom = 0U},
        RenderTargetRectQ16{.left = 2U, .top = 0U, .right = 1U, .bottom = 1U},
        RenderTargetRectQ16{.left = 0U, .top = 2U, .right = 1U, .bottom = 1U},
        RenderTargetRectQ16{.left = 0U, .top = 0U, .right = limit + 1U, .bottom = 1U},
        RenderTargetRectQ16{.left = 0U, .top = 0U, .right = 1U, .bottom = limit + 1U},
    };
    for (const auto& destination : invalid_targets)
    {
        const RenderTextureBlitCommand command = Command(0U, FullSource(), destination,
            static_cast<RenderTextureFitMode>(0xffU),
            static_cast<RenderTextureFilterMode>(0xffU));
        CheckError(RenderDrawList::Create(std::span{&command, 1U}),
            RenderDrawListErrorCode::InvalidTargetRect,
            "zero, inverted, or out-of-range target rectangle is rejected before modes");
    }

    const RenderTextureBlitCommand invalid_fit = Command(0U, FullSource(), FullTarget(),
        static_cast<RenderTextureFitMode>(0xffU),
        static_cast<RenderTextureFilterMode>(0xffU));
    CheckError(RenderDrawList::Create(std::span{&invalid_fit, 1U}),
        RenderDrawListErrorCode::InvalidFitMode,
        "invalid fit mode is rejected before invalid filter mode");

    const RenderTextureBlitCommand invalid_filter = Command(0U, FullSource(), FullTarget(),
        RenderTextureFitMode::Contain, static_cast<RenderTextureFilterMode>(0xffU));
    CheckError(RenderDrawList::Create(std::span{&invalid_filter, 1U}),
        RenderDrawListErrorCode::InvalidFilterMode, "invalid filter mode is rejected");

    auto later_invalid_handle = Command(1U);
    later_invalid_handle.texture = {};
    const std::array source_order{invalid_filter, later_invalid_handle};
    CheckError(RenderDrawList::Create(source_order), RenderDrawListErrorCode::InvalidFilterMode,
        "commands are validated completely in source order");

    const std::array boundaries{
        Command(0U, FullSource(), FullTarget(),
            RenderTextureFitMode::Contain, RenderTextureFilterMode::Nearest),
        Command(1U, FullSource(), FullTarget(),
            RenderTextureFitMode::Stretch, RenderTextureFilterMode::Linear),
    };
    Check(RenderDrawList::Create(boundaries).has_value(),
        "exact normalized boundaries and every declared mode are accepted");
}

void CheckMap(const RenderSourceRectQ16 normalized,
    const std::uint32_t source_width, const std::uint32_t source_height,
    const RenderSourceRectPixels expected, const std::string_view message)
{
    const auto first = omega::runtime::MapTextureSourceRect(
        normalized, source_width, source_height);
    const auto repeat = omega::runtime::MapTextureSourceRect(
        normalized, source_width, source_height);
    Check(first && repeat && *first == expected && *repeat == expected, message);
    if (first)
    {
        Check(first->left < first->right && first->top < first->bottom &&
                  first->right <= source_width && first->bottom <= source_height,
            "mapped source rectangle is nonempty and bounded");
    }
}

void CheckSourceMapping()
{
    CheckError(omega::runtime::MapTextureSourceRect({}, 0U, 0U),
        RenderDrawListErrorCode::InvalidSourceExtent,
        "source extent is validated before source rectangle");
    CheckError(omega::runtime::MapTextureSourceRect(FullSource(), 0U, 1U),
        RenderDrawListErrorCode::InvalidSourceExtent, "zero source width is rejected");
    CheckError(omega::runtime::MapTextureSourceRect(FullSource(), 1U, 0U),
        RenderDrawListErrorCode::InvalidSourceExtent, "zero source height is rejected");
    CheckError(omega::runtime::MapTextureSourceRect({}, 1U, 1U),
        RenderDrawListErrorCode::InvalidSourceRect, "empty source rectangle is rejected");
    CheckError(omega::runtime::MapTextureSourceRect(
                   {.left = 2U, .top = 0U, .right = 1U, .bottom = 1U}, 1U, 1U),
        RenderDrawListErrorCode::InvalidSourceRect, "inverted source rectangle is rejected");
    CheckError(omega::runtime::MapTextureSourceRect(
                   {.left = 0U, .top = 0U,
                       .right = omega::runtime::kNormalizedRenderExtent + 1U,
                       .bottom = 1U}, 1U, 1U),
        RenderDrawListErrorCode::InvalidSourceRect,
        "out-of-range source rectangle is rejected");

    CheckMap(FullSource(), 8U, 8U, {0U, 0U, 8U, 8U},
        "full normalized source maps to the full texture");
    CheckMap({16'384U, 16'384U, 49'152U, 49'152U}, 8U, 4U,
        {2U, 1U, 6U, 3U}, "quarter boundaries map exactly on divisible extents");
    CheckMap({1U, 1U, 32'767U, 32'767U}, 3U, 3U,
        {0U, 0U, 2U, 2U}, "source starts floor and ends ceil");
    CheckMap({65'535U, 65'535U, 65'536U, 65'536U}, 8U, 8U,
        {7U, 7U, 8U, 8U}, "far-edge source quantum includes the final texel");
    constexpr std::uint32_t maximum = std::numeric_limits<std::uint32_t>::max();
    CheckMap(FullSource(), maximum, maximum, {0U, 0U, maximum, maximum},
        "maximum source mapping arithmetic does not overflow");
}

void CheckPlan(const RenderSourceRectPixels source,
    const RenderTargetRectQ16 destination, const RenderTextureFitMode fit_mode,
    const std::uint32_t target_width, const std::uint32_t target_height,
    const RenderTextureBlitPlan expected, const std::string_view message)
{
    const auto first = omega::runtime::PlanTextureBlit(
        source, destination, fit_mode, target_width, target_height);
    const auto repeat = omega::runtime::PlanTextureBlit(
        source, destination, fit_mode, target_width, target_height);
    Check(first && repeat && *first == expected && *repeat == expected, message);
    if (!first)
        return;

    const auto floor_edge = [](const std::uint32_t edge, const std::uint32_t extent)
    {
        return static_cast<std::uint32_t>(
            static_cast<std::uint64_t>(edge) * extent /
            omega::runtime::kNormalizedRenderExtent);
    };
    const auto ceil_edge = [](const std::uint32_t edge, const std::uint32_t extent)
    {
        const std::uint64_t scaled = static_cast<std::uint64_t>(edge) * extent;
        return static_cast<std::uint32_t>((scaled +
            omega::runtime::kNormalizedRenderExtent - 1U) /
            omega::runtime::kNormalizedRenderExtent);
    };
    Check(first->source == source &&
              first->destination.left >= floor_edge(destination.left, target_width) &&
              first->destination.top >= floor_edge(destination.top, target_height) &&
              first->destination.right <= ceil_edge(destination.right, target_width) &&
              first->destination.bottom <= ceil_edge(destination.bottom, target_height) &&
              first->destination.left < first->destination.right &&
              first->destination.top < first->destination.bottom,
        "planned output preserves the crop and stays inside its mapped destination");
}

void CheckPlanner()
{
    const RenderSourceRectPixels source{0U, 0U, 1U, 1U};
    CheckError(omega::runtime::PlanTextureBlit({}, {},
                   static_cast<RenderTextureFitMode>(0xffU), 0U, 0U),
        RenderDrawListErrorCode::InvalidSourceExtent,
        "mapped source extent is validated before every target field");
    CheckError(omega::runtime::PlanTextureBlit({2U, 0U, 1U, 1U}, FullTarget(),
                   RenderTextureFitMode::Contain, 1U, 1U),
        RenderDrawListErrorCode::InvalidSourceExtent,
        "inverted mapped source rectangle is rejected");
    CheckError(omega::runtime::PlanTextureBlit(source, {},
                   static_cast<RenderTextureFitMode>(0xffU), 0U, 1U),
        RenderDrawListErrorCode::InvalidTargetExtent,
        "target extent is validated before rectangle and fit mode");
    CheckError(omega::runtime::PlanTextureBlit(source, FullTarget(),
                   RenderTextureFitMode::Contain, 1U, 0U),
        RenderDrawListErrorCode::InvalidTargetExtent, "zero target height is rejected");
    CheckError(omega::runtime::PlanTextureBlit(source, {},
                   static_cast<RenderTextureFitMode>(0xffU), 1U, 1U),
        RenderDrawListErrorCode::InvalidTargetRect,
        "target rectangle is validated before fit mode");
    CheckError(omega::runtime::PlanTextureBlit(source,
                   {.left = 0U, .top = 0U,
                       .right = omega::runtime::kNormalizedRenderExtent + 1U,
                       .bottom = 1U}, RenderTextureFitMode::Contain, 1U, 1U),
        RenderDrawListErrorCode::InvalidTargetRect,
        "out-of-range target rectangle is rejected");
    CheckError(omega::runtime::PlanTextureBlit(source, FullTarget(),
                   static_cast<RenderTextureFitMode>(0xffU), 1U, 1U),
        RenderDrawListErrorCode::InvalidFitMode, "invalid fit mode is rejected");

    CheckPlan({2U, 3U, 6U, 11U}, FullTarget(), RenderTextureFitMode::Contain,
        800U, 600U, {{2U, 3U, 6U, 11U}, {250U, 0U, 550U, 600U}},
        "offset cropped source is retained and aspect-contained using crop dimensions");
    CheckPlan({2U, 3U, 6U, 7U}, {1U, 1U, 32'767U, 32'767U},
        RenderTextureFitMode::Stretch, 3U, 3U,
        {{2U, 3U, 6U, 7U}, {0U, 0U, 2U, 2U}},
        "stretch maps a fractional destination and preserves an offset crop");
    CheckPlan({0U, 0U, 9U, 16U}, FullTarget(), RenderTextureFitMode::Contain,
        800U, 600U, {{0U, 0U, 9U, 16U}, {231U, 0U, 569U, 600U}},
        "tall source uses round-half-up width");
    CheckPlan({0U, 0U, 4U, 2U}, FullTarget(), RenderTextureFitMode::Contain,
        800U, 600U, {{0U, 0U, 4U, 2U}, {0U, 100U, 800U, 500U}},
        "wide source is vertically centered");
    CheckPlan({0U, 0U, 2U, 1U}, FullTarget(), RenderTextureFitMode::Contain,
        3U, 3U, {{0U, 0U, 2U, 1U}, {0U, 0U, 3U, 2U}},
        "wide source uses round-half-up height");
    CheckPlan(source, FullTarget(), RenderTextureFitMode::Contain,
        4U, 3U, {source, {0U, 0U, 3U, 3U}},
        "odd centering leaves the extra pixel on the far edge");
    CheckPlan(source, {1U, 1U, 32'767U, 32'767U}, RenderTextureFitMode::Contain,
        3U, 3U, {source, {0U, 0U, 2U, 2U}},
        "target starts floor and ends ceil");
    constexpr std::uint32_t normalized = omega::runtime::kNormalizedRenderExtent;
    CheckPlan(source, {normalized - 1U, normalized - 1U, normalized, normalized},
        RenderTextureFitMode::Contain, normalized, normalized,
        {source, {normalized - 1U, normalized - 1U, normalized, normalized}},
        "far-edge target quantum maps to the final target pixel");

    constexpr std::uint32_t maximum = std::numeric_limits<std::uint32_t>::max();
    CheckPlan({0U, 0U, maximum, 1U}, FullTarget(), RenderTextureFitMode::Contain,
        1U, 1U, {{0U, 0U, maximum, 1U}, {0U, 0U, 1U, 1U}},
        "one-pixel target clamps an extreme-wide planned height to one");
    CheckPlan({0U, 0U, 1U, maximum}, FullTarget(), RenderTextureFitMode::Contain,
        1U, 1U, {{0U, 0U, 1U, maximum}, {0U, 0U, 1U, 1U}},
        "one-pixel target clamps an extreme-tall planned width to one");
    CheckPlan({0U, 0U, maximum, maximum}, FullTarget(), RenderTextureFitMode::Contain,
        maximum, maximum,
        {{0U, 0U, maximum, maximum}, {0U, 0U, maximum, maximum}},
        "maximum square arithmetic does not overflow");
    CheckPlan({0U, 0U, maximum, 1U}, FullTarget(), RenderTextureFitMode::Contain,
        maximum, maximum,
        {{0U, 0U, maximum, 1U},
            {0U, 2'147'483'647U, maximum, 2'147'483'648U}},
        "maximum wide arithmetic does not overflow");
    CheckPlan({0U, 0U, 1U, maximum}, FullTarget(), RenderTextureFitMode::Contain,
        maximum, maximum,
        {{0U, 0U, 1U, maximum},
            {2'147'483'647U, 0U, 2'147'483'648U, maximum}},
        "maximum tall arithmetic does not overflow");
}
} // namespace

int main()
{
    CheckContractAndErrors();
    CheckDefaultCapacityAndOwnership();
    CheckCommandValidation();
    CheckSourceMapping();
    CheckPlanner();
    if (failures == 0)
        std::cout << "omega_render_draw_list_tests: all checks passed\n";
    return failures == 0 ? 0 : 1;
}
