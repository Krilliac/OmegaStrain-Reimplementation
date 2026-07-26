#include "omega/content/retail_level_table.h"

#include <array>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <optional>
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
} // namespace

int main()
{
    using omega::content::FindRetailLevelIndex;
    using omega::content::IsRetailLevelCode;
    using omega::content::kMaxRetailLevelCodeLength;
    using omega::content::kRetailLevelCount;
    using omega::content::RetailLevelCodeAt;
    using omega::content::RetailLevelCodes;

    const auto codes = RetailLevelCodes();

    Check(kRetailLevelCount == 18U && codes.size() == kRetailLevelCount,
          "the decoded table exposes its full eighteen-entry storage extent");
    // The single worked example carried over from the argument-loader note.
    Check(RetailLevelCodeAt(9U) == "MINSK",
          "index 9 is MINSK, reproducing the recorded reference worked example");
    Check(FindRetailLevelIndex("MINSK") == std::optional<std::size_t>{9U},
          "MINSK resolves back to index 9");

    // Round-trip: every index maps to a name that maps back to the same index.
    bool round_trip = true;
    for (std::size_t index = 0U; index < kRetailLevelCount; ++index)
    {
        const std::string_view code = RetailLevelCodeAt(index);
        if (code.empty() || FindRetailLevelIndex(code) != std::optional<std::size_t>{index})
            round_trip = false;
    }
    Check(round_trip, "index-to-name and name-to-index are exact inverses");

    // Entries are unique, non-empty, uppercase ASCII alphanumeric, and bounded.
    bool well_formed = true;
    bool unique = true;
    for (std::size_t index = 0U; index < codes.size(); ++index)
    {
        const std::string_view code = codes[index];
        if (code.empty() || code.size() > kMaxRetailLevelCodeLength)
            well_formed = false;
        for (const char character : code)
        {
            const bool upper = character >= 'A' && character <= 'Z';
            const bool digit = character >= '0' && character <= '9';
            if (!upper && !digit)
                well_formed = false;
        }
        for (std::size_t other = index + 1U; other < codes.size(); ++other)
        {
            if (codes[other] == code)
                unique = false;
        }
    }
    Check(well_formed, "every entry is bounded uppercase ASCII alphanumeric");
    Check(unique, "no entry is duplicated");

    // Lookup ignores ASCII case so callers need not allocate an uppercase copy.
    Check(FindRetailLevelIndex("minsk") == std::optional<std::size_t>{9U} &&
              FindRetailLevelIndex("MiNsK") == std::optional<std::size_t>{9U} &&
              IsRetailLevelCode("training") && IsRetailLevelCode("TRAINING"),
          "lookup is ASCII case-insensitive in both directions");

    // Synthetic non-members must be rejected. None of these is a decoded name.
    constexpr std::array<std::string_view, 10> kSyntheticRejects{
        "TESTLVL",  // the historical synthetic test code
        "LEVEL7",   // synthetic placeholder
        "MINSK2",   // real prefix, wrong code
        "MINS",     // truncated prefix of a real code
        "MINSKX",   // real code with a trailing byte
        "A",        // one byte, previously accepted by length-only validation
        "BAD-LEVEL",// non-alphanumeric
        "../MINSK", // traversal shaped
        " MINSK",   // leading space
        "MINSK ",   // trailing space
    };
    bool rejects_synthetic = true;
    for (const std::string_view candidate : kSyntheticRejects)
    {
        if (IsRetailLevelCode(candidate) || FindRetailLevelIndex(candidate))
            rejects_synthetic = false;
    }
    Check(rejects_synthetic, "synthetic non-member codes are rejected");

    Check(!IsRetailLevelCode(std::string_view{}) && !FindRetailLevelIndex(""),
          "an empty code is rejected");

    const std::string overlong(kMaxRetailLevelCodeLength + 1U, 'A');
    Check(!IsRetailLevelCode(overlong),
          "a code longer than the longest entry is rejected without scanning");

    const std::string very_long(4096U, 'M');
    Check(!IsRetailLevelCode(very_long), "a pathologically long code is rejected");

    // Out-of-range index access is total and allocation-free.
    Check(RetailLevelCodeAt(kRetailLevelCount).empty() &&
              RetailLevelCodeAt(kRetailLevelCount + 1U).empty() &&
              RetailLevelCodeAt(static_cast<std::size_t>(-1)).empty(),
          "an out-of-range index yields an empty view rather than reading past the table");

    // The span refers to stable static storage, not a per-call temporary.
    Check(RetailLevelCodes().data() == codes.data(),
          "the exposed table is immutable static storage");

    if (failures != 0)
    {
        std::cerr << failures << " retail level table test(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "retail level table tests passed\n";
    return EXIT_SUCCESS;
}
