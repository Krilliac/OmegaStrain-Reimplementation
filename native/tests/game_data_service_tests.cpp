#include "omega/content/game_data_service.h"
#include "omega/runtime/content_startup.h"

#include <algorithm>
#include <bit>
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

void WriteU16(
    std::vector<std::byte>& bytes, const std::size_t offset, const std::uint16_t value)
{
    bytes[offset] = static_cast<std::byte>(value & 0xFFU);
    bytes[offset + 1U] = static_cast<std::byte>((value >> 8U) & 0xFFU);
}

void WriteU32(
    std::vector<std::byte>& bytes, const std::size_t offset, const std::uint32_t value)
{
    for (unsigned shift = 0; shift < 32; shift += 8)
        bytes[offset + shift / 8U] = static_cast<std::byte>((value >> shift) & 0xFFU);
}

void WriteF32(std::vector<std::byte>& bytes, const std::size_t offset, const float value)
{
    WriteU32(bytes, offset, std::bit_cast<std::uint32_t>(value));
}

std::vector<std::byte> Bytes(const std::string_view value)
{
    std::vector<std::byte> bytes;
    AppendText(bytes, value);
    return bytes;
}

struct HogMember
{
    std::string_view name;
    std::vector<std::byte> payload;
};

std::vector<std::byte> MakeHog(
    const std::vector<HogMember>& members, const std::size_t trailing_zero_bytes = 0)
{
    const std::size_t names_offset = 0x14U + 4U * (members.size() + 1U);
    std::size_t names_end = names_offset;
    std::size_t payload_bytes = 0;
    for (const auto& member : members)
    {
        names_end += member.name.size() + 1U;
        payload_bytes += member.payload.size();
    }
    const std::size_t data_offset = (names_end + 15U) & ~std::size_t{15U};
    std::vector<std::byte> bytes(data_offset, std::byte{0});
    WriteU32(bytes, 0x00, 0x4052673D);
    WriteU32(bytes, 0x04, static_cast<std::uint32_t>(members.size()));
    WriteU32(bytes, 0x08, 0x14);
    WriteU32(bytes, 0x0C, static_cast<std::uint32_t>(names_offset));
    WriteU32(bytes, 0x10, static_cast<std::uint32_t>(data_offset));

    std::size_t name_cursor = names_offset;
    std::uint32_t payload_cursor = 0;
    for (std::size_t index = 0; index < members.size(); ++index)
    {
        WriteU32(bytes, 0x14U + 4U * index, payload_cursor);
        for (const char character : members[index].name)
            bytes[name_cursor++] = static_cast<std::byte>(character);
        ++name_cursor;
        payload_cursor += static_cast<std::uint32_t>(members[index].payload.size());
    }
    WriteU32(bytes, 0x14U + 4U * members.size(), payload_cursor);
    bytes.reserve(data_offset + payload_bytes + trailing_zero_bytes);
    for (const auto& member : members)
        bytes.insert(bytes.end(), member.payload.begin(), member.payload.end());
    bytes.resize(bytes.size() + trailing_zero_bytes, std::byte{0});
    return bytes;
}

std::vector<std::byte> MakeDataHog()
{
    return MakeHog({HogMember{.name = "CELL.HOG", .payload = Bytes("xyz")}});
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

void WriteBounds(std::vector<std::byte>& bytes, const std::size_t offset,
    const float maximum_x)
{
    WriteF32(bytes, offset, 0.0F);
    WriteF32(bytes, offset + 4U, 0.0F);
    WriteF32(bytes, offset + 8U, 0.0F);
    WriteF32(bytes, offset + 12U, 1.0F);
    WriteF32(bytes, offset + 16U, maximum_x);
    WriteF32(bytes, offset + 20U, 1.0F);
    WriteF32(bytes, offset + 24U, 0.0F);
    WriteF32(bytes, offset + 28U, 1.0F);
}

void WriteVertex(std::vector<std::byte>& bytes, const std::size_t offset,
    const float x, const float y)
{
    WriteF32(bytes, offset, x);
    WriteF32(bytes, offset + 4U, y);
    WriteF32(bytes, offset + 8U, 0.0F);
    WriteF32(bytes, offset + 12U, 1.0F);
}

std::vector<std::byte> MakeDirectLeafCol(
    const float maximum_x, const std::uint8_t version = 5)
{
    std::vector<std::byte> bytes(176U, std::byte{0});
    bytes[0] = std::byte{'C'};
    bytes[1] = std::byte{'O'};
    bytes[2] = std::byte{'L'};
    bytes[3] = static_cast<std::byte>(version);
    WriteU32(bytes, 4, 0);
    WriteU32(bytes, 8, 48);
    WriteU32(bytes, 12, 1);
    WriteU32(bytes, 16, 48);
    WriteU32(bytes, 20, 1);
    WriteU32(bytes, 24, 96);
    WriteU32(bytes, 28, 3);
    WriteU32(bytes, 32, 112);
    WriteU32(bytes, 36, 160);
    WriteU32(bytes, 40, 1);
    WriteU32(bytes, 44, 176);
    WriteBounds(bytes, 48, maximum_x);
    WriteU32(bytes, 80, 1);
    WriteU32(bytes, 84, 160);
    WriteU16(bytes, 100, 0);
    WriteU16(bytes, 102, 1);
    WriteU16(bytes, 104, 2);
    WriteVertex(bytes, 112, 0.0F, 0.0F);
    WriteVertex(bytes, 128, maximum_x, 0.0F);
    WriteVertex(bytes, 144, 0.0F, 1.0F);
    WriteU32(bytes, 160, 0);
    return bytes;
}

std::vector<std::byte> MakeCellHog(const std::string_view vum_name,
    const std::vector<HogMember>& spatial_members, const std::size_t trailing_zero_bytes = 11)
{
    std::vector<HogMember> members;
    members.reserve(spatial_members.size() + 1U);
    members.push_back(HogMember{.name = vum_name, .payload = Bytes("vum-a")});
    members.insert(members.end(), spatial_members.begin(), spatial_members.end());
    return MakeHog(members, trailing_zero_bytes);
}

std::vector<std::byte> MakeSpatialDataHog(
    const std::vector<std::byte>& cell_a, const std::vector<std::byte>& cell_b)
{
    return MakeHog({HogMember{.name = "CeLlA.HoG", .payload = cell_a},
        HogMember{.name = "cElLb.hOg", .payload = cell_b}});
}

std::vector<std::byte> MakeSpatialPop()
{
    std::vector<std::byte> bytes;
    AppendU32(bytes, 70);
    AppendText(bytes, "TER:");
    AppendU32(bytes, 2);
    AppendU32(bytes, 4);
    AppendU32(bytes, 20);
    AppendText(bytes, "cElLb.VuM");
    bytes.push_back(std::byte{0});
    while (bytes.size() % 4U != 0)
        bytes.push_back(std::byte{0});
    AppendU32(bytes, 4);
    AppendU32(bytes, 10);
    AppendText(bytes, "CELLA.vum");
    bytes.push_back(std::byte{0});
    while (bytes.size() % 4U != 0)
        bytes.push_back(std::byte{0});
    AppendText(bytes, "GOB:");
    return bytes;
}

struct SpatialFixture
{
    std::vector<std::byte> col_a;
    std::vector<std::byte> col_b;
    std::vector<std::byte> cell_a;
    std::vector<std::byte> cell_b;
    std::vector<std::byte> data_hog;
    std::vector<std::byte> pop;
};

SpatialFixture MakeSpatialFixture()
{
    SpatialFixture fixture;
    fixture.col_a = MakeDirectLeafCol(1.0F, 3);
    fixture.col_b = MakeDirectLeafCol(2.0F, 5);
    fixture.cell_a = MakeCellHog("CeLlA.vUm",
        {HogMember{.name = "aLpHa.CoL", .payload = fixture.col_a}});
    fixture.cell_b = MakeCellHog("cElLb.VuM",
        {HogMember{.name = "BeTa.cOl", .payload = fixture.col_b}});
    fixture.data_hog = MakeSpatialDataHog(fixture.cell_a, fixture.cell_b);
    fixture.pop = MakeSpatialPop();
    return fixture;
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

template <typename Value>
void CheckDecodeError(const std::expected<Value, omega::content::GameDataError>& result,
    const omega::asset::DecodeErrorCode code, const std::string_view message)
{
    Check(!result && result.error().code == omega::content::GameDataErrorCode::DecodeFailed &&
              result.error().decode_error && result.error().decode_error->code == code,
        message);
}

std::expected<omega::asset::LevelSpatialIR, omega::content::GameDataError>
LoadSpatialWithLimits(const std::filesystem::path& root,
    const omega::asset::LevelManifestIR& manifest, const omega::asset::DecodeLimits limits,
    const std::uint64_t maximum_nested_hog_bytes = 64ULL * 1024ULL * 1024ULL)
{
    omega::content::GameDataServiceConfig config{.root = root};
    config.maximum_nested_hog_bytes = maximum_nested_hog_bytes;
    config.decode_limits = limits;
    auto service = omega::content::GameDataService::Open(std::move(config));
    if (!service)
        return std::unexpected(service.error());
    return service->LoadLevelSpatial(manifest);
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

    const SpatialFixture spatial_fixture = MakeSpatialFixture();
    Check(spatial_fixture.cell_a.size() == 256U && spatial_fixture.cell_b.size() == 256U &&
              spatial_fixture.data_hog.size() == 576U,
        "nested spatial fixtures have stable bounded byte sizes");
    Check(WriteBytes(root / "GAMEDATA" / "MINSK" / "DATA.HOG",
              spatial_fixture.data_hog) &&
              WriteBytes(root / "GAMEDATA" / "MINSK" / "DATA.POP", spatial_fixture.pop),
        "mixed-case two-cell spatial fixture is written");

    omega::runtime::LaunchOptions startup_options;
    startup_options.data_root = root;
    startup_options.level_code = "MINSK";
    startup_options.probe_only = true;
    auto startup = omega::runtime::StartContent(startup_options);
    Check(startup && startup->game_data && startup->level_manifest && startup->level_spatial &&
              startup->debug_image,
        "the exact non-SDL startup path owns validated manifest, spatial, and debug data");
    if (startup && startup->level_manifest && startup->level_spatial && startup->debug_image)
    {
        Check(startup->level_manifest->terrain_cells.size() == 2 &&
                  startup->level_spatial->terrain_cells.size() == 2 &&
                  startup->debug_image->width != 0 && startup->debug_image->height != 0,
            "application startup composes matching manifest and spatial cardinalities");
    }

    auto spatial_service = omega::content::GameDataService::Open({.root = root});
    Check(spatial_service.has_value(), "two-cell spatial fixture opens through GameDataService");
    if (spatial_service)
    {
        auto spatial_manifest = spatial_service->LoadLevelManifest("minsk");
        Check(spatial_manifest && spatial_manifest->terrain_cells.size() == 2 &&
                  spatial_manifest->terrain_cells[0].data_hog_entry == "CELLB.HOG" &&
                  spatial_manifest->terrain_cells[1].data_hog_entry == "CELLA.HOG",
            "mixed-case POP references normalize while preserving manifest order");
        if (spatial_manifest)
        {
            auto spatial = spatial_service->LoadLevelSpatial(*spatial_manifest);
            Check(spatial && spatial->terrain_cells.size() == 2,
                "level spatial loading returns one owned mesh per manifest cell");
            if (spatial && spatial->terrain_cells.size() == 2)
            {
                const bool ordered = spatial->terrain_cells[0].vertices.size() == 3 &&
                                     spatial->terrain_cells[1].vertices.size() == 3 &&
                                     spatial->terrain_cells[0].vertices[1].x == 2.0F &&
                                     spatial->terrain_cells[1].vertices[1].x == 1.0F;
                Check(ordered,
                    "archive lookup is case-insensitive and output follows manifest, not HOG, order");

                const omega::asset::LevelSpatialIR owned = *spatial;
                Check(WriteText(root / "GAMEDATA" / "MINSK" / "DATA.HOG", "replaced"),
                    "spatial source archive is replaced after decoding");
                Check(owned.terrain_cells[0].vertices[1] ==
                          omega::asset::Float3IR{2.0F, 0.0F, 0.0F} &&
                          owned.terrain_cells[1].vertices[1] ==
                              omega::asset::Float3IR{1.0F, 0.0F, 0.0F},
                    "level spatial output owns all mesh data after source bytes change");
                Check(WriteBytes(root / "GAMEDATA" / "MINSK" / "DATA.HOG",
                          spatial_fixture.data_hog),
                    "valid spatial source archive is restored after ownership proof");
            }

            const auto duplicate_outer_names = MakeHog(
                {HogMember{.name = "CeLlA.HoG", .payload = spatial_fixture.cell_a},
                    HogMember{.name = "CELLA.HOG", .payload = spatial_fixture.cell_a},
                    HogMember{.name = "cElLb.hOg", .payload = spatial_fixture.cell_b}});
            Check(WriteBytes(root / "GAMEDATA" / "MINSK" / "DATA.HOG",
                      duplicate_outer_names),
                "outer DATA.HOG with duplicate normalized names is written");
            CheckDecodeError(spatial_service->LoadLevelSpatial(*spatial_manifest),
                omega::asset::DecodeErrorCode::DuplicateReference,
                "case-colliding outer HOG names are rejected before cell lookup");

            const auto no_col_cell = MakeCellHog(
                "CeLlA.vUm", std::vector<HogMember>{});
            Check(WriteBytes(root / "GAMEDATA" / "MINSK" / "DATA.HOG",
                      MakeSpatialDataHog(no_col_cell, spatial_fixture.cell_b)),
                "cell archive without a COL member is written");
            CheckDecodeError(spatial_service->LoadLevelSpatial(*spatial_manifest),
                omega::asset::DecodeErrorCode::InvalidReference,
                "a cell archive with zero COL members has a typed invalid-reference error");

            const auto two_col_cell = MakeCellHog("CeLlA.vUm",
                {HogMember{.name = "aLpHa.CoL", .payload = spatial_fixture.col_a},
                    HogMember{.name = "other.COL", .payload = spatial_fixture.col_a}});
            Check(WriteBytes(root / "GAMEDATA" / "MINSK" / "DATA.HOG",
                      MakeSpatialDataHog(two_col_cell, spatial_fixture.cell_b)),
                "cell archive with two COL members is written");
            CheckDecodeError(spatial_service->LoadLevelSpatial(*spatial_manifest),
                omega::asset::DecodeErrorCode::DuplicateReference,
                "a cell archive with two COL members has a typed duplicate-reference error");

            Check(WriteBytes(root / "GAMEDATA" / "MINSK" / "DATA.HOG",
                      MakeSpatialDataHog(Bytes("not a HOG"), spatial_fixture.cell_b)),
                "malformed nested cell archive is written");
            CheckError(spatial_service->LoadLevelSpatial(*spatial_manifest),
                omega::content::GameDataErrorCode::MalformedArchive,
                "malformed nested HOG data has a stable archive error category");

            auto nonzero_tail_cell = spatial_fixture.cell_a;
            nonzero_tail_cell.back() = std::byte{0x7E};
            Check(WriteBytes(root / "GAMEDATA" / "MINSK" / "DATA.HOG",
                      MakeSpatialDataHog(nonzero_tail_cell, spatial_fixture.cell_b)),
                "nested cell archive with a nonzero tail is written");
            CheckError(spatial_service->LoadLevelSpatial(*spatial_manifest),
                omega::content::GameDataErrorCode::MalformedArchive,
                "nonzero bytes after a nested HOG logical end are rejected");

            auto unsupported_col = spatial_fixture.col_a;
            unsupported_col[3] = std::byte{4};
            const auto unsupported_col_cell = MakeCellHog("CeLlA.vUm",
                {HogMember{.name = "aLpHa.CoL", .payload = unsupported_col}});
            Check(WriteBytes(root / "GAMEDATA" / "MINSK" / "DATA.HOG",
                      MakeSpatialDataHog(unsupported_col_cell, spatial_fixture.cell_b)),
                "cell archive with an unsupported COL variant is written");
            CheckDecodeError(spatial_service->LoadLevelSpatial(*spatial_manifest),
                omega::asset::DecodeErrorCode::UnsupportedVariant,
                "typed COL decoder failures survive the level-spatial service boundary");

            Check(WriteBytes(root / "GAMEDATA" / "MINSK" / "DATA.HOG",
                      spatial_fixture.data_hog),
                "valid spatial source archive is restored for budget tests");

            auto limits = omega::asset::DecodeLimits{};
            auto exact_nested = LoadSpatialWithLimits(root, *spatial_manifest, limits,
                spatial_fixture.cell_a.size());
            Check(exact_nested.has_value(), "exact nested-HOG byte cap succeeds");
            auto below_nested = LoadSpatialWithLimits(root, *spatial_manifest, limits,
                spatial_fixture.cell_a.size() - 1U);
            CheckDecodeError(below_nested, omega::asset::DecodeErrorCode::LimitExceeded,
                "one-below nested-HOG byte cap has a typed limit-exceeded error");

            const std::uint64_t exact_input_bytes = spatial_fixture.data_hog.size() +
                spatial_fixture.cell_a.size() + spatial_fixture.col_a.size() +
                spatial_fixture.cell_b.size() + spatial_fixture.col_b.size();
            Check(exact_input_bytes == 1440U,
                "two-cell spatial fixture pins the exact cumulative input budget");
            limits = omega::asset::DecodeLimits{};
            limits.maximum_input_bytes = exact_input_bytes;
            Check(LoadSpatialWithLimits(root, *spatial_manifest, limits).has_value(),
                "exact shared level-spatial input budget succeeds");
            limits.maximum_input_bytes = exact_input_bytes - 1U;
            CheckDecodeError(LoadSpatialWithLimits(root, *spatial_manifest, limits),
                omega::asset::DecodeErrorCode::LimitExceeded,
                "one-below shared level-spatial input budget fails");

            constexpr std::uint64_t exact_items = 20;
            limits = omega::asset::DecodeLimits{};
            limits.maximum_items = exact_items;
            Check(LoadSpatialWithLimits(root, *spatial_manifest, limits).has_value(),
                "exact shared level-spatial item budget succeeds");
            limits.maximum_items = exact_items - 1U;
            CheckDecodeError(LoadSpatialWithLimits(root, *spatial_manifest, limits),
                omega::asset::DecodeErrorCode::LimitExceeded,
                "one-below shared level-spatial item budget fails");

            constexpr std::uint64_t mesh_output_bytes =
                sizeof(omega::asset::SpatialMeshIR) +
                3U * sizeof(omega::asset::Float3IR) +
                sizeof(omega::asset::SpatialTriangleIR) + sizeof(std::uint32_t) +
                sizeof(omega::asset::SpatialLeafIR);
            constexpr std::uint64_t exact_output_bytes =
                sizeof(omega::asset::LevelSpatialIR) + 2U * mesh_output_bytes;
            limits = omega::asset::DecodeLimits{};
            limits.maximum_output_bytes = exact_output_bytes;
            Check(LoadSpatialWithLimits(root, *spatial_manifest, limits).has_value(),
                "exact shared logical level-spatial output budget succeeds");
            limits.maximum_output_bytes = exact_output_bytes - 1U;
            CheckDecodeError(LoadSpatialWithLimits(root, *spatial_manifest, limits),
                omega::asset::DecodeErrorCode::LimitExceeded,
                "one-below shared logical level-spatial output budget fails");

            constexpr std::uint64_t exact_scratch_bytes = sizeof(std::uint8_t) +
                sizeof(omega::asset::SpatialElementRefIR) + sizeof(std::uint32_t);
            limits = omega::asset::DecodeLimits{};
            limits.maximum_scratch_bytes = exact_scratch_bytes;
            Check(LoadSpatialWithLimits(root, *spatial_manifest, limits).has_value(),
                "exact shared peak level-spatial scratch budget succeeds");
            limits.maximum_scratch_bytes = exact_scratch_bytes - 1U;
            CheckDecodeError(LoadSpatialWithLimits(root, *spatial_manifest, limits),
                omega::asset::DecodeErrorCode::LimitExceeded,
                "one-below shared peak level-spatial scratch budget fails");

            limits = omega::asset::DecodeLimits{};
            limits.maximum_nesting_depth = 1;
            Check(LoadSpatialWithLimits(root, *spatial_manifest, limits).has_value(),
                "one cell-HOG edge plus a direct-leaf COL fits exact depth one");
            limits.maximum_nesting_depth = 0;
            CheckDecodeError(LoadSpatialWithLimits(root, *spatial_manifest, limits),
                omega::asset::DecodeErrorCode::LimitExceeded,
                "one-below composed archive and COL nesting-depth budget fails");
        }
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
