#pragma once

#include "omega/runtime/render_frame_packet.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace omega::app
{
inline constexpr std::uint32_t kScreenshotWidth = 640U;
inline constexpr std::uint32_t kScreenshotHeight = 448U;
inline constexpr std::size_t kScreenshotPixelCount =
    static_cast<std::size_t>(kScreenshotWidth) * kScreenshotHeight;
inline constexpr std::size_t kScreenshotRgba8ByteCount =
    kScreenshotPixelCount * sizeof(runtime::RenderClearColorRgba8);
inline constexpr std::size_t kScreenshotBmpHeaderByteCount = 54U;
inline constexpr std::size_t kScreenshotBmpByteCount =
    kScreenshotBmpHeaderByteCount + kScreenshotRgba8ByteCount;

using ScreenshotRgba8Pixels =
    std::vector<runtime::RenderClearColorRgba8>;

enum class ScreenshotPlatform : std::uint8_t
{
    Windows = 0U,
    MacOS = 1U,
    Xdg = 2U,
};

struct ScreenshotSearchRoots
{
    std::optional<std::filesystem::path> local_app_data;
    std::optional<std::filesystem::path> xdg_data_home;
    std::optional<std::filesystem::path> home;
};

enum class ScreenshotErrorCode : std::uint8_t
{
    InvalidFrame = 0U,
    PathUnavailable,
    UnsafePath,
    DirectoryCreateFailed,
    NameExhausted,
    FileCreateFailed,
    FileWriteFailed,
    FileCommitFailed,
    PublishFailed,
    AllocationFailed,
    InternalFailure,
};

struct ScreenshotWriteResult
{
    // Private caller-owned diagnostic. OmegaApp deliberately does not print
    // this host path; the user-facing log remains categorical.
    std::filesystem::path path;
    std::uint32_t sequence = 0U;
};

// [any thread; reentrant] Compile-time platform identity only; no environment
// or filesystem access.
[[nodiscard]] ScreenshotPlatform HostScreenshotPlatform() noexcept;

// [any thread; reentrant] Pure platform-local directory selection from
// already-captured roots. Empty and relative roots are ignored. No path
// normalization, token expansion, environment lookup, or filesystem access.
[[nodiscard]] std::optional<std::filesystem::path>
ResolveDefaultScreenshotDirectory(
    ScreenshotPlatform platform, const ScreenshotSearchRoots& roots);

[[nodiscard]] constexpr std::string_view ScreenshotErrorCodeName(
    const ScreenshotErrorCode code) noexcept
{
    switch (code)
    {
    case ScreenshotErrorCode::InvalidFrame:
        return "invalid-frame";
    case ScreenshotErrorCode::PathUnavailable:
        return "path-unavailable";
    case ScreenshotErrorCode::UnsafePath:
        return "unsafe-path";
    case ScreenshotErrorCode::DirectoryCreateFailed:
        return "directory-create-failed";
    case ScreenshotErrorCode::NameExhausted:
        return "name-exhausted";
    case ScreenshotErrorCode::FileCreateFailed:
        return "file-create-failed";
    case ScreenshotErrorCode::FileWriteFailed:
        return "file-write-failed";
    case ScreenshotErrorCode::FileCommitFailed:
        return "file-commit-failed";
    case ScreenshotErrorCode::PublishFailed:
        return "publish-failed";
    case ScreenshotErrorCode::AllocationFailed:
        return "allocation-failed";
    case ScreenshotErrorCode::InternalFailure:
        return "internal-failure";
    }
    return "internal-failure";
}

// [any thread; reentrant] Produces exactly one bounded 32-bit BI_RGB BMP from
// the fixed 640x448 row-major RGBA8 frame. No metadata, source identity, path,
// or unbounded payload is added.
[[nodiscard]] std::expected<std::vector<std::byte>, ScreenshotErrorCode>
EncodeScreenshotBmp(
    std::span<const runtime::RenderClearColorRgba8> pixels) noexcept;

// [main thread; no concurrent calls for the same directory] Writes one
// same-directory temporary with exclusive creation, durable flush, and a
// no-replace publication step. Existing links/reparse points and dot traversal
// are rejected. At most 1,000 fixed candidate names are considered.
[[nodiscard]] std::expected<ScreenshotWriteResult, ScreenshotErrorCode>
WriteScreenshotBmp(const std::filesystem::path& directory,
    std::span<const runtime::RenderClearColorRgba8> pixels) noexcept;

// [main thread] Captures platform roots from the process environment only when
// explicitly called, then delegates to the bounded writer above. F12 is the
// only production call site; the disabled path performs no environment or file
// I/O.
[[nodiscard]] std::expected<ScreenshotWriteResult, ScreenshotErrorCode>
WriteScreenshotBmpToDefaultDirectory(
    std::span<const runtime::RenderClearColorRgba8> pixels) noexcept;
} // namespace omega::app
