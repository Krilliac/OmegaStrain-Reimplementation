#include "omega/asset/pop_terrain_index.h"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <vector>

namespace
{
void AppendU32(std::vector<std::byte>& bytes, const std::uint32_t value)
{
    for (unsigned shift = 0; shift < 32; shift += 8)
        bytes.push_back(static_cast<std::byte>((value >> shift) & 0xFFU));
}

void AppendText(std::vector<std::byte>& bytes, const std::string_view value)
{
    for (const char character : value)
        bytes.push_back(static_cast<std::byte>(character));
}

void AppendRecord(std::vector<std::byte>& bytes, const std::uint32_t kind,
    const std::uint32_t index, const std::string_view name, const std::byte alignment_fill)
{
    AppendU32(bytes, kind);
    AppendU32(bytes, index);
    AppendText(bytes, name);
    bytes.push_back(std::byte{0});
    while (bytes.size() % 4U != 0)
        bytes.push_back(alignment_fill);
}

std::vector<std::byte> MakePop()
{
    std::vector<std::byte> bytes;
    AppendU32(bytes, 70);
    AppendText(bytes, "TER:");
    AppendU32(bytes, 2);
    AppendRecord(bytes, 4, 10, "CELL_A.HOG", std::byte{0});
    AppendRecord(bytes, 9, 22, "B", std::byte{0x7F});
    AppendText(bytes, "GOB:");
    AppendU32(bytes, 0x12345678);
    return bytes;
}

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

int PopTerrainIndexFailureCount()
{
    const auto complete = MakePop();
    bool all_short_prefixes_rejected = true;
    for (std::size_t size = 0; size < complete.size() - 4U; ++size)
    {
        if (omega::asset::PopTerrainIndex::Parse(
                std::span<const std::byte>(complete).first(size)))
            all_short_prefixes_rejected = false;
    }
    Check(all_short_prefixes_rejected, "truncated POP prefixes are rejected safely");

    auto parsed = omega::asset::PopTerrainIndex::Parse(complete);
    Check(parsed.has_value(), "valid observed POP terrain prefix parses");
    if (parsed)
    {
        Check(parsed->records().size() == 2, "declared record count is preserved");
        Check(parsed->records()[0].kind == 4 && parsed->records()[0].index == 10,
            "record numeric fields are preserved without semantic invention");
        Check(parsed->records()[0].name == "CELL_A.HOG", "terrain name is preserved");
        Check(parsed->records()[1].name == "B", "second terrain name is preserved");
        Check(parsed->nonzero_alignment_record_count() == 1,
            "nonzero alignment bytes are skipped and counted");
        Check(parsed->next_section_offset() + 4U <= complete.size(),
            "next section offset remains inside the input");
    }

    auto bad_header = MakePop();
    bad_header[0] = std::byte{69};
    Check(!omega::asset::PopTerrainIndex::Parse(bad_header), "unknown header word is rejected");

    auto bad_tag = MakePop();
    bad_tag[4] = std::byte{'X'};
    Check(!omega::asset::PopTerrainIndex::Parse(bad_tag), "unknown leading tag is rejected");

    auto excessive_count = MakePop();
    excessive_count[8] = std::byte{1};
    excessive_count[9] = std::byte{0};
    excessive_count[10] = std::byte{16};
    excessive_count[11] = std::byte{0};
    Check(!omega::asset::PopTerrainIndex::Parse(excessive_count),
        "excessive record count is rejected before allocation");

    auto empty_name = MakePop();
    empty_name[20] = std::byte{0};
    Check(!omega::asset::PopTerrainIndex::Parse(empty_name), "empty terrain name is rejected");

    auto non_ascii_name = MakePop();
    non_ascii_name[20] = std::byte{0x1F};
    Check(!omega::asset::PopTerrainIndex::Parse(non_ascii_name),
        "non-printable terrain name is rejected");

    auto bad_next_tag = MakePop();
    if (parsed)
        bad_next_tag[parsed->next_section_offset()] = std::byte{'X'};
    Check(!omega::asset::PopTerrainIndex::Parse(bad_next_tag),
        "missing observed GOB boundary is rejected");

    return failures;
}
