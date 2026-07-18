#pragma once

#include "omega/asset/decode.h"
#include "omega/asset/level_content_ir.h"
#include "omega/asset/level_ir.h"
#include "omega/asset/level_material_catalogs_ir.h"
#include "omega/asset/level_spatial_ir.h"
#include "omega/asset/source_locator.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace omega::content
{
class LevelTextureStore;
struct GameDataServiceTestAccess;

enum class RetailBuild
{
    NtscUScus97264,
};

[[nodiscard]] std::string_view RetailBuildName(RetailBuild build) noexcept;

struct GameDataIdentity
{
    RetailBuild build = RetailBuild::NtscUScus97264;
    std::string boot_executable;
};

enum class GameDataErrorCode
{
    InvalidConfiguration,
    ForeignService,
    MountFailed,
    MissingRequiredFile,
    UnsupportedBuild,
    InvalidLevelCode,
    ReadFailed,
    MalformedArchive,
    DecodeFailed,
};

[[nodiscard]] std::string_view GameDataErrorCodeName(GameDataErrorCode code) noexcept;

struct GameDataError
{
    GameDataErrorCode code = GameDataErrorCode::InvalidConfiguration;
    std::string message;
    std::optional<asset::DecodeError> decode_error;
};

struct GameDataServiceConfig
{
    std::filesystem::path root;
    // Bound archive input/copy workspace independently from semantic DecodeLimits scratch.
    std::uint64_t maximum_system_config_bytes = 4096;
    std::uint64_t maximum_pop_bytes = 64ULL * 1024ULL * 1024ULL;
    std::uint64_t maximum_data_hog_bytes = 64ULL * 1024ULL * 1024ULL;
    std::uint64_t maximum_nested_hog_bytes = 64ULL * 1024ULL * 1024ULL;
    // Whole-level spatial/material composition is corpus-confirmed at 72 MiB of cumulative input;
    // standalone asset decoders retain their narrower 64 MiB default.
    asset::DecodeLimits decode_limits{.maximum_input_bytes = 72ULL * 1024ULL * 1024ULL};
};

// Non-hot-reloadable data-root service. OmegaApp is the sole owner; future AssetService instances
// receive a non-owning reference. The mounted VFS is immutable after Open(), so concurrent level
// bootstrap reads do not mutate shared state.
class GameDataService final
{
public:
    // [game thread] Validates and freezes one owner-supplied NTSC-U retail data tree.
    [[nodiscard]] static std::expected<GameDataService, GameDataError> Open(
        GameDataServiceConfig config);

    // [game thread, with no concurrent readers; destruction after all readers have joined]
    ~GameDataService();
    GameDataService(GameDataService&&) noexcept;
    GameDataService& operator=(GameDataService&&) noexcept;
    GameDataService(const GameDataService&) = delete;
    GameDataService& operator=(const GameDataService&) = delete;

    // [any thread after Open(); immutable]
    [[nodiscard]] const GameDataIdentity& identity() const noexcept;

    // [any worker thread after Open(); thread-safe] Returns canonical owned data only. The caller
    // never receives archive views or retail-format storage.
    [[nodiscard]] std::expected<asset::LevelManifestIR, GameDataError> LoadLevelManifest(
        std::string_view level_code) const;

    // [any worker thread after Open(); thread-safe] Resolves the manifest's common archive and
    // every cell HOG, then returns one owned neutral spatial mesh per terrain cell. Archive objects,
    // retail offsets, and byte spans remain local to this call.
    [[nodiscard]] std::expected<asset::LevelSpatialIR, GameDataError> LoadLevelSpatial(
        const asset::LevelManifestIR& manifest) const;

    // [any worker thread after Open(); thread-safe] Resolves the manifest's common archive and
    // every cell HOG, then returns the unique VUM member's owned semantic material/name catalog per
    // terrain cell. Output follows manifest order; names retain no assigned role or asset binding.
    [[nodiscard]] std::expected<asset::LevelMaterialCatalogsIR, GameDataError>
    LoadLevelMaterialCatalogs(const asset::LevelManifestIR& manifest) const;

    // [any worker thread after Open(); thread-safe] Resolves each archive and cell once, then
    // returns both canonical spatial meshes and material/name catalogs as one all-or-error value
    // under one shared operation budget. Parallel vector positions preserve manifest order but
    // imply no binding.
    [[nodiscard]] std::expected<asset::LevelContentIR, GameDataError> LoadLevelContent(
        const asset::LevelManifestIR& manifest) const;

private:
    // Opaque non-owning source identity. It follows the service implementation across moves but
    // cannot keep the service or its VFS alive.
    struct SourceBinding
    {
        std::weak_ptr<const void> identity;
    };

    // Owned terminal bytes plus work already performed while resolving ancestor containers. The
    // terminal member is deliberately not charged here: an Open caller charges it as a container,
    // while a Load caller charges it once as decoder input. A terminal .HOG component still obeys
    // the service's nested-HOG byte cap before it crosses this boundary.
    struct ResolvedSourceLocator
    {
        std::vector<std::byte> terminal_bytes;
        std::uint64_t ancestor_input_bytes = 0;
        std::uint64_t ancestor_directory_items = 0;
        std::uint32_t archive_depth = 0;
    };

    // [any worker thread after Open(); thread-safe] The expected binding is checked before any VFS
    // query or read. All returned storage is independently owned.
    [[nodiscard]] SourceBinding source_binding() const noexcept;
    [[nodiscard]] std::expected<ResolvedSourceLocator, GameDataError> ResolveSourceLocator(
        const SourceBinding& expected_source, const asset::SourceLocator& locator,
        asset::DecodeLimits caller_limits) const;

    struct Impl;
    explicit GameDataService(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;

    friend class LevelTextureStore;
    friend struct GameDataServiceTestAccess;
};
} // namespace omega::content
