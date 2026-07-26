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
// Reference table order from the sanitized evidence in
// analysis/elf/level-name-table.md. No reference address or instruction-level
// detail enters the native implementation.
constexpr std::array<std::string_view, kRetailLevelCount> kRetailLevelCodes{
    "TORONTO1",
    "TORONTO2",
    "TORONTO3",
    "ITALY",
    "BELARUS1",
    "BELARUS2",
    "KYRGSTAN",
    "YEMEN1",
    "YEMEN2",
    "MINSK",
    "CHECHNYA",
    "LORELEI",
    "TOKYO",
    "MYANMAR",
    "ZURICH",
    "MNTNEGR1",
    "UKRAINE",
    "TRAINING",
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

} // namespace omega::content
