#include "omega/content/retail_level_table.h"

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

namespace omega::content
{
namespace
{
// Reference table order, recovered by chasing the eighteen pointers stored from
// VA 0x0048C160 with stride four. The comment beside each entry is the entry
// address, which is the only provenance carried into the native tree; it is not
// an implementation address and nothing here indexes memory by it.
constexpr std::array<std::string_view, kRetailLevelCount> kRetailLevelCodes{
    "TORONTO1", // 0x0048C160
    "TORONTO2", // 0x0048C164
    "TORONTO3", // 0x0048C168
    "ITALY",    // 0x0048C16C
    "BELARUS1", // 0x0048C170
    "BELARUS2", // 0x0048C174
    "KYRGSTAN", // 0x0048C178
    "YEMEN1",   // 0x0048C17C
    "YEMEN2",   // 0x0048C180
    "MINSK",    // 0x0048C184  worked example from analysis/elf/argument-loader.md
    "CHECHNYA", // 0x0048C188
    "LORELEI",  // 0x0048C18C
    "TOKYO",    // 0x0048C190
    "MYANMAR",  // 0x0048C194
    "ZURICH",   // 0x0048C198
    "MNTNEGR1", // 0x0048C19C
    "UKRAINE",  // 0x0048C1A0
    "TRAINING", // 0x0048C1A4  admitted only by the wider consumer bound
};

[[nodiscard]] constexpr char ToUpperAscii(const char value) noexcept
{
    return (value >= 'a' && value <= 'z')
               ? static_cast<char>(value - ('a' - 'A'))
               : value;
}

[[nodiscard]] constexpr bool EqualsAsciiCaseInsensitive(
    const std::string_view left, const std::string_view right) noexcept
{
    if (left.size() != right.size())
        return false;
    for (std::size_t index = 0U; index < left.size(); ++index)
    {
        if (ToUpperAscii(left[index]) != ToUpperAscii(right[index]))
            return false;
    }
    return true;
}
} // namespace

std::span<const std::string_view> RetailLevelCodes() noexcept
{
    return std::span<const std::string_view>{kRetailLevelCodes};
}

std::string_view RetailLevelCodeAt(const std::size_t index) noexcept
{
    if (index >= kRetailLevelCount)
        return std::string_view{};
    return kRetailLevelCodes[index];
}

std::optional<std::size_t> FindRetailLevelIndex(const std::string_view code) noexcept
{
    // A length pre-check keeps the common rejection path from scanning at all.
    if (code.empty() || code.size() > kMaxRetailLevelCodeLength)
        return std::nullopt;
    for (std::size_t index = 0U; index < kRetailLevelCount; ++index)
    {
        if (EqualsAsciiCaseInsensitive(code, kRetailLevelCodes[index]))
            return index;
    }
    return std::nullopt;
}

bool IsRetailLevelCode(const std::string_view code) noexcept
{
    return FindRetailLevelIndex(code).has_value();
}

bool IsBoundedConsumerLevelIndex(const std::size_t index) noexcept
{
    return index < kRetailLevelBoundedConsumerCount;
}
} // namespace omega::content
