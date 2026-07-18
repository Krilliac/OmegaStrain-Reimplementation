#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace omega::runtime
{
// Project-owned strict configuration text. This grammar is a synthetic-shell choice for the
// native runtime; it makes no claim about any retail configuration format.
//
//   config   = *line
//   line     = raw-bytes terminated by LF, CRLF, or end of input
//   blank    = only SP / HTAB bytes; ignored
//   comment  = first non-blank byte is '#'; the whole line is ignored
//   entry    = key blank* "=" blank* value
//   key      = segment *("." segment), 1 to kMaxConfigKeyBytes bytes total
//   segment  = 1*( "a"-"z" / "0"-"9" / "_" )
//   value    = 0 to kMaxConfigValueBytes bytes, each HTAB or 0x20-0xFF, with leading and
//              trailing SP/HTAB trimmed
//
// The first '=' on an entry line splits key from value; later '=' bytes are literal value
// bytes. '#' is a comment marker only as the first non-blank byte of a line; inside a value
// it is a literal byte. Values are opaque byte strings: bytes >= 0x20 pass through without
// UTF-8 validation (documented synthetic-shell choice), while NUL and all other control
// bytes are rejected. Empty values are legal and distinct from absent keys. Duplicate keys
// are rejected, matching the strict duplicate-flag launch policy. Every budget violation is
// a hard parse error, never a silent truncation.

// Synthetic-shell bounds for one key and one value, applied after trimming.
inline constexpr std::size_t kMaxConfigKeyBytes = 64U;
inline constexpr std::size_t kMaxConfigValueBytes = 256U;

// Hard parse budgets. The defaults are synthetic-shell choices sized generously above any
// expected runtime configuration; callers may tighten them but a zero budget is rejected.
struct ConfigLimits
{
    std::size_t max_input_bytes = 64U * 1024U;
    std::size_t max_line_bytes = 512U;
    std::size_t max_entries = 256U;
};

struct ConfigEntry
{
    std::string key;
    std::string value;
};

class ConfigStore;

// [any thread; reentrant] Parses configuration text under the given budgets. Pure: no
// filesystem access occurs here. Errors carry the 1-based line number where applicable.
[[nodiscard]] std::expected<ConfigStore, std::string> ParseConfigText(
    std::string_view text, const ConfigLimits& limits = ConfigLimits{});

// Parsed settings plus an explicit override channel. The parsed entries are immutable;
// ApplyOverride is the only mutation and exists for future --set command-line wiring.
class ConfigStore
{
public:
    // [any thread; reentrant] Number of stored entries, including applied overrides.
    [[nodiscard]] std::size_t entry_count() const noexcept;

    // [any thread; reentrant] Entry budget inherited from the parse limits.
    [[nodiscard]] std::size_t entry_budget() const noexcept;

    // [any thread; reentrant] Entries in first-seen order. The span is invalidated by
    // ApplyOverride.
    [[nodiscard]] std::span<const ConfigEntry> entries() const noexcept;

    // [any thread; reentrant] True when the key is present, regardless of value shape.
    [[nodiscard]] bool Contains(std::string_view key) const noexcept;

    // [any thread; reentrant] Absent keys return std::nullopt; a present key always has a
    // valid string value. The view is invalidated by ApplyOverride.
    [[nodiscard]] std::optional<std::string_view> GetString(std::string_view key) const noexcept;

    // [any thread; reentrant] Absent keys succeed with an empty optional; a present value
    // that is not exactly "true" or "false" is an error.
    [[nodiscard]] std::expected<std::optional<bool>, std::string> GetBool(
        std::string_view key) const;

    // [any thread; reentrant] Absent keys succeed with an empty optional; a present value
    // must be a full-match canonical base-10 int64 (optional '-', no '+', no leading zeros,
    // no "-0") within range, otherwise an error.
    [[nodiscard]] std::expected<std::optional<std::int64_t>, std::string> GetInt64(
        std::string_view key) const;

    // [any thread; reentrant] Like GetString, but an absent key is an error.
    [[nodiscard]] std::expected<std::string, std::string> RequireString(
        std::string_view key) const;

    // [any thread; reentrant] Like GetBool, but an absent key is an error.
    [[nodiscard]] std::expected<bool, std::string> RequireBool(std::string_view key) const;

    // [any thread; reentrant] Like GetInt64, but an absent key is an error.
    [[nodiscard]] std::expected<std::int64_t, std::string> RequireInt64(
        std::string_view key) const;

    // [game thread] Replaces or adds one entry after validating key and value with the same
    // rules as parsed text, including SP/HTAB trimming before validation, budgets, and
    // storage. New keys count against the entry budget; replacements do not grow it.
    // Concurrent readers are not synchronized against this mutation.
    [[nodiscard]] std::expected<void, std::string> ApplyOverride(
        std::string_view key, std::string_view value);

private:
    friend std::expected<ConfigStore, std::string> ParseConfigText(
        std::string_view text, const ConfigLimits& limits);

    ConfigStore(std::vector<ConfigEntry> entries, std::size_t entry_budget);

    [[nodiscard]] const ConfigEntry* Find(std::string_view key) const noexcept;

    std::vector<ConfigEntry> entries_;
    std::size_t entry_budget_ = 0;
};

// [game thread] Bounded file loader: reads at most limits.max_input_bytes + 1 bytes so an
// oversize file is rejected without an unbounded read, then delegates to ParseConfigText.
[[nodiscard]] std::expected<ConfigStore, std::string> LoadConfigFile(
    const std::filesystem::path& path, const ConfigLimits& limits = ConfigLimits{});
} // namespace omega::runtime
