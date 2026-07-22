#include "launcher_config.h"

#include "omega/runtime/config_service.h"
#include "omega/debug/subsystem_entry_break.h"

#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace omega::launcher
{
namespace
{
constexpr std::string_view kDataSourceKey = "content.data_root";
constexpr std::string_view kOpeningMovieMemberKey =
    "content.opening_movie_member";
constexpr std::string_view kGamepadEnabledKey = "input.gamepad_enabled";

[[nodiscard]] bool IsContinuationByte(const unsigned char byte) noexcept
{
    return byte >= 0x80U && byte <= 0xBFU;
}

[[nodiscard]] bool IsValidUtf8(const std::string_view text) noexcept
{
    std::size_t cursor = 0U;
    while (cursor < text.size())
    {
        const unsigned char first = static_cast<unsigned char>(text[cursor]);
        if (first <= 0x7FU)
        {
            ++cursor;
            continue;
        }

        if (first >= 0xC2U && first <= 0xDFU)
        {
            if (cursor + 1U >= text.size() ||
                !IsContinuationByte(static_cast<unsigned char>(text[cursor + 1U])))
                return false;
            cursor += 2U;
            continue;
        }

        if (first >= 0xE0U && first <= 0xEFU)
        {
            if (cursor + 2U >= text.size())
                return false;
            const unsigned char second = static_cast<unsigned char>(text[cursor + 1U]);
            const unsigned char third = static_cast<unsigned char>(text[cursor + 2U]);
            const bool valid_second = first == 0xE0U   ? second >= 0xA0U && second <= 0xBFU
                                      : first == 0xEDU ? second >= 0x80U && second <= 0x9FU
                                                       : IsContinuationByte(second);
            if (!valid_second || !IsContinuationByte(third))
                return false;
            cursor += 3U;
            continue;
        }

        if (first >= 0xF0U && first <= 0xF4U)
        {
            if (cursor + 3U >= text.size())
                return false;
            const unsigned char second = static_cast<unsigned char>(text[cursor + 1U]);
            const unsigned char third = static_cast<unsigned char>(text[cursor + 2U]);
            const unsigned char fourth = static_cast<unsigned char>(text[cursor + 3U]);
            const bool valid_second = first == 0xF0U   ? second >= 0x90U && second <= 0xBFU
                                      : first == 0xF4U ? second >= 0x80U && second <= 0x8FU
                                                       : IsContinuationByte(second);
            if (!valid_second || !IsContinuationByte(third) || !IsContinuationByte(fourth))
                return false;
            cursor += 4U;
            continue;
        }
        return false;
    }
    return true;
}

[[nodiscard]] bool IsSafePathValue(const std::string_view value) noexcept
{
    if (value.empty() || value.size() > runtime::kMaxConfigValueBytes || value.front() == ' ' ||
        value.front() == '\t' || value.back() == ' ' || value.back() == '\t')
        return false;
    for (const unsigned char byte : value)
    {
        if (byte < 0x20U || byte == 0x7FU)
            return false;
    }
    return IsValidUtf8(value);
}

[[nodiscard]] std::expected<std::filesystem::path, std::string> DecodePathValue(
    const std::string_view value)
{
    if (!IsSafePathValue(value))
        return std::unexpected("launcher content path is not a valid UTF-8 config value");
    try
    {
        std::u8string utf8;
        utf8.resize(value.size());
        std::memcpy(utf8.data(), value.data(), value.size());
        std::filesystem::path decoded(utf8);
        if (decoded.empty())
            return std::unexpected("launcher content path is empty");
        return decoded;
    }
    catch (...)
    {
        return std::unexpected("launcher content path cannot be decoded");
    }
}

[[nodiscard]] std::expected<std::string, std::string> EncodePathValue(
    const std::filesystem::path& path)
{
    if (path.empty())
        return std::unexpected("launcher content path is empty");
    try
    {
        const std::u8string utf8 = path.u8string();
        std::string encoded;
        encoded.resize(utf8.size());
        std::memcpy(encoded.data(), utf8.data(), utf8.size());
        if (!IsSafePathValue(encoded))
        {
            return std::unexpected("launcher content path is not a valid UTF-8 config value");
        }
        return encoded;
    }
    catch (...)
    {
        return std::unexpected("launcher content path cannot be encoded");
    }
}

[[nodiscard]] std::expected<std::optional<runtime::ConfigStore>, std::string> LoadExistingStore(
    const std::filesystem::path& config_path)
{
    try
    {
        std::error_code status_error;
        const std::filesystem::file_status status =
            std::filesystem::symlink_status(config_path, status_error);
        if (status_error)
        {
            if (status_error == std::errc::no_such_file_or_directory)
                return std::optional<runtime::ConfigStore>{};
            return std::unexpected("unable to inspect launcher config file");
        }
        if (status.type() == std::filesystem::file_type::not_found)
            return std::optional<runtime::ConfigStore>{};

        auto loaded = runtime::LoadConfigFile(config_path);
        if (!loaded)
            return std::unexpected("launcher config: " + loaded.error());
        return std::optional<runtime::ConfigStore>{std::move(*loaded)};
    }
    catch (...)
    {
        return std::unexpected("unable to inspect launcher config file");
    }
}

void AppendEntry(std::string& text, const std::string_view key, const std::string_view value)
{
    text.append(key);
    text.append(" = ");
    text.append(value);
    text.push_back('\n');
}

[[nodiscard]] std::string SerializeStore(const runtime::ConfigStore& store)
{
    std::string text;
    for (const runtime::ConfigEntry& entry : store.entries())
        AppendEntry(text, entry.key, entry.value);
    return text;
}

[[nodiscard]] std::filesystem::path MakeTemporaryPath(const std::filesystem::path& target,
                                                      const std::uint64_t nonce)
{
    std::filesystem::path temporary = target;
#if defined(_WIN32)
    temporary +=
        L".openomega.tmp." + std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(nonce);
#else
    temporary += ".openomega.tmp." + std::to_string(static_cast<long long>(::getpid())) + "." +
                 std::to_string(nonce);
#endif
    return temporary;
}

enum class TemporaryWriteResult : std::uint8_t
{
    Written = 0U,
    Collision,
    Failure,
};

[[nodiscard]] TemporaryWriteResult WriteTemporaryFile(const std::filesystem::path& path,
                                                      const std::string_view bytes)
{
#if defined(_WIN32)
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        const DWORD error = GetLastError();
        if (error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS)
            return TemporaryWriteResult::Collision;
        return TemporaryWriteResult::Failure;
    }

    bool ok = true;
    std::size_t offset = 0U;
    while (offset < bytes.size())
    {
        const std::size_t remaining = bytes.size() - offset;
        const DWORD requested = static_cast<DWORD>(remaining);
        DWORD written = 0U;
        if (WriteFile(file, bytes.data() + offset, requested, &written, nullptr) == FALSE ||
            written != requested)
        {
            ok = false;
            break;
        }
        offset += written;
    }
    if (ok && FlushFileBuffers(file) == FALSE)
        ok = false;
    if (CloseHandle(file) == FALSE)
        ok = false;
    if (!ok)
    {
        static_cast<void>(DeleteFileW(path.c_str()));
        return TemporaryWriteResult::Failure;
    }
    return TemporaryWriteResult::Written;
#else
    const int file = ::open(path.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0600);
    if (file < 0)
    {
        if (errno == EEXIST)
            return TemporaryWriteResult::Collision;
        return TemporaryWriteResult::Failure;
    }

    bool ok = true;
    std::size_t offset = 0U;
    while (offset < bytes.size())
    {
        const ssize_t written = ::write(file, bytes.data() + offset, bytes.size() - offset);
        if (written < 0 && errno == EINTR)
            continue;
        if (written <= 0)
        {
            ok = false;
            break;
        }
        offset += static_cast<std::size_t>(written);
    }
    while (ok && ::fsync(file) != 0)
    {
        if (errno != EINTR)
            ok = false;
    }
    if (::close(file) != 0)
        ok = false;
    if (!ok)
    {
        static_cast<void>(::unlink(path.c_str()));
        return TemporaryWriteResult::Failure;
    }
    return TemporaryWriteResult::Written;
#endif
}

[[nodiscard]] std::expected<std::filesystem::path, std::string> WriteSiblingTemporary(
    const std::filesystem::path& target, const std::string_view bytes)
{
    static std::atomic<std::uint64_t> next_nonce{1U};
    try
    {
        for (std::size_t attempt = 0U; attempt < 64U; ++attempt)
        {
            const std::filesystem::path temporary =
                MakeTemporaryPath(target, next_nonce.fetch_add(1U, std::memory_order_relaxed));
            switch (WriteTemporaryFile(temporary, bytes))
            {
            case TemporaryWriteResult::Written:
                return temporary;
            case TemporaryWriteResult::Collision:
                continue;
            case TemporaryWriteResult::Failure:
                return std::unexpected("unable to write launcher config temporary file");
            }
        }
    }
    catch (...)
    {
        return std::unexpected("unable to reserve launcher config temporary file");
    }
    return std::unexpected("unable to reserve launcher config temporary file");
}

void RemoveTemporary(const std::filesystem::path& temporary) noexcept
{
#if defined(_WIN32)
    static_cast<void>(DeleteFileW(temporary.c_str()));
#else
    static_cast<void>(::unlink(temporary.c_str()));
#endif
}

[[nodiscard]] std::expected<void, std::string> ReplaceConfigFile(
    const std::filesystem::path& temporary, const std::filesystem::path& target)
{
#if defined(_WIN32)
    if (MoveFileExW(temporary.c_str(), target.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == FALSE)
    {
        RemoveTemporary(temporary);
        return std::unexpected("unable to replace launcher config file");
    }
#else
    std::error_code rename_error;
    std::filesystem::rename(temporary, target, rename_error);
    if (rename_error)
    {
        RemoveTemporary(temporary);
        return std::unexpected("unable to replace launcher config file");
    }
#endif
    return {};
}
} // namespace

std::expected<LauncherPreferences, std::string> LoadLauncherPreferences(
    const std::filesystem::path& config_path)
{
    OMEGA_DEBUG_BREAK_SUBSYSTEM_ENTRY("omega_launcher_core");
    auto existing = LoadExistingStore(config_path);
    if (!existing)
        return std::unexpected(std::move(existing).error());

    LauncherPreferences preferences;
    if (!existing->has_value())
        return preferences;

    const runtime::ConfigStore& store = **existing;
    if (const auto value = store.GetString(kDataSourceKey))
    {
        auto decoded = DecodePathValue(*value);
        if (!decoded)
            return std::unexpected(std::move(decoded).error());
        preferences.data_source = std::move(*decoded);
    }

    if (const auto value = store.GetString(kOpeningMovieMemberKey))
    {
        if (value->empty() || !IsSafePathValue(*value))
        {
            return std::unexpected(
                "launcher opening movie selection is not a valid UTF-8 config value");
        }
        preferences.opening_movie_member = std::string(*value);
    }

    auto gamepad = store.GetBool(kGamepadEnabledKey);
    if (!gamepad)
        return std::unexpected("launcher gamepad setting: " + gamepad.error());
    preferences.gamepad_enabled = gamepad->value_or(false);
    return preferences;
}

std::expected<void, std::string> SaveLauncherPreferencesAtomically(
    const std::filesystem::path& config_path, const LauncherPreferences& preferences)
{
    std::optional<std::string> encoded_source;
    if (preferences.data_source)
    {
        auto encoded = EncodePathValue(*preferences.data_source);
        if (!encoded)
            return std::unexpected(std::move(encoded).error());
        encoded_source = std::move(*encoded);
    }
    if (preferences.opening_movie_member &&
        (preferences.opening_movie_member->empty() ||
         !IsSafePathValue(*preferences.opening_movie_member)))
    {
        return std::unexpected(
            "launcher opening movie selection is not a valid UTF-8 config value");
    }

    auto existing = LoadExistingStore(config_path);
    if (!existing)
        return std::unexpected(std::move(existing).error());

    std::string candidate;
    bool wrote_source = false;
    bool wrote_opening_movie_member = false;
    bool wrote_gamepad = false;
    if (existing->has_value())
    {
        for (const runtime::ConfigEntry& entry : (**existing).entries())
        {
            if (entry.key == kDataSourceKey)
            {
                if (encoded_source)
                {
                    AppendEntry(candidate, kDataSourceKey, *encoded_source);
                    wrote_source = true;
                }
                continue;
            }
            if (entry.key == kGamepadEnabledKey)
            {
                AppendEntry(candidate, kGamepadEnabledKey,
                            preferences.gamepad_enabled ? "true" : "false");
                wrote_gamepad = true;
                continue;
            }
            if (entry.key == kOpeningMovieMemberKey)
            {
                if (preferences.opening_movie_member)
                {
                    AppendEntry(candidate, kOpeningMovieMemberKey,
                                *preferences.opening_movie_member);
                    wrote_opening_movie_member = true;
                }
                continue;
            }
            AppendEntry(candidate, entry.key, entry.value);
        }
    }
    if (encoded_source && !wrote_source)
        AppendEntry(candidate, kDataSourceKey, *encoded_source);
    if (preferences.opening_movie_member && !wrote_opening_movie_member)
    {
        AppendEntry(candidate, kOpeningMovieMemberKey,
                    *preferences.opening_movie_member);
    }
    if (!wrote_gamepad)
    {
        AppendEntry(candidate, kGamepadEnabledKey, preferences.gamepad_enabled ? "true" : "false");
    }

    auto validated = runtime::ParseConfigText(candidate);
    if (!validated)
        return std::unexpected("launcher config serialization: " + validated.error());
    const std::string canonical = SerializeStore(*validated);

    try
    {
        const std::filesystem::path parent = config_path.parent_path();
        if (!parent.empty())
        {
            std::error_code directory_error;
            std::filesystem::create_directories(parent, directory_error);
            if (directory_error)
                return std::unexpected("unable to create launcher config directory");
        }
    }
    catch (...)
    {
        return std::unexpected("unable to create launcher config directory");
    }

    auto temporary = WriteSiblingTemporary(config_path, canonical);
    if (!temporary)
        return std::unexpected(std::move(temporary).error());
    return ReplaceConfigFile(*temporary, config_path);
}
} // namespace omega::launcher
