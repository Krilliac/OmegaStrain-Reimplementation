#include "screenshot_capture.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <cstdlib>
#include <limits>
#include <new>
#include <string>
#include <system_error>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace omega::app
{
namespace
{
constexpr std::uint32_t kMaximumScreenshotFileAttempts = 1'000U;
constexpr std::string_view kScreenshotPrefix = "screenshot-";
constexpr std::string_view kScreenshotSuffix = ".bmp";
constexpr std::string_view kScreenshotTemporaryPrefix =
    ".openomega-screenshot-";
constexpr std::string_view kScreenshotTemporarySuffix = ".tmp";

static_assert(kScreenshotBmpByteCount <=
              std::numeric_limits<std::uint32_t>::max());
static_assert(kScreenshotRgba8ByteCount <=
              std::numeric_limits<std::uint32_t>::max());

[[nodiscard]] bool IsUsableRoot(
    const std::optional<std::filesystem::path>& root) noexcept
{
    return root && !root->empty() && root->is_absolute();
}

void StoreLe16(
    std::vector<std::byte>& bytes, const std::size_t offset,
    const std::uint16_t value) noexcept
{
    bytes[offset + 0U] = static_cast<std::byte>(value & 0xFFU);
    bytes[offset + 1U] = static_cast<std::byte>((value >> 8U) & 0xFFU);
}

void StoreLe32(
    std::vector<std::byte>& bytes, const std::size_t offset,
    const std::uint32_t value) noexcept
{
    bytes[offset + 0U] = static_cast<std::byte>(value & 0xFFU);
    bytes[offset + 1U] = static_cast<std::byte>((value >> 8U) & 0xFFU);
    bytes[offset + 2U] = static_cast<std::byte>((value >> 16U) & 0xFFU);
    bytes[offset + 3U] = static_cast<std::byte>((value >> 24U) & 0xFFU);
}

[[nodiscard]] bool HasDotTraversal(
    const std::filesystem::path& path) noexcept
{
    try
    {
        for (const std::filesystem::path& component : path)
        {
            if (component == "." || component == "..")
                return true;
        }
        return false;
    }
    catch (...)
    {
        return true;
    }
}

[[nodiscard]] bool IsWindowsReparsePoint(
    const std::filesystem::path& path) noexcept
{
#if defined(_WIN32)
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U;
#else
    (void)path;
    return false;
#endif
}

[[nodiscard]] std::expected<void, ScreenshotErrorCode>
EnsureSafeDirectory(const std::filesystem::path& directory) noexcept
{
    try
    {
        if (directory.empty() || !directory.is_absolute() ||
            HasDotTraversal(directory))
        {
            return std::unexpected(ScreenshotErrorCode::UnsafePath);
        }

        std::filesystem::path current = directory.root_path();
        for (const std::filesystem::path& component : directory.relative_path())
        {
            current /= component;
            std::error_code error;
            auto status = std::filesystem::symlink_status(current, error);
            if ((!error && status.type() == std::filesystem::file_type::not_found) ||
                error == std::errc::no_such_file_or_directory)
            {
                error.clear();
                const bool created = std::filesystem::create_directory(current, error);
                if (error || !created)
                {
                    return std::unexpected(
                        ScreenshotErrorCode::DirectoryCreateFailed);
                }
                status = std::filesystem::symlink_status(current, error);
            }
            if (error)
            {
                return std::unexpected(
                    ScreenshotErrorCode::DirectoryCreateFailed);
            }
            if (std::filesystem::is_symlink(status) ||
                IsWindowsReparsePoint(current) ||
                !std::filesystem::is_directory(status))
            {
                return std::unexpected(ScreenshotErrorCode::UnsafePath);
            }
        }
        return {};
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected(ScreenshotErrorCode::AllocationFailed);
    }
    catch (...)
    {
        return std::unexpected(ScreenshotErrorCode::InternalFailure);
    }
}

[[nodiscard]] std::expected<std::string, ScreenshotErrorCode>
MakeSequenceName(const std::string_view prefix, const std::uint32_t sequence,
    const std::string_view suffix) noexcept
{
    try
    {
        std::array<char, 16U> digits{};
        const auto converted = std::to_chars(
            digits.data(), digits.data() + digits.size(), sequence);
        if (converted.ec != std::errc{})
            return std::unexpected(ScreenshotErrorCode::InternalFailure);
        const std::size_t digit_count =
            static_cast<std::size_t>(converted.ptr - digits.data());
        constexpr std::size_t kMinimumDigitCount = 6U;
        std::string name;
        name.reserve(prefix.size() + kMinimumDigitCount + suffix.size());
        name.append(prefix);
        if (digit_count < kMinimumDigitCount)
            name.append(kMinimumDigitCount - digit_count, '0');
        name.append(digits.data(), digit_count);
        name.append(suffix);
        return name;
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected(ScreenshotErrorCode::AllocationFailed);
    }
    catch (...)
    {
        return std::unexpected(ScreenshotErrorCode::InternalFailure);
    }
}

enum class ExclusiveWriteResult : std::uint8_t
{
    Success = 0U,
    Collision,
    CreateFailed,
    WriteFailed,
    CommitFailed,
};

[[nodiscard]] ExclusiveWriteResult WriteExclusiveTemporary(
    const std::filesystem::path& path,
    const std::span<const std::byte> bytes) noexcept
{
#if defined(_WIN32)
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0U, nullptr,
        CREATE_NEW,
        FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_OPEN_REPARSE_POINT |
            FILE_FLAG_WRITE_THROUGH,
        nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        const DWORD error = GetLastError();
        return error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS
            ? ExclusiveWriteResult::Collision
            : ExclusiveWriteResult::CreateFailed;
    }

    std::size_t written_total = 0U;
    bool write_ok = true;
    while (written_total < bytes.size())
    {
        const std::size_t remaining = bytes.size() - written_total;
        const DWORD requested = static_cast<DWORD>(std::min<std::size_t>(
            remaining, std::numeric_limits<DWORD>::max()));
        DWORD written = 0U;
        if (!WriteFile(file, bytes.data() + written_total,
                requested, &written, nullptr) ||
            written != requested)
        {
            write_ok = false;
            break;
        }
        written_total += written;
    }
    const bool committed = write_ok && FlushFileBuffers(file) != FALSE;
    const bool closed = CloseHandle(file) != FALSE;
    if (!write_ok)
        return ExclusiveWriteResult::WriteFailed;
    if (!committed || !closed)
        return ExclusiveWriteResult::CommitFailed;
    return ExclusiveWriteResult::Success;
#else
    const int file = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW,
        S_IRUSR | S_IWUSR);
    if (file < 0)
    {
        return errno == EEXIST ? ExclusiveWriteResult::Collision
                              : ExclusiveWriteResult::CreateFailed;
    }
    std::size_t written_total = 0U;
    bool write_ok = true;
    while (written_total < bytes.size())
    {
        const ssize_t written = ::write(
            file, bytes.data() + written_total, bytes.size() - written_total);
        if (written <= 0)
        {
            write_ok = false;
            break;
        }
        written_total += static_cast<std::size_t>(written);
    }
    const bool committed = write_ok && ::fsync(file) == 0;
    const bool closed = ::close(file) == 0;
    if (!write_ok)
        return ExclusiveWriteResult::WriteFailed;
    if (!committed || !closed)
        return ExclusiveWriteResult::CommitFailed;
    return ExclusiveWriteResult::Success;
#endif
}

void RemoveOwnedTemporary(const std::filesystem::path& path) noexcept
{
#if defined(_WIN32)
    (void)DeleteFileW(path.c_str());
#else
    (void)::unlink(path.c_str());
#endif
}

[[nodiscard]] ExclusiveWriteResult PublishNoReplace(
    const std::filesystem::path& temporary,
    const std::filesystem::path& destination) noexcept
{
#if defined(_WIN32)
    if (MoveFileExW(
            temporary.c_str(), destination.c_str(), MOVEFILE_WRITE_THROUGH))
    {
        return ExclusiveWriteResult::Success;
    }
    const DWORD error = GetLastError();
    return error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS
        ? ExclusiveWriteResult::Collision
        : ExclusiveWriteResult::CommitFailed;
#else
    if (::link(temporary.c_str(), destination.c_str()) == 0)
    {
        if (::unlink(temporary.c_str()) == 0)
            return ExclusiveWriteResult::Success;
        return ExclusiveWriteResult::CommitFailed;
    }
    return errno == EEXIST ? ExclusiveWriteResult::Collision
                          : ExclusiveWriteResult::CommitFailed;
#endif
}

#if defined(_WIN32)
[[nodiscard]] std::optional<std::filesystem::path>
ReadWideEnvironmentPath(const wchar_t* name) noexcept
{
    try
    {
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
        const wchar_t* value = _wgetenv(name);
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
        if (value == nullptr)
            return std::nullopt;
        return std::filesystem::path(value);
    }
    catch (...)
    {
        return std::nullopt;
    }
}
#else
[[nodiscard]] std::optional<std::filesystem::path>
ReadEnvironmentPath(const char* name) noexcept
{
    try
    {
        const char* value = std::getenv(name);
        if (value == nullptr)
            return std::nullopt;
        return std::filesystem::path(value);
    }
    catch (...)
    {
        return std::nullopt;
    }
}
#endif
} // namespace

ScreenshotPlatform HostScreenshotPlatform() noexcept
{
#if defined(_WIN32)
    return ScreenshotPlatform::Windows;
#elif defined(__APPLE__)
    return ScreenshotPlatform::MacOS;
#else
    return ScreenshotPlatform::Xdg;
#endif
}

std::optional<std::filesystem::path> ResolveDefaultScreenshotDirectory(
    const ScreenshotPlatform platform, const ScreenshotSearchRoots& roots)
{
    switch (platform)
    {
    case ScreenshotPlatform::Windows:
        if (!IsUsableRoot(roots.local_app_data))
            return std::nullopt;
        return *roots.local_app_data / "OpenOmega" / "screenshots";
    case ScreenshotPlatform::MacOS:
        if (!IsUsableRoot(roots.home))
            return std::nullopt;
        return *roots.home / "Library" / "Application Support" /
               "OpenOmega" / "screenshots";
    case ScreenshotPlatform::Xdg:
        if (IsUsableRoot(roots.xdg_data_home))
            return *roots.xdg_data_home / "openomega" / "screenshots";
        if (!IsUsableRoot(roots.home))
            return std::nullopt;
        return *roots.home / ".local" / "share" / "openomega" /
               "screenshots";
    }
    return std::nullopt;
}

std::expected<std::vector<std::byte>, ScreenshotErrorCode>
EncodeScreenshotBmp(
    const std::span<const runtime::RenderClearColorRgba8> pixels) noexcept
{
    if (pixels.size() != kScreenshotPixelCount)
        return std::unexpected(ScreenshotErrorCode::InvalidFrame);
    try
    {
        std::vector<std::byte> bytes(kScreenshotBmpByteCount, std::byte{0});
        bytes[0U] = std::byte{0x42U};
        bytes[1U] = std::byte{0x4DU};
        StoreLe32(bytes, 2U, static_cast<std::uint32_t>(bytes.size()));
        StoreLe32(bytes, 10U,
            static_cast<std::uint32_t>(kScreenshotBmpHeaderByteCount));
        StoreLe32(bytes, 14U, 40U);
        StoreLe32(bytes, 18U, kScreenshotWidth);
        StoreLe32(bytes, 22U, kScreenshotHeight);
        StoreLe16(bytes, 26U, 1U);
        StoreLe16(bytes, 28U, 32U);
        StoreLe32(bytes, 34U,
            static_cast<std::uint32_t>(kScreenshotRgba8ByteCount));
        StoreLe32(bytes, 38U, 2'835U);
        StoreLe32(bytes, 42U, 2'835U);

        std::size_t output = kScreenshotBmpHeaderByteCount;
        for (std::uint32_t output_y = 0U;
             output_y < kScreenshotHeight; ++output_y)
        {
            const std::uint32_t source_y =
                kScreenshotHeight - 1U - output_y;
            const std::size_t source_row =
                static_cast<std::size_t>(source_y) * kScreenshotWidth;
            for (std::uint32_t x = 0U; x < kScreenshotWidth; ++x)
            {
                const runtime::RenderClearColorRgba8 pixel =
                    pixels[source_row + x];
                bytes[output + 0U] = static_cast<std::byte>(pixel.blue);
                bytes[output + 1U] = static_cast<std::byte>(pixel.green);
                bytes[output + 2U] = static_cast<std::byte>(pixel.red);
                bytes[output + 3U] = static_cast<std::byte>(pixel.alpha);
                output += 4U;
            }
        }
        return bytes;
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected(ScreenshotErrorCode::AllocationFailed);
    }
    catch (...)
    {
        return std::unexpected(ScreenshotErrorCode::InternalFailure);
    }
}

std::expected<ScreenshotWriteResult, ScreenshotErrorCode>
WriteScreenshotBmp(const std::filesystem::path& directory,
    const std::span<const runtime::RenderClearColorRgba8> pixels) noexcept
{
    try
    {
        auto encoded = EncodeScreenshotBmp(pixels);
        if (!encoded)
            return std::unexpected(encoded.error());
        auto safe_directory = EnsureSafeDirectory(directory);
        if (!safe_directory)
            return std::unexpected(safe_directory.error());

        for (std::uint32_t sequence = 1U;
             sequence <= kMaximumScreenshotFileAttempts; ++sequence)
        {
            auto final_name =
                MakeSequenceName(kScreenshotPrefix, sequence, kScreenshotSuffix);
            auto temporary_name = MakeSequenceName(kScreenshotTemporaryPrefix,
                sequence, kScreenshotTemporarySuffix);
            if (!final_name || !temporary_name)
            {
                return std::unexpected(!final_name ? final_name.error()
                                                   : temporary_name.error());
            }
            const std::filesystem::path final_path = directory / *final_name;
            const std::filesystem::path temporary_path =
                directory / *temporary_name;

            std::error_code inspection_error;
            const auto final_status =
                std::filesystem::symlink_status(final_path, inspection_error);
            if (inspection_error &&
                inspection_error != std::errc::no_such_file_or_directory)
            {
                return std::unexpected(ScreenshotErrorCode::UnsafePath);
            }
            if (!inspection_error && std::filesystem::exists(final_status))
                continue;

            const ExclusiveWriteResult write =
                WriteExclusiveTemporary(temporary_path, *encoded);
            if (write == ExclusiveWriteResult::Collision)
                continue;
            if (write == ExclusiveWriteResult::CreateFailed)
                return std::unexpected(ScreenshotErrorCode::FileCreateFailed);
            if (write == ExclusiveWriteResult::WriteFailed)
            {
                RemoveOwnedTemporary(temporary_path);
                return std::unexpected(ScreenshotErrorCode::FileWriteFailed);
            }
            if (write == ExclusiveWriteResult::CommitFailed)
            {
                RemoveOwnedTemporary(temporary_path);
                return std::unexpected(ScreenshotErrorCode::FileCommitFailed);
            }

            const ExclusiveWriteResult published =
                PublishNoReplace(temporary_path, final_path);
            if (published == ExclusiveWriteResult::Collision)
            {
                RemoveOwnedTemporary(temporary_path);
                continue;
            }
            if (published != ExclusiveWriteResult::Success)
            {
                RemoveOwnedTemporary(temporary_path);
                return std::unexpected(ScreenshotErrorCode::PublishFailed);
            }

            const auto published_status =
                std::filesystem::symlink_status(final_path, inspection_error);
            if (inspection_error || std::filesystem::is_symlink(published_status) ||
                IsWindowsReparsePoint(final_path) ||
                !std::filesystem::is_regular_file(published_status))
            {
                return std::unexpected(ScreenshotErrorCode::UnsafePath);
            }
            const std::uintmax_t published_size =
                std::filesystem::file_size(final_path, inspection_error);
            if (inspection_error || published_size != encoded->size())
                return std::unexpected(ScreenshotErrorCode::PublishFailed);
            return ScreenshotWriteResult{
                .path = final_path,
                .sequence = sequence,
            };
        }
        return std::unexpected(ScreenshotErrorCode::NameExhausted);
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected(ScreenshotErrorCode::AllocationFailed);
    }
    catch (...)
    {
        return std::unexpected(ScreenshotErrorCode::InternalFailure);
    }
}

std::expected<ScreenshotWriteResult, ScreenshotErrorCode>
WriteScreenshotBmpToDefaultDirectory(
    const std::span<const runtime::RenderClearColorRgba8> pixels) noexcept
{
    ScreenshotSearchRoots roots;
#if defined(_WIN32)
    roots.local_app_data = ReadWideEnvironmentPath(L"LOCALAPPDATA");
#elif defined(__APPLE__)
    roots.home = ReadEnvironmentPath("HOME");
#else
    roots.xdg_data_home = ReadEnvironmentPath("XDG_DATA_HOME");
    roots.home = ReadEnvironmentPath("HOME");
#endif
    auto directory =
        ResolveDefaultScreenshotDirectory(HostScreenshotPlatform(), roots);
    if (!directory)
        return std::unexpected(ScreenshotErrorCode::PathUnavailable);
    return WriteScreenshotBmp(*directory, pixels);
}
} // namespace omega::app
