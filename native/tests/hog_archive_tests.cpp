#include "omega/archive/hog_archive.h"

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

void AppendString(std::vector<std::byte>& bytes, const std::string_view value)
{
    for (const char character : value)
        bytes.push_back(static_cast<std::byte>(character));
    bytes.push_back(std::byte{0});
}

std::vector<std::byte> MakeArchive()
{
    std::vector<std::byte> bytes;
    AppendU32(bytes, 0x4052673D);
    AppendU32(bytes, 2);
    AppendU32(bytes, 0x14);
    AppendU32(bytes, 0x20);
    AppendU32(bytes, 0x30);
    AppendU32(bytes, 0);
    AppendU32(bytes, 3);
    AppendU32(bytes, 7);
    AppendString(bytes, "A.BIN");
    AppendString(bytes, "B.SO");
    bytes.resize(0x30, std::byte{0});
    bytes.push_back(std::byte{'a'});
    bytes.push_back(std::byte{'b'});
    bytes.push_back(std::byte{'c'});
    bytes.push_back(std::byte{'D'});
    bytes.push_back(std::byte{'E'});
    bytes.push_back(std::byte{'F'});
    bytes.push_back(std::byte{'G'});
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

int VirtualFileSystemFailureCount();

int main()
{
    const auto complete_archive = MakeArchive();
    bool all_truncations_rejected = true;
    for (std::size_t size = 0; size < complete_archive.size(); ++size)
    {
        std::vector<std::byte> truncated(complete_archive.begin(), complete_archive.begin() + size);
        if (omega::archive::HogArchive::FromBytes(std::move(truncated)))
            all_truncations_rejected = false;
    }
    Check(all_truncations_rejected, "every truncated archive prefix is rejected safely");

    auto archive = omega::archive::HogArchive::FromBytes(MakeArchive());
    Check(archive.has_value(), "valid archive parses");
    if (archive)
    {
        Check(archive->header().tag == 0x4052673D, "tag is preserved");
        Check(archive->entries().size() == 2, "entry count is preserved");
        Check(archive->entries()[0].name == "A.BIN", "first name parses");
        Check(archive->entries()[0].offset == 0x30, "first absolute offset parses");
        Check(archive->entries()[0].size == 3, "first size parses");
        Check(archive->entries()[1].name == "B.SO", "second name parses");
        Check(archive->payload(archive->entries()[1]).size() == 4, "payload span is bounded");
        Check(archive->find("B.SO") != nullptr, "find locates an entry");
        Check(archive->find("MISSING") == nullptr, "find rejects an absent entry");
        Check(archive->logical_size() == MakeArchive().size(), "exact archive reports its logical size");
        Check(archive->padding_size() == 0, "exact archive reports no trailing padding");
    }

    auto zero_padded = MakeArchive();
    const auto logical_size = zero_padded.size();
    zero_padded.resize(logical_size + 17, std::byte{0});
    auto padded_archive = omega::archive::HogArchive::FromBytes(std::move(zero_padded));
    Check(padded_archive.has_value(), "all-zero sector padding is accepted");
    if (padded_archive)
    {
        Check(padded_archive->logical_size() == logical_size, "logical size excludes sector padding");
        Check(padded_archive->padding_size() == 17, "padding size is exposed");
        Check(padded_archive->bytes().size() == logical_size + 17, "owned physical span is preserved");
        const omega::archive::HogEntry padding_entry{
            .name = "PADDING",
            .offset = logical_size,
            .size = 1,
        };
        Check(padded_archive->payload(padding_entry).empty(), "payload views cannot enter padding");
    }

    auto nonzero_padded = MakeArchive();
    nonzero_padded.push_back(std::byte{0});
    nonzero_padded.push_back(std::byte{1});
    Check(!omega::archive::HogArchive::FromBytes(std::move(nonzero_padded)),
        "non-zero trailing bytes are rejected");

    auto bad_terminal = MakeArchive();
    bad_terminal[28] = std::byte{8};
    Check(!omega::archive::HogArchive::FromBytes(std::move(bad_terminal)),
        "terminal payload mismatch is rejected");

    auto bad_order = MakeArchive();
    bad_order[24] = std::byte{9};
    Check(!omega::archive::HogArchive::FromBytes(std::move(bad_order)),
        "non-monotonic offsets are rejected");

    auto bad_name = MakeArchive();
    bad_name[32] = std::byte{0};
    Check(!omega::archive::HogArchive::FromBytes(std::move(bad_name)),
        "empty filenames are rejected");

    auto dirty_name_padding = MakeArchive();
    dirty_name_padding[43] = std::byte{1};
    Check(!omega::archive::HogArchive::FromBytes(std::move(dirty_name_padding)),
        "non-zero name-table padding is rejected");

    auto excessive_count = MakeArchive();
    excessive_count[4] = std::byte{1};
    excessive_count[5] = std::byte{0};
    excessive_count[6] = std::byte{16};
    excessive_count[7] = std::byte{0};
    Check(!omega::archive::HogArchive::FromBytes(std::move(excessive_count)),
        "excessive entry counts are rejected before allocation");

    failures += VirtualFileSystemFailureCount();
    if (failures == 0)
        std::cout << "omega_core_tests: all checks passed\n";
    return failures == 0 ? 0 : 1;
}
