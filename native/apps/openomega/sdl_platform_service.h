#pragma once

#include <expected>
#include <memory>
#include <string>

namespace omega::app
{
// Non-hot-reloadable main-thread owner for SDL's process-global lifetime. SDL-facing leaves own
// their subsystem resources but must be destroyed before this service.
class SdlPlatformService final
{
public:
    // [main thread, startup] Sets project metadata and initializes SDL's global runtime.
    [[nodiscard]] static std::expected<SdlPlatformService, std::string> Create();

    // [main thread, after every SDL-facing leaf has stopped]
    ~SdlPlatformService();
    SdlPlatformService(SdlPlatformService&&) noexcept;
    SdlPlatformService& operator=(SdlPlatformService&&) noexcept = delete;
    SdlPlatformService(const SdlPlatformService&) = delete;
    SdlPlatformService& operator=(const SdlPlatformService&) = delete;

    // [main thread] False only for a moved-from instance.
    [[nodiscard]] bool ready() const noexcept;

private:
    struct Impl;
    explicit SdlPlatformService(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};
} // namespace omega::app
