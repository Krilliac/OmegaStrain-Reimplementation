#include "omega/retail/pop_level_manifest_decoder.h"

#include "omega/asset/pop_terrain_index.h"

#include <cstddef>
#include <cstdint>
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

void AppendRecord(std::vector<std::byte>& bytes, const std::uint32_t kind,
    const std::uint32_t index, const std::string_view name)
{
    AppendU32(bytes, kind);
    AppendU32(bytes, index);
    AppendText(bytes, name);
    bytes.push_back(std::byte{0});
    while (bytes.size() % 4U != 0)
        bytes.push_back(std::byte{0x5A});
}

std::vector<std::byte> MakePop()
{
    std::vector<std::byte> bytes;
    AppendU32(bytes, 70);
    AppendText(bytes, "TER:");
    AppendU32(bytes, 2);
    AppendRecord(bytes, 4, 10, "CELL_A.VUM");
    AppendRecord(bytes, 9, 22, "cell_b.vum");
    AppendText(bytes, "GOB:");
    return bytes;
}

std::vector<omega::archive::HogEntry> MakeEntries()
{
    return {
        omega::archive::HogEntry{.name = "cell_a.hog", .offset = 64, .size = 128},
        omega::archive::HogEntry{.name = "CELL_B.HOG", .offset = 192, .size = 256},
    };
}

omega::asset::SourceLocator MakeSource()
{
    return omega::asset::SourceLocator{
        .game_path = "levels\\minsk\\data.hog",
        .hog_entries = {"outer.hog"},
    };
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

void CheckError(const omega::asset::DecodeResult<omega::asset::LevelManifestIR>& result,
    const omega::asset::DecodeErrorCode code, const std::string_view message)
{
    Check(!result && result.error().code == code, message);
}
} // namespace

int PopLevelManifestDecoderFailureCount()
{
    auto pop = MakePop();
    auto entries = MakeEntries();
    auto source = MakeSource();
    auto decoded = omega::retail::DecodePopLevelManifest(pop, entries, source);
    Check(decoded.has_value(), "POP terrain references resolve into an owned level manifest");
    if (decoded)
        Check(decoded->terrain_cells.size() == 2, "terrain-cell order and count are preserved");
    if (decoded && decoded->terrain_cells.size() >= 2)
    {
        Check(decoded->terrain_cells[0].observed_kind == 4 &&
                  decoded->terrain_cells[0].observed_index == 10,
            "uninterpreted POP numeric fields are preserved");
        Check(decoded->terrain_cells[1].observed_kind == 9 &&
                  decoded->terrain_cells[1].observed_index == 22,
            "second uninterpreted POP record remains ordered");
        Check(decoded->data_hog_source.game_path == "LEVELS/MINSK/DATA.HOG",
            "source game path is normalized into the canonical VFS form");
        Check(decoded->data_hog_source.hog_entries.size() == 1 &&
                  decoded->data_hog_source.hog_entries[0] == "OUTER.HOG",
            "common nested-HOG source chain is stored once on the manifest");
        Check(decoded->terrain_cells[0].data_hog_entry == "CELL_A.HOG" &&
                  decoded->terrain_cells[1].data_hog_entry == "CELL_B.HOG",
            "case-insensitive lookups publish canonical entry names");
    }

    pop.assign(1, std::byte{0});
    entries[0].name = "MUTATED.HOG";
    source.game_path = "MUTATED";
    if (decoded && !decoded->terrain_cells.empty())
        Check(decoded->terrain_cells[0].data_hog_entry == "CELL_A.HOG",
            "manifest strings remain valid after all caller inputs mutate");

    auto missing_entries = MakeEntries();
    missing_entries.pop_back();
    CheckError(omega::retail::DecodePopLevelManifest(MakePop(), missing_entries, MakeSource()),
        omega::asset::DecodeErrorCode::InvalidReference,
        "a missing DATA.HOG member fails the entire manifest");

    auto duplicate_entries = MakeEntries();
    duplicate_entries.push_back(
        omega::archive::HogEntry{.name = "CELL_A.HOG", .offset = 448, .size = 64});
    CheckError(omega::retail::DecodePopLevelManifest(MakePop(), duplicate_entries, MakeSource()),
        omega::asset::DecodeErrorCode::DuplicateReference,
        "case-insensitive duplicate DATA.HOG members are rejected");
    auto duplicate_stems = MakeEntries();
    duplicate_stems.push_back(
        omega::archive::HogEntry{.name = "CELL_A.BIN", .offset = 448, .size = 64});
    CheckError(omega::retail::DecodePopLevelManifest(MakePop(), duplicate_stems, MakeSource()),
        omega::asset::DecodeErrorCode::DuplicateReference,
        "cross-extension duplicate DATA.HOG reference stems are rejected");

    auto unsafe_source = MakeSource();
    unsafe_source.game_path = "../DATA.HOG";
    CheckError(omega::retail::DecodePopLevelManifest(MakePop(), MakeEntries(), unsafe_source),
        omega::asset::DecodeErrorCode::InvalidReference,
        "unsafe source paths are rejected before publication");

    auto item_limits = omega::asset::DecodeLimits{};
    item_limits.maximum_items = 3;
    CheckError(omega::retail::DecodePopLevelManifest(
                   MakePop(), MakeEntries(), MakeSource(), item_limits),
        omega::asset::DecodeErrorCode::LimitExceeded,
        "source and output records share one cumulative item limit");

    auto byte_limits = omega::asset::DecodeLimits{};
    byte_limits.maximum_output_bytes = 32;
    CheckError(omega::retail::DecodePopLevelManifest(
                   MakePop(), MakeEntries(), MakeSource(), byte_limits),
        omega::asset::DecodeErrorCode::LimitExceeded,
        "owned manifest data is rejected before exceeding the output-byte limit");

    auto exact_source = MakeSource();
    exact_source.game_path = "levels//minsk\\data.hog";
    exact_source.hog_entries = {"outer//nested.hog"};
    constexpr std::string_view canonical_exact_game_path = "LEVELS/MINSK/DATA.HOG";
    constexpr std::string_view canonical_exact_hog_entry = "OUTER/NESTED.HOG";
    const auto exact_entries = MakeEntries();
    std::uint64_t exact_output_bytes = sizeof(omega::asset::LevelManifestIR) +
        canonical_exact_game_path.size() +
        exact_source.hog_entries.size() * sizeof(std::string) +
        canonical_exact_hog_entry.size() +
        2U * sizeof(omega::asset::LevelCellSourceIR) +
        exact_entries[0].name.size() + exact_entries[1].name.size();
    auto exact_output_limits = omega::asset::DecodeLimits{};
    exact_output_limits.maximum_output_bytes = exact_output_bytes;
    Check(omega::retail::DecodePopLevelManifest(
              MakePop(), exact_entries, exact_source, exact_output_limits)
              .has_value(),
        "exact canonical multi-component output budget succeeds");
    --exact_output_limits.maximum_output_bytes;
    CheckError(omega::retail::DecodePopLevelManifest(
                   MakePop(), exact_entries, exact_source, exact_output_limits),
        omega::asset::DecodeErrorCode::LimitExceeded,
        "one byte below the complete manifest budget fails closed");

    auto exact_scratch_limits = omega::asset::DecodeLimits{};
    const auto scratch_entries = MakeEntries();
    constexpr std::uint64_t record_count = 2;
    const std::uint64_t exact_scratch_bytes =
        record_count * (sizeof(omega::asset::PopTerrainRecord) + 2U * sizeof(void*)) +
        record_count * sizeof(const std::string*) +
        scratch_entries.size() *
            (2U * sizeof(std::string) + 5U * sizeof(void*)) +
        2U * (scratch_entries[0].name.size() + scratch_entries[1].name.size()) +
        2U * exact_scratch_limits.maximum_string_bytes +
        std::string_view("CELL_A.VUM").size() + std::string_view("cell_b.vum").size();
    exact_scratch_limits.maximum_scratch_bytes = exact_scratch_bytes;
    Check(omega::retail::DecodePopLevelManifest(
              MakePop(), scratch_entries, MakeSource(), exact_scratch_limits)
              .has_value(),
        "exact parser, resolution, and directory scratch budget succeeds");
    --exact_scratch_limits.maximum_scratch_bytes;
    CheckError(omega::retail::DecodePopLevelManifest(
                   MakePop(), scratch_entries, MakeSource(), exact_scratch_limits),
        omega::asset::DecodeErrorCode::LimitExceeded,
        "one byte below the complete scratch budget fails closed");

    auto maximum_depth_source = MakeSource();
    maximum_depth_source.hog_entries.assign(7, "NESTED.HOG");
    Check(omega::retail::DecodePopLevelManifest(
              MakePop(), MakeEntries(), maximum_depth_source)
              .has_value(),
        "source chain may use the exact nesting-depth boundary after cell resolution");
    maximum_depth_source.hog_entries.push_back("TOO_DEEP.HOG");
    CheckError(omega::retail::DecodePopLevelManifest(
                   MakePop(), MakeEntries(), maximum_depth_source),
        omega::asset::DecodeErrorCode::LimitExceeded,
        "source chain beyond the nesting-depth boundary is rejected");

    auto parser_limit_source = omega::asset::SourceLocator{.game_path = "D.HOG"};
    auto parser_limits = omega::asset::DecodeLimits{};
    parser_limits.maximum_string_bytes = 5;
    CheckError(omega::retail::DecodePopLevelManifest(
                   MakePop(), std::span<const omega::archive::HogEntry>{},
                   parser_limit_source, parser_limits),
        omega::asset::DecodeErrorCode::LimitExceeded,
        "typed POP parser limit errors remain limit errors at the decoder boundary");

    auto input_limits = omega::asset::DecodeLimits{};
    input_limits.maximum_input_bytes = MakePop().size() - 1U;
    CheckError(omega::retail::DecodePopLevelManifest(
                   MakePop(), MakeEntries(), MakeSource(), input_limits),
        omega::asset::DecodeErrorCode::LimitExceeded,
        "POP bytes honor the caller input limit");

    auto malformed = MakePop();
    malformed[4] = std::byte{'X'};
    CheckError(omega::retail::DecodePopLevelManifest(malformed, MakeEntries(), MakeSource()),
        omega::asset::DecodeErrorCode::Malformed,
        "malformed POP structure remains distinct from reference errors");

    std::vector<std::byte> truncated_header;
    AppendU32(truncated_header, 70);
    AppendText(truncated_header, "TER:");
    AppendU32(truncated_header, 0xFFFFFFFFU);
    for (std::size_t size = truncated_header.size(); size < 16U; ++size)
    {
        CheckError(omega::retail::DecodePopLevelManifest(
                       truncated_header, MakeEntries(), MakeSource()),
            omega::asset::DecodeErrorCode::Truncated,
            "short POP headers remain truncation errors before count preflight");
        truncated_header.push_back(std::byte{0});
    }

    return failures;
}
