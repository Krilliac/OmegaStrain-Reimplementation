#include "omega/runtime/config_service.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>

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

std::expected<omega::runtime::ConfigStore, std::string> Parse(const std::string_view text)
{
    return omega::runtime::ParseConfigText(text);
}

bool WriteTextFile(const std::filesystem::path& path, const std::string_view text)
{
    std::ofstream output(path, std::ios::binary);
    if (!output)
        return false;
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    return output.good();
}
} // namespace

int ConfigServiceFailureCount()
{
    auto empty = Parse("");
    Check(empty.has_value() && empty->entry_count() == 0U,
        "empty text parses to an empty store");
    Check(empty && !empty->Contains("anything"),
        "an empty store contains nothing");

    auto store = Parse("# leading comment\r\n"
                       "\r\n"
                       "   \t \n"
                       "  window.width_px = 640 \r\n"
                       "render.vsync=true\n"
                       "net.motd = score=12 #not a comment\n"
                       "empty.value =\n"
                       "audio.gain = -3");
    Check(store.has_value(), "comments, blanks, CRLF, and entries parse together");
    if (store)
    {
        Check(store->entry_count() == 5U, "exactly the entry lines are stored");
        Check(store->GetString("window.width_px") == "640",
            "surrounding blanks are trimmed from keys and values");
        Check(store->GetString("net.motd") == "score=12 #not a comment",
            "later '=' and interior '#' bytes are literal value bytes");
        Check(store->GetString("empty.value") == "",
            "an empty value is stored as an empty string");
        auto vsync = store->GetBool("render.vsync");
        Check(vsync.has_value() && vsync->has_value() && **vsync,
            "GetBool accepts exactly 'true'");
        auto gain = store->GetInt64("audio.gain");
        Check(gain.has_value() && gain->has_value() && **gain == -3,
            "GetInt64 accepts a canonical negative value");

        auto absent_bool = store->GetBool("render.missing");
        Check(absent_bool.has_value() && !absent_bool->has_value(),
            "an absent bool is success-with-empty, not an error");
        auto absent_int = store->GetInt64("render.missing");
        Check(absent_int.has_value() && !absent_int->has_value(),
            "an absent int is success-with-empty, not an error");
        Check(!store->GetString("render.missing").has_value(),
            "an absent string is a plain empty optional");
        Check(!store->RequireString("render.missing"),
            "RequireString turns absence into an error");
        Check(!store->RequireBool("render.missing"),
            "RequireBool turns absence into an error");
        Check(!store->RequireInt64("render.missing"),
            "RequireInt64 turns absence into an error");
        auto required = store->RequireInt64("window.width_px");
        Check(required.has_value() && *required == 640,
            "RequireInt64 returns a present canonical value");
        auto empty_bool = store->GetBool("empty.value");
        Check(!empty_bool.has_value(),
            "a present empty value is invalid for bool, not absent");
    }

    auto opaque = Parse(std::string("raw.bytes = a") + "\xFF" + "\xFE" + "z");
    Check(opaque.has_value() && opaque->GetString("raw.bytes") &&
              opaque->GetString("raw.bytes")->size() == 4U,
        "non-UTF-8 bytes >= 0x20 pass through values as opaque bytes");

    Check(!Parse("just a key"), "a line without '=' is rejected");
    Check(!Parse("= value"), "an empty key is rejected");
    Check(!Parse("Window.Width = 1"), "uppercase key bytes are rejected");
    Check(!Parse("a b = 1"), "interior blanks in a key are rejected");
    Check(!Parse(".a = 1"), "a leading dot is rejected");
    Check(!Parse("a. = 1"), "a trailing dot is rejected");
    Check(!Parse("a..b = 1"), "an empty dotted segment is rejected");
    Check(!Parse("a=1\nb=2\na=3"), "duplicate keys are rejected strictly");
    Check(Parse(std::string(omega::runtime::kMaxConfigKeyBytes, 'k') + " = 1").has_value(),
        "a key at exactly the key budget is accepted");
    Check(!Parse(std::string(omega::runtime::kMaxConfigKeyBytes + 1U, 'k') + " = 1"),
        "a key one byte over the key budget is rejected");
    Check(Parse("v = " + std::string(omega::runtime::kMaxConfigValueBytes, 'x')).has_value(),
        "a value at exactly the value budget is accepted");
    Check(!Parse("v = " + std::string(omega::runtime::kMaxConfigValueBytes + 1U, 'x')),
        "a value one byte over the value budget is rejected");
    Check(!Parse(std::string("v = a\x01") + "b"), "control bytes in values are rejected");
    Check(!Parse("k = v\r"),
        "a bare CR on the EOF-terminated final line is a rejected value byte, not a "
        "terminator");
    Check(Parse("k = v\r\n").has_value(),
        "a CRLF-terminated final line still parses");
    Check(!Parse(std::string_view("v = a\0b", 7U)), "NUL bytes in values are rejected");

    auto second_line_error = Parse("ok = 1\nbroken line\n");
    Check(!second_line_error &&
              second_line_error.error().find("line 2") != std::string::npos,
        "parse errors carry the 1-based line number");

    constexpr omega::runtime::ConfigLimits kTiny{
        .max_input_bytes = 48U, .max_line_bytes = 16U, .max_entries = 2U};
    Check(omega::runtime::ParseConfigText("k = 0123456789", kTiny).has_value(),
        "a line at the line budget is accepted");
    Check(!omega::runtime::ParseConfigText("k = 0123456789abc", kTiny),
        "a line over the line budget is rejected");
    Check(!omega::runtime::ParseConfigText("# padded comment line", kTiny),
        "even a comment line must respect the line budget");
    Check(omega::runtime::ParseConfigText(std::string(48U, '#'), kTiny).error()
                  .find("line budget") != std::string::npos,
        "input at the byte budget still enforces the line budget");
    Check(!omega::runtime::ParseConfigText(std::string(49U, '#'), kTiny),
        "input over the byte budget is rejected");
    Check(omega::runtime::ParseConfigText("a = 1\nb = 2", kTiny).has_value(),
        "an entry count at the budget is accepted");
    Check(!omega::runtime::ParseConfigText("a = 1\nb = 2\nc = 3", kTiny),
        "an entry count over the budget is rejected");
    constexpr omega::runtime::ConfigLimits kZero{
        .max_input_bytes = 0U, .max_line_bytes = 16U, .max_entries = 2U};
    Check(!omega::runtime::ParseConfigText("", kZero), "zero budgets are rejected");

    auto integers = Parse("zero = 0\n"
                          "max = 9223372036854775807\n"
                          "min = -9223372036854775808\n"
                          "pad = 01\n"
                          "plus = +1\n"
                          "negzero = -0\n"
                          "over = 9223372036854775808\n"
                          "under = -9223372036854775809\n"
                          "mixed = 1x\n"
                          "boolish = truex");
    Check(integers.has_value(), "integer strictness fixtures parse as strings");
    if (integers)
    {
        auto zero = integers->GetInt64("zero");
        Check(zero.has_value() && zero->has_value() && **zero == 0,
            "GetInt64 accepts a bare zero");
        auto max_value = integers->GetInt64("max");
        Check(max_value.has_value() && max_value->has_value() &&
                  **max_value == std::numeric_limits<std::int64_t>::max(),
            "GetInt64 accepts int64 max");
        auto min_value = integers->GetInt64("min");
        Check(min_value.has_value() && min_value->has_value() &&
                  **min_value == std::numeric_limits<std::int64_t>::min(),
            "GetInt64 accepts int64 min");
        Check(!integers->GetInt64("pad").has_value(),
            "leading zeros are rejected as non-canonical");
        Check(!integers->GetInt64("plus").has_value(), "a '+' sign is rejected");
        Check(!integers->GetInt64("negzero").has_value(), "'-0' is rejected");
        Check(!integers->GetInt64("over").has_value(),
            "one past int64 max is rejected as out of range");
        Check(!integers->GetInt64("under").has_value(),
            "one past int64 min is rejected as out of range");
        Check(!integers->GetInt64("mixed").has_value(),
            "trailing non-digit bytes are rejected");
        Check(!integers->GetBool("boolish").has_value(),
            "'truex' is rejected by the exact bool match");
        Check(!integers->RequireInt64("pad").has_value(),
            "Require keeps present-but-invalid distinct from absent");
        Check(integers->GetString("pad") == "01",
            "an invalid int is still readable as its raw string");
    }

    auto overridable = omega::runtime::ParseConfigText("a = 1", kTiny);
    Check(overridable.has_value(), "override fixture parses");
    if (overridable)
    {
        Check(overridable->entry_budget() == 2U,
            "the store preserves its entry budget");
        Check(overridable->ApplyOverride("a", "2").has_value() &&
                  overridable->GetString("a") == "2" && overridable->entry_count() == 1U,
            "an override replaces a parsed value without growing the store");
        Check(overridable->ApplyOverride("b.new", "true").has_value() &&
                  overridable->entry_count() == 2U,
            "an override may add a new validated key");
        auto added = overridable->GetBool("b.new");
        Check(added.has_value() && added->has_value() && **added,
            "typed getters observe overridden values");
        Check(!overridable->ApplyOverride("c", "3"),
            "an added override beyond the entry budget is rejected");
        Check(overridable->ApplyOverride("a", "3").has_value(),
            "replacement overrides still work at the entry budget");
        Check(!overridable->ApplyOverride("Bad", "1"),
            "override keys face the same grammar as parsed keys");
        Check(!overridable->ApplyOverride("d..e", "1"),
            "override keys reject empty dotted segments");
        Check(!overridable->ApplyOverride(
                  "a", std::string(omega::runtime::kMaxConfigValueBytes + 1U, 'x')),
            "override values face the value budget");
        Check(!overridable->ApplyOverride("a", std::string("x\ny")),
            "override values reject control bytes");
        Check(overridable->entry_count() == 2U && overridable->GetString("a") == "3",
            "rejected overrides leave the store unchanged");
        Check(overridable->ApplyOverride("a", " 5 ").has_value() &&
                  overridable->GetString("a") == "5",
            "override values are SP/HTAB trimmed exactly like parsed values");
        auto trimmed = overridable->GetInt64("a");
        Check(trimmed.has_value() && trimmed->has_value() && **trimmed == 5,
            "typed getters accept a blank-padded override after trimming");
        Check(overridable->ApplyOverride("a",
                  " " + std::string(omega::runtime::kMaxConfigValueBytes, 'x') + " ")
                  .has_value(),
            "the override value budget applies after trimming, matching the parser");
        Check(overridable->ApplyOverride(" a ", "6").has_value() &&
                  overridable->GetString("a") == "6" && overridable->entry_count() == 2U,
            "override keys are trimmed before validation and match existing entries");
    }

    const auto unique_suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path() /
                      ("omega-config-tests-" + std::to_string(unique_suffix));
    std::error_code file_error;
    std::filesystem::remove_all(root, file_error);
    std::filesystem::create_directories(root, file_error);
    Check(!file_error, "temporary config test directory is created");

    const auto config_path = root / "SHELL.CFG";
    Check(WriteTextFile(config_path, "window.width_px = 640\n# tail comment\n"),
        "config fixture file is written");
    auto loaded = omega::runtime::LoadConfigFile(config_path, omega::runtime::ConfigLimits{});
    Check(loaded.has_value() && loaded->RequireInt64("window.width_px") == 640,
        "the bounded loader delegates to the pure parser");

    constexpr omega::runtime::ConfigLimits kFileLimits{
        .max_input_bytes = 8U, .max_line_bytes = 8U, .max_entries = 2U};
    const auto exact_path = root / "EXACT.CFG";
    Check(WriteTextFile(exact_path, "a = 42\n"),
        "exact-budget fixture file is written");
    Check(omega::runtime::LoadConfigFile(exact_path, kFileLimits).has_value(),
        "a file within the byte budget loads");
    const auto oversize_path = root / "OVERSIZE.CFG";
    Check(WriteTextFile(oversize_path, "a = 4200\n"),
        "oversize fixture file is written");
    auto oversize = omega::runtime::LoadConfigFile(oversize_path, kFileLimits);
    Check(!oversize && oversize.error().find("budget") != std::string::npos,
        "a file over the byte budget is rejected by the bounded read");
    Check(!omega::runtime::LoadConfigFile(root / "MISSING.CFG", kFileLimits),
        "a missing config file is a clear error");

    std::filesystem::remove_all(root, file_error);
    Check(!file_error, "temporary config test directory is removed");
    return failures;
}
