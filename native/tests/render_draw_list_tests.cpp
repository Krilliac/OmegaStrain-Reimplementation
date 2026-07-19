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
using omega::runtime::RenderTargetRectPixels;
using omega::runtime::RenderTargetRectQ16;
using omega::runtime::RenderTextureBlitCommand;
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

constexpr RenderTargetRectQ16 FullRect() noexcept
{
    return RenderTargetRectQ16{
        .left = 0U,
        .top = 0U,
        .right = omega::runtime::kNormalizedRenderExtent,
        .bottom = omega::runtime::kNormalizedRenderExtent,
    };
}

constexpr RenderTextureBlitCommand Command(
    const std::uint32_t index = 0U, const RenderTargetRectQ16 rect = FullRect()) noexcept
{
    return RenderTextureBlitCommand{.texture = Handle(index), .destination = rect};
}

void CheckContractAndErrors()
{
    static_assert(std::is_trivially_copyable_v<RenderTargetRectQ16>);
    static_assert(std::is_standard_layout_v<RenderTargetRectQ16>);
    static_assert(std::is_trivially_copyable_v<RenderTargetRectPixels>);
    static_assert(std::is_standard_layout_v<RenderTargetRectPixels>);
    static_assert(std::is_trivially_copyable_v<RenderTextureBlitCommand>);
    static_assert(std::is_standard_layout_v<RenderTextureBlitCommand>);
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
    static_assert(noexcept(omega::runtime::PlanContainedTextureBlit(
        FullRect(), 1U, 1U, 1U, 1U)));

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
        ExpectedError{RenderDrawListErrorCode::InvalidTargetRect, "invalid-target-rect",
            "render draw list target rectangle is invalid"},
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
    overflow[1].destination = {};
    CheckError(RenderDrawList::Create(overflow), RenderDrawListErrorCode::CapacityExceeded,
        "capacity plus one is rejected before command validation");

    const RenderTextureBlitCommand first = Command(1U);
    const RenderTextureBlitCommand second = Command(2U,
        RenderTargetRectQ16{.left = 1U, .top = 2U, .right = 3U, .bottom = 4U});
    const std::array original{first, second, first};
    auto caller = original;
    auto created = RenderDrawList::Create(caller);
    caller.fill(RenderTextureBlitCommand{});
    Check(created && created->commands().size() == original.size() &&
              created->commands()[0] == original[0] &&
              created->commands()[1] == original[1] &&
              created->commands()[2] == original[2],
        "creation owns a copy and preserves order and duplicates");
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
    CheckError(RenderDrawList::Create(std::span{&invalid_handle, 1U}),
        RenderDrawListErrorCode::InvalidTextureHandle, "default handle is rejected");

    constexpr std::uint32_t limit = omega::runtime::kNormalizedRenderExtent;
    const std::array invalid_rects{
        RenderTargetRectQ16{.left = 0U, .top = 0U, .right = 0U, .bottom = 1U},
        RenderTargetRectQ16{.left = 0U, .top = 0U, .right = 1U, .bottom = 0U},
        RenderTargetRectQ16{.left = 2U, .top = 0U, .right = 1U, .bottom = 1U},
        RenderTargetRectQ16{.left = 0U, .top = 2U, .right = 1U, .bottom = 1U},
        RenderTargetRectQ16{.left = limit + 1U, .top = 0U,
            .right = limit + 2U, .bottom = 1U},
        RenderTargetRectQ16{.left = 0U, .top = limit + 1U,
            .right = 1U, .bottom = limit + 2U},
        RenderTargetRectQ16{.left = 0U, .top = 0U, .right = limit + 1U, .bottom = 1U},
        RenderTargetRectQ16{.left = 0U, .top = 0U, .right = 1U, .bottom = limit + 1U},
    };
    for (const auto& rect : invalid_rects)
    {
        const RenderTextureBlitCommand command = Command(0U, rect);
        CheckError(RenderDrawList::Create(std::span{&command, 1U}),
            RenderDrawListErrorCode::InvalidTargetRect,
            "zero, inverted, or out-of-range target rectangle is rejected");
    }
    const std::array boundary{Command(0U, FullRect())};
    Check(RenderDrawList::Create(boundary).has_value(),
        "exact normalized boundary is accepted");
}

void CheckPlan(const RenderTargetRectQ16 normalized,
    const std::uint32_t source_width, const std::uint32_t source_height,
    const std::uint32_t target_width, const std::uint32_t target_height,
    const RenderTargetRectPixels expected, const std::string_view message)
{
    const auto first = omega::runtime::PlanContainedTextureBlit(
        normalized, source_width, source_height, target_width, target_height);
    const auto repeat = omega::runtime::PlanContainedTextureBlit(
        normalized, source_width, source_height, target_width, target_height);
    Check(first && repeat && *first == expected && *repeat == expected,
        message);
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
    Check(first->left >= floor_edge(normalized.left, target_width) &&
              first->top >= floor_edge(normalized.top, target_height) &&
              first->right <= ceil_edge(normalized.right, target_width) &&
              first->bottom <= ceil_edge(normalized.bottom, target_height) &&
              first->left < first->right && first->top < first->bottom,
        "planned output is deterministic and contained in its mapped destination");
}

void CheckPlanner()
{
    CheckError(omega::runtime::PlanContainedTextureBlit(FullRect(), 0U, 1U, 1U, 1U),
        RenderDrawListErrorCode::InvalidSourceExtent, "zero source width is rejected");
    CheckError(omega::runtime::PlanContainedTextureBlit(FullRect(), 1U, 0U, 1U, 1U),
        RenderDrawListErrorCode::InvalidSourceExtent, "zero source height is rejected");
    CheckError(omega::runtime::PlanContainedTextureBlit(FullRect(), 1U, 1U, 0U, 1U),
        RenderDrawListErrorCode::InvalidTargetExtent, "zero target width is rejected");
    CheckError(omega::runtime::PlanContainedTextureBlit(FullRect(), 1U, 1U, 1U, 0U),
        RenderDrawListErrorCode::InvalidTargetExtent, "zero target height is rejected");
    CheckError(omega::runtime::PlanContainedTextureBlit({}, 1U, 1U, 1U, 1U),
        RenderDrawListErrorCode::InvalidTargetRect, "invalid planner rectangle is rejected");
    CheckError(omega::runtime::PlanContainedTextureBlit(
                   {.left = 2U, .top = 0U, .right = 1U, .bottom = 1U},
                   1U, 1U, 1U, 1U),
        RenderDrawListErrorCode::InvalidTargetRect,
        "inverted planner rectangle is rejected");
    CheckError(omega::runtime::PlanContainedTextureBlit(
                   {.left = 0U, .top = 0U,
                       .right = omega::runtime::kNormalizedRenderExtent + 1U,
                       .bottom = 1U},
                   1U, 1U, 1U, 1U),
        RenderDrawListErrorCode::InvalidTargetRect,
        "out-of-range planner rectangle is rejected");

    CheckPlan(FullRect(), 1U, 1U, 600U, 600U, {0U, 0U, 600U, 600U},
        "full-target square source fills a square target");
    CheckPlan(FullRect(), 16U, 9U, 800U, 600U, {0U, 75U, 800U, 525U},
        "wide source is aspect-contained");
    CheckPlan(FullRect(), 9U, 16U, 800U, 600U, {231U, 0U, 569U, 600U},
        "tall source uses nearest rounded width");
    CheckPlan(FullRect(), 1U, 1U, 4U, 3U, {0U, 0U, 3U, 3U},
        "odd centering leaves the extra pixel on the far edge");
    CheckPlan({1U, 1U, 32'767U, 32'767U}, 1U, 1U, 3U, 3U,
        {0U, 0U, 2U, 2U}, "Q16 starts floor and ends ceil");
    constexpr std::uint32_t normalized = omega::runtime::kNormalizedRenderExtent;
    CheckPlan({normalized - 1U, normalized - 1U, normalized, normalized},
        1U, 1U, normalized, normalized,
        {normalized - 1U, normalized - 1U, normalized, normalized},
        "far-edge one-Q16-quantum rectangle maps to the final target pixel");
    CheckPlan(FullRect(), std::numeric_limits<std::uint32_t>::max(), 1U, 1U, 1U,
        {0U, 0U, 1U, 1U}, "one-pixel target retains a nonempty extreme-aspect source");

    constexpr std::uint32_t maximum = std::numeric_limits<std::uint32_t>::max();
    CheckPlan(FullRect(), maximum, maximum, maximum, maximum,
        {0U, 0U, maximum, maximum}, "maximum square arithmetic does not overflow");
    CheckPlan(FullRect(), maximum, 1U, maximum, maximum,
        {0U, 2'147'483'647U, maximum, 2'147'483'648U},
        "maximum wide arithmetic does not overflow");
    CheckPlan(FullRect(), 1U, maximum, maximum, maximum,
        {2'147'483'647U, 0U, 2'147'483'648U, maximum},
        "maximum tall arithmetic does not overflow");
}
} // namespace

int main()
{
    CheckContractAndErrors();
    CheckDefaultCapacityAndOwnership();
    CheckCommandValidation();
    CheckPlanner();
    if (failures == 0)
        std::cout << "omega_render_draw_list_tests: all checks passed\n";
    return failures == 0 ? 0 : 1;
}
