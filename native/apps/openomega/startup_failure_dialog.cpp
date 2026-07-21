#include "startup_failure_dialog.h"

#include <SDL3/SDL_messagebox.h>
#include <SDL3/SDL_stdinc.h>

#include <array>
#include <cstddef>
#include <string_view>

namespace omega::app
{
namespace
{
constexpr std::string_view kUnknownCategory = "unknown";
constexpr std::string_view kUnknownDetail = "No additional detail is available.";
constexpr std::string_view kPrefix =
    "OpenOmega could not reach the main menu.\n\nStage: ";
constexpr std::string_view kCodePrefix = "\nCode: ";
constexpr std::string_view kDetailPrefix = "\nDetail: ";
constexpr std::string_view kSuffix =
    "\n\nCheck the private OpenOmega configuration, game-data root, or launch arguments, "
    "then try again.";

static_assert(kStartupFailureDialogMaximumTextSize + 1U <=
              kStartupFailureDialogTextCapacity);

[[nodiscard]] constexpr std::string_view StartupFailureStageLabel(
    const StartupFailureStage stage) noexcept
{
    switch (stage)
    {
    case StartupFailureStage::RuntimeConfiguration:
        return "runtime configuration";
    case StartupFailureStage::RuntimeSettings:
        return "runtime settings";
    case StartupFailureStage::ContentLaunchProfile:
        return "content launch profile";
    case StartupFailureStage::ContentStartup:
        return "content startup";
    case StartupFailureStage::NativePersistence:
        return "native persistence";
    case StartupFailureStage::ApplicationStartup:
        return "application startup";
    }
    return "startup";
}

template<std::size_t Limit>
struct ProjectedField
{
    std::array<char, Limit + 1U> bytes{};
    std::size_t size = 0U;
};

template<std::size_t Limit>
[[nodiscard]] ProjectedField<Limit> ProjectField(
    const std::string_view source, const std::string_view empty_fallback) noexcept
{
    static_assert(Limit >= 3U);

    ProjectedField<Limit> projected;
    bool pending_space = false;
    bool overflow = false;

    const auto push = [&projected, &overflow](const char value) noexcept {
        if (projected.size < projected.bytes.size())
            projected.bytes[projected.size++] = value;
        else
            overflow = true;
        if (projected.size > Limit)
            overflow = true;
    };

    for (const unsigned char byte : source)
    {
        const bool collapsible_space = byte == static_cast<unsigned char>(' ') ||
                                       byte == static_cast<unsigned char>('\t') ||
                                       byte == static_cast<unsigned char>('\r') ||
                                       byte == static_cast<unsigned char>('\n');
        if (collapsible_space)
        {
            if (projected.size != 0U)
                pending_space = true;
            continue;
        }

        if (pending_space)
        {
            push(' ');
            pending_space = false;
            if (overflow)
                break;
        }

        const char output = byte >= 0x21U && byte <= 0x7eU
                                ? static_cast<char>(byte)
                                : '?';
        push(output);
        if (overflow)
            break;
    }

    if (projected.size == 0U)
    {
        for (const char byte : empty_fallback)
        {
            if (projected.size == Limit)
                break;
            projected.bytes[projected.size++] = byte;
        }
    }
    else if (overflow)
    {
        projected.size = Limit;
        projected.bytes[Limit - 3U] = '.';
        projected.bytes[Limit - 2U] = '.';
        projected.bytes[Limit - 1U] = '.';
    }

    projected.bytes[projected.size] = '\0';
    return projected;
}

void Append(StartupFailureDialogText& destination, const std::string_view source) noexcept
{
    for (const char byte : source)
    {
        if (destination.size >= destination.bytes.size() - 1U)
            break;
        destination.bytes[destination.size++] = byte;
    }
    destination.bytes[destination.size] = '\0';
}
} // namespace

StartupFailureDialogText BuildStartupFailureDialogText(
    const StartupFailureDialogRequest& request) noexcept
{
    const auto category = ProjectField<kStartupFailureDialogCategoryLimit>(
        request.category, kUnknownCategory);
    const auto detail = ProjectField<kStartupFailureDialogDetailLimit>(
        request.detail, kUnknownDetail);

    StartupFailureDialogText text;
    Append(text, kPrefix);
    Append(text, StartupFailureStageLabel(request.stage));
    Append(text, kCodePrefix);
    Append(text, std::string_view(category.bytes.data(), category.size));
    Append(text, kDetailPrefix);
    Append(text, std::string_view(detail.bytes.data(), detail.size));
    Append(text, kSuffix);
    return text;
}

StartupFailureDialogPolicy ResolveStartupFailureDialogPolicy(
    const char* const disable_value) noexcept
{
    return disable_value != nullptr && disable_value[0] == '1' && disable_value[1] == '\0'
               ? StartupFailureDialogPolicy::Suppress
               : StartupFailureDialogPolicy::Allow;
}

StartupFailureDialogPolicy ReadStartupFailureDialogPolicyFromEnvironment() noexcept
{
    return ResolveStartupFailureDialogPolicy(
        SDL_getenv(kStartupFailureDialogDisableEnvironmentVariable));
}

StartupFailureDialogOutcome TryShowStartupFailureDialog(
    const StartupFailureDialogRequest& request,
    const StartupFailureDialogPolicy policy) noexcept
{
    if (policy != StartupFailureDialogPolicy::Allow)
        return StartupFailureDialogOutcome::Suppressed;

    const StartupFailureDialogText text = BuildStartupFailureDialogText(request);
    return SDL_ShowSimpleMessageBox(
               SDL_MESSAGEBOX_ERROR, kStartupFailureDialogTitle, text.bytes.data(), nullptr)
               ? StartupFailureDialogOutcome::Presented
               : StartupFailureDialogOutcome::Unavailable;
}
} // namespace omega::app
