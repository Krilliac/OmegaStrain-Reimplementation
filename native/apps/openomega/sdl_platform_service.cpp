#include "sdl_platform_service.h"

#include <SDL3/SDL.h>

#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace omega::app
{
namespace
{
[[nodiscard]] std::string SdlError(const std::string_view operation)
{
    const char* detail = SDL_GetError();
    return std::string(operation) + ": " +
           (detail != nullptr && detail[0] != '\0' ? detail : "unknown SDL error");
}
} // namespace

struct SdlPlatformService::Impl
{
    ~Impl()
    {
        if (initialized)
            SDL_Quit();
    }

    bool initialized = false;
};

std::expected<SdlPlatformService, std::string> SdlPlatformService::Create()
{
    auto impl = std::make_unique<Impl>();
    if (!SDL_SetAppMetadata("OpenOmega", "0.1.0", "io.github.krilliac.openomega"))
        return std::unexpected(SdlError("SDL_SetAppMetadata"));
    if (!SDL_Init(0))
        return std::unexpected(SdlError("SDL_Init"));
    impl->initialized = true;
    return SdlPlatformService(std::move(impl));
}

SdlPlatformService::SdlPlatformService(std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl))
{
}

SdlPlatformService::~SdlPlatformService() = default;
SdlPlatformService::SdlPlatformService(SdlPlatformService&&) noexcept = default;

bool SdlPlatformService::ready() const noexcept
{
    return impl_ != nullptr && impl_->initialized;
}
} // namespace omega::app
