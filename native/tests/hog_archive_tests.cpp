#include "omega/archive/hog_archive.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <span>
#include <string>
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

bool WriteFile(const std::filesystem::path& path, const std::span<const std::byte> bytes)
{
    std::ofstream output(path, std::ios::binary);
    if (!output)
        return false;
    if (!bytes.empty())
        output.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
    return output.good();
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
int PopTerrainIndexFailureCount();
int PopLevelManifestDecoderFailureCount();
int ContainerDescriptorFailureCount();
int SkmContainerDescriptorFailureCount();
int ColSpatialMeshDecoderFailureCount();
int TdxTextureStorageDecoderFailureCount();
int VumMaterialCatalogDecoderFailureCount();
int VumRenderPayloadDescriptorFailureCount();
int GameDataServiceFailureCount();
int LaunchOptionsFailureCount();
int LogServiceFailureCount();
int ConfigServiceFailureCount();
int JobServiceFailureCount();
int SpatialDebugImageFailureCount();
int FrameSchedulerFailureCount();
int InputTrackerFailureCount();
int RuntimeSettingsFailureCount();
int SimulationWorldFailureCount();

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
    Check(!omega::archive::HogArchive::FromBytes(zero_padded),
        "top-level owned bytes reject trailing padding");
    auto padded_archive = omega::archive::HogArchive::FromSpan(zero_padded);
    Check(padded_archive.has_value(), "nested spans accept all-zero sector padding");
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
        const omega::archive::HogEntry header_entry{
            .name = "HEADER",
            .offset = 0,
            .size = 4,
        };
        Check(padded_archive->payload(header_entry).empty(),
            "payload views cannot expose a fabricated header range");
    }
    Check(!omega::archive::HogArchive::FromSpan(zero_padded, zero_padded.size() - 1U),
        "nested spans honor the caller's byte cap before copying");

    auto nonzero_padded = MakeArchive();
    nonzero_padded.push_back(std::byte{0});
    nonzero_padded.push_back(std::byte{1});
    Check(!omega::archive::HogArchive::FromSpan(nonzero_padded),
        "nested spans reject non-zero trailing bytes");

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

    const auto unique_suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path() /
                      ("omega-hog-range-tests-" + std::to_string(unique_suffix));
    std::error_code file_error;
    std::filesystem::create_directories(root, file_error);
    Check(!file_error, "HOG range test directory is created");

    auto nested_bytes = MakeArchive();
    const auto nested_logical_size = nested_bytes.size();
    nested_bytes.resize(nested_bytes.size() + 17U, std::byte{0});
    std::vector<std::byte> backing_bytes(13U, std::byte{0x7A});
    const auto nested_offset = backing_bytes.size();
    backing_bytes.insert(backing_bytes.end(), nested_bytes.begin(), nested_bytes.end());
    backing_bytes.push_back(std::byte{0x11});
    backing_bytes.push_back(std::byte{0x22});
    const auto backing_path = root / "BACKING.BIN";
    Check(WriteFile(backing_path, backing_bytes), "synthetic backing file is written");

    const omega::archive::HogFileRange nested_range{
        .offset = nested_offset,
        .size = nested_bytes.size(),
    };
    auto range_index = omega::archive::HogIndex::OpenRange(
        backing_path, nested_range, nested_bytes.size());
    Check(range_index.has_value(), "range index accepts a valid embedded HOG with zero tail");
    if (range_index)
    {
        Check(range_index->archive_size() == nested_bytes.size(),
            "range index reports the containing span size");
        Check(range_index->logical_size() == nested_logical_size,
            "range index reports the nested logical size");
        Check(range_index->padding_size() == 17U,
            "range index reports validated nested padding");
        Check(range_index->entries()[0].offset == 0x30,
            "range index entry offsets remain relative to the nested archive");
    }
    auto exact_range_index = omega::archive::HogIndex::OpenRange(
        backing_path,
        omega::archive::HogFileRange{.offset = nested_offset, .size = nested_logical_size},
        nested_logical_size);
    Check(exact_range_index && exact_range_index->padding_size() == 0,
        "range index accepts an exact nested span and ignores bytes outside that span");

    auto range_archive = omega::archive::HogArchive::OpenRange(
        backing_path, nested_range, nested_bytes.size());
    Check(range_archive.has_value(), "owned range loader reads only the embedded HOG span");
    if (range_archive)
    {
        Check(range_archive->padding_size() == 17U,
            "owned range loader preserves validated padding metadata");
        Check(range_archive->payload(range_archive->entries()[0]).size() == 3U,
            "owned range payload views are relative and bounded");
    }

    Check(!omega::archive::HogIndex::OpenRange(
              backing_path, nested_range, nested_bytes.size() - 1U),
        "range index honors the caller's byte cap");
    Check(!omega::archive::HogArchive::OpenRange(
              backing_path, nested_range, nested_bytes.size() - 1U),
        "owned range loader honors the caller's byte cap");
    Check(!omega::archive::HogIndex::OpenRange(backing_path,
              omega::archive::HogFileRange{
                  .offset = std::numeric_limits<std::uint64_t>::max() - 3U,
                  .size = 8U,
              },
              8U),
        "overflowing file ranges are rejected before seeking");
    Check(!omega::archive::HogArchive::OpenRange(backing_path,
              omega::archive::HogFileRange{
                  .offset = backing_bytes.size() - 1U,
                  .size = 2U,
              },
              2U),
        "file ranges extending beyond EOF are rejected");

    const auto padded_path = root / "PADDED.HOG";
    Check(WriteFile(padded_path, nested_bytes), "synthetic padded HOG file is written");
    Check(!omega::archive::HogIndex::Open(padded_path),
        "top-level streaming index keeps strict physical EOF");
    Check(!omega::archive::HogArchive::Open(padded_path, nested_bytes.size()),
        "top-level owned loader keeps strict physical EOF");
    Check(omega::archive::HogIndex::OpenRange(
              padded_path,
              omega::archive::HogFileRange{.offset = 0, .size = nested_bytes.size()},
              nested_bytes.size())
              .has_value(),
        "the same padded bytes are valid only through the nested range API");

    auto dirty_nested_bytes = nested_bytes;
    dirty_nested_bytes.back() = std::byte{1};
    const auto dirty_path = root / "DIRTY.HOG";
    Check(WriteFile(dirty_path, dirty_nested_bytes), "synthetic malformed HOG file is written");
    Check(!omega::archive::HogIndex::OpenRange(
              dirty_path,
              omega::archive::HogFileRange{.offset = 0, .size = dirty_nested_bytes.size()},
              dirty_nested_bytes.size()),
        "streaming range index rejects non-zero nested tail bytes");
    Check(!omega::archive::HogArchive::OpenRange(
              dirty_path,
              omega::archive::HogFileRange{.offset = 0, .size = dirty_nested_bytes.size()},
              dirty_nested_bytes.size()),
        "owned range loader rejects non-zero nested tail bytes");

    const auto missing_path = root / "MISSING.HOG";
    Check(!omega::archive::HogIndex::OpenRange(
              missing_path, omega::archive::HogFileRange{.offset = 0, .size = 24U}, 24U),
        "range index reports a missing backing file");
    Check(!omega::archive::HogArchive::OpenRange(
              missing_path, omega::archive::HogFileRange{.offset = 0, .size = 24U}, 24U),
        "owned range loader reports a missing backing file");

    std::filesystem::remove_all(root, file_error);
    Check(!file_error, "HOG range test directory is removed");

    failures += VirtualFileSystemFailureCount();
    failures += PopTerrainIndexFailureCount();
    failures += PopLevelManifestDecoderFailureCount();
    failures += ContainerDescriptorFailureCount();
    failures += SkmContainerDescriptorFailureCount();
    failures += ColSpatialMeshDecoderFailureCount();
    failures += TdxTextureStorageDecoderFailureCount();
    failures += VumMaterialCatalogDecoderFailureCount();
    failures += VumRenderPayloadDescriptorFailureCount();
    failures += GameDataServiceFailureCount();
    failures += LaunchOptionsFailureCount();
    failures += LogServiceFailureCount();
    failures += ConfigServiceFailureCount();
    failures += JobServiceFailureCount();
    failures += SpatialDebugImageFailureCount();
    failures += FrameSchedulerFailureCount();
    failures += InputTrackerFailureCount();
    failures += RuntimeSettingsFailureCount();
    failures += SimulationWorldFailureCount();
    if (failures == 0)
        std::cout << "omega_core_tests: all checks passed\n";
    return failures == 0 ? 0 : 1;
}
