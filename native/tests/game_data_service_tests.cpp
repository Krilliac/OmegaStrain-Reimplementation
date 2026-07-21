#include "omega/content/game_data_service.h"
#include "omega/runtime/content_startup.h"
#include "omega/runtime/content_startup_diagnostic.h"
#include "pop_commands.h"

#include <algorithm>
#include <array>
#include <barrier>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <initializer_list>
#include <limits>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace omega::content
{
struct GameDataServiceTestAccess final
{
    [[nodiscard]] static GameDataService::SourceBinding Bind(
        const GameDataService& service) noexcept
    {
        return service.source_binding();
    }

    [[nodiscard]] static std::expected<GameDataService::ResolvedSourceLocator, GameDataError>
    Resolve(const GameDataService& service, const GameDataService::SourceBinding& binding,
        const asset::SourceLocator& locator, const asset::DecodeLimits limits = {})
    {
        return service.ResolveSourceLocator(binding, locator, limits);
    }
};
} // namespace omega::content

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

constexpr std::string_view kSyntheticOpeningMovieMember = "MiXeD-Opening.PSS";
constexpr std::string_view kSyntheticOpeningMoviePayload =
    "synthetic opening movie bytes";

class StreamCapture final
{
public:
    explicit StreamCapture(std::ostream& stream)
        : stream_(stream), original_(stream.rdbuf(buffer_.rdbuf()))
    {
    }

    ~StreamCapture()
    {
        if (active_)
            stream_.rdbuf(original_);
    }

    StreamCapture(const StreamCapture&) = delete;
    StreamCapture& operator=(const StreamCapture&) = delete;

    [[nodiscard]] std::string Release()
    {
        stream_.rdbuf(original_);
        active_ = false;
        return buffer_.str();
    }

private:
    std::ostream& stream_;
    std::ostringstream buffer_;
    std::streambuf* original_ = nullptr;
    bool active_ = true;
};

struct ToolRun
{
    int exit_code = 0;
    std::string standard_output;
    std::string standard_error;
};

[[nodiscard]] ToolRun RunLevelMaterialCatalogTool(const std::filesystem::path& root)
{
    StreamCapture output(std::cout);
    StreamCapture error(std::cerr);
    const int exit_code = omega::tool::LevelMaterialCatalogsVerifyTree(root);
    return ToolRun{
        .exit_code = exit_code,
        .standard_output = output.Release(),
        .standard_error = error.Release(),
    };
}

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

bool WriteSparseSingleMemberHog(const std::filesystem::path& path,
    const std::string_view member_name, const std::uint32_t payload_bytes)
{
    const std::size_t names_offset = 0x14U + 2U * sizeof(std::uint32_t);
    const std::size_t names_end = names_offset + member_name.size() + 1U;
    const std::size_t data_offset = (names_end + 15U) & ~std::size_t{15U};
    std::vector<std::byte> prefix(data_offset, std::byte{0});
    WriteU32(prefix, 0x00U, 0x4052673D);
    WriteU32(prefix, 0x04U, 1U);
    WriteU32(prefix, 0x08U, 0x14U);
    WriteU32(prefix, 0x0CU, static_cast<std::uint32_t>(names_offset));
    WriteU32(prefix, 0x10U, static_cast<std::uint32_t>(data_offset));
    WriteU32(prefix, 0x14U, 0U);
    WriteU32(prefix, 0x18U, payload_bytes);
    WriteText(prefix, names_offset, member_name);

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output)
        return false;
    output.write(reinterpret_cast<const char*>(prefix.data()),
        static_cast<std::streamsize>(prefix.size()));
    if (payload_bytes != 0U)
    {
        const std::uint64_t last_byte = data_offset +
            static_cast<std::uint64_t>(payload_bytes) - 1U;
        output.seekp(static_cast<std::streamoff>(last_byte), std::ios::beg);
        output.put('\0');
    }
    return output.good();
}

std::vector<std::byte> MakeDataHog()
{
    return MakeHog({HogMember{.name = "CELL.HOG", .payload = Bytes("xyz")}});
}

std::vector<std::byte> MakeEmptyHog()
{
    return MakeHog({});
}

std::vector<std::byte> MakeDirect24Tdx(const std::uint8_t seed)
{
    constexpr std::uint16_t width = 16;
    constexpr std::uint16_t height = 16;
    constexpr std::uint32_t descriptor_bytes = 128;
    constexpr std::uint32_t primary_base = 0x20;
    constexpr std::uint32_t primary_start = primary_base + descriptor_bytes;
    constexpr std::uint32_t payload_bytes = width * height * 3U;
    constexpr std::uint32_t stride = primary_start + payload_bytes;

    std::vector<std::byte> bytes(64, std::byte{0});
    WriteU16(bytes, 0x00, 5);
    WriteU16(bytes, 0x02, 0);
    WriteU16(bytes, 0x04, width);
    WriteU16(bytes, 0x06, height);
    WriteU16(bytes, 0x08, 24);
    WriteU16(bytes, 0x0A, 0x01);
    WriteU16(bytes, 0x0C, 1);
    WriteU16(bytes, 0x0E, 3);
    WriteU16(bytes, 0x22, 1);
    WriteU16(bytes, 0x24, 1);
    WriteU16(bytes, 0x26, 0);
    WriteU16(bytes, 0x34, descriptor_bytes);
    WriteU16(bytes, 0x36, 0);
    WriteU32(bytes, 0x38, stride);

    std::vector<std::byte> block(stride, std::byte{0});
    WriteU32(block, 0x18, primary_base);
    WriteU32(block, 0x1C, primary_base);
    WriteU32(block, 0x00, 0x20);
    constexpr std::size_t object = primary_base + 0x20;
    WriteU32(block, object + 0x04, 0x01U << 24U);
    WriteU32(block, object + 0x20, width);
    WriteU32(block, object + 0x24, height);
    WriteU32(block, object + 0x40, payload_bytes / 4U);
    WriteU32(block, object + 0x54, 0);
    for (std::uint32_t index = 0; index < payload_bytes; ++index)
        block[primary_start + index] =
            static_cast<std::byte>(static_cast<std::uint8_t>(seed + index));
    bytes.insert(bytes.end(), block.begin(), block.end());
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
    if (error)
        return false;
    std::filesystem::create_directories(root / "ZMEDIA", error);
    return !error &&
           WriteText(root / "SYSTEM.CNF",
               "BOOT2 = cdrom0:\\SCUS_972.64;1\r\nVER = 1.00\r\nVMODE = NTSC\r\n") &&
           WriteText(root / "SCUS_972.64", "synthetic placeholder") &&
           WriteBytes(root / "GAMEDATA" / "MINSK" / "DATA.HOG", MakeDataHog()) &&
           WriteBytes(root / "GAMEDATA" / "MINSK" / "DATA.POP", MakePop()) &&
           WriteBytes(root / "GAMEDATA" / "MINSK" / "TEX.HOG", MakeEmptyHog()) &&
           WriteBytes(root / "GAMEDATA" / "MINSK" / "MAPTEX.HOG", MakeEmptyHog()) &&
           WriteBytes(root / "ZMEDIA" / "ZMOVIES.HOG",
               MakeHog({HogMember{.name = kSyntheticOpeningMovieMember,
                   .payload = Bytes(kSyntheticOpeningMoviePayload)}}));
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

void CheckStartupDiagnostic(const omega::runtime::ContentStartupError& error,
    const std::string_view expected_category, const std::string_view message)
{
    const auto diagnostic = omega::runtime::DescribeContentStartupError(error);
    Check(diagnostic && diagnostic->category == expected_category &&
              diagnostic->message == error.message &&
              diagnostic->message.data() == error.message.data(),
        message);
}

void RunContentStartupDiagnosticShapeTests()
{
    constexpr std::array codes{
        omega::runtime::ContentStartupErrorCode::InvalidOptions,
        omega::runtime::ContentStartupErrorCode::GameData,
        omega::runtime::ContentStartupErrorCode::LevelTextures,
        omega::runtime::ContentStartupErrorCode::DebugImage,
    };
    constexpr std::array<std::uint8_t, 4> required_nested_masks{0U, 1U, 2U, 0U};
    constexpr std::array<std::string_view, 4> expected_categories{
        "invalid-options",
        "missing-required-file",
        "invalid-reference",
        "debug-image",
    };

    for (std::size_t code_index = 0; code_index < codes.size(); ++code_index)
    {
        for (std::uint8_t nested_mask = 0U; nested_mask < 4U; ++nested_mask)
        {
            omega::runtime::ContentStartupError error{
                .code = codes[code_index],
                .message = "borrowed startup diagnostic",
            };
            if ((nested_mask & 1U) != 0U)
            {
                error.game_data_error.emplace();
                error.game_data_error->code =
                    omega::content::GameDataErrorCode::MissingRequiredFile;
            }
            if ((nested_mask & 2U) != 0U)
            {
                error.level_texture_error.emplace();
                error.level_texture_error->code =
                    omega::content::LevelTextureStoreErrorCode::InvalidReference;
            }

            const auto diagnostic = omega::runtime::DescribeContentStartupError(error);
            const bool should_succeed = nested_mask == required_nested_masks[code_index];
            Check(diagnostic.has_value() == should_succeed,
                "the startup diagnostic adapter accepts only the nested shape for its outer code");
            if (diagnostic)
            {
                Check(diagnostic->category == expected_categories[code_index] &&
                          diagnostic->message == error.message &&
                          diagnostic->message.data() == error.message.data(),
                    "a valid startup diagnostic borrows the outer message and stable category");
            }
            else
            {
                Check(diagnostic.error() ==
                          omega::runtime::ContentStartupDiagnosticErrorCode::
                              InconsistentRepresentation,
                    "an invalid startup diagnostic shape has the stable typed error");
            }
        }
    }

    omega::runtime::ContentStartupError empty_message{
        .code = omega::runtime::ContentStartupErrorCode::InvalidOptions,
    };
    const auto empty_message_diagnostic =
        omega::runtime::DescribeContentStartupError(empty_message);
    Check(!empty_message_diagnostic &&
              empty_message_diagnostic.error() ==
                  omega::runtime::ContentStartupDiagnosticErrorCode::
                      InconsistentRepresentation,
        "an empty outer startup message is rejected");

    omega::runtime::ContentStartupError invalid_outer{
        .code = static_cast<omega::runtime::ContentStartupErrorCode>(0xFFU),
        .message = "invalid outer code",
    };
    const auto invalid_outer_diagnostic =
        omega::runtime::DescribeContentStartupError(invalid_outer);
    Check(!invalid_outer_diagnostic &&
              invalid_outer_diagnostic.error() ==
                  omega::runtime::ContentStartupDiagnosticErrorCode::
                      InconsistentRepresentation &&
              omega::runtime::ContentStartupErrorCodeName(invalid_outer.code) == "unknown",
        "an unknown outer startup code is rejected despite its stable code-name fallback");

    omega::runtime::ContentStartupError invalid_game_data_code{
        .code = omega::runtime::ContentStartupErrorCode::GameData,
        .message = "unknown nested game-data code",
    };
    invalid_game_data_code.game_data_error.emplace();
    invalid_game_data_code.game_data_error->code =
        static_cast<omega::content::GameDataErrorCode>(0xFFU);
    const auto invalid_game_data_diagnostic =
        omega::runtime::DescribeContentStartupError(invalid_game_data_code);
    Check(!invalid_game_data_diagnostic &&
              invalid_game_data_diagnostic.error() ==
                  omega::runtime::ContentStartupDiagnosticErrorCode::
                      InconsistentRepresentation &&
              omega::content::GameDataErrorCodeName(
                  invalid_game_data_code.game_data_error->code) == "unknown",
        "an unknown nested game-data code is rejected despite its stable name fallback");

    omega::runtime::ContentStartupError invalid_level_texture_code{
        .code = omega::runtime::ContentStartupErrorCode::LevelTextures,
        .message = "unknown nested level-texture code",
    };
    invalid_level_texture_code.level_texture_error.emplace();
    invalid_level_texture_code.level_texture_error->code =
        static_cast<omega::content::LevelTextureStoreErrorCode>(0xFFU);
    const auto invalid_level_texture_diagnostic =
        omega::runtime::DescribeContentStartupError(invalid_level_texture_code);
    Check(!invalid_level_texture_diagnostic &&
              invalid_level_texture_diagnostic.error() ==
                  omega::runtime::ContentStartupDiagnosticErrorCode::
                      InconsistentRepresentation &&
              omega::content::LevelTextureStoreErrorCodeName(
                  invalid_level_texture_code.level_texture_error->code) == "unknown",
        "an unknown nested level-texture code is rejected despite its stable name fallback");

    const omega::runtime::ContentStartupError debug_image_error{
        .code = omega::runtime::ContentStartupErrorCode::DebugImage,
        .message = "synthetic debug-image failure",
    };
    CheckStartupDiagnostic(debug_image_error, "debug-image",
        "a direct debug-image startup error has a borrowed stable diagnostic");
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

std::expected<omega::asset::LevelContentIR, omega::content::GameDataError>
LoadContentWithLimits(const std::filesystem::path& root,
    const omega::asset::LevelManifestIR& manifest, const omega::asset::DecodeLimits limits,
    const std::uint64_t maximum_nested_hog_bytes = 64ULL * 1024ULL * 1024ULL)
{
    omega::content::GameDataServiceConfig config{.root = root};
    config.maximum_nested_hog_bytes = maximum_nested_hog_bytes;
    config.decode_limits = limits;
    auto service = omega::content::GameDataService::Open(std::move(config));
    if (!service)
        return std::unexpected(service.error());
    return service->LoadLevelContent(manifest);
}

void RunSourceResolverTests(const std::filesystem::path& root)
{
    using Access = omega::content::GameDataServiceTestAccess;
    auto service = omega::content::GameDataService::Open({.root = root});
    Check(service.has_value(), "source resolver fixture service opens");
    if (!service)
        return;
    const auto binding = Access::Bind(*service);
    const auto game_path = std::string{"GAMEDATA/MINSK/DATA.HOG"};

    const auto locator_scratch = [](const omega::asset::SourceLocator& locator) {
        std::uint64_t bytes =
            sizeof(std::string) + sizeof(std::vector<std::string>) + locator.game_path.size() +
            locator.hog_entries.size() * sizeof(std::string);
        for (const auto& component : locator.hog_entries)
            bytes += component.size();
        return bytes;
    };
    const auto directory_scratch = [](const std::initializer_list<std::string_view> names) {
        using ScratchDirectory = std::unordered_map<std::string, const void*>;
        std::uint64_t bytes = names.size() *
            (sizeof(ScratchDirectory::value_type) + 3U * sizeof(void*));
        for (const auto name : names)
            bytes += name.size();
        return bytes;
    };

    const auto direct_hog = MakeDataHog();
    const omega::asset::SourceLocator direct_locator{.game_path = game_path};
    const std::uint64_t direct_locator_scratch = locator_scratch(direct_locator);
    auto direct_file = Access::Resolve(*service, binding, direct_locator);
    Check(direct_file && direct_file->terminal_bytes == direct_hog &&
              direct_file->ancestor_input_bytes == 0U &&
              direct_file->ancestor_directory_items == 0U &&
              direct_file->archive_depth == 0U &&
              direct_file->peak_scratch_bytes == direct_locator_scratch,
        "empty source chain returns the owned VFS file without charging a terminal parser");

    auto direct_scratch_limits = omega::asset::DecodeLimits{};
    direct_scratch_limits.maximum_scratch_bytes = direct_locator_scratch;
    Check(Access::Resolve(*service, binding, direct_locator, direct_scratch_limits).has_value(),
        "source resolver accepts the exact normalized-locator scratch limit");
    --direct_scratch_limits.maximum_scratch_bytes;
    const std::filesystem::path direct_hog_path =
        root / "GAMEDATA" / "MINSK" / "DATA.HOG";
    std::error_code direct_hog_error;
    const bool direct_hog_removed = std::filesystem::remove(
        direct_hog_path, direct_hog_error);
    Check(direct_hog_removed && !direct_hog_error,
        "direct source is removed after the service freezes its VFS mapping");
    if (direct_hog_removed && !direct_hog_error)
    {
        CheckDecodeError(Access::Resolve(
                             *service, binding, direct_locator, direct_scratch_limits),
            omega::asset::DecodeErrorCode::LimitExceeded,
            "source resolver rejects one below normalized-locator scratch before VFS I/O");
        Check(WriteBytes(direct_hog_path, direct_hog),
            "direct source is restored after scratch-ordering coverage");
    }

    auto direct_leaf = Access::Resolve(*service, binding, omega::asset::SourceLocator{
        .game_path = game_path,
        .hog_entries = {"cell.hog"},
    });
    Check(direct_leaf && direct_leaf->terminal_bytes == Bytes("xyz") &&
              direct_leaf->ancestor_input_bytes == direct_hog.size() &&
              direct_leaf->ancestor_directory_items == 1U &&
              direct_leaf->archive_depth == 0U,
        "direct source member charges one ancestor archive and excludes the terminal leaf");

    auto exact_limits = omega::asset::DecodeLimits{};
    exact_limits.maximum_input_bytes = direct_hog.size() + 3U;
    exact_limits.maximum_items = 1U;
    exact_limits.maximum_nesting_depth = 0U;
    Check(Access::Resolve(*service, binding, omega::asset::SourceLocator{
              .game_path = game_path,
              .hog_entries = {"CELL.HOG"},
          }, exact_limits)
              .has_value(),
        "direct source member accepts exact input, item, and depth limits");
    --exact_limits.maximum_input_bytes;
    CheckDecodeError(Access::Resolve(*service, binding, omega::asset::SourceLocator{
                         .game_path = game_path,
                         .hog_entries = {"CELL.HOG"},
                     }, exact_limits),
        omega::asset::DecodeErrorCode::LimitExceeded,
        "direct source member rejects a one-below cumulative input limit");
    exact_limits.maximum_input_bytes = direct_hog.size() + 3U;
    exact_limits.maximum_items = 0U;
    CheckDecodeError(Access::Resolve(*service, binding, omega::asset::SourceLocator{
                         .game_path = game_path,
                         .hog_entries = {"CELL.HOG"},
                     }, exact_limits),
        omega::asset::DecodeErrorCode::LimitExceeded,
        "direct source member rejects a one-below ancestor-directory item limit");

    const auto leaf_bytes = Bytes("resolved-leaf");
    const auto inner_hog = MakeHog({HogMember{
        .name = "SECRET-LEAF.BIN",
        .payload = leaf_bytes,
    }});
    const auto outer_hog = MakeHog({HogMember{
        .name = "InNeR.HoG",
        .payload = inner_hog,
    }});
    Check(WriteBytes(root / "GAMEDATA" / "MINSK" / "DATA.HOG", outer_hog),
        "nested source resolver fixture is written");

    auto terminal_container = Access::Resolve(*service, binding,
        omega::asset::SourceLocator{
            .game_path = game_path,
            .hog_entries = {"inner.hog"},
        });
    Check(terminal_container && terminal_container->terminal_bytes == inner_hog &&
              terminal_container->ancestor_input_bytes == outer_hog.size() &&
              terminal_container->ancestor_directory_items == 1U &&
              terminal_container->archive_depth == 0U,
        "a final locator component may return an owned nested container");

    auto terminal_cap_config = omega::content::GameDataServiceConfig{.root = root};
    terminal_cap_config.maximum_nested_hog_bytes = inner_hog.size() - 1U;
    auto terminal_cap_service =
        omega::content::GameDataService::Open(std::move(terminal_cap_config));
    Check(terminal_cap_service.has_value(), "terminal-container cap fixture service opens");
    if (terminal_cap_service)
    {
        const auto terminal_cap_binding = Access::Bind(*terminal_cap_service);
        CheckDecodeError(Access::Resolve(*terminal_cap_service, terminal_cap_binding,
                             omega::asset::SourceLocator{
                                 .game_path = game_path,
                                 .hog_entries = {"INNER.HOG"},
                             }),
            omega::asset::DecodeErrorCode::LimitExceeded,
            "terminal HOG member obeys the configured nested-container byte cap");
    }

    const omega::asset::SourceLocator nested_locator{
        .game_path = game_path,
        .hog_entries = {"inner.hog", "secret-leaf.bin"},
    };
    const std::uint64_t nested_resolver_scratch = locator_scratch(nested_locator) +
        std::max(directory_scratch({"InNeR.HoG"}),
            directory_scratch({"SECRET-LEAF.BIN"}));
    auto nested_leaf = Access::Resolve(*service, binding, nested_locator);
    Check(nested_leaf && nested_leaf->terminal_bytes == leaf_bytes &&
              nested_leaf->ancestor_input_bytes == outer_hog.size() + inner_hog.size() &&
              nested_leaf->ancestor_directory_items == 2U &&
              nested_leaf->archive_depth == 1U &&
              nested_leaf->peak_scratch_bytes == nested_resolver_scratch,
        "nested source resolution returns exact ancestor input, directory items, and depth");

    auto nested_scratch_limits = omega::asset::DecodeLimits{};
    nested_scratch_limits.maximum_scratch_bytes = nested_resolver_scratch;
    Check(Access::Resolve(*service, binding, nested_locator, nested_scratch_limits).has_value(),
        "nested source resolver accepts exact locator-plus-directory scratch");
    --nested_scratch_limits.maximum_scratch_bytes;
    CheckDecodeError(Access::Resolve(
                         *service, binding, nested_locator, nested_scratch_limits),
        omega::asset::DecodeErrorCode::LimitExceeded,
        "nested source resolver rejects one below peak sequential-directory scratch");

    auto nested_limits = omega::asset::DecodeLimits{};
    nested_limits.maximum_input_bytes =
        outer_hog.size() + inner_hog.size() + leaf_bytes.size();
    nested_limits.maximum_items = 2U;
    nested_limits.maximum_nesting_depth = 1U;
    Check(Access::Resolve(*service, binding, nested_locator, nested_limits).has_value(),
        "nested source resolution accepts exact cumulative limits");
    nested_limits.maximum_nesting_depth = 0U;
    CheckDecodeError(Access::Resolve(*service, binding, nested_locator, nested_limits),
        omega::asset::DecodeErrorCode::LimitExceeded,
        "nested source resolution rejects one-below depth before lookup");
    nested_limits.maximum_nesting_depth = 1U;
    --nested_limits.maximum_input_bytes;
    CheckDecodeError(Access::Resolve(*service, binding, nested_locator, nested_limits),
        omega::asset::DecodeErrorCode::LimitExceeded,
        "nested source resolution rejects one-below cumulative input");
    ++nested_limits.maximum_input_bytes;
    nested_limits.maximum_items = 1U;
    CheckDecodeError(Access::Resolve(*service, binding, nested_locator, nested_limits),
        omega::asset::DecodeErrorCode::LimitExceeded,
        "nested source resolution rejects one-below cumulative directory items");

    const auto colliding_hog = MakeHog({
        HogMember{.name = "Secret.bin", .payload = leaf_bytes},
        HogMember{.name = "SECRET.BIN", .payload = leaf_bytes},
    });
    Check(WriteBytes(root / "GAMEDATA" / "MINSK" / "DATA.HOG", colliding_hog),
        "normalized-collision source fixture is written");
    auto collision = Access::Resolve(*service, binding, omega::asset::SourceLocator{
        .game_path = game_path,
        .hog_entries = {"SECRET.BIN"},
    });
    CheckDecodeError(collision, omega::asset::DecodeErrorCode::DuplicateReference,
        "source resolver rejects a normalized archive-name collision");
    Check(!collision && collision.error().message.find("SECRET") == std::string::npos &&
              (!collision.error().decode_error ||
                  collision.error().decode_error->message.find("SECRET") == std::string::npos),
        "source resolver collision diagnostics do not echo member identity");

    Check(WriteBytes(root / "GAMEDATA" / "MINSK" / "DATA.HOG", direct_hog),
        "valid source resolver fixture is restored");
    auto foreign_service = omega::content::GameDataService::Open({.root = root});
    Check(foreign_service.has_value(), "foreign source resolver fixture service opens");
    if (foreign_service)
    {
        auto foreign = Access::Resolve(*foreign_service, binding,
            omega::asset::SourceLocator{.game_path = "MISSING/SECRET.HOG"});
        CheckError(foreign, omega::content::GameDataErrorCode::ForeignService,
            "foreign source binding is rejected before locator validation or I/O");
        Check(!foreign && foreign.error().message.find("MISSING") == std::string::npos &&
                  foreign.error().message.find("SECRET") == std::string::npos,
            "foreign-binding diagnostics do not echo locator identity");
    }

    auto move_source = omega::content::GameDataService::Open({.root = root});
    auto move_destination = omega::content::GameDataService::Open({.root = root});
    Check(move_source && move_destination, "move-identity resolver services open");
    if (move_source && move_destination)
    {
        const auto source_binding = Access::Bind(*move_source);
        const auto overwritten_binding = Access::Bind(*move_destination);
        *move_destination = std::move(*move_source);
        Check(Access::Resolve(*move_destination, source_binding,
                  omega::asset::SourceLocator{.game_path = game_path})
                  .has_value(),
            "service move-assignment preserves the moved source identity");
        CheckError(Access::Resolve(*move_destination, overwritten_binding,
                       omega::asset::SourceLocator{.game_path = game_path}),
            omega::content::GameDataErrorCode::ForeignService,
            "service move-assignment invalidates the overwritten destination identity");

        auto move_constructed = std::move(*move_destination);
        Check(Access::Resolve(move_constructed, source_binding,
                  omega::asset::SourceLocator{.game_path = game_path})
                  .has_value(),
            "service move construction preserves source identity");
    }
}
} // namespace

int GameDataServiceFailureCount()
{
    static_assert(std::is_move_constructible_v<omega::asset::OpeningMovieSource>);
    static_assert(!std::is_move_assignable_v<omega::asset::OpeningMovieSource>);
    static_assert(!std::is_copy_constructible_v<omega::asset::OpeningMovieSource>);
    static_assert(!std::is_copy_assignable_v<omega::asset::OpeningMovieSource>);
    static_assert(omega::asset::kOpeningMovieMaximumSourceBytes ==
                  512ULL * 1024ULL * 1024ULL);
    static_assert(sizeof(omega::runtime::ContentStartupStage) == 1U);
    static_assert(sizeof(omega::runtime::ContentStartupStateErrorCode) == 1U);
    static_assert(sizeof(omega::runtime::ContentStartupDiagnosticErrorCode) == 1U);
    static_assert(static_cast<std::uint8_t>(
                      omega::runtime::ContentStartupStage::NoContent) == 0U);
    static_assert(static_cast<std::uint8_t>(
                      omega::runtime::ContentStartupStage::DataMounted) == 1U);
    static_assert(static_cast<std::uint8_t>(
                      omega::runtime::ContentStartupStage::LevelContent) == 2U);
    static_assert(static_cast<std::uint8_t>(
                      omega::runtime::ContentStartupStateErrorCode::InconsistentOwnership) ==
                  0U);
    static_assert(static_cast<std::uint8_t>(
                      omega::runtime::ContentStartupDiagnosticErrorCode::
                          InconsistentRepresentation) == 0U);
    static_assert(noexcept(omega::runtime::ClassifyContentStartupState(
        std::declval<const omega::runtime::ContentStartupState&>())));
    static_assert(noexcept(omega::runtime::DescribeContentStartupError(
        std::declval<const omega::runtime::ContentStartupError&>())));

    RunContentStartupDiagnosticShapeTests();

    const omega::runtime::ContentStartupState empty_startup_state;
    const auto empty_startup_stage =
        omega::runtime::ClassifyContentStartupState(empty_startup_state);
    Check(empty_startup_stage &&
              *empty_startup_stage == omega::runtime::ContentStartupStage::NoContent,
        "an empty startup ownership state classifies as NoContent");

    omega::runtime::ContentStartupState manifest_only_state;
    manifest_only_state.level_manifest.emplace();
    const auto manifest_only_stage =
        omega::runtime::ClassifyContentStartupState(manifest_only_state);
    Check(!manifest_only_stage &&
              manifest_only_stage.error() ==
                  omega::runtime::ContentStartupStateErrorCode::InconsistentOwnership,
        "a manifest-only startup ownership state is rejected");

    omega::runtime::ContentStartupState debug_only_state;
    debug_only_state.debug_image.emplace();
    const auto debug_only_stage =
        omega::runtime::ClassifyContentStartupState(debug_only_state);
    Check(!debug_only_stage &&
              debug_only_stage.error() ==
                  omega::runtime::ContentStartupStateErrorCode::InconsistentOwnership,
        "a debug-image-only startup ownership state is rejected");

    Check(omega::asset::DecodeLimits{}.maximum_input_bytes == 64ULL * 1024ULL * 1024ULL,
        "standalone decoders retain their narrower default input budget");
    Check(omega::content::GameDataServiceConfig{}.decode_limits.maximum_input_bytes ==
              72ULL * 1024ULL * 1024ULL,
        "the service default shared input budget covers confirmed whole-level composition");
    Check(MakeEmptyHog().size() == 32U,
        "empty texture-container fixtures retain a bounded canonical HOG header");

    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path() /
        ("openomega-game-data-tests-" + std::to_string(nonce));
    const auto private_missing_root = root.parent_path() /
        ("PrivateUser-SecretVault-missing-game-data-" + std::to_string(nonce));
    std::error_code missing_root_error;
    std::filesystem::remove_all(private_missing_root, missing_root_error);
    auto missing_root_service =
        omega::content::GameDataService::Open({.root = private_missing_root});
    Check(!missing_root_service &&
              missing_root_service.error().code ==
                  omega::content::GameDataErrorCode::MountFailed &&
              missing_root_service.error().message == "unable to mount game-data root" &&
              missing_root_service.error().message.find("PrivateUser") == std::string::npos &&
              missing_root_service.error().message.find(private_missing_root.string()) ==
                  std::string::npos,
        "missing private-looking roots fail with a fixed host-path-free diagnostic");
    Check(MakeValidTree(root), "synthetic NTSC-U-like data tree is created");

    RunSourceResolverTests(root);

    auto service = omega::content::GameDataService::Open({.root = root});
    Check(service.has_value(), "valid owner-supplied data tree opens");
    if (service)
    {
        Check(service->identity().build == omega::content::RetailBuild::NtscUScus97264 &&
                  service->identity().boot_executable == "SCUS_972.64",
            "validated retail identity is published without executable contents");
        auto opening_movie = service->LoadOpeningMovieSource(
            kSyntheticOpeningMovieMember);
        Check(opening_movie &&
                  std::ranges::equal(opening_movie->bytes(),
                      Bytes(kSyntheticOpeningMoviePayload)),
            "the fixed movie archive returns exactly the explicitly selected owned member");
        if (opening_movie)
        {
            omega::asset::OpeningMovieSource moved = std::move(*opening_movie);
            Check(opening_movie->empty() &&
                      std::ranges::equal(moved.bytes(),
                          Bytes(kSyntheticOpeningMoviePayload)),
                "opening movie bytes have one observable move-only owner");
        }
        auto mixed_case_opening_movie = service->LoadOpeningMovieSource(
            "mixed-opening.pss");
        Check(mixed_case_opening_movie &&
                  std::ranges::equal(mixed_case_opening_movie->bytes(),
                      Bytes(kSyntheticOpeningMoviePayload)),
            "explicit archive-member lookup follows the existing case-insensitive game path rule");
        auto missing_opening_movie = service->LoadOpeningMovieSource(
            "PrivateOwner-MissingMovie.pss");
        CheckDecodeError(missing_opening_movie,
            omega::asset::DecodeErrorCode::InvalidReference,
            "a missing explicit movie member has a typed invalid-reference failure");
        Check(!missing_opening_movie &&
                  missing_opening_movie.error().message.find("PrivateOwner") ==
                      std::string::npos &&
                  (!missing_opening_movie.error().decode_error ||
                      missing_opening_movie.error().decode_error->message.find(
                          "PrivateOwner") == std::string::npos),
            "missing movie-member diagnostics do not echo the selected identity");
        auto manifest = service->LoadLevelManifest("minsk");
        Check(manifest.has_value(), "named level loads through the frozen VFS");
        if (manifest)
        {
            Check(manifest->data_hog_source.game_path == "GAMEDATA/MINSK/DATA.HOG",
                "level bootstrap publishes a normalized canonical source path");
            Check(manifest->texture_sources.size() == 2U &&
                      manifest->texture_sources[0].game_path ==
                          "GAMEDATA/MINSK/TEX.HOG" &&
                      manifest->texture_sources[0].hog_entries.empty() &&
                      manifest->texture_sources[1].game_path ==
                          "GAMEDATA/MINSK/MAPTEX.HOG" &&
                      manifest->texture_sources[1].hog_entries.empty(),
                "level bootstrap owns exactly the primary then map texture container sources");
            auto copied_texture_sources = manifest->texture_sources;
            copied_texture_sources[0].game_path.clear();
            Check(copied_texture_sources[0].game_path.empty() &&
                      manifest->texture_sources[0].game_path == "GAMEDATA/MINSK/TEX.HOG",
                "canonical texture source paths are independently owned values");
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

        std::error_code texture_file_error;
        Check(std::filesystem::remove(
                  root / "GAMEDATA" / "MINSK" / "TEX.HOG", texture_file_error) &&
                  !texture_file_error,
            "primary texture-container fixture is removed");
        {
            auto missing_primary_service =
                omega::content::GameDataService::Open({.root = root});
            Check(missing_primary_service.has_value(),
                "a fresh service opens after the primary texture fixture is removed");
            if (missing_primary_service)
            {
                auto missing_primary_texture =
                    missing_primary_service->LoadLevelManifest("MINSK");
                CheckError(missing_primary_texture,
                    omega::content::GameDataErrorCode::MissingRequiredFile,
                    "a missing primary texture container has a stable error category");
                Check(!missing_primary_texture &&
                          missing_primary_texture.error().message ==
                              "level is missing required primary texture container" &&
                          manifest && manifest->texture_sources.size() == 2U,
                    "missing primary texture diagnostics are sanitized and prior sources remain "
                    "owned");
            }
        }
        Check(WriteBytes(
                  root / "GAMEDATA" / "MINSK" / "TEX.HOG", MakeEmptyHog()),
            "primary texture-container fixture is restored");

        texture_file_error.clear();
        Check(std::filesystem::remove(
                  root / "GAMEDATA" / "MINSK" / "MAPTEX.HOG", texture_file_error) &&
                  !texture_file_error,
            "map texture-container fixture is removed");
        {
            auto missing_map_service =
                omega::content::GameDataService::Open({.root = root});
            Check(missing_map_service.has_value(),
                "a fresh service opens after the map texture fixture is removed");
            if (missing_map_service)
            {
                auto missing_map_texture = missing_map_service->LoadLevelManifest("MINSK");
                CheckError(missing_map_texture,
                    omega::content::GameDataErrorCode::MissingRequiredFile,
                    "a missing map texture container has a stable error category");
                Check(!missing_map_texture &&
                          missing_map_texture.error().message ==
                              "level is missing required map texture container" &&
                          manifest && manifest->texture_sources.size() == 2U,
                    "missing map texture diagnostics are sanitized and prior sources remain "
                    "owned");
            }
        }
        Check(WriteBytes(
                  root / "GAMEDATA" / "MINSK" / "MAPTEX.HOG", MakeEmptyHog()),
            "map texture-container fixture is restored");

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

    constexpr std::string_view data_hog_game_path = "GAMEDATA/MINSK/DATA.HOG";
    constexpr std::string_view primary_texture_game_path = "GAMEDATA/MINSK/TEX.HOG";
    constexpr std::string_view map_texture_game_path = "GAMEDATA/MINSK/MAPTEX.HOG";
    constexpr std::string_view resolved_cell_entry = "CELL.HOG";
    const std::uint64_t exact_manifest_output_bytes =
        sizeof(omega::asset::LevelManifestIR) + data_hog_game_path.size() +
        sizeof(omega::asset::LevelCellSourceIR) + resolved_cell_entry.size() +
        2U * sizeof(omega::asset::SourceLocator) + primary_texture_game_path.size() +
        map_texture_game_path.size();
    constexpr std::uint64_t exact_manifest_items = 4U;

    auto exact_manifest_config = omega::content::GameDataServiceConfig{.root = root};
    exact_manifest_config.decode_limits.maximum_output_bytes = exact_manifest_output_bytes;
    exact_manifest_config.decode_limits.maximum_items = exact_manifest_items;
    auto exact_manifest_service =
        omega::content::GameDataService::Open(std::move(exact_manifest_config));
    Check(exact_manifest_service.has_value(),
        "exact manifest result budgets do not prevent root validation");
    if (exact_manifest_service)
    {
        auto exact_manifest = exact_manifest_service->LoadLevelManifest("MINSK");
        Check(exact_manifest && exact_manifest->texture_sources.size() == 2U,
            "exact cumulative item/output budgets admit the complete owned manifest");
    }

    auto short_output_config = omega::content::GameDataServiceConfig{.root = root};
    short_output_config.decode_limits.maximum_output_bytes = exact_manifest_output_bytes - 1U;
    short_output_config.decode_limits.maximum_items = exact_manifest_items;
    auto short_output_service =
        omega::content::GameDataService::Open(std::move(short_output_config));
    Check(short_output_service.has_value(),
        "one-below manifest output budget does not prevent root validation");
    if (short_output_service)
        CheckDecodeError(short_output_service->LoadLevelManifest("MINSK"),
            omega::asset::DecodeErrorCode::LimitExceeded,
            "one-below cumulative manifest output budget fails before publication");

    auto short_item_config = omega::content::GameDataServiceConfig{.root = root};
    short_item_config.decode_limits.maximum_output_bytes = exact_manifest_output_bytes;
    short_item_config.decode_limits.maximum_items = exact_manifest_items - 1U;
    auto short_item_service =
        omega::content::GameDataService::Open(std::move(short_item_config));
    Check(short_item_service.has_value(),
        "one-below manifest item budget does not prevent root validation");
    if (short_item_service)
        CheckDecodeError(short_item_service->LoadLevelManifest("MINSK"),
            omega::asset::DecodeErrorCode::LimitExceeded,
            "one-below cumulative manifest item budget fails before publication");

    omega::runtime::LaunchOptions invalid_startup;
    invalid_startup.level_code = "MINSK";
    auto invalid = omega::runtime::StartContent(invalid_startup);
    Check(!invalid && invalid.error().code ==
              omega::runtime::ContentStartupErrorCode::InvalidOptions,
        "application startup independently enforces its data-root invariant");
    if (!invalid)
    {
        CheckStartupDiagnostic(invalid.error(), "invalid-options",
            "the synthetic invalid-options startup failure has a borrowed diagnostic");
    }

    omega::runtime::LaunchOptions service_only_options;
    service_only_options.data_root = root;
    auto service_only = omega::runtime::StartContent(service_only_options);
    Check(service_only && service_only->game_data && !service_only->level_manifest &&
              !service_only->level_content && !service_only->debug_image &&
              !service_only->level_texture_store,
        "startup without a level leaves the level texture inventory disengaged");
    if (service_only)
    {
        const auto service_only_stage =
            omega::runtime::ClassifyContentStartupState(*service_only);
        Check(service_only_stage &&
                  *service_only_stage ==
                      omega::runtime::ContentStartupStage::DataMounted,
            "a service-only startup ownership state classifies as DataMounted");

        service_only->level_manifest.emplace();
        const auto incomplete_level_stage =
            omega::runtime::ClassifyContentStartupState(*service_only);
        Check(!incomplete_level_stage &&
                  incomplete_level_stage.error() ==
                      omega::runtime::ContentStartupStateErrorCode::InconsistentOwnership,
            "an incomplete level-content startup ownership state is rejected");
    }

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
    Check(startup && startup->game_data && startup->level_manifest && startup->level_content &&
              startup->debug_image && startup->level_texture_store,
        "the exact non-SDL startup path owns validated manifest, spatial, material, debug, and "
        "texture-inventory data");
    if (startup)
    {
        const auto startup_stage = omega::runtime::ClassifyContentStartupState(*startup);
        Check(startup_stage &&
                  *startup_stage == omega::runtime::ContentStartupStage::LevelContent,
            "a complete startup ownership state classifies as LevelContent");
    }

    const auto direct_tdx = MakeDirect24Tdx(0x52);
    Check(WriteBytes(root / "GAMEDATA" / "MINSK" / "TEX.HOG",
              MakeHog({HogMember{.name = "STARTUP.TDX", .payload = direct_tdx}})),
        "a valid direct startup TDX fixture is written");
    auto textured_startup = omega::runtime::StartContent(startup_options);
    Check(textured_startup && textured_startup->game_data &&
              textured_startup->level_texture_store &&
              textured_startup->level_texture_store->size() == 1U,
        "startup inventories one direct TDX without loading or binding it");
    if (textured_startup && textured_startup->game_data &&
        textured_startup->level_texture_store &&
        textured_startup->level_texture_store->size() == 1U)
    {
        auto handle = textured_startup->level_texture_store->HandleAt(0);
        Check(handle.has_value(), "startup texture inventory publishes its bounded handle");
        if (handle)
        {
            auto moved_startup = std::move(*textured_startup);
            auto loaded = moved_startup.level_texture_store->Load(
                *moved_startup.game_data, *handle);
            Check(loaded && loaded->storage.width == 16U && loaded->storage.height == 16U,
                "moving startup state retains the store-to-service binding and handle identity");
        }
    }
    Check(WriteBytes(root / "GAMEDATA" / "MINSK" / "TEX.HOG", MakeEmptyHog()),
        "the empty primary texture fixture is restored after the move proof");

    Check(WriteText(root / "GAMEDATA" / "MINSK" / "TEX.HOG", "not a HOG"),
        "a malformed startup texture source is written");
    auto malformed_texture_startup = omega::runtime::StartContent(startup_options);
    Check(!malformed_texture_startup &&
              malformed_texture_startup.error().code ==
                  omega::runtime::ContentStartupErrorCode::LevelTextures &&
              !malformed_texture_startup.error().game_data_error &&
              malformed_texture_startup.error().level_texture_error &&
              malformed_texture_startup.error().level_texture_error->code ==
                  omega::content::LevelTextureStoreErrorCode::MalformedArchive,
        "startup returns no partial state and preserves the typed malformed-texture error");
    if (!malformed_texture_startup)
    {
        CheckStartupDiagnostic(malformed_texture_startup.error(), "malformed-archive",
            "the synthetic malformed-texture startup failure has a borrowed diagnostic");
    }
    Check(WriteBytes(root / "GAMEDATA" / "MINSK" / "TEX.HOG", MakeEmptyHog()),
        "the empty primary texture fixture is restored after the typed startup failure");

    if (startup && startup->level_manifest && startup->level_content && startup->debug_image)
    {
        Check(startup->level_manifest->terrain_cells.size() == 2 &&
                  startup->level_content->spatial.terrain_cells.size() == 2 &&
                  startup->level_content->material_catalogs.terrain_cells.size() == 2 &&
                  startup->level_content->material_catalogs.terrain_cells[0].names[0] ==
                      "BASE.TDX" &&
                  startup->level_content->material_catalogs.terrain_cells[1].names[0] ==
                      "CELL.TDX" &&
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
                  changed_geometry_startup->level_content &&
                  changed_geometry_startup->debug_image,
            "startup rebuilds the synthetic diagnostic from changed canonical spatial geometry");
        if (changed_geometry_startup && changed_geometry_startup->level_manifest &&
            changed_geometry_startup->level_content &&
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
                      changed_geometry_startup->level_content->material_catalogs ==
                          startup->level_content->material_catalogs &&
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
        Check(changed_material_startup && changed_material_startup->level_content &&
                  changed_material_startup->debug_image &&
                  changed_material_startup->level_content->spatial ==
                      startup->level_content->spatial &&
                  changed_material_startup->level_content->material_catalogs !=
                      startup->level_content->material_catalogs &&
                  changed_material_startup->level_content->material_catalogs.terrain_cells[1]
                          .names[0] == "NEXT.TDX" &&
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

            auto content = spatial_service->LoadLevelContent(*spatial_manifest);
            Check(content && content->spatial.terrain_cells.size() == 2 &&
                      content->material_catalogs.terrain_cells.size() == 2 &&
                      content->spatial.terrain_cells[0].vertices[1].x == 2.0F &&
                      content->spatial.terrain_cells[1].vertices[1].x == 1.0F &&
                      content->material_catalogs.terrain_cells[0].names[0] == "BASE.TDX" &&
                      content->material_catalogs.terrain_cells[1].names[0] == "CELL.TDX",
                "single-pass level content preserves both canonical collections in manifest order");
            if (content)
            {
                const omega::asset::LevelContentIR owned = *content;
                Check(WriteText(root / "GAMEDATA" / "MINSK" / "DATA.HOG", "replaced"),
                    "single-pass content source archive is replaced after decoding");
                Check(owned.spatial.terrain_cells[0].vertices[1] ==
                          omega::asset::Float3IR{2.0F, 0.0F, 0.0F} &&
                          owned.material_catalogs.terrain_cells[1].materials[1]
                                  .name_indices[2] == 1,
                    "single-pass level content owns meshes and catalogs after source replacement");
                Check(WriteBytes(root / "GAMEDATA" / "MINSK" / "DATA.HOG",
                          spatial_fixture.data_hog),
                    "valid content source archive is restored after ownership proof");
            }

            std::array<bool, 4> concurrent_results{};
            {
                std::barrier start_line(static_cast<std::ptrdiff_t>(concurrent_results.size()));
                std::vector<std::jthread> workers;
                workers.reserve(concurrent_results.size());
                for (std::size_t worker = 0; worker < concurrent_results.size(); ++worker)
                {
                    workers.emplace_back([&, worker] {
                        start_line.arrive_and_wait();
                        bool matches = true;
                        for (std::size_t repetition = 0; repetition < 8; ++repetition)
                        {
                            auto loaded_content =
                                spatial_service->LoadLevelContent(*spatial_manifest);
                            if (!loaded_content || !content || *loaded_content != *content)
                                matches = false;
                        }
                        concurrent_results[worker] = matches;
                    });
                }
            }
            Check(std::ranges::all_of(concurrent_results, [](const bool value) { return value; }),
                "immutable single-pass level content loads agree across concurrent worker bursts");

            auto empty_manifest = *spatial_manifest;
            empty_manifest.terrain_cells.clear();
            auto empty_limits = omega::asset::DecodeLimits{};
            empty_limits.maximum_nesting_depth = 0;
            auto empty_content =
                LoadContentWithLimits(root, empty_manifest, empty_limits);
            Check(empty_content && empty_content->spatial.terrain_cells.empty() &&
                      empty_content->material_catalogs.terrain_cells.empty(),
                "empty level content needs no cell archive depth and returns paired empty state");

            auto nested_manifest = *spatial_manifest;
            nested_manifest.data_hog_source.hog_entries = {"INNER.HOG"};
            const auto nested_source = MakeHog(
                {HogMember{.name = "INNER.HOG", .payload = spatial_fixture.data_hog}});
            Check(WriteBytes(root / "GAMEDATA" / "MINSK" / "DATA.HOG", nested_source),
                "single-pass nested source-chain fixture is written");
            auto nested_content = spatial_service->LoadLevelContent(nested_manifest);
            Check(nested_content && content && *nested_content == *content,
                "single-pass content resolves a non-empty container-only source chain");
            constexpr std::uint64_t nested_material_items = 34;
            auto nested_material_limits = omega::asset::DecodeLimits{};
            nested_material_limits.maximum_items = nested_material_items;
            Check(LoadMaterialCatalogsWithLimits(
                      root, nested_manifest, nested_material_limits).has_value(),
                "exact nested-source level-material item budget succeeds");
            nested_material_limits.maximum_items = nested_material_items - 1U;
            CheckDecodeError(
                LoadMaterialCatalogsWithLimits(root, nested_manifest, nested_material_limits),
                omega::asset::DecodeErrorCode::LimitExceeded,
                "one-below nested-source level-material item budget fails");
            auto nested_limits = omega::asset::DecodeLimits{};
            nested_limits.maximum_nesting_depth = 1;
            CheckDecodeError(LoadContentWithLimits(root, nested_manifest, nested_limits),
                omega::asset::DecodeErrorCode::LimitExceeded,
                "source chain plus cell edge rejects a one-below combined depth budget");
            Check(WriteBytes(root / "GAMEDATA" / "MINSK" / "DATA.HOG",
                      spatial_fixture.data_hog),
                "direct single-pass source archive is restored after nested-chain coverage");

            auto repeated_manifest = *spatial_manifest;
            repeated_manifest.terrain_cells[1].data_hog_entry =
                repeated_manifest.terrain_cells[0].data_hog_entry;
            auto repeated_materials =
                spatial_service->LoadLevelMaterialCatalogs(repeated_manifest);
            Check(repeated_materials && repeated_materials->terrain_cells.size() == 2 &&
                      repeated_materials->terrain_cells[0] ==
                          repeated_materials->terrain_cells[1],
                "repeated manifest references preserve cardinality instead of deduplicating");
            auto repeated_content = spatial_service->LoadLevelContent(repeated_manifest);
            Check(repeated_content && repeated_content->spatial.terrain_cells.size() == 2 &&
                      repeated_content->material_catalogs.terrain_cells.size() == 2 &&
                      repeated_content->spatial.terrain_cells[0] ==
                          repeated_content->spatial.terrain_cells[1] &&
                      repeated_content->material_catalogs.terrain_cells[0] ==
                          repeated_content->material_catalogs.terrain_cells[1],
                "single-pass loading preserves repeated manifest cardinality without deduplication");

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
            CheckDecodeError(spatial_service->LoadLevelContent(*spatial_manifest),
                omega::asset::DecodeErrorCode::DuplicateReference,
                "single-pass loading rejects case-colliding outer HOG names before cell lookup");

            const auto no_col_cell = MakeCellHog(
                "CeLlA.vUm", spatial_fixture.vum_a, std::vector<HogMember>{});
            Check(WriteBytes(root / "GAMEDATA" / "MINSK" / "DATA.HOG",
                      MakeSpatialDataHog(no_col_cell, spatial_fixture.cell_b)),
                "cell archive without a COL member is written");
            CheckDecodeError(spatial_service->LoadLevelSpatial(*spatial_manifest),
                omega::asset::DecodeErrorCode::InvalidReference,
                "a cell archive with zero COL members has a typed invalid-reference error");
            CheckDecodeError(spatial_service->LoadLevelContent(*spatial_manifest),
                omega::asset::DecodeErrorCode::InvalidReference,
                "single-pass content requires exactly one COL member per cell");

            const auto two_col_cell = MakeCellHog("CeLlA.vUm", spatial_fixture.vum_a,
                {HogMember{.name = "aLpHa.CoL", .payload = spatial_fixture.col_a},
                    HogMember{.name = "other.COL", .payload = spatial_fixture.col_a}});
            Check(WriteBytes(root / "GAMEDATA" / "MINSK" / "DATA.HOG",
                      MakeSpatialDataHog(two_col_cell, spatial_fixture.cell_b)),
                "cell archive with two COL members is written");
            CheckDecodeError(spatial_service->LoadLevelSpatial(*spatial_manifest),
                omega::asset::DecodeErrorCode::DuplicateReference,
                "a cell archive with two COL members has a typed duplicate-reference error");
            CheckDecodeError(spatial_service->LoadLevelContent(*spatial_manifest),
                omega::asset::DecodeErrorCode::DuplicateReference,
                "single-pass content rejects multiple COL members per cell");

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
            CheckError(spatial_service->LoadLevelContent(*spatial_manifest),
                omega::content::GameDataErrorCode::MalformedArchive,
                "single-pass content preserves the malformed nested-HOG error category");

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
            CheckDecodeError(spatial_service->LoadLevelContent(*spatial_manifest),
                omega::asset::DecodeErrorCode::UnsupportedVariant,
                "typed COL decoder failures survive the single-pass content boundary");

            const auto no_vum_cell = MakeHog(
                {HogMember{.name = "aLpHa.CoL", .payload = spatial_fixture.col_a}}, 11U);
            Check(WriteBytes(root / "GAMEDATA" / "MINSK" / "DATA.HOG",
                      MakeSpatialDataHog(no_vum_cell, spatial_fixture.cell_b)),
                "cell archive without a VUM member is written");
            CheckDecodeError(spatial_service->LoadLevelMaterialCatalogs(*spatial_manifest),
                omega::asset::DecodeErrorCode::InvalidReference,
                "a cell archive with zero VUM members has a typed invalid-reference error");
            CheckDecodeError(spatial_service->LoadLevelContent(*spatial_manifest),
                omega::asset::DecodeErrorCode::InvalidReference,
                "single-pass content requires exactly one VUM member per cell");
            const auto failed_material_startup = omega::runtime::StartContent(startup_options);
            Check(!failed_material_startup &&
                      failed_material_startup.error().code ==
                          omega::runtime::ContentStartupErrorCode::GameData &&
                      failed_material_startup.error().game_data_error &&
                      failed_material_startup.error().game_data_error->decode_error &&
                      failed_material_startup.error().game_data_error->decode_error->code ==
                          omega::asset::DecodeErrorCode::InvalidReference,
                "startup returns no partial state when one cell lacks its canonical VUM catalog");
            if (!failed_material_startup)
            {
                CheckStartupDiagnostic(failed_material_startup.error(), "decode-failed",
                    "the synthetic game-data startup failure has a borrowed diagnostic");
            }

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
            CheckDecodeError(spatial_service->LoadLevelContent(*spatial_manifest),
                omega::asset::DecodeErrorCode::DuplicateReference,
                "single-pass content rejects multiple VUM members per cell");

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
            CheckDecodeError(spatial_service->LoadLevelContent(*spatial_manifest),
                omega::asset::DecodeErrorCode::Truncated,
                "typed VUM decoder failures survive the single-pass content boundary");

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

            auto empty_material_manifest = *spatial_manifest;
            empty_material_manifest.terrain_cells.clear();
            constexpr std::uint64_t empty_material_items = 2;
            limits = omega::asset::DecodeLimits{};
            limits.maximum_items = empty_material_items;
            Check(LoadMaterialCatalogsWithLimits(
                      root, empty_material_manifest, limits).has_value(),
                "exact zero-cell level-material item budget charges only the source directory");
            limits.maximum_items = empty_material_items - 1U;
            CheckDecodeError(
                LoadMaterialCatalogsWithLimits(root, empty_material_manifest, limits),
                omega::asset::DecodeErrorCode::LimitExceeded,
                "one-below zero-cell level-material item budget fails");

            auto one_cell_material_manifest = *spatial_manifest;
            one_cell_material_manifest.terrain_cells.resize(1U);
            constexpr std::uint64_t one_cell_material_items = 17;
            limits = omega::asset::DecodeLimits{};
            limits.maximum_items = one_cell_material_items;
            Check(LoadMaterialCatalogsWithLimits(
                      root, one_cell_material_manifest, limits).has_value(),
                "exact one-cell level-material item budget includes the manifest cell");
            limits.maximum_items = one_cell_material_items - 1U;
            CheckDecodeError(
                LoadMaterialCatalogsWithLimits(root, one_cell_material_manifest, limits),
                omega::asset::DecodeErrorCode::LimitExceeded,
                "one-below one-cell level-material item budget fails");

            constexpr std::uint64_t material_items = 32;
            limits = omega::asset::DecodeLimits{};
            limits.maximum_items = material_items;
            Check(LoadMaterialCatalogsWithLimits(root, *spatial_manifest, limits).has_value(),
                "exact two-cell level-material item budget includes both manifest cells");
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

            limits = omega::asset::DecodeLimits{};
            Check(LoadContentWithLimits(root, *spatial_manifest, limits,
                      spatial_fixture.cell_a.size())
                      .has_value(),
                "single-pass content accepts the exact cell-HOG byte cap");
            CheckDecodeError(
                LoadContentWithLimits(root, *spatial_manifest, limits,
                    spatial_fixture.cell_a.size() - 1U),
                omega::asset::DecodeErrorCode::LimitExceeded,
                "single-pass content rejects a one-below cell-HOG byte cap");

            const std::uint64_t content_input_bytes = spatial_fixture.data_hog.size() +
                spatial_fixture.cell_a.size() + spatial_fixture.col_a.size() +
                spatial_fixture.vum_a.size() + spatial_fixture.cell_b.size() +
                spatial_fixture.col_b.size() + spatial_fixture.vum_b.size();
            Check(content_input_bytes == 4108U,
                "two-cell fixture pins single-pass cumulative parser and decoder input work");
            limits = omega::asset::DecodeLimits{};
            limits.maximum_input_bytes = content_input_bytes;
            Check(LoadContentWithLimits(root, *spatial_manifest, limits).has_value(),
                "exact shared single-pass content input budget succeeds");
            limits.maximum_input_bytes = content_input_bytes - 1U;
            CheckDecodeError(LoadContentWithLimits(root, *spatial_manifest, limits),
                omega::asset::DecodeErrorCode::LimitExceeded,
                "one-below shared single-pass content input budget fails");

            constexpr std::uint64_t content_items = 44;
            limits = omega::asset::DecodeLimits{};
            limits.maximum_items = content_items;
            Check(LoadContentWithLimits(root, *spatial_manifest, limits).has_value(),
                "exact shared single-pass content item budget succeeds");
            limits.maximum_items = content_items - 1U;
            CheckDecodeError(LoadContentWithLimits(root, *spatial_manifest, limits),
                omega::asset::DecodeErrorCode::LimitExceeded,
                "one-below shared single-pass content item budget fails");

            constexpr std::uint64_t content_output_bytes =
                sizeof(omega::asset::LevelContentIR) +
                2U * (mesh_output_bytes + catalog_output_bytes);
            limits = omega::asset::DecodeLimits{};
            limits.maximum_output_bytes = content_output_bytes;
            Check(LoadContentWithLimits(root, *spatial_manifest, limits).has_value(),
                "exact shared logical single-pass content output budget succeeds");
            limits.maximum_output_bytes = content_output_bytes - 1U;
            CheckDecodeError(LoadContentWithLimits(root, *spatial_manifest, limits),
                omega::asset::DecodeErrorCode::LimitExceeded,
                "one-below shared logical single-pass content output budget fails");

            limits = omega::asset::DecodeLimits{};
            limits.maximum_scratch_bytes = exact_scratch_bytes;
            Check(LoadContentWithLimits(root, *spatial_manifest, limits).has_value(),
                "single-pass content uses the exact peak COL scratch budget");
            limits.maximum_scratch_bytes = exact_scratch_bytes - 1U;
            CheckDecodeError(LoadContentWithLimits(root, *spatial_manifest, limits),
                omega::asset::DecodeErrorCode::LimitExceeded,
                "single-pass content rejects one-below peak scratch budget");

            limits = omega::asset::DecodeLimits{};
            limits.maximum_nesting_depth = 1;
            Check(LoadContentWithLimits(root, *spatial_manifest, limits).has_value(),
                "single-pass content fits the exact cell archive and leaf decoder depth");
            limits.maximum_nesting_depth = 0;
            CheckDecodeError(LoadContentWithLimits(root, *spatial_manifest, limits),
                omega::asset::DecodeErrorCode::LimitExceeded,
                "single-pass content rejects one-below archive depth");
        }
    }

    const ToolRun aggregate_tool = RunLevelMaterialCatalogTool(root);
    Check(aggregate_tool.exit_code == 0 && aggregate_tool.standard_error.empty() &&
              aggregate_tool.standard_output ==
                  "{\"levels\":1,\"valid\":1,\"errors\":0,\"terrain_cells\":2,"
                  "\"catalogs\":2,\"names\":4,\"materials\":4,"
                  "\"name_references\":8}\n",
        "level material tool publishes only exact tree-wide canonical aggregate counts");
    Check(aggregate_tool.standard_output.find("MINSK") == std::string::npos &&
              aggregate_tool.standard_output.find("BASE.TDX") == std::string::npos &&
              aggregate_tool.standard_output.find("CELL.TDX") == std::string::npos &&
              aggregate_tool.standard_output.find("GAMEDATA") == std::string::npos,
        "successful level material aggregate output contains no level, asset, or path identity");

    const auto invalid_level_directory = root / "GAMEDATA" / "SECRET-LEVEL";
    std::error_code discovery_fixture_error;
    std::filesystem::create_directories(invalid_level_directory, discovery_fixture_error);
    Check(!discovery_fixture_error &&
              WriteText(invalid_level_directory / "DATA.POP", "synthetic"),
        "invalid-code level directory is created for discovery-error sanitization");
    const ToolRun discovery_error_tool = RunLevelMaterialCatalogTool(root);
    Check(discovery_error_tool.exit_code == 2 &&
              discovery_error_tool.standard_output ==
                  "{\"levels\":1,\"valid\":1,\"errors\":1,\"terrain_cells\":2,"
                  "\"catalogs\":2,\"names\":4,\"materials\":4,"
                  "\"name_references\":8}\n" &&
              discovery_error_tool.standard_error ==
                  "game-data: discover: level-entry-error\n" &&
              discovery_error_tool.standard_error.find("SECRET-LEVEL") ==
                  std::string::npos,
        "level material tool reports discovery failures without directory identity");
    std::filesystem::remove_all(invalid_level_directory, discovery_fixture_error);
    Check(!discovery_fixture_error,
        "invalid-code discovery fixture is removed after aggregate-tool coverage");

    const auto malformed_tool_cell = MakeCellHog("CeLlA.vUm", Bytes("not-vum"),
        {HogMember{.name = "aLpHa.CoL", .payload = spatial_fixture.col_a}});
    Check(WriteBytes(root / "GAMEDATA" / "MINSK" / "DATA.HOG",
              MakeSpatialDataHog(malformed_tool_cell, spatial_fixture.cell_b)),
        "malformed material fixture is written for aggregate-tool error sanitization");
    const ToolRun failed_aggregate_tool = RunLevelMaterialCatalogTool(root);
    Check(failed_aggregate_tool.exit_code == 2 &&
              failed_aggregate_tool.standard_output ==
                  "{\"levels\":1,\"valid\":0,\"errors\":1,\"terrain_cells\":0,"
                  "\"catalogs\":0,\"names\":0,\"materials\":0,"
                  "\"name_references\":0}\n" &&
              failed_aggregate_tool.standard_error ==
                  "game-data: materials: decode-failed:truncated\n",
        "level material tool fails one level atomically with sanitized typed diagnostics");
    Check(failed_aggregate_tool.standard_error.find("MINSK") == std::string::npos &&
              failed_aggregate_tool.standard_error.find("CeLlA") == std::string::npos &&
              failed_aggregate_tool.standard_error.find("GAMEDATA") == std::string::npos,
        "level material aggregate errors contain no level, member, or path identity");
    Check(WriteBytes(root / "GAMEDATA" / "MINSK" / "DATA.HOG",
              spatial_fixture.data_hog),
        "valid material fixture is restored after aggregate-tool failure coverage");

    const auto movie_archive_path = root / "ZMEDIA" / "ZMOVIES.HOG";
    std::error_code movie_archive_error;
    Check(std::filesystem::remove(movie_archive_path, movie_archive_error) &&
              !movie_archive_error,
        "synthetic opening movie archive is removed");
    auto missing_movie_archive_service =
        omega::content::GameDataService::Open({.root = root});
    Check(missing_movie_archive_service.has_value(),
        "an absent optional opening movie archive does not block game-data startup");
    if (missing_movie_archive_service)
    {
        auto missing_archive = missing_movie_archive_service->LoadOpeningMovieSource(
            "PrivateOwner-Movie.pss");
        CheckError(missing_archive,
            omega::content::GameDataErrorCode::MissingRequiredFile,
            "an absent fixed movie archive fails only the explicit movie request");
        Check(!missing_archive &&
                  missing_archive.error().message.find("PrivateOwner") == std::string::npos &&
                  missing_archive.error().message.find("ZMEDIA") == std::string::npos,
            "missing movie-archive diagnostics contain no path or member identity");
    }

    Check(WriteText(movie_archive_path, "not a HOG"),
        "malformed opening movie archive fixture is written");
    auto malformed_movie_archive_service =
        omega::content::GameDataService::Open({.root = root});
    Check(malformed_movie_archive_service.has_value(),
        "a malformed optional opening movie archive does not block game-data startup");
    if (malformed_movie_archive_service)
    {
        auto malformed_archive = malformed_movie_archive_service->LoadOpeningMovieSource(
            "PrivateOwner-Movie.pss");
        CheckError(malformed_archive,
            omega::content::GameDataErrorCode::MalformedArchive,
            "a malformed fixed movie archive fails only the explicit movie request");
        Check(!malformed_archive &&
                  malformed_archive.error().message.find("PrivateOwner") ==
                      std::string::npos &&
                  malformed_archive.error().message.find("ZMEDIA") == std::string::npos,
            "malformed movie-archive diagnostics contain no path or member identity");
    }

    constexpr std::uint64_t oversized_movie_bytes =
        omega::asset::kOpeningMovieMaximumSourceBytes + 1U;
    static_assert(oversized_movie_bytes <=
                  std::numeric_limits<std::uint32_t>::max());
    Check(WriteSparseSingleMemberHog(movie_archive_path, "Synthetic-Oversized.pss",
              static_cast<std::uint32_t>(oversized_movie_bytes)),
        "a sparse oversized movie-member fixture is written without allocating its payload");
    auto oversized_movie_archive_service =
        omega::content::GameDataService::Open({.root = root});
    Check(oversized_movie_archive_service.has_value(),
        "an indexed archive with an oversized member does not block game-data startup");
    if (oversized_movie_archive_service)
    {
        auto oversized_member = oversized_movie_archive_service->LoadOpeningMovieSource(
            "synthetic-oversized.pss");
        CheckDecodeError(oversized_member,
            omega::asset::DecodeErrorCode::LimitExceeded,
            "a movie member one byte above 512 MiB is rejected before payload allocation");
    }
    Check(WriteBytes(movie_archive_path,
              MakeHog({HogMember{.name = kSyntheticOpeningMovieMember,
                  .payload = Bytes(kSyntheticOpeningMoviePayload)}})),
        "valid opening movie archive fixture is restored");

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
