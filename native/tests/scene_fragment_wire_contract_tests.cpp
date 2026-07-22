#include <array>
#include <bit>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace
{
using Bytes = std::vector<std::uint8_t>;

void AppendU8(Bytes& output, const std::uint8_t value)
{
    output.push_back(value);
}

void AppendU32(Bytes& output, const std::uint32_t value)
{
    output.push_back(static_cast<std::uint8_t>(value));
    output.push_back(static_cast<std::uint8_t>(value >> 8U));
    output.push_back(static_cast<std::uint8_t>(value >> 16U));
    output.push_back(static_cast<std::uint8_t>(value >> 24U));
}

void AppendU64(Bytes& output, const std::uint64_t value)
{
    AppendU32(output, static_cast<std::uint32_t>(value));
    AppendU32(output, static_cast<std::uint32_t>(value >> 32U));
}

void AppendF32(Bytes& output, const float value)
{
    AppendU32(output, std::bit_cast<std::uint32_t>(value));
}

void AppendF64(Bytes& output, const double value)
{
    AppendU64(output, std::bit_cast<std::uint64_t>(value));
}

void AppendRange(
    Bytes& output, const bool present, const double minimum, const double maximum)
{
    AppendU8(output, present ? 1U : 0U);
    AppendF64(output, minimum);
    AppendF64(output, maximum);
}

std::string Hex(const Bytes& bytes)
{
    constexpr std::string_view digits = "0123456789abcdef";
    std::string output;
    output.reserve(bytes.size() * 2U + 1U);
    for (const std::uint8_t value : bytes)
    {
        output.push_back(digits[value >> 4U]);
        output.push_back(digits[value & 0x0fU]);
    }
    output.push_back('\n');
    return output;
}

Bytes BuildSyntheticProducerFragment()
{
    constexpr std::string_view magic = "OMEGASCNPART0001";
    constexpr std::array<std::uint8_t, 32> configuration_digest{
        0x61U, 0xc1U, 0x17U, 0x8bU, 0xaaU, 0xffU, 0x42U, 0xfcU,
        0x5bU, 0x05U, 0xb7U, 0x27U, 0x69U, 0x72U, 0xceU, 0x1fU,
        0x1bU, 0xb8U, 0x91U, 0xfaU, 0xcfU, 0xcfU, 0x72U, 0x2fU,
        0x88U, 0x4dU, 0x0cU, 0xe6U, 0x7fU, 0x36U, 0x1bU, 0x98U,
    };
    constexpr std::array<std::uint32_t, 12> counts{
        1U, 1U, 1U, 1U, 16U, 1U, 1U, 1U, 16U, 1U, 1U, 1U,
    };

    Bytes output;
    output.reserve(670U);
    output.insert(output.end(), magic.begin(), magic.end());
    AppendU32(output, 1U);
    output.insert(output.end(), configuration_digest.begin(), configuration_digest.end());
    for (const std::uint32_t count : counts)
        AppendU32(output, count);

    AppendU32(output, 1U); // frame ID
    AppendU32(output, 1U); // span ID

    AppendU32(output, 1U); // EE site ID
    AppendU32(output, 1U); // EE site span
    AppendU32(output, 4U); // EE site width

    AppendU32(output, 1U); // record ID
    AppendU32(output, 1U); // record span
    AppendU32(output, 8U); // record size

    for (std::uint32_t component = 1U; component <= 16U; ++component)
        AppendU32(output, component);
    AppendU32(output, 1U); // VIF destination extent ID

    AppendU32(output, 1U); // INL frame
    AppendU32(output, 1U); // INL site
    AppendU32(output, 1U); // INL record
    AppendU32(output, 0U); // INL record-relative offset
    AppendU32(output, 4U); // INL width
    AppendU64(output, 1U); // INL occurrences

    AppendU32(output, 1U); // VIF frame
    AppendU32(output, 1U); // VIF record
    AppendU32(output, 0U); // VIF source-relative offset
    AppendU32(output, 1U); // VIF source words
    AppendU32(output, 1U); // VIF remaining output elements
    AppendU32(output, 0U); // VIF buffered prefix
    AppendU32(output, 1U); // VIF destination extent
    AppendU8(output, 0U);  // VIF mode
    AppendU32(output, 1U); // VIF CL
    AppendU32(output, 1U); // VIF WL
    AppendU8(output, 0U);  // VIF contiguous cycle class
    AppendU8(output, 4U);  // VIF components
    AppendU8(output, 32U); // VIF component width
    AppendU8(output, 0U);  // VIF masked
    AppendU8(output, 0U);  // VIF unsigned
    AppendU64(output, 1U); // VIF occurrences

    for (std::uint32_t component = 1U; component <= 16U; ++component)
    {
        AppendU32(output, 1U);
        AppendU32(output, component);
        const bool diagonal = component == 1U || component == 6U ||
                              component == 11U || component == 16U;
        AppendF32(output, diagonal ? 1.0F : 0.0F);
    }

    AppendU32(output, 1U); // GS state ID

    AppendU32(output, 1U); // draw ID
    AppendU32(output, 1U); // draw frame
    AppendU32(output, 1U); // draw state
    AppendU8(output, 0U);  // submitted
    AppendU64(output, 3U); // index count
    AppendU64(output, 3U); // unique references
    constexpr std::array<std::uint64_t, 7> primitive_counts{0U, 0U, 0U, 1U, 0U, 0U, 0U};
    for (const std::uint64_t count : primitive_counts)
        AppendU64(output, count);
    AppendU8(output, 0U); // no texture-coordinate class
    AppendRange(output, true, -1.0, 1.0);
    AppendRange(output, true, -1.0, 1.0);
    AppendRange(output, true, 0.0, 1.0);
    AppendRange(output, false, 0.0, 0.0);
    AppendRange(output, false, 0.0, 0.0);
    AppendRange(output, false, 0.0, 0.0);

    AppendU32(output, 1U); // edge record
    AppendU32(output, 1U); // edge draw
    return output;
}
} // namespace

int main(const int argc, const char* const argv[])
{
    if (argc != 2)
    {
        std::cerr << "scene fragment wire contract: FAILED\n";
        return 1;
    }
    std::ifstream fixture(argv[1], std::ios::binary);
    if (!fixture)
    {
        std::cerr << "scene fragment wire contract: FAILED\n";
        return 1;
    }
    const std::string expected{
        std::istreambuf_iterator<char>(fixture), std::istreambuf_iterator<char>()};
    std::string normalized_expected;
    normalized_expected.reserve(expected.size());
    for (const char value : expected)
    {
        if (value != '\r' && value != '\n')
            normalized_expected.push_back(value);
    }
    normalized_expected.push_back('\n');
    const Bytes fragment = BuildSyntheticProducerFragment();
    if (fragment.size() != 670U || normalized_expected.size() != 1341U ||
        Hex(fragment) != normalized_expected)
    {
        std::cerr << "scene fragment wire contract: FAILED\n";
        return 1;
    }
    std::cout << "scene fragment wire contract: OK\n";
    return 0;
}
