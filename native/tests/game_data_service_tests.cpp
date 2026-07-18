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

void WriteText(
    std::vector<std::byte>& bytes, const std::size_t offset, const std::string_view value)
{
    for (std::size_t index = 0; index < value.size(); ++index)
        bytes[offset + index] = static_cast<std::byte>(value[index]);
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

void WriteVumMaterial(std::vector<std::byte>& bytes, const std::size_t offset,
    const std::vector<std::uint32_t>& name_indices)
{
    WriteText(bytes, offset, "MTRL");
    WriteU32(bytes, offset + 68U, 0xFFFFFFFFU);
    WriteU32(bytes, offset + 84U, 0xFFFFFFFFU);
    constexpr std::uint32_t inactive = 0xFFFFFFFFU;
    for (std::size_t slot = 0; slot < 3; ++slot)
    {
        WriteU32(bytes, offset + 56U + slot * 4U,
            slot < name_indices.size() ? name_indices[slot] : inactive);
        WriteU32(bytes, offset + 72U + slot * 4U, inactive);
    }
    if (name_indices.size() == 1)
        WriteU32(bytes, offset + 72U, 2);
    else if (name_indices.size() == 2)
    {
        WriteU32(bytes, offset + 72U, 2);
        WriteU32(bytes, offset + 76U, 11);
    }
    else
    {
        WriteU32(bytes, offset + 72U, 2);
        WriteU32(bytes, offset + 76U, 12);
        WriteU32(bytes, offset + 80U, 14);
    }
    WriteU32(bytes, offset + 88U, static_cast<std::uint32_t>(name_indices.size()));
}

std::vector<std::byte> MakeVumCatalog(const std::string_view first_name)
{
    constexpr std::uint32_t names_end = 132;
    constexpr std::uint32_t materials_end = 316;
    constexpr std::uint32_t payload_a = 368;
    constexpr std::uint32_t payload_b = 384;
    constexpr std::uint32_t primary_end = 432;
    constexpr std::uint32_t first_material = names_end;
    constexpr std::uint32_t second_material = names_end + 92U;
    constexpr std::uint32_t metadata_t = materials_end;
    constexpr std::uint32_t metadata_q = materials_end + 16U;
    constexpr std::uint32_t metadata_p = materials_end + 32U;

    if (first_name.size() != 8U)
        return {};
    std::vector<std::byte> bytes(448, std::byte{0});
    WriteText(bytes, 0, "VUMS");
    WriteU32(bytes, 4, 2);
    WriteU32(bytes, 12, 3);
    WriteU32(bytes, 16, 1);
    WriteU32(bytes, 20, 2);
    WriteU32(bytes, 24, 2);
    WriteU32(bytes, 28, 1);
    WriteU32(bytes, 80, names_end);
    WriteU32(bytes, 84, materials_end);
    WriteU32(bytes, 88, primary_end);
    WriteU32(bytes, 92, payload_a);
    WriteU32(bytes, 96, payload_b);
    WriteText(bytes, 112, first_name);
    WriteText(bytes, 121, "DETAIL.TDX");
    WriteVumMaterial(bytes, first_material, {0});
    WriteVumMaterial(bytes, second_material, {1, 0, 1});
    WriteU32(bytes, metadata_t + 8U, metadata_q);
    WriteU32(bytes, metadata_q + 4U, payload_a);
    WriteU32(bytes, payload_a + 4U, payload_b + 16U);
    WriteU32(bytes, metadata_q + 12U, payload_b + 32U);
    WriteU32(bytes, metadata_p, payload_b + 36U);
    WriteU32(bytes, metadata_p + 8U, payload_b + 40U);
    WriteU32(bytes, metadata_p + 12U, payload_b + 44U);
    std::fill(bytes.begin() + primary_end, bytes.end(), std::byte{0xA5});
    return bytes;
}

std::vector<std::byte> MakeCellHog(const std::string_view vum_name,
    const std::vector<std::byte>& vum_payload,
    const std::vector<HogMember>& spatial_members, const std::size_t trailing_zero_bytes = 11)
{
    std::vector<HogMember> members;
    members.reserve(spatial_members.size() + 1U);
    members.push_back(HogMember{.name = vum_name, .payload = vum_payload});
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
    std::vector<std::byte> vum_a;
    std::vector<std::byte> vum_b;
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
    fixture.vum_a = MakeVumCatalog("CELL.TDX");
    fixture.vum_b = MakeVumCatalog("BASE.TDX");
    fixture.cell_a = MakeCellHog("CeLlA.vUm", fixture.vum_a,
        {HogMember{.name = "aLpHa.CoL", .payload = fixture.col_a}});
    fixture.cell_b = MakeCellHog("cElLb.VuM", fixture.vum_b,
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

std::expected<omega::asset::LevelMaterialCatalogsIR, omega::content::GameDataError>
LoadMaterialCatalogsWithLimits(const std::filesystem::path& root,
    const omega::asset::LevelManifestIR& manifest, const omega::asset::DecodeLimits limits,
    const std::uint64_t maximum_nested_hog_bytes = 64ULL * 1024ULL * 1024ULL)
{
    omega::content::GameDataServiceConfig config{.root = root};
    config.maximum_nested_hog_bytes = maximum_nested_hog_bytes;
    config.decode_limits = limits;
    auto service = omega::content::GameDataService::Open(std::move(config));
    if (!service)
        return std::unexpected(service.error());
    return service->LoadLevelMaterialCatalogs(manifest);
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
    Check(spatial_fixture.vum_a.size() == 448U && spatial_fixture.vum_b.size() == 448U &&
              spatial_fixture.cell_a.size() == 699U &&
              spatial_fixture.cell_b.size() == 699U &&
              spatial_fixture.data_hog.size() == 1462U,
        "nested spatial/material fixtures have stable bounded byte sizes");
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
              startup->level_material_catalogs && startup->debug_image,
        "the exact non-SDL startup path owns validated manifest, spatial, material, and debug data");
    if (startup && startup->level_manifest && startup->level_spatial &&
        startup->level_material_catalogs && startup->debug_image)
    {
        Check(startup->level_manifest->terrain_cells.size() == 2 &&
                  startup->level_spatial->terrain_cells.size() == 2 &&
                  startup->level_material_catalogs->terrain_cells.size() == 2 &&
                  startup->level_material_catalogs->terrain_cells[0].names[0] == "BASE.TDX" &&
                  startup->level_material_catalogs->terrain_cells[1].names[0] == "CELL.TDX" &&
                  startup->debug_image->width != 0 && startup->debug_image->height != 0,
            "application startup composes matching manifest-order canonical cardinalities");

        const std::vector<std::byte> original_debug_pixels(
            startup->debug_image->pixels().begin(), startup->debug_image->pixels().end());
        auto changed_col_a = MakeDirectLeafCol(4.0F, 3);
        WriteF32(changed_col_a, 48U + 20U, 3.0F);
        WriteF32(changed_col_a, 128U + 4U, 1.0F);
        WriteF32(changed_col_a, 144U, 1.0F);
        WriteF32(changed_col_a, 144U + 4U, 3.0F);
        const auto changed_cell_a = MakeCellHog("CeLlA.vUm", spatial_fixture.vum_a,
            {HogMember{.name = "aLpHa.CoL", .payload = std::move(changed_col_a)}});
        Check(WriteBytes(root / "GAMEDATA" / "MINSK" / "DATA.HOG",
                  MakeSpatialDataHog(changed_cell_a, spatial_fixture.cell_b)),
            "same-manifest fixture with changed canonical spatial geometry is written");

        auto changed_geometry_startup = omega::runtime::StartContent(startup_options);
        Check(changed_geometry_startup && changed_geometry_startup->level_manifest &&
                  changed_geometry_startup->level_spatial &&
                  changed_geometry_startup->level_material_catalogs &&
                  changed_geometry_startup->debug_image,
            "startup rebuilds the synthetic diagnostic from changed canonical spatial geometry");
        if (changed_geometry_startup && changed_geometry_startup->level_manifest &&
            changed_geometry_startup->level_material_catalogs &&
            changed_geometry_startup->debug_image)
        {
            const std::vector<std::byte> changed_debug_pixels(
                changed_geometry_startup->debug_image->pixels().begin(),
                changed_geometry_startup->debug_image->pixels().end());
            const auto& original_cells = startup->level_manifest->terrain_cells;
            const auto& changed_cells =
                changed_geometry_startup->level_manifest->terrain_cells;
            const bool same_manifest_records = changed_cells.size() == original_cells.size() &&
                std::equal(original_cells.begin(), original_cells.end(), changed_cells.begin(),
                    [](const omega::asset::LevelCellSourceIR& left,
                        const omega::asset::LevelCellSourceIR& right) {
                        return left.observed_kind == right.observed_kind &&
                               left.observed_index == right.observed_index &&
                               left.data_hog_entry == right.data_hog_entry;
                    });
            Check(same_manifest_records &&
                      *changed_geometry_startup->level_material_catalogs ==
                          *startup->level_material_catalogs &&
                      (changed_geometry_startup->debug_image->width !=
                              startup->debug_image->width ||
                          changed_geometry_startup->debug_image->height !=
                              startup->debug_image->height ||
                          changed_debug_pixels != original_debug_pixels),
                "unchanged manifest records produce a different diagnostic when canonical "
                "spatial geometry changes");
        }
        Check(WriteBytes(root / "GAMEDATA" / "MINSK" / "DATA.HOG",
                  spatial_fixture.data_hog),
            "original spatial fixture is restored after the startup diagnostic proof");

        const auto changed_vum_a = MakeVumCatalog("NEXT.TDX");
        const auto changed_material_cell_a = MakeCellHog("CeLlA.vUm", changed_vum_a,
            {HogMember{.name = "aLpHa.CoL", .payload = spatial_fixture.col_a}});
        Check(WriteBytes(root / "GAMEDATA" / "MINSK" / "DATA.HOG",
                  MakeSpatialDataHog(changed_material_cell_a, spatial_fixture.cell_b)),
            "same-manifest fixture with only one canonical material catalog changed is written");
        auto changed_material_startup = omega::runtime::StartContent(startup_options);
        Check(changed_material_startup && changed_material_startup->level_spatial &&
                  changed_material_startup->level_material_catalogs &&
                  changed_material_startup->debug_image &&
                  *changed_material_startup->level_spatial == *startup->level_spatial &&
                  *changed_material_startup->level_material_catalogs !=
                      *startup->level_material_catalogs &&
                  changed_material_startup->level_material_catalogs->terrain_cells[1].names[0] ==
                      "NEXT.TDX" &&
                  changed_material_startup->debug_image->width == startup->debug_image->width &&
                  changed_material_startup->debug_image->height == startup->debug_image->height &&
                  std::equal(changed_material_startup->debug_image->pixels().begin(),
                      changed_material_startup->debug_image->pixels().end(),
                      startup->debug_image->pixels().begin(),
                      startup->debug_image->pixels().end()),
            "material-only changes remain owned but do not invent spatial or diagnostic bindings");
        Check(WriteBytes(root / "GAMEDATA" / "MINSK" / "DATA.HOG",
                  spatial_fixture.data_hog),
            "original fixture is restored after the material independence proof");
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

            auto material_catalogs =
                spatial_service->LoadLevelMaterialCatalogs(*spatial_manifest);
            Check(material_catalogs && material_catalogs->terrain_cells.size() == 2 &&
                      material_catalogs->terrain_cells[0].names[0] == "BASE.TDX" &&
                      material_catalogs->terrain_cells[1].names[0] == "CELL.TDX",
                "level material loading follows manifest order rather than outer HOG order");
            if (material_catalogs && material_catalogs->terrain_cells.size() == 2)
            {
                const omega::asset::LevelMaterialCatalogsIR owned = *material_catalogs;
                Check(WriteText(root / "GAMEDATA" / "MINSK" / "DATA.HOG", "replaced"),
                    "material source archive is replaced after decoding");
                Check(owned.terrain_cells[0].names[1] == "DETAIL.TDX" &&
                          owned.terrain_cells[1].materials[1].name_indices[2] == 1,
                    "level material output owns all catalog data after source bytes change");
                Check(WriteBytes(root / "GAMEDATA" / "MINSK" / "DATA.HOG",
                          spatial_fixture.data_hog),
                    "valid material source archive is restored after ownership proof");
            }

            auto repeated_manifest = *spatial_manifest;
            repeated_manifest.terrain_cells[1].data_hog_entry =
                repeated_manifest.terrain_cells[0].data_hog_entry;
            auto repeated_materials =
                spatial_service->LoadLevelMaterialCatalogs(repeated_manifest);
            Check(repeated_materials && repeated_materials->terrain_cells.size() == 2 &&
                      repeated_materials->terrain_cells[0] ==
                          repeated_materials->terrain_cells[1],
                "repeated manifest references preserve cardinality instead of deduplicating");

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
            CheckDecodeError(spatial_service->LoadLevelMaterialCatalogs(*spatial_manifest),
                omega::asset::DecodeErrorCode::DuplicateReference,
                "material loading rejects case-colliding outer HOG names before cell lookup");

            const auto no_col_cell = MakeCellHog(
                "CeLlA.vUm", spatial_fixture.vum_a, std::vector<HogMember>{});
            Check(WriteBytes(root / "GAMEDATA" / "MINSK" / "DATA.HOG",
                      MakeSpatialDataHog(no_col_cell, spatial_fixture.cell_b)),
                "cell archive without a COL member is written");
            CheckDecodeError(spatial_service->LoadLevelSpatial(*spatial_manifest),
                omega::asset::DecodeErrorCode::InvalidReference,
                "a cell archive with zero COL members has a typed invalid-reference error");

            const auto two_col_cell = MakeCellHog("CeLlA.vUm", spatial_fixture.vum_a,
                {HogMember{.name = "aLpHa.CoL", .payload = spatial_fixture.col_a},
                    HogMember{.name = "other.COL", .payload = spatial_fixture.col_a}});
            Check(WriteBytes(root / "GAMEDATA" / "MINSK" / "DATA.HOG",
                      MakeSpatialDataHog(two_col_cell, spatial_fixture.cell_b)),
                "cell archive with two COL members is written");
            CheckDecodeError(spatial_service->LoadLevelSpatial(*spatial_manifest),
                omega::asset::DecodeErrorCode::DuplicateReference,
                "a cell archive with two COL members has a typed duplicate-reference error");

            auto malformed_cell = spatial_fixture.cell_a;
            WriteU32(malformed_cell, 0x08, 0x15);
            Check(WriteBytes(root / "GAMEDATA" / "MINSK" / "DATA.HOG",
                      MakeSpatialDataHog(malformed_cell, spatial_fixture.cell_b)),
                "malformed nested cell archive is written");
            CheckError(spatial_service->LoadLevelSpatial(*spatial_manifest),
                omega::content::GameDataErrorCode::MalformedArchive,
                "malformed nested HOG data has a stable archive error category");
            CheckError(spatial_service->LoadLevelMaterialCatalogs(*spatial_manifest),
                omega::content::GameDataErrorCode::MalformedArchive,
                "material loading preserves the malformed nested-HOG error category");

            auto nonzero_tail_cell = spatial_fixture.cell_a;
            nonzero_tail_cell.back() = std::byte{0x7E};
            Check(WriteBytes(root / "GAMEDATA" / "MINSK" / "DATA.HOG",
                      MakeSpatialDataHog(nonzero_tail_cell, spatial_fixture.cell_b)),
                "nested cell archive with a nonzero tail is written");
            CheckError(spatial_service->LoadLevelSpatial(*spatial_manifest),
                omega::content::GameDataErrorCode::MalformedArchive,
                "nonzero bytes after a nested HOG logical end are rejected");
            CheckError(spatial_service->LoadLevelMaterialCatalogs(*spatial_manifest),
                omega::content::GameDataErrorCode::MalformedArchive,
                "material loading rejects nonzero bytes after a nested HOG logical end");

            auto unsupported_col = spatial_fixture.col_a;
            unsupported_col[3] = std::byte{4};
            const auto unsupported_col_cell = MakeCellHog(
                "CeLlA.vUm", spatial_fixture.vum_a,
                {HogMember{.name = "aLpHa.CoL", .payload = unsupported_col}});
            Check(WriteBytes(root / "GAMEDATA" / "MINSK" / "DATA.HOG",
                      MakeSpatialDataHog(unsupported_col_cell, spatial_fixture.cell_b)),
                "cell archive with an unsupported COL variant is written");
            CheckDecodeError(spatial_service->LoadLevelSpatial(*spatial_manifest),
                omega::asset::DecodeErrorCode::UnsupportedVariant,
                "typed COL decoder failures survive the level-spatial service boundary");

            const auto no_vum_cell = MakeHog(
                {HogMember{.name = "aLpHa.CoL", .payload = spatial_fixture.col_a}}, 11U);
            Check(WriteBytes(root / "GAMEDATA" / "MINSK" / "DATA.HOG",
                      MakeSpatialDataHog(no_vum_cell, spatial_fixture.cell_b)),
                "cell archive without a VUM member is written");
            CheckDecodeError(spatial_service->LoadLevelMaterialCatalogs(*spatial_manifest),
                omega::asset::DecodeErrorCode::InvalidReference,
                "a cell archive with zero VUM members has a typed invalid-reference error");
            const auto failed_material_startup = omega::runtime::StartContent(startup_options);
            Check(!failed_material_startup &&
                      failed_material_startup.error().code ==
                          omega::runtime::ContentStartupErrorCode::GameData &&
                      failed_material_startup.error().game_data_error &&
                      failed_material_startup.error().game_data_error->decode_error &&
                      failed_material_startup.error().game_data_error->decode_error->code ==
                          omega::asset::DecodeErrorCode::InvalidReference,
                "startup returns no partial state when one cell lacks its canonical VUM catalog");

            const auto two_vum_cell = MakeHog(
                {HogMember{.name = "CeLlA.vUm", .payload = spatial_fixture.vum_a},
                    HogMember{.name = "OTHER.VUM", .payload = spatial_fixture.vum_b},
                    HogMember{.name = "aLpHa.CoL", .payload = spatial_fixture.col_a}},
                11U);
            Check(WriteBytes(root / "GAMEDATA" / "MINSK" / "DATA.HOG",
                      MakeSpatialDataHog(two_vum_cell, spatial_fixture.cell_b)),
                "cell archive with two distinct VUM members is written");
            CheckDecodeError(spatial_service->LoadLevelMaterialCatalogs(*spatial_manifest),
                omega::asset::DecodeErrorCode::DuplicateReference,
                "a cell archive with two VUM members has a typed duplicate-reference error");

            const auto colliding_vum_names = MakeHog(
                {HogMember{.name = "CeLlA.vUm", .payload = spatial_fixture.vum_a},
                    HogMember{.name = "CELLA.VUM", .payload = spatial_fixture.vum_b},
                    HogMember{.name = "aLpHa.CoL", .payload = spatial_fixture.col_a}},
                11U);
            Check(WriteBytes(root / "GAMEDATA" / "MINSK" / "DATA.HOG",
                      MakeSpatialDataHog(colliding_vum_names, spatial_fixture.cell_b)),
                "cell archive with case-colliding normalized VUM names is written");
            CheckDecodeError(spatial_service->LoadLevelMaterialCatalogs(*spatial_manifest),
                omega::asset::DecodeErrorCode::DuplicateReference,
                "normalized VUM name collisions fail before unique-member selection");

            const auto malformed_vum_cell = MakeCellHog("CeLlA.vUm", Bytes("not-vum"),
                {HogMember{.name = "aLpHa.CoL", .payload = spatial_fixture.col_a}});
            Check(WriteBytes(root / "GAMEDATA" / "MINSK" / "DATA.HOG",
                      MakeSpatialDataHog(malformed_vum_cell, spatial_fixture.cell_b)),
                "cell archive with a malformed VUM catalog is written");
            CheckDecodeError(spatial_service->LoadLevelMaterialCatalogs(*spatial_manifest),
                omega::asset::DecodeErrorCode::Truncated,
                "typed VUM decoder failures survive the level-material service boundary");

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
            Check(exact_input_bytes == 3212U,
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
                "exact peak semantic COL scratch budget succeeds through level loading");
            limits.maximum_scratch_bytes = exact_scratch_bytes - 1U;
            CheckDecodeError(LoadSpatialWithLimits(root, *spatial_manifest, limits),
                omega::asset::DecodeErrorCode::LimitExceeded,
                "one-below peak semantic COL scratch budget fails through level loading");

            limits = omega::asset::DecodeLimits{};
            limits.maximum_nesting_depth = 1;
            Check(LoadSpatialWithLimits(root, *spatial_manifest, limits).has_value(),
                "one cell-HOG edge plus a direct-leaf COL fits exact depth one");
            limits.maximum_nesting_depth = 0;
            CheckDecodeError(LoadSpatialWithLimits(root, *spatial_manifest, limits),
                omega::asset::DecodeErrorCode::LimitExceeded,
                "one-below composed archive and COL nesting-depth budget fails");

            limits = omega::asset::DecodeLimits{};
            auto exact_material_nested = LoadMaterialCatalogsWithLimits(root,
                *spatial_manifest, limits, spatial_fixture.cell_a.size());
            Check(exact_material_nested.has_value(),
                "exact material cell-HOG byte cap succeeds");
            auto below_material_nested = LoadMaterialCatalogsWithLimits(root,
                *spatial_manifest, limits, spatial_fixture.cell_a.size() - 1U);
            CheckDecodeError(below_material_nested,
                omega::asset::DecodeErrorCode::LimitExceeded,
                "one-below material cell-HOG byte cap has a typed limit-exceeded error");

            const std::uint64_t material_input_bytes = spatial_fixture.data_hog.size() +
                spatial_fixture.cell_a.size() + spatial_fixture.vum_a.size() +
                spatial_fixture.cell_b.size() + spatial_fixture.vum_b.size();
            Check(material_input_bytes == 3756U,
                "two-cell material fixture pins exact cumulative parser and decoder input work");
            limits = omega::asset::DecodeLimits{};
            limits.maximum_input_bytes = material_input_bytes;
            Check(LoadMaterialCatalogsWithLimits(root, *spatial_manifest, limits).has_value(),
                "exact shared level-material input budget succeeds");
            limits.maximum_input_bytes = material_input_bytes - 1U;
            CheckDecodeError(
                LoadMaterialCatalogsWithLimits(root, *spatial_manifest, limits),
                omega::asset::DecodeErrorCode::LimitExceeded,
                "one-below shared level-material input budget fails");

            constexpr std::uint64_t material_items = 30;
            limits = omega::asset::DecodeLimits{};
            limits.maximum_items = material_items;
            Check(LoadMaterialCatalogsWithLimits(root, *spatial_manifest, limits).has_value(),
                "exact shared level-material item budget succeeds without resetting per cell");
            limits.maximum_items = material_items - 1U;
            CheckDecodeError(
                LoadMaterialCatalogsWithLimits(root, *spatial_manifest, limits),
                omega::asset::DecodeErrorCode::LimitExceeded,
                "one-below shared level-material item budget fails");

            constexpr std::uint64_t catalog_output_bytes =
                sizeof(omega::asset::MaterialCatalogIR) + 2U * sizeof(std::string) +
                2U * sizeof(omega::asset::MaterialCatalogEntryIR) + 18U;
            constexpr std::uint64_t material_output_bytes =
                sizeof(omega::asset::LevelMaterialCatalogsIR) +
                2U * catalog_output_bytes;
            limits = omega::asset::DecodeLimits{};
            limits.maximum_output_bytes = material_output_bytes;
            Check(LoadMaterialCatalogsWithLimits(root, *spatial_manifest, limits).has_value(),
                "exact shared logical level-material output budget succeeds");
            limits.maximum_output_bytes = material_output_bytes - 1U;
            CheckDecodeError(
                LoadMaterialCatalogsWithLimits(root, *spatial_manifest, limits),
                omega::asset::DecodeErrorCode::LimitExceeded,
                "one-below shared logical level-material output budget fails");

            limits = omega::asset::DecodeLimits{};
            limits.maximum_scratch_bytes = 0;
            Check(LoadMaterialCatalogsWithLimits(root, *spatial_manifest, limits).has_value(),
                "level material loading needs no dynamic semantic scratch");

            limits = omega::asset::DecodeLimits{};
            limits.maximum_nesting_depth = 1;
            Check(LoadMaterialCatalogsWithLimits(root, *spatial_manifest, limits).has_value(),
                "one cell-HOG edge fits the exact level-material depth budget");
            limits.maximum_nesting_depth = 0;
            CheckDecodeError(
                LoadMaterialCatalogsWithLimits(root, *spatial_manifest, limits),
                omega::asset::DecodeErrorCode::LimitExceeded,
                "one-below level-material archive depth budget fails");
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
