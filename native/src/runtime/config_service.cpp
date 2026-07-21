#include "omega/runtime/config_service.h"

#include <charconv>
#include <fstream>
#include <limits>
#include <system_error>
#include <utility>

namespace omega::runtime
{
namespace
{
[[nodiscard]] std::string_view TrimBlanks(std::string_view text) noexcept
{
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
        text.remove_prefix(1U);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t'))
        text.remove_suffix(1U);
    return text;
}

[[nodiscard]] std::optional<std::string> LimitsError(const ConfigLimits& limits)
{
    if (limits.max_input_bytes == 0U || limits.max_line_bytes == 0U || limits.max_entries == 0U)
        return "config limits must all be positive";
    return std::nullopt;
}

[[nodiscard]] std::optional<std::string> KeyError(const std::string_view key)
{
    if (key.empty())
        return "config key is empty";
    if (key.size() > kMaxConfigKeyBytes)
        return "config key exceeds the " + std::to_string(kMaxConfigKeyBytes) + "-byte key budget";
    if (key.front() == '.' || key.back() == '.')
        return "config key '" + std::string(key) + "' may not begin or end with '.'";
    char previous = '\0';
    for (const char character : key)
    {
        const bool lower = character >= 'a' && character <= 'z';
        const bool digit = character >= '0' && character <= '9';
        if (!lower && !digit && character != '_' && character != '.')
            return "config key contains a byte outside [a-z0-9_.]";
        if (character == '.' && previous == '.')
            return "config key '" + std::string(key) + "' contains an empty dotted segment";
        previous = character;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::string> ValueError(const std::string_view value)
{
    if (value.size() > kMaxConfigValueBytes)
        return "config value exceeds the " + std::to_string(kMaxConfigValueBytes) +
               "-byte value budget";
    for (const char character : value)
    {
        const unsigned char byte = static_cast<unsigned char>(character);
        if (byte < 0x20U && byte != 0x09U)
            return "config value contains a control byte";
    }
    return std::nullopt;
}

[[nodiscard]] std::expected<std::int64_t, std::string> ParseStrictInt64(
    const std::string_view key, const std::string_view value)
{
    const auto reject = [&key, &value]() -> std::unexpected<std::string>
    {
        return std::unexpected("config key '" + std::string(key) +
            "' must be a canonical base-10 int64, got '" + std::string(value) + "'");
    };
    std::string_view digits = value;
    const bool negative = !digits.empty() && digits.front() == '-';
    if (negative)
        digits.remove_prefix(1U);
    if (digits.empty())
        return reject();
    if (digits.front() == '0' && (digits.size() > 1U || negative))
        return reject();
    for (const char character : digits)
    {
        if (character < '0' || character > '9')
            return reject();
    }
    std::int64_t parsed = 0;
    const auto conversion =
        std::from_chars(value.data(), value.data() + value.size(), parsed, 10);
    if (conversion.ec == std::errc::result_out_of_range)
        return std::unexpected("config key '" + std::string(key) +
            "' is outside the int64 range: '" + std::string(value) + "'");
    if (conversion.ec != std::errc{} || conversion.ptr != value.data() + value.size())
        return reject();
    return parsed;
}
} // namespace

std::expected<ConfigStore, std::string> ParseConfigText(
    const std::string_view text, const ConfigLimits& limits)
{
    if (const auto error = LimitsError(limits))
        return std::unexpected(*error);
    if (text.size() > limits.max_input_bytes)
        return std::unexpected("config input of " + std::to_string(text.size()) +
            " bytes exceeds the " + std::to_string(limits.max_input_bytes) + "-byte budget");

    std::vector<ConfigEntry> entries;
    std::size_t line_number = 0;
    std::size_t cursor = 0;
    bool final_line_seen = false;
    while (!final_line_seen)
    {
        ++line_number;
        const std::string line_label = "config line " + std::to_string(line_number);
        std::string_view line;
        const std::size_t newline = text.find('\n', cursor);
        if (newline == std::string_view::npos)
        {
            line = text.substr(cursor);
            final_line_seen = true;
        }
        else
        {
            line = text.substr(cursor, newline - cursor);
            cursor = newline + 1U;
        }
        // A CR is consumed only as part of a CRLF terminator. On the EOF-terminated final
        // line there is no LF, so a trailing CR is a raw value byte and must be rejected by
        // the control-byte rule instead of silently stripped.
        if (newline != std::string_view::npos && line.ends_with('\r'))
            line.remove_suffix(1U);
        if (line.size() > limits.max_line_bytes)
            return std::unexpected(line_label + " exceeds the " +
                std::to_string(limits.max_line_bytes) + "-byte line budget");

        const std::string_view content = TrimBlanks(line);
        if (content.empty() || content.front() == '#')
            continue;
        const std::size_t equals = content.find('=');
        if (equals == std::string_view::npos)
            return std::unexpected(line_label + " is missing '='");
        const std::string_view key = TrimBlanks(content.substr(0, equals));
        const std::string_view value = TrimBlanks(content.substr(equals + 1U));
        if (const auto error = KeyError(key))
            return std::unexpected(line_label + ": " + *error);
        if (const auto error = ValueError(value))
            return std::unexpected(line_label + ": " + *error);
        for (const ConfigEntry& existing : entries)
        {
            if (existing.key == key)
                return std::unexpected(
                    line_label + " duplicates key '" + std::string(key) + "'");
        }
        if (entries.size() == limits.max_entries)
            return std::unexpected(line_label + " exceeds the entry budget of " +
                std::to_string(limits.max_entries) + " entries");
        entries.push_back(ConfigEntry{std::string(key), std::string(value)});
    }
    return ConfigStore(std::move(entries), limits.max_entries);
}

ConfigStore::ConfigStore(std::vector<ConfigEntry> entries, const std::size_t entry_budget)
    : entries_(std::move(entries)), entry_budget_(entry_budget)
{
}

std::size_t ConfigStore::entry_count() const noexcept
{
    return entries_.size();
}

std::size_t ConfigStore::entry_budget() const noexcept
{
    return entry_budget_;
}

std::span<const ConfigEntry> ConfigStore::entries() const noexcept
{
    return entries_;
}

const ConfigEntry* ConfigStore::Find(const std::string_view key) const noexcept
{
    for (const ConfigEntry& entry : entries_)
    {
        if (entry.key == key)
            return &entry;
    }
    return nullptr;
}

bool ConfigStore::Contains(const std::string_view key) const noexcept
{
    return Find(key) != nullptr;
}

std::optional<std::string_view> ConfigStore::GetString(const std::string_view key) const noexcept
{
    const ConfigEntry* entry = Find(key);
    if (entry == nullptr)
        return std::nullopt;
    return std::string_view(entry->value);
}

std::expected<std::optional<bool>, std::string> ConfigStore::GetBool(
    const std::string_view key) const
{
    const ConfigEntry* entry = Find(key);
    if (entry == nullptr)
        return std::optional<bool>{};
    if (entry->value == "true")
        return std::optional<bool>{true};
    if (entry->value == "false")
        return std::optional<bool>{false};
    return std::unexpected("config key '" + std::string(key) +
        "' must be exactly 'true' or 'false', got '" + entry->value + "'");
}

std::expected<std::optional<std::int64_t>, std::string> ConfigStore::GetInt64(
    const std::string_view key) const
{
    const ConfigEntry* entry = Find(key);
    if (entry == nullptr)
        return std::optional<std::int64_t>{};
    auto parsed = ParseStrictInt64(key, entry->value);
    if (!parsed)
        return std::unexpected(std::move(parsed).error());
    return std::optional<std::int64_t>{*parsed};
}

std::expected<std::string, std::string> ConfigStore::RequireString(
    const std::string_view key) const
{
    const ConfigEntry* entry = Find(key);
    if (entry == nullptr)
        return std::unexpected(
            "config key '" + std::string(key) + "' is required but absent");
    return entry->value;
}

std::expected<bool, std::string> ConfigStore::RequireBool(const std::string_view key) const
{
    auto value = GetBool(key);
    if (!value)
        return std::unexpected(std::move(value).error());
    if (!value->has_value())
        return std::unexpected(
            "config key '" + std::string(key) + "' is required but absent");
    return **value;
}

std::expected<std::int64_t, std::string> ConfigStore::RequireInt64(
    const std::string_view key) const
{
    auto value = GetInt64(key);
    if (!value)
        return std::unexpected(std::move(value).error());
    if (!value->has_value())
        return std::unexpected(
            "config key '" + std::string(key) + "' is required but absent");
    return **value;
}

std::expected<void, std::string> ConfigStore::ApplyOverride(
    const std::string_view raw_key, const std::string_view raw_value)
{
    // Same rules as parsed text: keys and values are SP/HTAB-trimmed before validation,
    // budgets, and storage, so the override channel accepts exactly what the file channel
    // accepts and never stores a value shape the grammar forbids.
    const std::string_view key = TrimBlanks(raw_key);
    const std::string_view value = TrimBlanks(raw_value);
    if (const auto error = KeyError(key))
        return std::unexpected("config override: " + *error);
    if (const auto error = ValueError(value))
        return std::unexpected("config override: " + *error);
    for (ConfigEntry& entry : entries_)
    {
        if (entry.key == key)
        {
            entry.value = std::string(value);
            return {};
        }
    }
    if (entries_.size() >= entry_budget_)
        return std::unexpected("config override '" + std::string(key) +
            "' exceeds the entry budget of " + std::to_string(entry_budget_) + " entries");
    entries_.push_back(ConfigEntry{std::string(key), std::string(value)});
    return {};
}

std::expected<ConfigStore, std::string> LoadConfigFile(
    const std::filesystem::path& path, const ConfigLimits& limits)
{
    if (const auto error = LimitsError(limits))
        return std::unexpected(*error);
    if (limits.max_input_bytes >=
        static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max()))
        return std::unexpected("config input budget is too large for this process");

    std::ifstream stream(path, std::ios::binary);
    if (!stream)
        return std::unexpected("unable to open config file");
    std::string bytes(limits.max_input_bytes + 1U, '\0');
    stream.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (stream.bad())
        return std::unexpected("unable to read config file");
    const std::size_t read_bytes = static_cast<std::size_t>(stream.gcount());
    if (read_bytes > limits.max_input_bytes)
        return std::unexpected("config file exceeds the " +
            std::to_string(limits.max_input_bytes) + "-byte budget");
    bytes.resize(read_bytes);
    return ParseConfigText(bytes, limits);
}
} // namespace omega::runtime
