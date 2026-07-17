#include "omega/content/game_data_service.h"

#include "omega/archive/hog_archive.h"
#include "omega/asset/source_locator.h"
#include "omega/retail/pop_level_manifest_decoder.h"
#include "omega/vfs/virtual_file_system.h"

#include <cstddef>
#include <span>
#include <utility>
#include <vector>

namespace omega::content
{
namespace
{
constexpr std::string_view kExpectedBootExecutable = "SCUS_972.64";
constexpr std::string_view kExpectedBootValue = "CDROM0:\\SCUS_972.64;1";
constexpr std::size_t kMaximumLevelCodeBytes = 32;

[[nodiscard]] GameDataError Error(const GameDataErrorCode code, std::string message)
{
    return GameDataError{
        .code = code,
        .message = std::move(message),
        .decode_error = std::nullopt,
    };
}

[[nodiscard]] std::string_view TrimAsciiWhitespace(std::string_view value) noexcept
{
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t'))
        value.remove_prefix(1);
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t' ||
                                 value.back() == '\r'))
        value.remove_suffix(1);
    return value;
}

[[nodiscard]] bool EqualsAsciiCaseInsensitive(
    const std::string_view left, const std::string_view right) noexcept
{
    if (left.size() != right.size())
        return false;
    for (std::size_t index = 0; index < left.size(); ++index)
    {
        const auto fold = [](const unsigned char value) {
            return value >= static_cast<unsigned char>('a') &&
                    value <= static_cast<unsigned char>('z')
                ? static_cast<unsigned char>(value - ('a' - 'A'))
                : value;
        };
        if (fold(static_cast<unsigned char>(left[index])) !=
            fold(static_cast<unsigned char>(right[index])))
            return false;
    }
    return true;
}

[[nodiscard]] std::expected<void, GameDataError> ValidateSystemConfig(
    const std::span<const std::byte> bytes)
{
    if (bytes.empty())
        return std::unexpected(Error(GameDataErrorCode::UnsupportedBuild,
            "SYSTEM.CNF is empty"));
    for (std::size_t index = 0; index < bytes.size(); ++index)
    {
        const auto value = std::to_integer<unsigned char>(bytes[index]);
        if (value != '\t' && value != '\r' && value != '\n' &&
            (value < 0x20U || value > 0x7EU))
            return std::unexpected(Error(GameDataErrorCode::UnsupportedBuild,
                "SYSTEM.CNF contains non-ASCII data"));
    }

    const auto* first = reinterpret_cast<const char*>(bytes.data());
    const std::string_view text(first, bytes.size());
    std::optional<std::string_view> boot_value;
    std::size_t cursor = 0;
    while (cursor <= text.size())
    {
        const std::size_t line_end = text.find('\n', cursor);
        const std::size_t count = line_end == std::string_view::npos
            ? text.size() - cursor
            : line_end - cursor;
        const std::string_view line = TrimAsciiWhitespace(text.substr(cursor, count));
        const std::size_t equals = line.find('=');
        if (equals != std::string_view::npos &&
            EqualsAsciiCaseInsensitive(TrimAsciiWhitespace(line.substr(0, equals)), "BOOT2"))
        {
            if (boot_value)
                return std::unexpected(Error(GameDataErrorCode::UnsupportedBuild,
                    "SYSTEM.CNF contains duplicate BOOT2 entries"));
            boot_value = TrimAsciiWhitespace(line.substr(equals + 1U));
        }
        if (line_end == std::string_view::npos)
            break;
        cursor = line_end + 1U;
    }

    if (!boot_value)
        return std::unexpected(Error(GameDataErrorCode::UnsupportedBuild,
            "SYSTEM.CNF has no BOOT2 entry"));
    if (!EqualsAsciiCaseInsensitive(*boot_value, kExpectedBootValue))
        return std::unexpected(Error(GameDataErrorCode::UnsupportedBuild,
            "unsupported retail build: expected NTSC-U SCUS-97264"));
    return {};
}

[[nodiscard]] std::expected<std::string, GameDataError> NormalizeLevelCode(
    const std::string_view level_code)
{
    if (level_code.empty() || level_code.size() > kMaximumLevelCodeBytes)
        return std::unexpected(Error(GameDataErrorCode::InvalidLevelCode,
            "level code must contain 1 to 32 ASCII letters or digits"));

    std::string normalized;
    normalized.reserve(level_code.size());
    for (const unsigned char value : level_code)
    {
        const bool is_upper = value >= static_cast<unsigned char>('A') &&
                              value <= static_cast<unsigned char>('Z');
        const bool is_lower = value >= static_cast<unsigned char>('a') &&
                              value <= static_cast<unsigned char>('z');
        const bool is_digit = value >= static_cast<unsigned char>('0') &&
                              value <= static_cast<unsigned char>('9');
        if (!is_upper && !is_lower && !is_digit)
            return std::unexpected(Error(GameDataErrorCode::InvalidLevelCode,
                "level code must contain only ASCII letters or digits"));
        normalized.push_back(static_cast<char>(is_lower ? value - ('a' - 'A') : value));
    }
    return normalized;
}
} // namespace

struct GameDataService::Impl
{
    GameDataIdentity identity;
    vfs::VirtualFileSystem files;
    GameDataServiceConfig config;
};

std::string_view RetailBuildName(const RetailBuild build) noexcept
{
    switch (build)
    {
    case RetailBuild::NtscUScus97264:
        return "NTSC-U SCUS-97264";
    }
    return "unknown";
}

std::string_view GameDataErrorCodeName(const GameDataErrorCode code) noexcept
{
    switch (code)
    {
    case GameDataErrorCode::InvalidConfiguration:
        return "invalid-configuration";
    case GameDataErrorCode::MountFailed:
        return "mount-failed";
    case GameDataErrorCode::MissingRequiredFile:
        return "missing-required-file";
    case GameDataErrorCode::UnsupportedBuild:
        return "unsupported-build";
    case GameDataErrorCode::InvalidLevelCode:
        return "invalid-level-code";
    case GameDataErrorCode::ReadFailed:
        return "read-failed";
    case GameDataErrorCode::MalformedArchive:
        return "malformed-archive";
    case GameDataErrorCode::DecodeFailed:
        return "decode-failed";
    }
    return "unknown";
}

std::expected<GameDataService, GameDataError> GameDataService::Open(
    GameDataServiceConfig config)
{
    if (config.root.empty() || config.maximum_system_config_bytes == 0 ||
        config.maximum_pop_bytes == 0 || config.maximum_data_hog_bytes == 0)
        return std::unexpected(Error(GameDataErrorCode::InvalidConfiguration,
            "game-data root and byte limits must be non-empty"));

    auto impl = std::make_unique<Impl>();
    impl->config = std::move(config);
    auto mounted = impl->files.MountDirectory(impl->config.root);
    if (!mounted)
        return std::unexpected(Error(GameDataErrorCode::MountFailed,
            "unable to mount game-data root: " + mounted.error()));
    impl->files.Freeze();

    if (!impl->files.Contains("SYSTEM.CNF"))
        return std::unexpected(Error(GameDataErrorCode::MissingRequiredFile,
            "game-data root is missing SYSTEM.CNF"));
    auto system_config = impl->files.Read(
        "SYSTEM.CNF", impl->config.maximum_system_config_bytes);
    if (!system_config)
        return std::unexpected(Error(GameDataErrorCode::ReadFailed,
            "unable to read SYSTEM.CNF: " + system_config.error()));
    auto validated = ValidateSystemConfig(*system_config);
    if (!validated)
        return std::unexpected(validated.error());

    if (!impl->files.Contains(kExpectedBootExecutable))
        return std::unexpected(Error(GameDataErrorCode::MissingRequiredFile,
            "NTSC-U data root is missing SCUS_972.64"));

    impl->identity = GameDataIdentity{
        .build = RetailBuild::NtscUScus97264,
        .boot_executable = std::string(kExpectedBootExecutable),
    };
    return GameDataService(std::move(impl));
}

GameDataService::GameDataService(std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl))
{
}

GameDataService::~GameDataService() = default;
GameDataService::GameDataService(GameDataService&&) noexcept = default;
GameDataService& GameDataService::operator=(GameDataService&&) noexcept = default;

const GameDataIdentity& GameDataService::identity() const noexcept
{
    return impl_->identity;
}

std::expected<asset::LevelManifestIR, GameDataError> GameDataService::LoadLevelManifest(
    const std::string_view level_code) const
{
    auto normalized_level = NormalizeLevelCode(level_code);
    if (!normalized_level)
        return std::unexpected(normalized_level.error());

    const std::string level_root = "GAMEDATA/" + *normalized_level;
    const std::string pop_path = level_root + "/DATA.POP";
    const std::string data_hog_path = level_root + "/DATA.HOG";
    if (!impl_->files.Contains(pop_path))
        return std::unexpected(Error(GameDataErrorCode::MissingRequiredFile,
            "level is missing DATA.POP: " + *normalized_level));
    if (!impl_->files.Contains(data_hog_path))
        return std::unexpected(Error(GameDataErrorCode::MissingRequiredFile,
            "level is missing DATA.HOG: " + *normalized_level));

    auto pop_bytes = impl_->files.Read(pop_path, impl_->config.maximum_pop_bytes);
    if (!pop_bytes)
        return std::unexpected(Error(GameDataErrorCode::ReadFailed,
            "unable to read level DATA.POP: " + pop_bytes.error()));
    auto data_hog_bytes = impl_->files.Read(
        data_hog_path, impl_->config.maximum_data_hog_bytes);
    if (!data_hog_bytes)
        return std::unexpected(Error(GameDataErrorCode::ReadFailed,
            "unable to read level DATA.HOG: " + data_hog_bytes.error()));

    auto data_hog = archive::HogArchive::FromBytes(std::move(*data_hog_bytes));
    if (!data_hog)
        return std::unexpected(Error(GameDataErrorCode::MalformedArchive,
            "invalid level DATA.HOG: " + data_hog.error()));

    auto manifest = retail::DecodePopLevelManifest(*pop_bytes, data_hog->entries(),
        asset::SourceLocator{.game_path = data_hog_path, .hog_entries = {}},
        impl_->config.decode_limits);
    if (!manifest)
    {
        return std::unexpected(GameDataError{
            .code = GameDataErrorCode::DecodeFailed,
            .message = "unable to decode level manifest: " + manifest.error().message,
            .decode_error = std::move(manifest.error()),
        });
    }
    return std::move(*manifest);
}
} // namespace omega::content
