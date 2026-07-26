#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

// Decoded level-code table recovered from the owned reference executable.
// The sanitized layout evidence and reproducible method are recorded in
// analysis/elf/level-name-table.md.
//
// PROVEN:
//   * The observed table has eighteen consecutive entries containing
//     NUL-terminated ASCII names.
//   * Index 9 is MINSK, reproducing the independently recorded worked example
//     in analysis/elf/argument-loader.md.
//   * Every decoded name appears as a GAMEDATA directory name in the tracked
//     public manifest. That structural set observation assigns no role to
//     manifest directory names absent from the table.
//
// NOT PROVEN (explicitly unproven; do not promote these to fact):
//   * That this order is the mission-presentation order used by the Command
//     Center or any other menu. The argument loader's use of the table is not
//     the menu's use. Nothing observed here establishes a presentation
//     sequence, an unlock order, or a chapter grouping.
//   * Any classification of individual entries as campaign, bonus, training,
//     or multiplayer content. No such attribute was decoded.
//   * That the reference table is the only level enumeration in the image.
//   * Any behavior of the reference title's own runtime. This table records
//     what the image stores, not how a retail build reacts to an unknown code.
//
// The names below are structural directory identifiers that already appear in
// this repository's own disc manifests. No other reference data is reproduced.

namespace omega::content
{
// Storage extent of the reference table: eighteen non-null pointer entries.
inline constexpr std::size_t kRetailLevelCount = 18U;

// Longest decoded name length in bytes. Every entry is 5 to 8 ASCII bytes.
inline constexpr std::size_t kMaxRetailLevelCodeLength = 8U;

// The decoded names in reference table order (index 0 first). The returned span
// refers to immutable static storage and stays valid for the program lifetime.
[[nodiscard]] std::span<const std::string_view> RetailLevelCodes() noexcept;

// Canonical uppercase name for a table index, or an empty view when the index
// is outside [0, kRetailLevelCount).
[[nodiscard]] std::string_view RetailLevelCodeAt(std::size_t index) noexcept;

// Table index for a level code, compared without ASCII case sensitivity so that
// callers need not allocate an uppercase copy first. Returns no value when the
// code is absent from the table.
[[nodiscard]] std::optional<std::size_t> FindRetailLevelIndex(
    std::string_view code) noexcept;

// Whether a code names a table entry, compared as in FindRetailLevelIndex.
[[nodiscard]] bool IsRetailLevelCode(std::string_view code) noexcept;

} // namespace omega::content
