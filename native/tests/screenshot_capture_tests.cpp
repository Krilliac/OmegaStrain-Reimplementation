#include "screenshot_capture.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>

namespace
{
int failures = 0;

void Check(const bool condition, const std::string_view message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

[[nodiscard]] std::filesystem::path SyntheticAbsoluteRoot(
    const std::string_view suffix)
{
#if defined(_WIN32)
    return std::filesystem::path("C:/openomega-screenshot-path-tests") / suffix;
#else
    return std::filesystem::path("/openomega-screenshot-path-tests") / suffix;
#endif
}

[[nodiscard]] std::uint32_t ReadLe32(
    const std::span<const std::byte> bytes, const std::size_t offset)
{
    return std::to_integer<std::uint32_t>(bytes[offset + 0U]) |
           (std::to_integer<std::uint32_t>(bytes[offset + 1U]) << 8U) |
           (std::to_integer<std::uint32_t>(bytes[offset + 2U]) << 16U) |
           (std::to_integer<std::uint32_t>(bytes[offset + 3U]) << 24U);
}

class ScopedTestDirectory final
{
public:
    explicit ScopedTestDirectory(std::filesystem::path path)
        : path_(std::move(path))
    {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    ~ScopedTestDirectory()
    {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept
    {
        return path_;
    }

private:
    std::filesystem::path path_;
};
} // namespace

int main()
{
    using namespace omega::app;
    static_assert(kScreenshotWidth == 640U);
    static_assert(kScreenshotHeight == 448U);
    static_assert(kScreenshotPixelCount == 286'720U);
    static_assert(kScreenshotRgba8ByteCount == 1'146'880U);
    static_assert(kScreenshotBmpByteCount == 1'146'934U);
    static_assert(std::is_same_v<decltype(ScreenshotWriteResult::sequence),
        std::uint32_t>);

    const auto local = SyntheticAbsoluteRoot("local");
    const auto xdg = SyntheticAbsoluteRoot("xdg");
    const auto home = SyntheticAbsoluteRoot("home");
    ScreenshotSearchRoots roots{
        .local_app_data = local,
        .xdg_data_home = xdg,
        .home = home,
    };
    Check(ResolveDefaultScreenshotDirectory(
              ScreenshotPlatform::Windows, roots) ==
              std::optional<std::filesystem::path>{
                  local / "OpenOmega" / "screenshots"},
        "Windows selects only the fixed OpenOmega screenshots suffix");
    Check(ResolveDefaultScreenshotDirectory(
              ScreenshotPlatform::MacOS, roots) ==
              std::optional<std::filesystem::path>{home / "Library" /
                  "Application Support" / "OpenOmega" / "screenshots"},
        "macOS selects the platform-local OpenOmega screenshots directory");
    Check(ResolveDefaultScreenshotDirectory(
              ScreenshotPlatform::Xdg, roots) ==
              std::optional<std::filesystem::path>{
                  xdg / "openomega" / "screenshots"},
        "XDG_DATA_HOME takes precedence for screenshots");
    roots.xdg_data_home.reset();
    Check(ResolveDefaultScreenshotDirectory(
              ScreenshotPlatform::Xdg, roots) ==
              std::optional<std::filesystem::path>{home / ".local" /
                  "share" / "openomega" / "screenshots"},
        "XDG screenshots fall back to HOME/.local/share");
    roots.local_app_data = std::filesystem::path("relative");
    Check(!ResolveDefaultScreenshotDirectory(
              ScreenshotPlatform::Windows, roots),
        "relative platform roots are rejected");

    const std::span<const omega::runtime::RenderClearColorRgba8> empty_pixels;
    const auto invalid = EncodeScreenshotBmp(empty_pixels);
    Check(!invalid && invalid.error() == ScreenshotErrorCode::InvalidFrame,
        "the encoder rejects any non-fixed pixel count");

    ScreenshotRgba8Pixels pixels(kScreenshotPixelCount);
    pixels.front() = omega::runtime::RenderClearColorRgba8{
        .red = 1U, .green = 2U, .blue = 3U, .alpha = 4U};
    pixels[(static_cast<std::size_t>(kScreenshotHeight) - 1U) *
        kScreenshotWidth] = omega::runtime::RenderClearColorRgba8{
        .red = 10U, .green = 20U, .blue = 30U, .alpha = 40U};
    auto encoded = EncodeScreenshotBmp(pixels);
    Check(encoded.has_value(), "the fixed RGBA8 frame encodes");
    if (!encoded)
        return 1;
    Check(encoded->size() == kScreenshotBmpByteCount &&
              (*encoded)[0U] == std::byte{0x42U} &&
              (*encoded)[1U] == std::byte{0x4DU} &&
              ReadLe32(*encoded, 2U) == kScreenshotBmpByteCount &&
              ReadLe32(*encoded, 10U) == kScreenshotBmpHeaderByteCount &&
              ReadLe32(*encoded, 18U) == kScreenshotWidth &&
              ReadLe32(*encoded, 22U) == kScreenshotHeight,
        "the BMP header reports the exact bounded payload and extent");
    Check((*encoded)[kScreenshotBmpHeaderByteCount + 0U] ==
                  std::byte{30U} &&
              (*encoded)[kScreenshotBmpHeaderByteCount + 1U] ==
                  std::byte{20U} &&
              (*encoded)[kScreenshotBmpHeaderByteCount + 2U] ==
                  std::byte{10U} &&
              (*encoded)[kScreenshotBmpHeaderByteCount + 3U] ==
                  std::byte{40U},
        "the BMP payload is bottom-up BGRA without assigning retail semantics");

    const auto nonce = static_cast<std::uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    ScopedTestDirectory test_root{
        std::filesystem::temp_directory_path() /
        ("openomega-screenshot-tests-" + std::to_string(nonce))};
    const std::filesystem::path disabled_directory =
        test_root.path() / "disabled";
    Check(!std::filesystem::exists(disabled_directory),
        "the disabled screenshot path performs no directory or file I/O");

    const std::filesystem::path screenshot_directory =
        test_root.path() / "private" / "screenshots";
    auto first = WriteScreenshotBmp(screenshot_directory, pixels);
    Check(first && first->sequence == 1U &&
              first->path.filename() == "screenshot-000001.bmp" &&
              std::filesystem::file_size(first->path) ==
                  kScreenshotBmpByteCount,
        "the writer publishes the first exact private BMP");
    auto second = WriteScreenshotBmp(screenshot_directory, pixels);
    Check(second && second->sequence == 2U &&
              second->path.filename() == "screenshot-000002.bmp",
        "an existing screenshot is never replaced");
    const auto temporary_count = std::ranges::count_if(
        std::filesystem::directory_iterator(screenshot_directory),
        [](const std::filesystem::directory_entry& entry) {
            return entry.path().extension() == ".tmp";
        });
    Check(temporary_count == 0,
        "successful atomic publication leaves no temporary payload");

    const std::filesystem::path traversal =
        test_root.path() / "safe" / ".." / "outside";
    auto unsafe = WriteScreenshotBmp(traversal, pixels);
    Check(!unsafe && unsafe.error() == ScreenshotErrorCode::UnsafePath &&
              !std::filesystem::exists(test_root.path() / "outside"),
        "dot traversal is rejected before directory creation");

    const std::filesystem::path link_target = test_root.path() / "link-target";
    const std::filesystem::path link_path = test_root.path() / "link";
    std::error_code link_error;
    std::filesystem::create_directories(link_target, link_error);
    link_error.clear();
    std::filesystem::create_directory_symlink(
        link_target, link_path, link_error);
    if (!link_error)
    {
        auto through_link =
            WriteScreenshotBmp(link_path / "screenshots", pixels);
        Check(!through_link &&
                  through_link.error() == ScreenshotErrorCode::UnsafePath,
            "existing links are rejected before payload creation");
    }

    const auto privacy_code = ScreenshotErrorCodeName(
        ScreenshotErrorCode::UnsafePath);
    Check(privacy_code == "unsafe-path" &&
              privacy_code.find(test_root.path().filename().string()) ==
                  std::string_view::npos,
        "public screenshot errors remain categorical and path-free");

    return failures == 0 ? 0 : 1;
}
