#pragma once

#include "omega/asset/decode.h"
#include "omega/asset/level_ir.h"
#include "omega/asset/level_material_catalogs_ir.h"
#include "omega/asset/level_spatial_ir.h"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace omega::content
{
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
    asset::DecodeLimits decode_limits;
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

    // [game thread, after all readers have joined]
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

private:
    struct Impl;
    explicit GameDataService(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};
} // namespace omega::content
