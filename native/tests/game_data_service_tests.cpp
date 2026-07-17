#include "omega/content/game_data_service.h"
#include "omega/runtime/content_startup.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <utility>
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

std::vector<std::byte> MakeDataHog()
{
    std::vector<std::byte> bytes;
    AppendU32(bytes, 0x4052673D);
    AppendU32(bytes, 1);
    AppendU32(bytes, 0x14);
    AppendU32(bytes, 0x1C);
    AppendU32(bytes, 0x30);
    AppendU32(bytes, 0);
    AppendU32(bytes, 3);
    AppendText(bytes, "CELL.HOG");
    bytes.push_back(std::byte{0});
    bytes.resize(0x30, std::byte{0});
    AppendText(bytes, "xyz");
    return bytes;
}

std::vector<std::byte> MakePop()
{
    std::vector<std::byte> bytes;
    AppendU32(bytes, 70);
    AppendText(bytes, "TER:");
    AppendU32(bytes, 1);
    AppendU32(bytes, 4);
    AppendU32(bytes, 10);
    AppendText(bytes, "CELL.VUM");
    bytes.push_back(std::byte{0});
    while (bytes.size() % 4U != 0)
        bytes.push_back(std::byte{0x5A});
    AppendText(bytes, "GOB:");
    return bytes;
}

bool WriteBytes(const std::filesystem::path& path, const std::span<const std::byte> bytes)
{
    std::ofstream output(path, std::ios::binary);
    if (!output)
        return false;
    if (!bytes.empty())
        output.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
    return output.good();
}

bool WriteText(const std::filesystem::path& path, const std::string_view text)
{
    const auto* first = reinterpret_cast<const std::byte*>(text.data());
    return WriteBytes(path, std::span(first, text.size()));
}

bool MakeValidTree(const std::filesystem::path& root)
{
    std::error_code error;
    std::filesystem::create_directories(root / "GAMEDATA" / "MINSK", error);
    return !error &&
           WriteText(root / "SYSTEM.CNF",
               "BOOT2 = cdrom0:\\SCUS_972.64;1\r\nVER = 1.00\r\nVMODE = NTSC\r\n") &&
           WriteText(root / "SCUS_972.64", "synthetic placeholder") &&
           WriteBytes(root / "GAMEDATA" / "MINSK" / "DATA.HOG", MakeDataHog()) &&
           WriteBytes(root / "GAMEDATA" / "MINSK" / "DATA.POP", MakePop());
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

template <typename Value>
void CheckError(const std::expected<Value, omega::content::GameDataError>& result,
    const omega::content::GameDataErrorCode code, const std::string_view message)
{
    Check(!result && result.error().code == code, message);
}
} // namespace

int GameDataServiceFailureCount()
{
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path() /
        ("openomega-game-data-tests-" + std::to_string(nonce));
    Check(MakeValidTree(root), "synthetic NTSC-U-like data tree is created");

    auto service = omega::content::GameDataService::Open({.root = root});
    Check(service.has_value(), "valid owner-supplied data tree opens");
    if (service)
    {
        Check(service->identity().build == omega::content::RetailBuild::NtscUScus97264 &&
                  service->identity().boot_executable == "SCUS_972.64",
            "validated retail identity is published without executable contents");
        auto manifest = service->LoadLevelManifest("minsk");
        Check(manifest.has_value(), "named level loads through the frozen VFS");
        if (manifest)
        {
            Check(manifest->data_hog_source.game_path == "GAMEDATA/MINSK/DATA.HOG",
                "level bootstrap publishes a normalized canonical source path");
            Check(manifest->terrain_cells.size() == 1 &&
                      manifest->terrain_cells[0].data_hog_entry == "CELL.HOG" &&
                      manifest->terrain_cells[0].observed_kind == 4 &&
                      manifest->terrain_cells[0].observed_index == 10,
                "level bootstrap preserves the canonical manifest record");
        }

        CheckError(service->LoadLevelManifest("../MINSK"),
            omega::content::GameDataErrorCode::InvalidLevelCode,
            "level traversal syntax is rejected before VFS lookup");
        CheckError(service->LoadLevelManifest("UNKNOWN"),
            omega::content::GameDataErrorCode::MissingRequiredFile,
            "missing named levels have a stable error category");

        Check(WriteText(root / "GAMEDATA" / "MINSK" / "DATA.HOG", "not a HOG"),
            "malformed archive replacement is written");
        CheckError(service->LoadLevelManifest("MINSK"),
            omega::content::GameDataErrorCode::MalformedArchive,
            "malformed DATA.HOG is distinguished from I/O and POP failures");
        Check(WriteBytes(root / "GAMEDATA" / "MINSK" / "DATA.HOG", MakeDataHog()),
            "valid archive fixture is restored");
        Check(WriteText(root / "GAMEDATA" / "MINSK" / "DATA.POP", "not a POP"),
            "malformed POP replacement is written");
        auto malformed_pop = service->LoadLevelManifest("MINSK");
        CheckError(malformed_pop, omega::content::GameDataErrorCode::DecodeFailed,
            "malformed DATA.POP is reported at the canonical decoder boundary");
        Check(!malformed_pop && malformed_pop.error().decode_error.has_value(),
            "typed decoder details survive the startup error translation");
        Check(WriteBytes(root / "GAMEDATA" / "MINSK" / "DATA.POP", MakePop()),
            "valid POP fixture is restored");
    }

    omega::runtime::LaunchOptions startup_options;
    startup_options.data_root = root;
    startup_options.level_code = "MINSK";
    startup_options.probe_only = true;
    auto startup = omega::runtime::StartContent(startup_options);
    Check(startup && startup->game_data && startup->level_manifest && startup->debug_image,
        "the exact non-SDL application startup path owns validated content and a debug view");
    if (startup && startup->level_manifest && startup->debug_image)
    {
        Check(startup->level_manifest->terrain_cells.size() == 1 &&
                  startup->debug_image->width == 12 && startup->debug_image->height == 12,
            "application startup composes the canonical manifest and renderer-neutral view");
    }

    omega::runtime::LaunchOptions invalid_startup;
    invalid_startup.level_code = "MINSK";
    auto invalid = omega::runtime::StartContent(invalid_startup);
    Check(!invalid && invalid.error().code ==
              omega::runtime::ContentStartupErrorCode::InvalidOptions,
        "application startup independently enforces its data-root invariant");

    auto small_read_config = omega::content::GameDataServiceConfig{.root = root};
    small_read_config.maximum_pop_bytes = 8;
    auto small_read_service = omega::content::GameDataService::Open(std::move(small_read_config));
    Check(small_read_service.has_value(), "per-level byte limits do not prevent root validation");
    if (small_read_service)
        CheckError(small_read_service->LoadLevelManifest("MINSK"),
            omega::content::GameDataErrorCode::ReadFailed,
            "POP reads honor the configured byte limit");

    auto small_decode_config = omega::content::GameDataServiceConfig{.root = root};
    small_decode_config.decode_limits.maximum_input_bytes = 8;
    auto small_decode_service = omega::content::GameDataService::Open(
        std::move(small_decode_config));
    Check(small_decode_service.has_value(), "decoder limits do not prevent root validation");
    if (small_decode_service)
    {
        auto limited = small_decode_service->LoadLevelManifest("MINSK");
        CheckError(limited, omega::content::GameDataErrorCode::DecodeFailed,
            "manifest decoding honors the configured input limit");
        Check(!limited && limited.error().decode_error &&
                  limited.error().decode_error->code ==
                      omega::asset::DecodeErrorCode::LimitExceeded,
            "decoder limit errors remain typed through startup");
    }

    Check(WriteText(root / "SYSTEM.CNF", "BOOT2 = cdrom0:\\SLES_000.00;1\r\n"),
        "wrong-region system configuration is written");
    CheckError(omega::content::GameDataService::Open({.root = root}),
        omega::content::GameDataErrorCode::UnsupportedBuild,
        "wrong-region data trees fail with an explicit unsupported-build error");

    Check(WriteText(root / "SYSTEM.CNF", "BOOT2 = cdrom0:\\SCUS_972.64;1\r\n"),
        "valid system configuration is restored");
    std::error_code error;
    Check(std::filesystem::remove(root / "SCUS_972.64", error) && !error,
        "synthetic boot executable placeholder is removed");
    CheckError(omega::content::GameDataService::Open({.root = root}),
        omega::content::GameDataErrorCode::MissingRequiredFile,
        "a matching BOOT2 line cannot mask a missing boot executable");

    Check(WriteText(root / "SCUS_972.64", "synthetic placeholder"),
        "synthetic boot executable placeholder is restored");
    Check(std::filesystem::remove(root / "SYSTEM.CNF", error) && !error,
        "synthetic SYSTEM.CNF is removed");
    CheckError(omega::content::GameDataService::Open({.root = root}),
        omega::content::GameDataErrorCode::MissingRequiredFile,
        "a root without SYSTEM.CNF has a stable missing-file error");

    CheckError(omega::content::GameDataService::Open({}),
        omega::content::GameDataErrorCode::InvalidConfiguration,
        "an empty service configuration is rejected before filesystem access");

    std::filesystem::remove_all(root, error);
    Check(!error, "synthetic game-data test tree is removed");
    return failures;
}
