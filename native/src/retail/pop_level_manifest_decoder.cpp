#include "omega/retail/pop_level_manifest_decoder.h"

#include "omega/asset/pop_terrain_index.h"
#include "omega/vfs/virtual_file_system.h"

#include <algorithm>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace omega::retail
{
namespace
{
[[nodiscard]] asset::DecodeError Error(
    const asset::DecodeErrorCode code, std::string message,
    const std::optional<std::uint64_t> byte_offset = std::nullopt)
{
    return asset::DecodeError{
        .code = code,
        .byte_offset = byte_offset,
        .message = std::move(message),
    };
}

[[nodiscard]] bool AddWithinLimit(
    std::uint64_t& total, const std::uint64_t amount, const std::uint64_t limit) noexcept
{
    if (amount > std::numeric_limits<std::uint64_t>::max() - total)
        return false;
    total += amount;
    return total <= limit;
}

[[nodiscard]] bool MultiplyWithinLimit(const std::uint64_t left, const std::uint64_t right,
    const std::uint64_t limit, std::uint64_t& result) noexcept
{
    if (left != 0 && right > std::numeric_limits<std::uint64_t>::max() / left)
        return false;
    result = left * right;
    return result <= limit;
}

[[nodiscard]] std::uint32_t ReadU32(
    const std::span<const std::byte> bytes, const std::size_t offset) noexcept
{
    return std::to_integer<std::uint32_t>(bytes[offset]) |
           (std::to_integer<std::uint32_t>(bytes[offset + 1]) << 8U) |
           (std::to_integer<std::uint32_t>(bytes[offset + 2]) << 16U) |
           (std::to_integer<std::uint32_t>(bytes[offset + 3]) << 24U);
}

[[nodiscard]] bool HasTerrainPrefix(const std::span<const std::byte> bytes) noexcept
{
    return bytes.size() >= 16U && ReadU32(bytes, 0) == 70 &&
           bytes[4] == std::byte{'T'} && bytes[5] == std::byte{'E'} &&
           bytes[6] == std::byte{'R'} && bytes[7] == std::byte{':'};
}

[[nodiscard]] asset::DecodeError MapParseError(const asset::PopTerrainParseError& error)
{
    asset::DecodeErrorCode code = asset::DecodeErrorCode::Malformed;
    switch (error.code)
    {
    case asset::PopTerrainParseErrorCode::Truncated:
        code = asset::DecodeErrorCode::Truncated;
        break;
    case asset::PopTerrainParseErrorCode::Malformed:
        code = asset::DecodeErrorCode::Malformed;
        break;
    case asset::PopTerrainParseErrorCode::Overflow:
        code = asset::DecodeErrorCode::Overflow;
        break;
    case asset::PopTerrainParseErrorCode::LimitExceeded:
        code = asset::DecodeErrorCode::LimitExceeded;
        break;
    }
    return Error(code, error.message, error.byte_offset);
}

[[nodiscard]] asset::DecodeResult<asset::SourceLocator> NormalizeSourceLocator(
    const asset::SourceLocator& source, const asset::DecodeLimits limits,
    std::uint64_t& logical_output_bytes)
{
    if (source.hog_entries.size() >= limits.maximum_nesting_depth)
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
            "DATA.HOG source depth leaves no room for a cell entry"));
    if (source.game_path.size() > limits.maximum_string_bytes)
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
            "DATA.HOG game path exceeds decoder string limit"));
    for (const auto& component : source.hog_entries)
    {
        if (component.size() > limits.maximum_string_bytes)
            return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
                "DATA.HOG archive entry exceeds decoder string limit"));
    }

    logical_output_bytes = sizeof(asset::LevelManifestIR);
    std::uint64_t string_objects = 0;
    if (!MultiplyWithinLimit(source.hog_entries.size(), sizeof(std::string),
            limits.maximum_output_bytes, string_objects) ||
        !AddWithinLimit(logical_output_bytes, string_objects, limits.maximum_output_bytes))
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
            "DATA.HOG source chain exceeds decoder output limit"));

    auto normalized_game_path = vfs::NormalizeGamePath(source.game_path);
    if (!normalized_game_path)
        return std::unexpected(Error(asset::DecodeErrorCode::InvalidReference,
            "invalid DATA.HOG game path: " + normalized_game_path.error()));
    if (!AddWithinLimit(logical_output_bytes, normalized_game_path->size(),
            limits.maximum_output_bytes))
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
            "DATA.HOG source exceeds decoder output limit"));

    asset::SourceLocator normalized{
        .game_path = std::move(*normalized_game_path),
        .hog_entries = {},
    };
    normalized.hog_entries.reserve(source.hog_entries.size());
    for (const auto& component : source.hog_entries)
    {
        auto normalized_component = vfs::NormalizeGamePath(component);
        if (!normalized_component)
            return std::unexpected(Error(asset::DecodeErrorCode::InvalidReference,
                "invalid DATA.HOG archive entry: " + normalized_component.error()));
        if (!AddWithinLimit(logical_output_bytes, normalized_component->size(),
                limits.maximum_output_bytes))
            return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
                "DATA.HOG source chain exceeds decoder output limit"));
        normalized.hog_entries.push_back(std::move(*normalized_component));
    }
    return normalized;
}

[[nodiscard]] asset::DecodeResult<std::uint64_t> PreflightScratch(
    const std::span<const archive::HogEntry> data_hog_entries,
    const std::uint64_t declared_records,
    const asset::DecodeLimits limits)
{
    std::uint64_t scratch_bytes = 0;
    std::uint64_t record_bytes = 0;
    constexpr std::uint64_t record_overhead =
        sizeof(asset::PopTerrainRecord) + 2U * sizeof(void*);
    if (!MultiplyWithinLimit(declared_records, record_overhead,
            limits.maximum_scratch_bytes, record_bytes) ||
        !AddWithinLimit(scratch_bytes, record_bytes, limits.maximum_scratch_bytes))
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
            "POP parser records exceed decoder scratch limit"));

    std::uint64_t resolved_bytes = 0;
    if (!MultiplyWithinLimit(declared_records, sizeof(const std::string*),
            limits.maximum_scratch_bytes, resolved_bytes) ||
        !AddWithinLimit(scratch_bytes, resolved_bytes, limits.maximum_scratch_bytes))
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
            "resolved reference table exceeds decoder scratch limit"));

    constexpr std::uint64_t map_overhead =
        sizeof(std::string) + sizeof(const archive::HogEntry*) + 5U * sizeof(void*);
    std::uint64_t directory_objects = 0;
    if (!MultiplyWithinLimit(data_hog_entries.size(), map_overhead,
            limits.maximum_scratch_bytes, directory_objects) ||
        !AddWithinLimit(scratch_bytes, directory_objects, limits.maximum_scratch_bytes))
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
            "normalized DATA.HOG directory exceeds decoder scratch limit"));
    for (const auto& entry : data_hog_entries)
    {
        if (entry.name.size() > limits.maximum_string_bytes)
            return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
                "DATA.HOG entry exceeds decoder string limit"));
        if (!AddWithinLimit(scratch_bytes, entry.name.size(), limits.maximum_scratch_bytes))
            return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
                "normalized DATA.HOG names exceed decoder scratch limit"));
    }
    if (!AddWithinLimit(scratch_bytes, limits.maximum_string_bytes,
            limits.maximum_scratch_bytes))
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
            "normalized lookup key exceeds decoder scratch limit"));
    return limits.maximum_scratch_bytes - scratch_bytes;
}
} // namespace

asset::DecodeResult<asset::LevelManifestIR> DecodePopLevelManifest(
    const std::span<const std::byte> pop_bytes,
    const std::span<const archive::HogEntry> data_hog_entries,
    const asset::SourceLocator& data_hog_source,
    const asset::DecodeLimits limits)
{
    if (pop_bytes.size() > limits.maximum_input_bytes)
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
            "POP input exceeds decoder byte limit"));
    if (data_hog_entries.size() > limits.maximum_items ||
        data_hog_source.hog_entries.size() > limits.maximum_items - data_hog_entries.size())
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
            "DATA.HOG directory and source chain exceed decoder item limit"));

    std::uint64_t logical_output_bytes = 0;
    auto normalized_source = NormalizeSourceLocator(
        data_hog_source, limits, logical_output_bytes);
    if (!normalized_source)
        return std::unexpected(normalized_source.error());

    const std::uint64_t source_items =
        data_hog_entries.size() + data_hog_source.hog_entries.size();
    const std::uint64_t maximum_records_by_items = limits.maximum_items - source_items;
    const std::uint64_t maximum_records_by_output =
        (limits.maximum_output_bytes - logical_output_bytes) / sizeof(asset::LevelCellSourceIR);
    const std::uint64_t maximum_records = std::min({maximum_records_by_items,
        maximum_records_by_output,
        static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())});

    std::uint64_t declared_records = 0;
    std::uint64_t parser_name_scratch_bytes = limits.maximum_scratch_bytes;
    if (HasTerrainPrefix(pop_bytes))
    {
        declared_records = ReadU32(pop_bytes, 8);
        if (declared_records > maximum_records)
            return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
                "POP records exceed cumulative item or output limit", 8));
        auto scratch = PreflightScratch(data_hog_entries, declared_records, limits);
        if (!scratch)
            return std::unexpected(scratch.error());
        parser_name_scratch_bytes = *scratch;
    }

    auto terrain = asset::PopTerrainIndex::Parse(pop_bytes, asset::PopTerrainParseLimits{
        .maximum_records = static_cast<std::uint32_t>(maximum_records),
        .maximum_name_bytes = limits.maximum_string_bytes,
        .maximum_owned_name_bytes = static_cast<std::size_t>(
            std::min<std::uint64_t>(parser_name_scratch_bytes,
                std::numeric_limits<std::size_t>::max())),
    });
    if (!terrain)
        return std::unexpected(MapParseError(terrain.error()));

    using EntryMap = std::unordered_map<std::string, const archive::HogEntry*>;
    EntryMap entries;
    entries.reserve(data_hog_entries.size());
    for (const auto& entry : data_hog_entries)
    {
        auto normalized = vfs::NormalizeGamePath(entry.name);
        if (!normalized)
            return std::unexpected(Error(asset::DecodeErrorCode::InvalidReference,
                "DATA.HOG contains an unsafe entry name"));
        const bool inserted = entries.emplace(std::move(*normalized), &entry).second;
        if (!inserted)
            return std::unexpected(Error(asset::DecodeErrorCode::DuplicateReference,
                "DATA.HOG contains duplicate normalized entry names"));
    }

    std::uint64_t record_objects = 0;
    if (!MultiplyWithinLimit(terrain->records().size(), sizeof(asset::LevelCellSourceIR),
            limits.maximum_output_bytes, record_objects) ||
        !AddWithinLimit(logical_output_bytes, record_objects, limits.maximum_output_bytes))
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
            "decoded level manifest records exceed decoder output limit"));

    std::vector<const std::string*> resolved;
    resolved.reserve(terrain->records().size());
    for (const auto& record : terrain->records())
    {
        auto normalized = vfs::NormalizeGamePath(record.name);
        if (!normalized)
            return std::unexpected(Error(asset::DecodeErrorCode::InvalidReference,
                "POP terrain record contains an unsafe DATA.HOG reference"));
        const auto match = entries.find(*normalized);
        if (match == entries.end())
            return std::unexpected(Error(asset::DecodeErrorCode::InvalidReference,
                "POP terrain record does not resolve in DATA.HOG"));
        if (!AddWithinLimit(logical_output_bytes, match->first.size(),
                limits.maximum_output_bytes))
            return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
                "decoded level manifest strings exceed decoder output limit"));
        resolved.push_back(&match->first);
    }

    asset::LevelManifestIR result{
        .data_hog_source = std::move(*normalized_source),
        .terrain_cells = {},
    };
    result.terrain_cells.reserve(terrain->records().size());
    for (std::size_t index = 0; index < terrain->records().size(); ++index)
    {
        result.terrain_cells.push_back(asset::LevelCellSourceIR{
            .observed_kind = terrain->records()[index].kind,
            .observed_index = terrain->records()[index].index,
            .data_hog_entry = *resolved[index],
        });
    }
    return result;
}
} // namespace omega::retail
