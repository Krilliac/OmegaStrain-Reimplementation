#include "omega/archive/hog_archive.h"
#include "omega/content/game_data_service.h"
#include "omega/vfs/virtual_file_system.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <thread>
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
    AppendU32(bytes, 1);
    AppendU32(bytes, 0x14);
    AppendU32(bytes, 0x1C);
    AppendU32(bytes, 0x30);
    AppendU32(bytes, 0);
    AppendU32(bytes, 4);
    AppendString(bytes, "TEST.SO");
    bytes.resize(0x30, std::byte{0});
    bytes.push_back(std::byte{'O'});
    bytes.push_back(std::byte{'M'});
    bytes.push_back(std::byte{'E'});
    bytes.push_back(std::byte{'G'});
    return bytes;
}

constexpr std::size_t kIsoSectorBytes = 2048;
constexpr std::uint32_t kIsoVolumeSectors = 31;
constexpr std::uint32_t kIsoRootSector = 20;
constexpr std::uint32_t kIsoGameDataSector = 21;
constexpr std::uint32_t kIsoMinskSector = 22;
constexpr std::uint32_t kIsoSystemSector = 23;
constexpr std::uint32_t kIsoExecutableSector = 24;
constexpr std::uint32_t kIsoLevelSector = 25;
constexpr std::uint32_t kIsoVersionTwoSector = 26;
constexpr std::uint32_t kIsoZMediaSector = 27;
constexpr std::uint32_t kIsoMovieArchiveSector = 28;

void WriteBothEndianU16(
    std::vector<std::byte>& bytes, const std::size_t offset, const std::uint16_t value)
{
    bytes[offset] = static_cast<std::byte>(value & 0xFFU);
    bytes[offset + 1] = static_cast<std::byte>((value >> 8U) & 0xFFU);
    bytes[offset + 2] = static_cast<std::byte>((value >> 8U) & 0xFFU);
    bytes[offset + 3] = static_cast<std::byte>(value & 0xFFU);
}

void WriteBothEndianU32(
    std::vector<std::byte>& bytes, const std::size_t offset, const std::uint32_t value)
{
    bytes[offset] = static_cast<std::byte>(value & 0xFFU);
    bytes[offset + 1] = static_cast<std::byte>((value >> 8U) & 0xFFU);
    bytes[offset + 2] = static_cast<std::byte>((value >> 16U) & 0xFFU);
    bytes[offset + 3] = static_cast<std::byte>((value >> 24U) & 0xFFU);
    bytes[offset + 4] = static_cast<std::byte>((value >> 24U) & 0xFFU);
    bytes[offset + 5] = static_cast<std::byte>((value >> 16U) & 0xFFU);
    bytes[offset + 6] = static_cast<std::byte>((value >> 8U) & 0xFFU);
    bytes[offset + 7] = static_cast<std::byte>(value & 0xFFU);
}

std::size_t WriteIsoDirectoryRecord(std::vector<std::byte>& bytes, std::size_t& cursor,
    const std::uint32_t extent_sector, const std::uint32_t data_length,
    const bool is_directory, const std::span<const std::byte> identifier)
{
    const std::size_t start = cursor;
    const std::size_t record_length =
        33 + identifier.size() + (identifier.size() % 2U == 0 ? 1U : 0U);
    bytes[start] = static_cast<std::byte>(record_length);
    WriteBothEndianU32(bytes, start + 2, extent_sector);
    WriteBothEndianU32(bytes, start + 10, data_length);
    bytes[start + 25] = is_directory ? std::byte{0x02} : std::byte{0};
    WriteBothEndianU16(bytes, start + 28, 1);
    bytes[start + 32] = static_cast<std::byte>(identifier.size());
    std::ranges::copy(identifier, bytes.begin() + static_cast<std::ptrdiff_t>(start + 33));
    cursor += record_length;
    return start;
}

std::size_t WriteIsoDirectoryRecord(std::vector<std::byte>& bytes, std::size_t& cursor,
    const std::uint32_t extent_sector, const std::uint32_t data_length,
    const bool is_directory, const std::string_view identifier)
{
    return WriteIsoDirectoryRecord(bytes, cursor, extent_sector, data_length, is_directory,
        std::as_bytes(std::span(identifier.data(), identifier.size())));
}

void WriteIsoPayload(std::vector<std::byte>& bytes, const std::uint32_t sector,
    const std::span<const std::byte> payload)
{
    const std::size_t offset = static_cast<std::size_t>(sector) * kIsoSectorBytes;
    std::ranges::copy(payload,
        bytes.begin() + static_cast<std::ptrdiff_t>(offset));
}

void WriteIsoPayload(std::vector<std::byte>& bytes, const std::uint32_t sector,
    const std::string_view payload)
{
    WriteIsoPayload(bytes, sector,
        std::as_bytes(std::span(payload.data(), payload.size())));
}

struct SyntheticIso9660
{
    std::vector<std::byte> bytes;
    std::size_t root_dot_record = 0;
    std::size_t root_parent_record = 0;
    std::size_t root_system_record = 0;
    std::size_t root_game_data_record = 0;
    std::size_t game_data_minsk_record = 0;
    std::size_t zmedia_movie_record = 0;
    std::size_t movie_archive_offset = 0;
};

SyntheticIso9660 MakeSyntheticIso9660(const bool duplicate_system_path = false)
{
    constexpr std::string_view system_config =
        "BOOT2 = cdrom0:\\SCUS_972.64;1\r\nVER = 1.00\r\nVMODE = NTSC\r\n";
    constexpr std::string_view executable = "synthetic executable placeholder";
    constexpr std::string_view level_payload = "MINSK-LEVEL";
    constexpr std::string_view version_two_payload = "VERSION-TWO";
    const auto movie_archive = MakeArchive();
    SyntheticIso9660 image;
    image.bytes.resize(kIsoVolumeSectors * kIsoSectorBytes, std::byte{0});

    const std::size_t primary = 16 * kIsoSectorBytes;
    image.bytes[primary] = std::byte{1};
    constexpr std::array<std::byte, 5> signature{
        std::byte{'C'}, std::byte{'D'}, std::byte{'0'}, std::byte{'0'}, std::byte{'1'}};
    std::ranges::copy(signature, image.bytes.begin() + static_cast<std::ptrdiff_t>(primary + 1));
    image.bytes[primary + 6] = std::byte{1};
    WriteBothEndianU32(image.bytes, primary + 80, kIsoVolumeSectors);
    WriteBothEndianU16(image.bytes, primary + 120, 1);
    WriteBothEndianU16(image.bytes, primary + 124, 1);
    WriteBothEndianU16(image.bytes, primary + 128, kIsoSectorBytes);
    image.bytes[primary + 881] = std::byte{1};
    std::size_t root_descriptor = primary + 156;
    constexpr std::array<std::byte, 1> current_directory{std::byte{0}};
    WriteIsoDirectoryRecord(image.bytes, root_descriptor, kIsoRootSector,
        kIsoSectorBytes, true, current_directory);

    const std::size_t terminator = 17 * kIsoSectorBytes;
    image.bytes[terminator] = std::byte{0xFF};
    std::ranges::copy(signature, image.bytes.begin() + static_cast<std::ptrdiff_t>(terminator + 1));
    image.bytes[terminator + 6] = std::byte{1};

    constexpr std::array<std::byte, 1> parent_directory{std::byte{1}};
    std::size_t cursor = kIsoRootSector * kIsoSectorBytes;
    image.root_dot_record = WriteIsoDirectoryRecord(image.bytes, cursor, kIsoRootSector,
        kIsoSectorBytes, true, current_directory);
    image.root_parent_record = WriteIsoDirectoryRecord(image.bytes, cursor, kIsoRootSector,
        kIsoSectorBytes, true, parent_directory);
    image.root_system_record = WriteIsoDirectoryRecord(image.bytes, cursor, kIsoSystemSector,
        static_cast<std::uint32_t>(system_config.size()), false, "SYSTEM.CNF;1");
    WriteIsoDirectoryRecord(image.bytes, cursor, kIsoExecutableSector,
        static_cast<std::uint32_t>(executable.size()), false, "SCUS_972.64;1");
    image.root_game_data_record = WriteIsoDirectoryRecord(image.bytes, cursor,
        kIsoGameDataSector, kIsoSectorBytes, true, "GAMEDATA");
    WriteIsoDirectoryRecord(image.bytes, cursor,
        kIsoZMediaSector, kIsoSectorBytes, true, "ZMEDIA");
    WriteIsoDirectoryRecord(image.bytes, cursor, kIsoVersionTwoSector,
        static_cast<std::uint32_t>(version_two_payload.size()), false, "PATCH.BIN;2");
    if (duplicate_system_path)
    {
        WriteIsoDirectoryRecord(image.bytes, cursor, kIsoSystemSector,
            static_cast<std::uint32_t>(system_config.size()), false, "system.cnf;1");
    }

    cursor = kIsoGameDataSector * kIsoSectorBytes;
    WriteIsoDirectoryRecord(image.bytes, cursor, kIsoGameDataSector,
        kIsoSectorBytes, true, current_directory);
    WriteIsoDirectoryRecord(image.bytes, cursor, kIsoRootSector,
        kIsoSectorBytes, true, parent_directory);
    image.game_data_minsk_record = WriteIsoDirectoryRecord(image.bytes, cursor,
        kIsoMinskSector, kIsoSectorBytes, true, "MINSK");

    cursor = kIsoMinskSector * kIsoSectorBytes;
    WriteIsoDirectoryRecord(image.bytes, cursor, kIsoMinskSector,
        kIsoSectorBytes, true, current_directory);
    WriteIsoDirectoryRecord(image.bytes, cursor, kIsoGameDataSector,
        kIsoSectorBytes, true, parent_directory);
    WriteIsoDirectoryRecord(image.bytes, cursor, kIsoLevelSector,
        static_cast<std::uint32_t>(level_payload.size()), false, "DATA.POP;1");

    cursor = kIsoZMediaSector * kIsoSectorBytes;
    WriteIsoDirectoryRecord(image.bytes, cursor, kIsoZMediaSector,
        kIsoSectorBytes, true, current_directory);
    WriteIsoDirectoryRecord(image.bytes, cursor, kIsoRootSector,
        kIsoSectorBytes, true, parent_directory);
    image.zmedia_movie_record = WriteIsoDirectoryRecord(image.bytes, cursor,
        kIsoMovieArchiveSector, static_cast<std::uint32_t>(movie_archive.size()),
        false, "ZMOVIES.HOG;1");

    WriteIsoPayload(image.bytes, kIsoSystemSector, system_config);
    WriteIsoPayload(image.bytes, kIsoExecutableSector, executable);
    WriteIsoPayload(image.bytes, kIsoLevelSector, level_payload);
    WriteIsoPayload(image.bytes, kIsoVersionTwoSector, version_two_payload);
    WriteIsoPayload(image.bytes, kIsoMovieArchiveSector, movie_archive);
    image.movie_archive_offset =
        static_cast<std::size_t>(kIsoMovieArchiveSector) * kIsoSectorBytes;
    return image;
}

bool WriteFile(const std::filesystem::path& path, const std::span<const std::byte> bytes)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output)
        return false;
    if (!bytes.empty())
        output.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(output);
}

bool WriteSparseOversizedMovieIso(const std::filesystem::path& path)
{
    constexpr std::string_view selected_name = "Synthetic-Opening.PSS";
    constexpr std::string_view oversized_name = "Synthetic-Oversized.PSS";
    constexpr std::uint32_t selected_bytes = 4U;
    constexpr std::uint64_t oversized_bytes =
        omega::asset::kOpeningMovieMaximumSourceBytes + 1U;
    static_assert(oversized_bytes <= std::numeric_limits<std::uint32_t>::max());

    const std::size_t names_offset = 0x14U + 3U * sizeof(std::uint32_t);
    const std::size_t names_end = names_offset + selected_name.size() + 1U +
                                  oversized_name.size() + 1U;
    const std::size_t data_offset = (names_end + 15U) & ~std::size_t{15U};
    const std::uint64_t payload_bytes = selected_bytes + oversized_bytes;
    const std::uint64_t archive_bytes = data_offset + payload_bytes;
    if (archive_bytes > std::numeric_limits<std::uint32_t>::max())
        return false;

    std::vector<std::byte> prefix(data_offset + selected_bytes, std::byte{0});
    const auto write_u32 = [&prefix](const std::size_t offset, const std::uint32_t value) {
        for (unsigned shift = 0; shift < 32; shift += 8)
            prefix[offset + shift / 8U] =
                static_cast<std::byte>((value >> shift) & 0xFFU);
    };
    write_u32(0x00U, 0x4052673D);
    write_u32(0x04U, 2U);
    write_u32(0x08U, 0x14U);
    write_u32(0x0CU, static_cast<std::uint32_t>(names_offset));
    write_u32(0x10U, static_cast<std::uint32_t>(data_offset));
    write_u32(0x14U, 0U);
    write_u32(0x18U, selected_bytes);
    write_u32(0x1CU, static_cast<std::uint32_t>(payload_bytes));
    std::ranges::copy(std::as_bytes(std::span(selected_name.data(), selected_name.size())),
        prefix.begin() + static_cast<std::ptrdiff_t>(names_offset));
    const std::size_t oversized_name_offset = names_offset + selected_name.size() + 1U;
    std::ranges::copy(std::as_bytes(std::span(oversized_name.data(), oversized_name.size())),
        prefix.begin() + static_cast<std::ptrdiff_t>(oversized_name_offset));
    constexpr std::array<std::byte, selected_bytes> selected_payload{
        std::byte{'O'}, std::byte{'M'}, std::byte{'E'}, std::byte{'G'}};
    std::ranges::copy(selected_payload,
        prefix.begin() + static_cast<std::ptrdiff_t>(data_offset));

    auto image = MakeSyntheticIso9660();
    const std::uint64_t archive_offset =
        static_cast<std::uint64_t>(kIsoMovieArchiveSector) * kIsoSectorBytes;
    const std::uint64_t archive_end = archive_offset + archive_bytes;
    const std::uint64_t volume_sectors =
        (archive_end + kIsoSectorBytes - 1U) / kIsoSectorBytes;
    if (volume_sectors > std::numeric_limits<std::uint32_t>::max())
        return false;
    const std::uint64_t volume_bytes = volume_sectors * kIsoSectorBytes;
    WriteBothEndianU32(image.bytes, 16U * kIsoSectorBytes + 80U,
        static_cast<std::uint32_t>(volume_sectors));
    WriteBothEndianU32(image.bytes, image.zmedia_movie_record + 10U,
        static_cast<std::uint32_t>(archive_bytes));
    std::ranges::copy(prefix,
        image.bytes.begin() + static_cast<std::ptrdiff_t>(image.movie_archive_offset));

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output)
        return false;
    output.write(reinterpret_cast<const char*>(image.bytes.data()),
        static_cast<std::streamsize>(image.bytes.size()));
    output.seekp(static_cast<std::streamoff>(volume_bytes - 1U), std::ios::beg);
    output.put('\0');
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

void RunVirtualFileSystemTests()
{
    auto normalized = omega::vfs::NormalizeGamePath("gamedata\\Minsk//scripts.hog");
    Check(normalized && *normalized == "GAMEDATA/MINSK/SCRIPTS.HOG", "game paths normalize");
    Check(!omega::vfs::NormalizeGamePath("../escape.bin"), "parent traversal is rejected");
    Check(!omega::vfs::NormalizeGamePath("C:\\absolute.bin"), "drive paths are rejected");
    Check(!omega::vfs::NormalizeGamePath(std::string(4097, 'A')), "oversized game paths are rejected");

    const auto unique_suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path() /
                      ("omega-vfs-tests-" + std::to_string(unique_suffix));
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root / "physical", error);
    Check(!error, "temporary test directory is created");
    std::filesystem::create_directories(root / "physical" / "ZMEDIA", error);
    Check(!error, "directory-backed archive fixture directory is created");
    std::filesystem::create_directories(root / "override", error);
    Check(!error, "override test directory is created");

    {
        std::ofstream output(root / "physical" / "SYSTEM.CNF", std::ios::binary);
        output << "BOOT2";
    }
    {
        std::ofstream output(root / "override" / "SYSTEM.CNF", std::ios::binary);
        output << "NEW";
    }
    const auto archive_path = root / "SCRIPTS.HOG";
    {
        const auto bytes = MakeArchive();
        std::ofstream output(archive_path, std::ios::binary);
        output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    Check(WriteFile(root / "physical" / "ZMEDIA" / "ZMOVIES.HOG", MakeArchive()),
        "directory-backed HOG fixture is written");
    const auto unsafe_archive_path = root / "UNSAFE.HOG";
    {
        auto bytes = MakeArchive();
        constexpr std::array<std::byte, 8> unsafe_name{
            std::byte{'.'}, std::byte{'.'}, std::byte{'/'}, std::byte{'X'},
            std::byte{'.'}, std::byte{'S'}, std::byte{'O'}, std::byte{0},
        };
        std::ranges::copy(unsafe_name, bytes.begin() + 0x1C);
        std::ofstream output(unsafe_archive_path, std::ios::binary);
        output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    const auto padded_archive_path = root / "PADDED.HOG";
    std::size_t padded_archive_size = 0;
    {
        auto bytes = MakeArchive();
        bytes.resize(bytes.size() + 12, std::byte{0});
        padded_archive_size = bytes.size();
        std::ofstream output(padded_archive_path, std::ios::binary);
        output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    const auto loaded_padded = omega::archive::HogArchive::OpenRange(
        padded_archive_path,
        omega::archive::HogFileRange{.offset = 0, .size = padded_archive_size},
        padded_archive_size);
    Check(loaded_padded && loaded_padded->padding_size() == 12,
        "bounded nested archive range accepts verified zero padding");
    Check(!omega::archive::HogArchive::OpenRange(
              padded_archive_path,
              omega::archive::HogFileRange{.offset = 0, .size = padded_archive_size},
              padded_archive_size - 1),
        "nested archive range rejects spans above the caller limit");
    Check(!omega::archive::HogArchive::Open(padded_archive_path, padded_archive_size),
        "top-level owned archive load remains strict about physical EOF");
    Check(!omega::archive::HogIndex::Open(padded_archive_path),
        "top-level streaming index remains strict about physical EOF");

    const auto iso_path = root / "synthetic-owned-game.ISO";
    const auto iso_fixture = MakeSyntheticIso9660();
    Check(WriteFile(iso_path, iso_fixture.bytes), "project-generated ISO9660 fixture is written");

    omega::vfs::VirtualFileSystem iso_vfs;
    Check(iso_vfs.MountDirectory(root / "physical").has_value(),
        "an older extracted directory mounts before the ISO fixture");
    Check(iso_vfs.MountIso9660(iso_path).has_value(), "synthetic ISO9660 image mounts");
    Check(iso_vfs.MountHogFromGameFile(
              "OPENING_MOVIE_ARCHIVE", "ZMEDIA/ZMOVIES.HOG").has_value(),
        "an ISO-backed HOG is indexed through bounded mounted-file reads");
    Check(!iso_vfs.Read("SYSTEM.CNF"), "ISO reads are rejected before mount freeze");
    Check(!iso_vfs.FileSize("OPENING_MOVIE_ARCHIVE/TEST.SO"),
        "mounted-file size metadata is rejected before mount freeze");
    iso_vfs.Freeze();
    Check(iso_vfs.frozen(), "ISO mount table reports frozen state");
    Check(!iso_vfs.MountIso9660(iso_path), "ISO mounts are rejected after freeze");
    Check(iso_vfs.Contains("system.cnf") && iso_vfs.Contains("ScUs_972.64"),
        "canonical ;1 files are indexed without versions and looked up case-insensitively");
    Check(!iso_vfs.Contains("SYSTEM.CNF;1"),
        "the VFS exposes a version-free canonical path rather than a second ;1 alias");
    Check(iso_vfs.Contains("opening_movie_archive/test.so"),
        "ISO-backed HOG members join the immutable normalized mount table");
    Check(iso_vfs.Contains("gamedata/minsk/data.pop"),
        "nested ISO9660 directories are indexed as normalized game paths");
    Check(iso_vfs.Contains("PATCH.BIN;2") && !iso_vfs.Contains("PATCH.BIN"),
        "non-canonical ISO9660 file versions are preserved conservatively");

    const auto iso_system = iso_vfs.Read("SYSTEM.CNF");
    Check(iso_system && iso_system->size() > 5,
        "a newer ISO mount deterministically overrides an older directory file");
    const auto nested = iso_vfs.Read("GAMEDATA\\MINSK\\DATA.POP");
    Check(nested && nested->size() == 11 && nested->front() == std::byte{'M'},
        "nested ISO9660 payload reads return owned bytes");
    Check(!iso_vfs.Read("GAMEDATA/MINSK/DATA.POP", 10),
        "ISO9660 payload size is checked before caller-bounded allocation");
    const auto iso_movie_size = iso_vfs.FileSize("OPENING_MOVIE_ARCHIVE/TEST.SO");
    const auto iso_movie = iso_vfs.Read("OPENING_MOVIE_ARCHIVE/TEST.SO", 4);
    Check(iso_movie_size && *iso_movie_size == 4U && iso_movie &&
              iso_movie->size() == 4U && (*iso_movie)[0] == std::byte{'O'} &&
              (*iso_movie)[3] == std::byte{'G'},
        "an ISO-backed HOG read returns only the selected member range");
    Check(!iso_vfs.Read("OPENING_MOVIE_ARCHIVE/TEST.SO", 3),
        "ISO-backed HOG members are rejected before an oversized allocation/read");

    omega::vfs::VirtualFileSystem directory_hog_vfs;
    Check(directory_hog_vfs.MountDirectory(root / "physical").has_value() &&
              directory_hog_vfs.MountHogFromGameFile(
                  "OPENING_MOVIE_ARCHIVE", "ZMEDIA/ZMOVIES.HOG").has_value(),
        "the same mounted-file HOG path indexes an extracted directory source");
    directory_hog_vfs.Freeze();
    const auto directory_movie = directory_hog_vfs.Read(
        "OPENING_MOVIE_ARCHIVE/TEST.SO", 4);
    Check(directory_movie && iso_movie && *directory_movie == *iso_movie,
        "directory and ISO mounted-file HOG paths publish identical selected bytes");

    std::atomic<bool> concurrent_reads_ok = true;
    std::vector<std::thread> readers;
    for (int thread_index = 0; thread_index < 8; ++thread_index)
    {
        readers.emplace_back([&iso_vfs, &concurrent_reads_ok] {
            for (int iteration = 0; iteration < 32; ++iteration)
            {
                const auto bytes = iso_vfs.Read("gamedata/minsk/data.pop", 64);
                if (!bytes || bytes->size() != 11 || bytes->front() != std::byte{'M'})
                    concurrent_reads_ok = false;
            }
        });
    }
    for (auto& reader : readers)
        reader.join();
    Check(concurrent_reads_ok, "frozen ISO9660 reads are thread-safe and independently owned");

    omega::vfs::VirtualFileSystem iso_then_override;
    Check(iso_then_override.MountIso9660(iso_path).has_value(),
        "ISO fixture mounts before a newer extracted override");
    Check(iso_then_override.MountDirectory(root / "override").has_value(),
        "newer extracted override mounts after ISO fixture");
    iso_then_override.Freeze();
    const auto overridden_iso = iso_then_override.Read("SYSTEM.CNF");
    Check(overridden_iso && overridden_iso->size() == 3 &&
              overridden_iso->front() == std::byte{'N'},
        "newer directory mounts deterministically override ISO payloads");

    const auto opened_iso = omega::content::GameDataService::Open({.root = iso_path});
    Check(opened_iso &&
              opened_iso->identity().build == omega::content::RetailBuild::NtscUScus97264 &&
              opened_iso->identity().boot_executable == "SCUS_972.64",
        "GameDataService accepts a regular .iso and publishes the validated NTSC-U identity");
    if (opened_iso)
    {
        const auto opening_movie = opened_iso->LoadOpeningMovieSource("test.so");
        Check(opening_movie && opening_movie->bytes().size() == 4U &&
                  opening_movie->bytes()[0] == std::byte{'O'} &&
                  opening_movie->bytes()[3] == std::byte{'G'},
            "GameDataService resolves an exact opening-movie member from an ISO-backed HOG");
    }
    const auto malformed_movie_iso_path = root / "malformed-movie-archive.iso";
    auto malformed_movie_iso = iso_fixture;
    malformed_movie_iso.bytes[malformed_movie_iso.movie_archive_offset + 0x10U] = std::byte{0};
    Check(WriteFile(malformed_movie_iso_path, malformed_movie_iso.bytes),
        "malformed embedded HOG fixture is written");
    const auto malformed_movie_service =
        omega::content::GameDataService::Open({.root = malformed_movie_iso_path});
    Check(malformed_movie_service.has_value(),
        "a malformed optional ISO-backed movie archive does not block game-data startup");
    if (malformed_movie_service)
    {
        const auto malformed_movie =
            malformed_movie_service->LoadOpeningMovieSource("Synthetic-Secret.PSS");
        Check(!malformed_movie && malformed_movie.error().code ==
                  omega::content::GameDataErrorCode::MalformedArchive &&
                  malformed_movie.error().message.find("Synthetic-Secret") == std::string::npos &&
                  malformed_movie.error().message.find(malformed_movie_iso_path.string()) ==
                      std::string::npos,
            "malformed ISO-backed archive failures remain categorical and sanitized");
    }

    const auto short_movie_range_path = root / "short-movie-range.iso";
    auto short_movie_range = iso_fixture;
    WriteBothEndianU32(short_movie_range.bytes, short_movie_range.zmedia_movie_record + 10U,
        static_cast<std::uint32_t>(MakeArchive().size() - 1U));
    Check(WriteFile(short_movie_range_path, short_movie_range.bytes),
        "short embedded HOG range fixture is written");
    const auto short_movie_range_service =
        omega::content::GameDataService::Open({.root = short_movie_range_path});
    Check(short_movie_range_service.has_value(),
        "an invalid optional ISO member range does not block game-data startup");
    if (short_movie_range_service)
    {
        const auto short_movie =
            short_movie_range_service->LoadOpeningMovieSource("TEST.SO");
        Check(!short_movie && short_movie.error().code ==
                  omega::content::GameDataErrorCode::MalformedArchive,
            "an ISO file range shorter than the HOG terminal boundary is rejected");
    }

    const auto sparse_movie_iso_path = root / "sparse-oversized-movie.iso";
    Check(WriteSparseOversizedMovieIso(sparse_movie_iso_path),
        "sparse ISO fixture with a HOG above the terminal payload cap is written");
    const auto sparse_movie_service =
        omega::content::GameDataService::Open({.root = sparse_movie_iso_path});
    Check(sparse_movie_service.has_value(),
        "an ISO-backed HOG larger than 512 MiB is indexed without loading the container");
    if (sparse_movie_service)
    {
        const auto selected_movie =
            sparse_movie_service->LoadOpeningMovieSource("synthetic-opening.pss");
        Check(selected_movie && selected_movie->bytes().size() == 4U &&
                  selected_movie->bytes()[0] == std::byte{'O'},
            "a bounded member is read from a larger sparse ISO-backed HOG");
        const auto oversized_movie =
            sparse_movie_service->LoadOpeningMovieSource("synthetic-oversized.pss");
        Check(!oversized_movie && oversized_movie.error().decode_error &&
                  oversized_movie.error().decode_error->code ==
                      omega::asset::DecodeErrorCode::LimitExceeded &&
                  oversized_movie.error().message.find("synthetic-oversized") ==
                      std::string::npos,
            "an ISO-backed member above 512 MiB is rejected before payload allocation");
    }

    const auto bad_descriptor_path = root / "private-owner-descriptor.iso";
    auto bad_descriptor = iso_fixture;
    bad_descriptor.bytes[16 * kIsoSectorBytes + 1] = std::byte{'X'};
    Check(WriteFile(bad_descriptor_path, bad_descriptor.bytes),
        "malformed descriptor fixture is written");
    omega::vfs::VirtualFileSystem bad_descriptor_vfs;
    const auto bad_descriptor_mount = bad_descriptor_vfs.MountIso9660(bad_descriptor_path);
    Check(!bad_descriptor_mount, "invalid ISO9660 descriptor signatures are rejected");
    Check(!bad_descriptor_mount ||
              bad_descriptor_mount.error().find(bad_descriptor_path.string()) == std::string::npos,
        "ISO9660 mount diagnostics do not expose the owner path");
    const auto bad_service = omega::content::GameDataService::Open({.root = bad_descriptor_path});
    Check(!bad_service &&
              bad_service.error().message.find(bad_descriptor_path.string()) == std::string::npos,
        "GameDataService suppresses lower-level ISO and owner-path diagnostics");

    const auto unsupported_regular_source =
        omega::content::GameDataService::Open({.root = archive_path});
    Check(!unsupported_regular_source &&
              unsupported_regular_source.error().code ==
                  omega::content::GameDataErrorCode::MountFailed &&
              unsupported_regular_source.error().message == "unable to mount game-data root",
        "a regular non-ISO source preserves the established categorical mount diagnostic");

    const auto bad_structure_version_path = root / "bad-structure-version.iso";
    auto bad_structure_version = iso_fixture;
    bad_structure_version.bytes[16 * kIsoSectorBytes + 881] = std::byte{0};
    Check(WriteFile(bad_structure_version_path, bad_structure_version.bytes),
        "bad file-structure-version fixture is written");
    omega::vfs::VirtualFileSystem bad_structure_version_vfs;
    Check(!bad_structure_version_vfs.MountIso9660(bad_structure_version_path),
        "a nonconforming ISO9660 file structure version is rejected");

    const auto bad_root_length_path = root / "bad-root-length.iso";
    auto bad_root_length = iso_fixture;
    bad_root_length.bytes[16 * kIsoSectorBytes + 156] = std::byte{35};
    Check(WriteFile(bad_root_length_path, bad_root_length.bytes),
        "oversized PVD root-record fixture is written");
    omega::vfs::VirtualFileSystem bad_root_length_vfs;
    Check(!bad_root_length_vfs.MountIso9660(bad_root_length_path),
        "the PVD root directory record must occupy its fixed 34-byte field");

    const auto bad_endian_path = root / "bad-endian.iso";
    auto bad_endian = iso_fixture;
    bad_endian.bytes[16 * kIsoSectorBytes + 84] = std::byte{1};
    Check(WriteFile(bad_endian_path, bad_endian.bytes),
        "disagreeing dual-endian field fixture is written");
    omega::vfs::VirtualFileSystem bad_endian_vfs;
    Check(!bad_endian_vfs.MountIso9660(bad_endian_path),
        "disagreeing ISO9660 dual-endian scalar copies are rejected");

    const auto bad_record_path = root / "bad-record.iso";
    auto bad_record = iso_fixture;
    bad_record.bytes[bad_record.root_system_record] = std::byte{10};
    Check(WriteFile(bad_record_path, bad_record.bytes), "malformed record fixture is written");
    omega::vfs::VirtualFileSystem bad_record_vfs;
    Check(!bad_record_vfs.MountIso9660(bad_record_path),
        "malformed ISO9660 directory records are rejected");

    const auto bad_range_path = root / "bad-range.iso";
    auto bad_range = iso_fixture;
    WriteBothEndianU32(bad_range.bytes, bad_range.root_system_record + 2,
        kIsoVolumeSectors + 1);
    Check(WriteFile(bad_range_path, bad_range.bytes), "out-of-range extent fixture is written");
    omega::vfs::VirtualFileSystem bad_range_vfs;
    Check(!bad_range_vfs.MountIso9660(bad_range_path),
        "ISO9660 file extents outside the declared image are rejected");

    const auto cycle_path = root / "cycle.iso";
    auto cycle = iso_fixture;
    WriteBothEndianU32(cycle.bytes, cycle.game_data_minsk_record + 2, kIsoRootSector);
    Check(WriteFile(cycle_path, cycle.bytes), "directory cycle fixture is written");
    omega::vfs::VirtualFileSystem cycle_vfs;
    Check(!cycle_vfs.MountIso9660(cycle_path),
        "ISO9660 directory cycles and extent aliases are rejected");

    const auto bad_hierarchy_path = root / "bad-hierarchy.iso";
    auto bad_hierarchy = iso_fixture;
    WriteBothEndianU32(bad_hierarchy.bytes, bad_hierarchy.root_parent_record + 2,
        kIsoGameDataSector);
    Check(WriteFile(bad_hierarchy_path, bad_hierarchy.bytes),
        "malformed dot-dot hierarchy fixture is written");
    omega::vfs::VirtualFileSystem bad_hierarchy_vfs;
    Check(!bad_hierarchy_vfs.MountIso9660(bad_hierarchy_path),
        "ISO9660 dot and dot-dot records must match their directory hierarchy");

    const auto duplicate_path = root / "duplicate.iso";
    const auto duplicate = MakeSyntheticIso9660(true);
    Check(WriteFile(duplicate_path, duplicate.bytes), "duplicate-path fixture is written");
    omega::vfs::VirtualFileSystem duplicate_vfs;
    Check(!duplicate_vfs.MountIso9660(duplicate_path),
        "duplicate case-insensitive/version-normalized ISO9660 paths are rejected");

    const auto budget_path = root / "directory-budget.iso";
    auto budget = iso_fixture;
    constexpr std::uint32_t oversized_directory_bytes = 16U * 1024U * 1024U + 2048U;
    constexpr std::uint32_t budget_volume_sectors =
        kIsoRootSector + oversized_directory_bytes / kIsoSectorBytes;
    budget.bytes.resize(static_cast<std::size_t>(budget_volume_sectors) * kIsoSectorBytes,
        std::byte{0});
    WriteBothEndianU32(budget.bytes, 16 * kIsoSectorBytes + 80, budget_volume_sectors);
    WriteBothEndianU32(budget.bytes, 16 * kIsoSectorBytes + 156 + 10,
        oversized_directory_bytes);
    Check(WriteFile(budget_path, budget.bytes), "directory-budget fixture is written");
    omega::vfs::VirtualFileSystem budget_vfs;
    Check(!budget_vfs.MountIso9660(budget_path),
        "ISO9660 directory allocation is rejected by a fixed pre-read safety budget");

    const auto mutation_path = root / "mutation.iso";
    Check(WriteFile(mutation_path, iso_fixture.bytes), "mutation fixture is written");
    omega::vfs::VirtualFileSystem mutation_vfs;
    Check(mutation_vfs.MountIso9660(mutation_path).has_value(),
        "mutation fixture mounts before it changes");
    Check(mutation_vfs.MountHogFromGameFile(
              "OPENING_MOVIE_ARCHIVE", "ZMEDIA/ZMOVIES.HOG").has_value(),
        "mutation fixture indexes its ISO-backed HOG before publication");
    mutation_vfs.Freeze();
    const auto original_write_time = std::filesystem::last_write_time(mutation_path, error);
    Check(!error, "mutation fixture timestamp is read");
    {
        std::fstream mutation_stream(mutation_path, std::ios::binary | std::ios::in | std::ios::out);
        mutation_stream.seekp(static_cast<std::streamoff>(kIsoLevelSector * kIsoSectorBytes));
        mutation_stream.put('X');
        Check(static_cast<bool>(mutation_stream), "mounted ISO payload is mutated for rejection test");
    }
    std::filesystem::last_write_time(
        mutation_path, original_write_time + std::chrono::seconds(5), error);
    Check(!error, "mutation fixture receives a deterministic changed identity");
    Check(!mutation_vfs.Read("GAMEDATA/MINSK/DATA.POP"),
        "payload mutation after mount is rejected before bytes are published");
    Check(!mutation_vfs.Read("OPENING_MOVIE_ARCHIVE/TEST.SO"),
        "ISO identity changes also invalidate previously indexed HOG member ranges");

    const auto truncation_path = root / "truncation.iso";
    Check(WriteFile(truncation_path, iso_fixture.bytes), "truncation fixture is written");
    omega::vfs::VirtualFileSystem truncation_vfs;
    Check(truncation_vfs.MountIso9660(truncation_path).has_value(),
        "truncation fixture mounts before it changes");
    truncation_vfs.Freeze();
    std::filesystem::resize_file(truncation_path, 18 * kIsoSectorBytes, error);
    Check(!error, "mounted ISO fixture is truncated for rejection test");
    Check(!truncation_vfs.Read("SYSTEM.CNF"),
        "ISO truncation after mount is rejected before bytes are published");

    omega::vfs::VirtualFileSystem vfs;
    Check(vfs.MountDirectory(root / "physical").has_value(), "physical directory mounts");
    Check(!vfs.MountHog("UNSAFE", unsafe_archive_path), "escaping HOG entry paths are rejected");
    Check(vfs.MountHog("GAMEDATA/MINSK/SCRIPTS", archive_path).has_value(), "HOG mounts");
    Check(vfs.MountDirectory(root / "override").has_value(), "newer override directory mounts");
    Check(!vfs.Read("SYSTEM.CNF"), "reads are rejected until mounts are frozen");
    Check(!vfs.Contains("SYSTEM.CNF"), "contains is unavailable until mounts are frozen");
    vfs.Freeze();
    Check(vfs.frozen(), "mount table reports frozen state");
    Check(!vfs.MountDirectory(root / "physical"), "directory mounts are rejected after freeze");
    Check(!vfs.MountHog("EXTRA", archive_path), "HOG mounts are rejected after freeze");
    Check(vfs.Contains("system.cnf"), "physical path lookup is case-insensitive");
    Check(vfs.Contains("gamedata/minsk/scripts/test.so"), "HOG path lookup is case-insensitive");

    const auto physical = vfs.Read("SYSTEM.CNF");
    Check(physical && physical->size() == 3 && (*physical)[0] == std::byte{'N'},
        "newer mounts override older physical payloads");
    Check(!vfs.Read("SYSTEM.CNF", 2), "physical reads honor caller byte limits");
    const auto embedded = vfs.Read("GAMEDATA/MINSK/SCRIPTS/TEST.SO");
    Check(embedded && embedded->size() == 4 && (*embedded)[0] == std::byte{'O'}, "HOG payload reads");
    Check(!vfs.Read("GAMEDATA/MINSK/SCRIPTS/TEST.SO", 3), "HOG reads honor caller byte limits");
    Check(!vfs.Read("MISSING.BIN"), "missing paths return an error");

    std::filesystem::remove_all(root, error);
    Check(!error, "temporary test directory is removed");
}

namespace
{
struct TestRegistration
{
    TestRegistration() { RunVirtualFileSystemTests(); }
};

TestRegistration registration;
} // namespace

int VirtualFileSystemFailureCount()
{
    return failures;
}
