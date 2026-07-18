#include "omega/content/level_texture_store.h"

#include "omega/archive/hog_archive.h"
#include "omega/content/game_data_service.h"
#include "omega/retail/tdx_texture_storage_decoder.h"
#include "omega/vfs/virtual_file_system.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace omega::content
{
namespace detail
{
struct LevelTextureStoreIdentity final
{
};
} // namespace detail

namespace
{
[[nodiscard]] LevelTextureStoreError Error(
    const LevelTextureStoreErrorCode code, std::string message)
{
    return LevelTextureStoreError{
        .code = code,
        .message = std::move(message),
        .decode_error = std::nullopt,
    };
}

[[nodiscard]] LevelTextureStoreError DecodeError(
    const LevelTextureStoreErrorCode code, const asset::DecodeErrorCode decode_code,
    std::string message)
{
    return LevelTextureStoreError{
        .code = code,
        .message = message,
        .decode_error = asset::DecodeError{
            .code = decode_code,
            .byte_offset = std::nullopt,
            .message = std::move(message),
        },
    };
}

[[nodiscard]] LevelTextureStoreError MapDecodeError(
    const asset::DecodeError& error, const std::string_view stage)
{
    LevelTextureStoreErrorCode code = LevelTextureStoreErrorCode::DecodeFailed;
    switch (error.code)
    {
    case asset::DecodeErrorCode::LimitExceeded:
    case asset::DecodeErrorCode::Overflow:
        code = LevelTextureStoreErrorCode::LimitExceeded;
        break;
    case asset::DecodeErrorCode::InvalidReference:
        code = LevelTextureStoreErrorCode::InvalidReference;
        break;
    case asset::DecodeErrorCode::DuplicateReference:
        code = LevelTextureStoreErrorCode::DuplicateReference;
        break;
    case asset::DecodeErrorCode::Truncated:
    case asset::DecodeErrorCode::Malformed:
    case asset::DecodeErrorCode::UnsupportedVariant:
        break;
    }
    return DecodeError(code, error.code, std::string(stage));
}

[[nodiscard]] LevelTextureStoreError MapGameDataError(
    const GameDataError& error, const std::string_view stage)
{
    if (error.code == GameDataErrorCode::ForeignService)
        return Error(LevelTextureStoreErrorCode::ForeignService, std::string(stage));
    if (error.decode_error)
        return MapDecodeError(*error.decode_error, stage);
    switch (error.code)
    {
    case GameDataErrorCode::ReadFailed:
    case GameDataErrorCode::MissingRequiredFile:
        return Error(LevelTextureStoreErrorCode::ReadFailed, std::string(stage));
    case GameDataErrorCode::MalformedArchive:
        return Error(LevelTextureStoreErrorCode::MalformedArchive, std::string(stage));
    case GameDataErrorCode::ForeignService:
        break;
    case GameDataErrorCode::InvalidConfiguration:
    case GameDataErrorCode::MountFailed:
    case GameDataErrorCode::UnsupportedBuild:
    case GameDataErrorCode::InvalidLevelCode:
    case GameDataErrorCode::DecodeFailed:
        return Error(LevelTextureStoreErrorCode::DecodeFailed, std::string(stage));
    }
    return Error(LevelTextureStoreErrorCode::DecodeFailed, std::string(stage));
}

[[nodiscard]] bool Add(
    const std::uint64_t left, const std::uint64_t right, std::uint64_t& result) noexcept
{
    if (right > std::numeric_limits<std::uint64_t>::max() - left)
        return false;
    result = left + right;
    return true;
}

[[nodiscard]] bool Multiply(
    const std::uint64_t left, const std::uint64_t right, std::uint64_t& result) noexcept
{
    if (left != 0 && right > std::numeric_limits<std::uint64_t>::max() / left)
        return false;
    result = left * right;
    return true;
}

[[nodiscard]] std::uint32_t ReadU32(
    const std::span<const std::byte> bytes, const std::size_t offset) noexcept
{
    return std::to_integer<std::uint32_t>(bytes[offset]) |
           (std::to_integer<std::uint32_t>(bytes[offset + 1U]) << 8U) |
           (std::to_integer<std::uint32_t>(bytes[offset + 2U]) << 16U) |
           (std::to_integer<std::uint32_t>(bytes[offset + 3U]) << 24U);
}

class OperationBudget final
{
public:
    explicit OperationBudget(const asset::DecodeLimits limits) noexcept : limits_(limits) {}

    [[nodiscard]] std::expected<void, LevelTextureStoreError> ConsumeInput(
        const std::uint64_t bytes)
    {
        return Consume(bytes, usage_.input_bytes, limits_.maximum_input_bytes,
            "level texture operation exceeds the input limit");
    }

    [[nodiscard]] std::expected<void, LevelTextureStoreError> ConsumeItems(
        const std::uint64_t items)
    {
        return Consume(items, usage_.items, limits_.maximum_items,
            "level texture operation exceeds the item limit");
    }

    [[nodiscard]] std::expected<void, LevelTextureStoreError> ConsumeOutput(
        const std::uint64_t bytes)
    {
        return Consume(bytes, usage_.logical_output_bytes, limits_.maximum_output_bytes,
            "level texture operation exceeds the output limit");
    }

    [[nodiscard]] std::expected<void, LevelTextureStoreError> ObserveDepth(
        const std::uint64_t depth)
    {
        if (depth > limits_.maximum_nesting_depth ||
            depth > std::numeric_limits<std::uint32_t>::max())
            return std::unexpected(DecodeError(LevelTextureStoreErrorCode::LimitExceeded,
                asset::DecodeErrorCode::LimitExceeded,
                "level texture operation exceeds the archive-depth limit"));
        usage_.archive_depth = std::max(
            usage_.archive_depth, static_cast<std::uint32_t>(depth));
        return {};
    }

    [[nodiscard]] std::expected<void, LevelTextureStoreError> ObserveScratch(
        const std::uint64_t bytes)
    {
        if (bytes > limits_.maximum_scratch_bytes)
            return std::unexpected(DecodeError(LevelTextureStoreErrorCode::LimitExceeded,
                asset::DecodeErrorCode::LimitExceeded,
                "level texture operation exceeds the scratch limit"));
        usage_.peak_scratch_bytes = std::max(usage_.peak_scratch_bytes, bytes);
        return {};
    }

    [[nodiscard]] asset::DecodeLimits RemainingLimits() const noexcept
    {
        asset::DecodeLimits child = limits_;
        child.maximum_input_bytes -= usage_.input_bytes;
        child.maximum_output_bytes -= usage_.logical_output_bytes;
        child.maximum_items -= usage_.items;
        child.maximum_nesting_depth -= usage_.archive_depth;
        // Scratch is a per-decoder peak, not a cumulative byte budget. The directory workspace
        // observed by Open must never reduce a later semantic decoder's independent limit.
        return child;
    }

    [[nodiscard]] asset::DecodeLimits RemainingResolveLimits(
        const std::uint64_t resident_scratch_bytes) const noexcept
    {
        asset::DecodeLimits child = limits_;
        child.maximum_input_bytes -= usage_.input_bytes;
        child.maximum_items -= usage_.items;
        child.maximum_scratch_bytes = resident_scratch_bytes <= limits_.maximum_scratch_bytes
            ? limits_.maximum_scratch_bytes - resident_scratch_bytes
            : 0U;
        // Resolution is sequential. Depth is a peak constraint and output is not retained by the
        // resolver; scratch is reduced only by the canonical source workspace that remains live.
        return child;
    }

    [[nodiscard]] const LevelTextureOperationUsage& usage() const noexcept { return usage_; }

private:
    [[nodiscard]] static std::expected<void, LevelTextureStoreError> Consume(
        const std::uint64_t amount, std::uint64_t& used, const std::uint64_t maximum,
        const std::string_view message)
    {
        std::uint64_t next = 0;
        if (!Add(used, amount, next))
            return std::unexpected(DecodeError(LevelTextureStoreErrorCode::LimitExceeded,
                asset::DecodeErrorCode::Overflow, std::string(message)));
        if (next > maximum)
            return std::unexpected(DecodeError(LevelTextureStoreErrorCode::LimitExceeded,
                asset::DecodeErrorCode::LimitExceeded, std::string(message)));
        used = next;
        return {};
    }

    asset::DecodeLimits limits_;
    LevelTextureOperationUsage usage_;
};

[[nodiscard]] std::expected<std::string, LevelTextureStoreError> NormalizeName(
    const std::string_view value, const asset::DecodeLimits limits)
{
    if (value.size() > limits.maximum_string_bytes)
        return std::unexpected(DecodeError(LevelTextureStoreErrorCode::LimitExceeded,
            asset::DecodeErrorCode::LimitExceeded,
            "level texture locator component exceeds the string limit"));
    auto normalized = vfs::NormalizeGamePath(value);
    if (!normalized)
        return std::unexpected(DecodeError(LevelTextureStoreErrorCode::InvalidReference,
            asset::DecodeErrorCode::InvalidReference,
            "level texture locator contains an invalid component"));
    if (normalized->size() > limits.maximum_string_bytes)
        return std::unexpected(DecodeError(LevelTextureStoreErrorCode::LimitExceeded,
            asset::DecodeErrorCode::LimitExceeded,
            "normalized level texture component exceeds the string limit"));
    return std::move(*normalized);
}

[[nodiscard]] std::expected<asset::SourceLocator, LevelTextureStoreError> NormalizeSource(
    const asset::SourceLocator& locator, const asset::DecodeLimits limits)
{
    const std::uint64_t depth = locator.hog_entries.size();
    if (depth > limits.maximum_nesting_depth)
        return std::unexpected(DecodeError(LevelTextureStoreErrorCode::LimitExceeded,
            asset::DecodeErrorCode::LimitExceeded,
            "level texture source exceeds the archive-depth limit"));

    auto game_path = NormalizeName(locator.game_path, limits);
    if (!game_path)
        return std::unexpected(game_path.error());

    asset::SourceLocator normalized{.game_path = std::move(*game_path), .hog_entries = {}};
    normalized.hog_entries.reserve(locator.hog_entries.size());
    for (const auto& component : locator.hog_entries)
    {
        auto name = NormalizeName(component, limits);
        if (!name)
            return std::unexpected(name.error());
        normalized.hog_entries.push_back(std::move(*name));
    }

    return normalized;
}

[[nodiscard]] std::expected<std::uint64_t, LevelTextureStoreError>
SourceWorkspaceUpperBound(
    const std::vector<asset::SourceLocator>& sources, const asset::DecodeLimits limits)
{
    std::uint64_t source_objects = 0;
    std::uint64_t workspace = sizeof(std::vector<asset::SourceLocator>);
    if (!Multiply(sources.size(), sizeof(asset::SourceLocator), source_objects) ||
        !Add(workspace, source_objects, workspace))
        return std::unexpected(DecodeError(LevelTextureStoreErrorCode::LimitExceeded,
            asset::DecodeErrorCode::Overflow,
            "level texture source workspace size overflows"));

    for (const auto& source : sources)
    {
        if (source.hog_entries.size() > limits.maximum_nesting_depth)
            return std::unexpected(DecodeError(LevelTextureStoreErrorCode::LimitExceeded,
                asset::DecodeErrorCode::LimitExceeded,
                "level texture source exceeds the archive-depth limit"));
        if (source.game_path.size() > limits.maximum_string_bytes)
            return std::unexpected(DecodeError(LevelTextureStoreErrorCode::LimitExceeded,
                asset::DecodeErrorCode::LimitExceeded,
                "level texture source path exceeds the string limit"));

        std::uint64_t component_objects = 0;
        if (!Multiply(source.hog_entries.size(), sizeof(std::string), component_objects) ||
            !Add(workspace, component_objects, workspace) ||
            !Add(workspace, source.game_path.size(), workspace))
            return std::unexpected(DecodeError(LevelTextureStoreErrorCode::LimitExceeded,
                asset::DecodeErrorCode::Overflow,
                "level texture source workspace size overflows"));

        for (const auto& component : source.hog_entries)
        {
            if (component.size() > limits.maximum_string_bytes)
                return std::unexpected(DecodeError(LevelTextureStoreErrorCode::LimitExceeded,
                    asset::DecodeErrorCode::LimitExceeded,
                    "level texture source component exceeds the string limit"));
            if (!Add(workspace, component.size(), workspace))
                return std::unexpected(DecodeError(LevelTextureStoreErrorCode::LimitExceeded,
                    asset::DecodeErrorCode::Overflow,
                    "level texture source workspace size overflows"));
        }
    }
    return workspace;
}

[[nodiscard]] bool LocatorLess(
    const asset::SourceLocator& left, const asset::SourceLocator& right) noexcept
{
    if (left.game_path != right.game_path)
        return left.game_path < right.game_path;
    return std::lexicographical_compare(left.hog_entries.begin(), left.hog_entries.end(),
        right.hog_entries.begin(), right.hog_entries.end());
}

[[nodiscard]] bool LocatorEqual(
    const asset::SourceLocator& left, const asset::SourceLocator& right) noexcept
{
    return left.game_path == right.game_path && left.hog_entries == right.hog_entries;
}

[[nodiscard]] std::expected<std::uint64_t, LevelTextureStoreError> LocatorOutputBytes(
    const asset::SourceLocator& locator)
{
    std::uint64_t string_objects = 0;
    std::uint64_t output_bytes = sizeof(asset::SourceLocator);
    if (!Multiply(locator.hog_entries.size(), sizeof(std::string), string_objects) ||
        !Add(output_bytes, string_objects, output_bytes) ||
        !Add(output_bytes, locator.game_path.size(), output_bytes))
        return std::unexpected(DecodeError(LevelTextureStoreErrorCode::LimitExceeded,
            asset::DecodeErrorCode::Overflow,
            "level texture locator output size overflows"));
    for (const auto& component : locator.hog_entries)
    {
        if (!Add(output_bytes, component.size(), output_bytes))
            return std::unexpected(DecodeError(LevelTextureStoreErrorCode::LimitExceeded,
                asset::DecodeErrorCode::Overflow,
                "level texture locator output size overflows"));
    }
    return output_bytes;
}

using ArchiveDirectory = std::map<std::string, const archive::HogEntry*>;

[[nodiscard]] std::expected<std::uint64_t, LevelTextureStoreError>
DirectoryWorkspaceUpperBound(
    const archive::HogArchive& parsed, const asset::DecodeLimits limits)
{
    constexpr std::uint64_t node_bytes =
        sizeof(ArchiveDirectory::value_type) + 4U * sizeof(void*);
    std::uint64_t workspace = 0;
    if (!Multiply(parsed.entries().size(), node_bytes, workspace))
        return std::unexpected(DecodeError(LevelTextureStoreErrorCode::LimitExceeded,
            asset::DecodeErrorCode::Overflow,
            "level texture directory workspace size overflows"));

    for (const auto& entry : parsed.entries())
    {
        if (entry.name.size() > limits.maximum_string_bytes)
            return std::unexpected(DecodeError(LevelTextureStoreErrorCode::LimitExceeded,
                asset::DecodeErrorCode::LimitExceeded,
                "level texture directory name exceeds the string limit"));
        if (!Add(workspace, entry.name.size(), workspace))
            return std::unexpected(DecodeError(LevelTextureStoreErrorCode::LimitExceeded,
                asset::DecodeErrorCode::Overflow,
                "level texture directory workspace size overflows"));
    }
    return workspace;
}

[[nodiscard]] std::expected<ArchiveDirectory, LevelTextureStoreError> BuildDirectory(
    const archive::HogArchive& parsed, const asset::DecodeLimits limits)
{
    ArchiveDirectory directory;
    for (const auto& entry : parsed.entries())
    {
        auto normalized = NormalizeName(entry.name, limits);
        if (!normalized)
            return std::unexpected(normalized.error());
        if (!directory.emplace(std::move(*normalized), &entry).second)
            return std::unexpected(DecodeError(
                LevelTextureStoreErrorCode::DuplicateReference,
                asset::DecodeErrorCode::DuplicateReference,
                "level texture archive contains an ambiguous normalized directory"));
    }
    return directory;
}

[[nodiscard]] std::expected<void, LevelTextureStoreError> InventoryTextureSource(
    OperationBudget& budget, const asset::DecodeLimits limits,
    const asset::SourceLocator& source, std::vector<std::byte> terminal_bytes,
    const std::uint64_t resident_source_workspace,
    std::vector<asset::SourceLocator>& locators)
{
    auto input = budget.ConsumeInput(terminal_bytes.size());
    if (!input)
        return std::unexpected(input.error());

    constexpr std::size_t minimum_hog_bytes = 0x14U + sizeof(std::uint32_t);
    if (terminal_bytes.size() >= minimum_hog_bytes)
    {
        auto directory_items = budget.ConsumeItems(ReadU32(terminal_bytes, 4U));
        if (!directory_items)
            return std::unexpected(directory_items.error());
    }

    auto parsed = archive::HogArchive::FromBytes(std::move(terminal_bytes));
    if (!parsed)
        return std::unexpected(Error(LevelTextureStoreErrorCode::MalformedArchive,
            "level texture source container is malformed"));

    auto workspace = DirectoryWorkspaceUpperBound(*parsed, limits);
    if (!workspace)
        return std::unexpected(workspace.error());
    std::uint64_t combined_workspace = 0;
    if (!Add(resident_source_workspace, *workspace, combined_workspace))
        return std::unexpected(DecodeError(LevelTextureStoreErrorCode::LimitExceeded,
            asset::DecodeErrorCode::Overflow,
            "level texture inventory workspace size overflows"));
    auto scratch = budget.ObserveScratch(combined_workspace);
    if (!scratch)
        return std::unexpected(scratch.error());

    // The complete normalized directory is constructed before extension filtering so every
    // collision is rejected, including collisions between entries the inventory does not use.
    auto directory = BuildDirectory(*parsed, limits);
    if (!directory)
        return std::unexpected(directory.error());

    for (const auto& [name, entry] : *directory)
    {
        static_cast<void>(entry);
        if (!name.ends_with(".TDX"))
            continue;
        asset::SourceLocator locator = source;
        locator.hog_entries.push_back(name);

        auto output_bytes = LocatorOutputBytes(locator);
        if (!output_bytes)
            return std::unexpected(output_bytes.error());
        auto item = budget.ConsumeItems(1U);
        if (!item)
            return std::unexpected(item.error());
        auto output = budget.ConsumeOutput(*output_bytes);
        if (!output)
            return std::unexpected(output.error());
        locators.push_back(std::move(locator));
    }
    return {};
}
} // namespace

struct LevelTextureStore::Impl
{
    std::shared_ptr<const detail::LevelTextureStoreIdentity> store_identity;
    std::weak_ptr<const void> source_identity;
    asset::DecodeLimits limits;
    std::vector<asset::SourceLocator> locators;
    LevelTextureOperationUsage open_usage;
};

std::string_view LevelTextureStoreErrorCodeName(const LevelTextureStoreErrorCode code) noexcept
{
    switch (code)
    {
    case LevelTextureStoreErrorCode::InvalidConfiguration:
        return "invalid-configuration";
    case LevelTextureStoreErrorCode::LimitExceeded:
        return "limit-exceeded";
    case LevelTextureStoreErrorCode::InvalidReference:
        return "invalid-reference";
    case LevelTextureStoreErrorCode::DuplicateReference:
        return "duplicate-reference";
    case LevelTextureStoreErrorCode::ReadFailed:
        return "read-failed";
    case LevelTextureStoreErrorCode::MalformedArchive:
        return "malformed-archive";
    case LevelTextureStoreErrorCode::DecodeFailed:
        return "decode-failed";
    case LevelTextureStoreErrorCode::ForeignService:
        return "foreign-service";
    case LevelTextureStoreErrorCode::InvalidHandle:
        return "invalid-handle";
    }
    return "unknown";
}

LevelTextureStore::LevelTextureStore(std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl))
{
}

LevelTextureStore::~LevelTextureStore() = default;
LevelTextureStore::LevelTextureStore(LevelTextureStore&&) noexcept = default;

std::expected<LevelTextureStore, LevelTextureStoreError> LevelTextureStore::Open(
    const GameDataService& game_data, const asset::LevelManifestIR& manifest,
    const LevelTextureStoreConfig config)
{
    const asset::DecodeLimits limits = config.limits;
    if (limits.maximum_input_bytes == 0 || limits.maximum_output_bytes == 0 ||
        limits.maximum_items == 0 || limits.maximum_string_bytes == 0)
        return std::unexpected(Error(LevelTextureStoreErrorCode::InvalidConfiguration,
            "level texture store limits must be non-zero"));
    if (manifest.texture_sources.empty())
        return std::unexpected(DecodeError(LevelTextureStoreErrorCode::InvalidReference,
            asset::DecodeErrorCode::InvalidReference,
            "level texture manifest has no explicit texture sources"));
    if (manifest.texture_sources.size() > limits.maximum_items)
        return std::unexpected(DecodeError(LevelTextureStoreErrorCode::LimitExceeded,
            asset::DecodeErrorCode::LimitExceeded,
            "level texture sources exceed the item limit"));

    OperationBudget budget(limits);
    std::uint64_t base_output = 0;
    if (!Add(sizeof(LevelTextureStore), sizeof(Impl), base_output) ||
        !Add(base_output, sizeof(detail::LevelTextureStoreIdentity), base_output))
        return std::unexpected(DecodeError(LevelTextureStoreErrorCode::LimitExceeded,
            asset::DecodeErrorCode::Overflow,
            "level texture store base output size overflows"));
    auto base = budget.ConsumeOutput(base_output);
    if (!base)
        return std::unexpected(base.error());

    const GameDataService::SourceBinding source_binding = game_data.source_binding();
    const auto source_identity = source_binding.identity.lock();
    if (!source_identity)
        return std::unexpected(Error(LevelTextureStoreErrorCode::ForeignService,
            "level texture source binding is unavailable"));

    auto source_workspace = SourceWorkspaceUpperBound(manifest.texture_sources, limits);
    if (!source_workspace)
        return std::unexpected(source_workspace.error());
    auto source_scratch = budget.ObserveScratch(*source_workspace);
    if (!source_scratch)
        return std::unexpected(source_scratch.error());

    std::vector<asset::SourceLocator> sources;
    sources.reserve(manifest.texture_sources.size());
    for (const auto& untrusted_source : manifest.texture_sources)
    {
        auto source = NormalizeSource(untrusted_source, limits);
        if (!source)
            return std::unexpected(source.error());
        sources.push_back(std::move(*source));
    }
    std::sort(sources.begin(), sources.end(), LocatorLess);
    sources.erase(std::unique(sources.begin(), sources.end(), LocatorEqual), sources.end());

    auto canonical_source_workspace = SourceWorkspaceUpperBound(sources, limits);
    if (!canonical_source_workspace)
        return std::unexpected(canonical_source_workspace.error());
    auto canonical_source_scratch = budget.ObserveScratch(*canonical_source_workspace);
    if (!canonical_source_scratch)
        return std::unexpected(canonical_source_scratch.error());

    std::vector<asset::SourceLocator> locators;
    for (const auto& source : sources)
    {
        const asset::DecodeLimits resolve_limits =
            budget.RemainingResolveLimits(*canonical_source_workspace);

        auto resolved = game_data.ResolveSourceLocator(source_binding, source, resolve_limits);
        if (!resolved)
            return std::unexpected(MapGameDataError(
                resolved.error(), "unable to resolve level texture container source"));

        std::uint64_t composed_resolver_scratch = 0;
        if (!Add(*canonical_source_workspace, resolved->peak_scratch_bytes,
                composed_resolver_scratch))
            return std::unexpected(DecodeError(LevelTextureStoreErrorCode::LimitExceeded,
                asset::DecodeErrorCode::Overflow,
                "level texture resolver workspace size overflows"));
        auto resolver_scratch = budget.ObserveScratch(composed_resolver_scratch);
        if (!resolver_scratch)
            return std::unexpected(resolver_scratch.error());

        auto ancestor_input = budget.ConsumeInput(resolved->ancestor_input_bytes);
        if (!ancestor_input)
            return std::unexpected(ancestor_input.error());
        auto ancestor_items = budget.ConsumeItems(resolved->ancestor_directory_items);
        if (!ancestor_items)
            return std::unexpected(ancestor_items.error());
        auto depth = budget.ObserveDepth(source.hog_entries.size());
        if (!depth)
            return std::unexpected(depth.error());

        // This call owns and destroys its directory workspace before the next source is resolved,
        // while the canonical source-list workspace remains resident across every source.
        auto inventoried = InventoryTextureSource(
            budget, limits, source, std::move(resolved->terminal_bytes),
            *canonical_source_workspace, locators);
        if (!inventoried)
            return std::unexpected(inventoried.error());
    }

    std::sort(locators.begin(), locators.end(), LocatorLess);
    locators.erase(std::unique(locators.begin(), locators.end(), LocatorEqual), locators.end());

    auto impl = std::make_unique<Impl>();
    impl->store_identity = std::make_shared<detail::LevelTextureStoreIdentity>();
    impl->source_identity = source_identity;
    impl->limits = limits;
    impl->locators = std::move(locators);
    impl->open_usage = budget.usage();
    return LevelTextureStore(std::move(impl));
}

std::expected<LoadedLevelTexture, LevelTextureStoreError> LevelTextureStore::Load(
    const GameDataService& game_data, const LevelTextureHandle& handle) const
{
    if (!impl_)
        return std::unexpected(Error(LevelTextureStoreErrorCode::InvalidHandle,
            "level texture store is unavailable"));

    const auto handle_identity = handle.store_identity_.lock();
    if (!handle_identity || handle_identity != impl_->store_identity ||
        handle.index_ >= impl_->locators.size())
        return std::unexpected(Error(LevelTextureStoreErrorCode::InvalidHandle,
            "level texture handle does not belong to this store"));

    const auto expected_source = impl_->source_identity.lock();
    const GameDataService::SourceBinding actual_binding = game_data.source_binding();
    const auto actual_source = actual_binding.identity.lock();
    if (!expected_source || !actual_source || expected_source != actual_source)
        return std::unexpected(Error(LevelTextureStoreErrorCode::ForeignService,
            "level texture store belongs to a different game-data service"));

    const GameDataService::SourceBinding expected_binding{
        .identity = impl_->source_identity,
    };
    auto resolved = game_data.ResolveSourceLocator(
        expected_binding, impl_->locators[handle.index_], impl_->limits);
    if (!resolved)
        return std::unexpected(MapGameDataError(
            resolved.error(), "unable to resolve level texture source"));

    OperationBudget budget(impl_->limits);
    auto resolver_scratch = budget.ObserveScratch(resolved->peak_scratch_bytes);
    if (!resolver_scratch)
        return std::unexpected(resolver_scratch.error());
    auto ancestor_input = budget.ConsumeInput(resolved->ancestor_input_bytes);
    if (!ancestor_input)
        return std::unexpected(ancestor_input.error());
    auto ancestor_items = budget.ConsumeItems(resolved->ancestor_directory_items);
    if (!ancestor_items)
        return std::unexpected(ancestor_items.error());
    auto depth = budget.ObserveDepth(resolved->archive_depth);
    if (!depth)
        return std::unexpected(depth.error());

    auto decoded = retail::DecodeTdxTextureStorageMeasured(
        resolved->terminal_bytes, budget.RemainingLimits());
    if (!decoded)
        return std::unexpected(MapDecodeError(
            decoded.error(), "unable to decode level texture storage"));

    auto terminal_input = budget.ConsumeInput(resolved->terminal_bytes.size());
    if (!terminal_input)
        return std::unexpected(terminal_input.error());
    auto decoded_items = budget.ConsumeItems(decoded->decoded_items);
    if (!decoded_items)
        return std::unexpected(decoded_items.error());
    auto decoded_output = budget.ConsumeOutput(decoded->logical_output_bytes);
    if (!decoded_output)
        return std::unexpected(decoded_output.error());

    return LoadedLevelTexture{
        .storage = std::move(decoded->storage),
        .usage = budget.usage(),
    };
}

std::size_t LevelTextureStore::size() const noexcept
{
    return impl_ ? impl_->locators.size() : 0;
}

std::expected<LevelTextureHandle, LevelTextureStoreError> LevelTextureStore::HandleAt(
    const std::size_t index) const noexcept
{
    if (!impl_ || index >= impl_->locators.size())
        return std::unexpected(LevelTextureStoreError{
            .code = LevelTextureStoreErrorCode::InvalidHandle,
        });
    return LevelTextureHandle(impl_->store_identity, index);
}

const LevelTextureOperationUsage& LevelTextureStore::open_usage() const noexcept
{
    static const LevelTextureOperationUsage empty;
    return impl_ ? impl_->open_usage : empty;
}
} // namespace omega::content
