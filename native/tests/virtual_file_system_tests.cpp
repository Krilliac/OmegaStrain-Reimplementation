#include "omega/archive/hog_archive.h"
#include "omega/vfs/virtual_file_system.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
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
    const auto loaded_padded = omega::archive::HogArchive::Open(
        padded_archive_path, padded_archive_size);
    Check(loaded_padded && loaded_padded->padding_size() == 12,
        "bounded archive load accepts verified zero padding");
    Check(!omega::archive::HogArchive::Open(padded_archive_path, padded_archive_size - 1),
        "archive load rejects files above the caller limit");
    Check(!omega::archive::HogIndex::Open(padded_archive_path),
        "top-level streaming index remains strict about physical EOF");

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
