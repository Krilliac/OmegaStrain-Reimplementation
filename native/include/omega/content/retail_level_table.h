#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

// Decoded level-code table recovered from the owned reference executable.
//
// PROVEN (deterministic pointer-chasing over the reference image; see
// analysis/elf/level-name-table.md for the reproducible method):
//   * A table of 32-bit pointers begins at reference VA 0x0048C160 with stride
//     four. Consumers construct that base as a LUI/ADDIU pair, scale a
//     bounds-checked index by four, add the base, and load the entry.
//   * Eighteen consecutive entries (indices 0 through 17) hold non-null
//     pointers to NUL-terminated ASCII names. The two words that follow are
//     zero, so eighteen is the table's storage extent, not a scan cutoff.
//   * Index 9 is MINSK, at entry address 0x0048C184, which reproduces the
//     independently recorded worked example in analysis/elf/argument-loader.md.
//   * All eighteen decoded names correspond one-to-one with the GAMEDATA
//     subdirectories of the owned disc. There is no name in the table without a
//     directory and no level directory without a table entry.
//   * Consumers disagree about the upper bound. Four bound the index below 17
//     and therefore never reach the final entry; two bound it below 18 and do
//     reach it. Both bound values were read from the compare instruction that
//     guards the same register subsequently used as the table index.
//
// NOT PROVEN (explicitly unproven; do not promote these to fact):
//   * That this order is the mission-presentation order used by the Command
//     Center or any other menu. The argument loader's use of the table is not
//     the menu's use. Nothing observed here establishes a presentation
//     sequence, an unlock order, or a chapter grouping.
//   * Why two consumers admit the final entry and four do not. Reading the
//     final entry as a non-campaign level is an inference from its position and
//     its name, not a decoded fact.
//   * Any classification of individual entries as campaign, bonus, training,
//     or multiplayer content. No such attribute was decoded.
//   * That the reference table is the only level enumeration in the image.
//     Neighboring tables at 0x0048C100, 0x0048C148, 0x0048C1B0, and 0x0048C200
//     are deliberately excluded and must not be conflated with this one.
//   * Any behavior of the reference title's own runtime. This table records
//     what the image stores, not how a retail build reacts to an unknown code.
//
// The names below are structural directory identifiers that already appear in
// this repository's own disc manifests. No other reference data is reproduced.

namespace omega::content
{
// Storage extent of the reference table: eighteen non-null pointer entries.
inline constexpr std::size_t kRetailLevelCount = 18U;

// The narrower bound applied by four of the six inspected reference consumers.
// It admits indices [0, kRetailLevelBoundedConsumerCount) and so excludes
// exactly the final entry. Recorded because the difference is real and
// load-bearing; the reason for it is an open question, not a decoded fact.
inline constexpr std::size_t kRetailLevelBoundedConsumerCount = 17U;

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

// Whether an index is admitted by the narrower reference consumer bound. This
// reports the observed bound only and assigns it no gameplay meaning.
[[nodiscard]] bool IsBoundedConsumerLevelIndex(std::size_t index) noexcept;
} // namespace omega::content
