#include "omega/content/game_data_service.h"
#include "omega/content/level_texture_store.h"
#include "omega/retail/tdx_texture_storage_decoder.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace
{
using omega::asset::DecodeLimits;
using omega::asset::LevelManifestIR;
using omega::asset::SourceLocator;
using omega::content::GameDataService;
using omega::content::LevelTextureHandle;
using omega::content::LevelTextureOperationUsage;
using omega::content::LevelTextureStore;
using omega::content::LevelTextureStoreConfig;
using omega::content::LevelTextureStoreError;
using omega::content::LevelTextureStoreErrorCode;

int failures = 0;

void Check(const bool condition, const std::string_view message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

std::string PrivacyComparable(const std::string_view value)
{
    std::string comparable;
    comparable.reserve(value.size());
    for (const unsigned char character : value)
    {
        if (character == static_cast<unsigned char>('\\'))
        {
            comparable.push_back('/');
            continue;
        }
        comparable.push_back(static_cast<char>(
            character >= static_cast<unsigned char>('A') &&
                    character <= static_cast<unsigned char>('Z')
                ? character + ('a' - 'A')
                : character));
    }
    return comparable;
}

template <typename Value>
void CheckError(const std::expected<Value, LevelTextureStoreError>& result,
                const LevelTextureStoreErrorCode code, const std::string_view message)
{
    Check(!result && result.error().code == code, message);
}

void WriteU16(std::vector<std::byte>& bytes, const std::size_t offset, const std::uint16_t value)
{
    bytes[offset] = static_cast<std::byte>(value & 0xFFU);
    bytes[offset + 1U] = static_cast<std::byte>((value >> 8U) & 0xFFU);
}

void WriteU32(std::vector<std::byte>& bytes, const std::size_t offset, const std::uint32_t value)
{
    for (unsigned shift = 0; shift < 32; shift += 8)
        bytes[offset + shift / 8U] = static_cast<std::byte>((value >> shift) & 0xFFU);
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

struct HogMember
{
    std::string name;
    std::vector<std::byte> payload;
};

std::vector<std::byte> MakeHog(const std::vector<HogMember>& members)
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
    bytes.reserve(data_offset + payload_bytes);
    for (const auto& member : members)
        bytes.insert(bytes.end(), member.payload.begin(), member.payload.end());
    return bytes;
}

bool WriteBytes(const std::filesystem::path& path, const std::span<const std::byte> bytes)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
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

class TempTree final
{
  public:
    explicit TempTree(const std::string_view label)
    {
        static std::atomic<std::uint64_t> next{0};
        const auto stamp =
            static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
        root_ = std::filesystem::temp_directory_path() /
                ("omega-level-texture-store-" + std::string(label) + "-" + std::to_string(stamp) +
                 "-" + std::to_string(next.fetch_add(1)));
        std::error_code error;
        std::filesystem::create_directories(root_ / "GAMEDATA" / "TEST", error);
        ready_ = !error &&
                 WriteText(root_ / "SYSTEM.CNF", "BOOT2 = cdrom0:\\SCUS_972.64;1\r\nVER "
                                                 "= 1.00\r\nVMODE = NTSC\r\n") &&
                 WriteText(root_ / "SCUS_972.64", "synthetic placeholder");
    }

    ~TempTree()
    {
        std::error_code error;
        std::filesystem::remove_all(root_, error);
    }

    TempTree(const TempTree&) = delete;
    TempTree& operator=(const TempTree&) = delete;

    [[nodiscard]] bool ready() const noexcept { return ready_; }
    [[nodiscard]] const std::filesystem::path& root() const noexcept { return root_; }

    [[nodiscard]] bool WriteGameFile(const std::string_view name,
                                     const std::span<const std::byte> bytes) const
    {
        return WriteBytes(root_ / "GAMEDATA" / "TEST" / std::string(name), bytes);
    }

    [[nodiscard]] bool RemoveGameFile(const std::string_view name) const
    {
        std::error_code error;
        const bool removed =
            std::filesystem::remove(root_ / "GAMEDATA" / "TEST" / std::string(name), error);
        return removed && !error;
    }

  private:
    std::filesystem::path root_;
    bool ready_ = false;
};

std::expected<GameDataService, omega::content::GameDataError> OpenService(const TempTree& tree)
{
    return GameDataService::Open({.root = tree.root()});
}

std::expected<LevelTextureStore, LevelTextureStoreError> OpenStore(const GameDataService& service,
                                                                   const LevelManifestIR& manifest)
{
    return LevelTextureStore::Open(service, manifest);
}

std::expected<LevelTextureStore, LevelTextureStoreError> OpenStore(const GameDataService& service,
                                                                   const LevelManifestIR& manifest,
                                                                   const DecodeLimits limits)
{
    return LevelTextureStore::Open(service, manifest, LevelTextureStoreConfig{.limits = limits});
}

void CheckInternalDefaultLimits()
{
    const DecodeLimits limits = LevelTextureStoreConfig{}.limits;
    Check(limits.maximum_input_bytes == 4ULL * 1024ULL * 1024ULL,
          "internal texture-store input limit is pinned to 4 MiB fieldwise headroom");
    Check(limits.maximum_output_bytes == 512ULL * 1024ULL,
          "internal texture-store output limit is pinned to 512 KiB fieldwise headroom");
    Check(limits.maximum_scratch_bytes == 128ULL * 1024ULL,
          "internal texture-store scratch limit is pinned to 128 KiB fieldwise headroom");
    Check(limits.maximum_items == (1ULL << 13U),
          "internal texture-store item limit is pinned to 8192 fieldwise headroom");
    Check(limits.maximum_string_bytes == 4096U,
          "internal texture-store string limit retains the common 4 KiB safety cap");
    Check(limits.maximum_nesting_depth == 1U,
          "internal texture-store depth keeps one edge of headroom above measured depth zero");
}

SourceLocator DirectSource(const std::string_view game_path)
{
    return SourceLocator{.game_path = std::string(game_path), .hog_entries = {}};
}

LevelManifestIR MakeManifest(std::vector<SourceLocator> texture_sources)
{
    LevelManifestIR manifest;
    manifest.data_hog_source = DirectSource("GAMEDATA/TEST/UNUSED.HOG");
    manifest.texture_sources = std::move(texture_sources);
    return manifest;
}

std::uint8_t FirstStorageByte(const omega::asset::TextureStorageIR& storage)
{
    if (storage.blocks.empty() || storage.blocks.front().planes.empty() ||
        storage.blocks.front().planes.front().bytes.empty())
        return 0;
    return std::to_integer<std::uint8_t>(storage.blocks.front().planes.front().bytes.front());
}

bool SameUsage(const LevelTextureOperationUsage& left, const LevelTextureOperationUsage& right)
{
    return left.input_bytes == right.input_bytes && left.items == right.items &&
           left.logical_output_bytes == right.logical_output_bytes &&
           left.archive_depth == right.archive_depth &&
           left.peak_scratch_bytes == right.peak_scratch_bytes;
}

bool SamePersistentUsage(const LevelTextureOperationUsage& left,
                         const LevelTextureOperationUsage& right)
{
    return left.input_bytes == right.input_bytes && left.items == right.items &&
           left.logical_output_bytes == right.logical_output_bytes &&
           left.archive_depth == right.archive_depth;
}

bool Sanitized(const LevelTextureStoreError& error, const std::filesystem::path& root)
{
    const std::array<std::string, 9> forbidden{
        root.string(), root.generic_string(), "GAMEDATA", "TEX.HOG", "MAPTEX.HOG",
        "PACK.HOG", "MUTABLE.TDX", "BROKEN.TDX", "COLLIDE.BIN"};
    const auto contains_forbidden = [&forbidden](const std::string& message)
    {
        const std::string comparable_message = PrivacyComparable(message);
        return std::ranges::any_of(
            forbidden, [&comparable_message](const std::string& value)
            {
                const std::string comparable_value = PrivacyComparable(value);
                return !comparable_value.empty() &&
                       comparable_message.find(comparable_value) != std::string::npos;
            });
    };
    if (contains_forbidden(error.message))
        return false;
    return !error.decode_error ||
           (!error.decode_error->byte_offset && !contains_forbidden(error.decode_error->message));
}

struct CanonicalFixture
{
    std::array<std::vector<std::byte>, 5> textures;
    std::vector<std::byte> primary_hog;
    std::vector<std::byte> map_hog;
};

CanonicalFixture MakeCanonicalFixture(const bool reversed)
{
    CanonicalFixture fixture;
    fixture.textures = {MakeDirect24Tdx(0x10), MakeDirect24Tdx(0x20), MakeDirect24Tdx(0x30),
                        MakeDirect24Tdx(0x40), MakeDirect24Tdx(0x70)};
    const auto nested = MakeHog({HogMember{
        .name = "DECOY.TDX",
        .payload = fixture.textures[4],
    }});

    std::vector<HogMember> primary{
        HogMember{.name = "Omega.tDx", .payload = fixture.textures[1]},
        HogMember{.name = "alpha.TDX", .payload = fixture.textures[0]},
    };
    std::vector<HogMember> map{
        HogMember{.name = "Zulu.tdx", .payload = fixture.textures[3]},
        HogMember{.name = "nested.HOG", .payload = nested},
        HogMember{.name = "beta.TDX", .payload = fixture.textures[2]},
        HogMember{.name = "readme.bin", .payload = {std::byte{0x01}}},
    };
    if (reversed)
    {
        std::reverse(primary.begin(), primary.end());
        std::reverse(map.begin(), map.end());
    }
    fixture.primary_hog = MakeHog(primary);
    fixture.map_hog = MakeHog(map);
    return fixture;
}

bool WriteCanonicalFixture(const TempTree& tree, const CanonicalFixture& fixture)
{
    return tree.WriteGameFile("TEX.HOG", fixture.primary_hog) &&
           tree.WriteGameFile("MAPTEX.HOG", fixture.map_hog);
}

struct ExpectedLoad
{
    const std::vector<std::byte>* tdx = nullptr;
    std::uint64_t source_bytes = 0;
    std::uint64_t directory_items = 0;
};

std::vector<std::uint8_t> CheckCanonicalLoads(const LevelTextureStore& store,
                                              const GameDataService& service,
                                              const CanonicalFixture& fixture)
{
    const std::array<ExpectedLoad, 4> expected{
        ExpectedLoad{&fixture.textures[2], fixture.map_hog.size(), 4},
        ExpectedLoad{&fixture.textures[3], fixture.map_hog.size(), 4},
        ExpectedLoad{&fixture.textures[0], fixture.primary_hog.size(), 2},
        ExpectedLoad{&fixture.textures[1], fixture.primary_hog.size(), 2},
    };
    std::vector<std::uint8_t> seeds;
    for (std::size_t index = 0; index < expected.size(); ++index)
    {
        auto handle = store.HandleAt(index);
        Check(handle.has_value(), "canonical handle is available");
        if (!handle)
            continue;
        auto loaded = store.Load(service, *handle);
        Check(loaded.has_value(), "canonical handle loads independently owned storage");
        if (!loaded)
            continue;
        auto standalone = omega::retail::DecodeTdxTextureStorageMeasured(*expected[index].tdx);
        Check(standalone.has_value(), "synthetic canonical TDX decodes standalone");
        if (!standalone)
            continue;
        Check(loaded->storage == standalone->storage,
              "store Load equals the standalone measured decoder result");
        Check(loaded->usage.input_bytes ==
                  expected[index].source_bytes + expected[index].tdx->size(),
              "Load reports exact ancestor plus terminal input");
        Check(loaded->usage.items == expected[index].directory_items + standalone->decoded_items,
              "Load reports exact ancestor plus semantic items");
        Check(loaded->usage.logical_output_bytes == standalone->logical_output_bytes,
              "Load reports exact canonical output");
        Check(loaded->usage.archive_depth == 0,
              "direct texture members do not add a container edge");
        seeds.push_back(FirstStorageByte(loaded->storage));
    }
    return seeds;
}

void CheckCanonicalOrderingAndDeduplication()
{
    const CanonicalFixture fixture = MakeCanonicalFixture(false);
    TempTree first_tree("canonical-first");
    Check(first_tree.ready() && WriteCanonicalFixture(first_tree, fixture),
          "canonical texture containers are written");
    auto first_service_open = OpenService(first_tree);
    Check(first_service_open.has_value(), "canonical game-data service opens");
    if (!first_service_open)
        return;
    GameDataService first_service = std::move(*first_service_open);

    auto manifest = MakeManifest(
        {DirectSource("gamedata\\test\\tex.hog"), DirectSource("GAMEDATA/TEST/MAPTEX.HOG")});
    auto first_store = OpenStore(first_service, manifest);
    Check(first_store.has_value(), "explicit primary and map texture sources open");
    if (!first_store)
        return;
    Check(first_store->size() == 4,
          "Open inventories direct TDX members and ignores a nested HOG TDX");
    const auto first_seeds = CheckCanonicalLoads(*first_store, first_service, fixture);
    const std::vector<std::uint8_t> expected_seeds{0x30, 0x40, 0x10, 0x20};
    Check(first_seeds == expected_seeds, "handles use canonical source then member lexical order");

    const CanonicalFixture reordered_fixture = MakeCanonicalFixture(true);
    TempTree reordered_tree("canonical-reordered");
    Check(reordered_tree.ready() && WriteCanonicalFixture(reordered_tree, reordered_fixture),
          "physically reordered texture containers are written");
    auto reordered_service_open = OpenService(reordered_tree);
    Check(reordered_service_open.has_value(), "reordered game-data service opens");
    if (reordered_service_open)
    {
        GameDataService reordered_service = std::move(*reordered_service_open);
        auto reordered_manifest = MakeManifest(
            {DirectSource("GAMEDATA/TEST/MAPTEX.HOG"), DirectSource("GAMEDATA/TEST/TEX.HOG")});
        auto reordered_store = OpenStore(reordered_service, reordered_manifest);
        Check(reordered_store.has_value(), "reversed source order opens");
        if (reordered_store)
        {
            const auto reordered_seeds =
                CheckCanonicalLoads(*reordered_store, reordered_service, reordered_fixture);
            Check(reordered_seeds == expected_seeds,
                  "source and physical directory order do not affect handles");
            Check(SameUsage(first_store->open_usage(), reordered_store->open_usage()),
                  "canonical reordering preserves exact Open usage");
        }
    }

    auto repeated_manifest = MakeManifest(
        {DirectSource("GAMEDATA/TEST/TEX.HOG"), DirectSource("gamedata\\test\\maptex.hog"),
         DirectSource("gamedata/test/tex.hog"), DirectSource("GAMEDATA/TEST/MAPTEX.HOG")});
    auto repeated_store = OpenStore(first_service, repeated_manifest);
    Check(repeated_store && repeated_store->size() == first_store->size(),
          "normalized exact source and resulting locator duplicates are removed");
    if (repeated_store)
    {
        Check(SamePersistentUsage(repeated_store->open_usage(), first_store->open_usage()),
              "deduplicated sources are resolved, retained, and charged only once");
        Check(repeated_store->open_usage().peak_scratch_bytes >=
                  first_store->open_usage().peak_scratch_bytes,
              "raw duplicate sources cannot reduce bounded construction scratch");
        Check(CheckCanonicalLoads(*repeated_store, first_service, fixture) == expected_seeds,
              "deduplication preserves canonical handle identity");
    }

    manifest.texture_sources.clear();
    Check(CheckCanonicalLoads(*first_store, first_service, fixture) == expected_seeds,
          "the store owns locators independently of later manifest mutation");
}

void CheckEmptyInvalidAndFailureSurfaces()
{
    TempTree tree("failure-surfaces");
    const auto valid_tdx = MakeDirect24Tdx(0x21);
    const auto valid_hog = MakeHog({HogMember{.name = "VALID.TDX", .payload = valid_tdx}});
    const auto empty_hog = MakeHog({});
    const auto collision_hog = MakeHog({
        HogMember{.name = "Collide.BIN", .payload = {std::byte{0x01}}},
        HogMember{.name = "cOLLIDE.bin", .payload = {std::byte{0x02}}},
    });
    const std::vector<std::byte> non_hog(12, std::byte{0x6A});
    Check(tree.ready() && tree.WriteGameFile("TEX.HOG", valid_hog) &&
              tree.WriteGameFile("EMPTY.HOG", empty_hog) &&
              tree.WriteGameFile("COLLISION.HOG", collision_hog) &&
              tree.WriteGameFile("NOT_A_HOG.BIN", non_hog),
          "failure-surface fixture is written");
    auto service_open = OpenService(tree);
    Check(service_open.has_value(), "failure-surface service opens");
    if (!service_open)
        return;
    GameDataService service = std::move(*service_open);
    const auto valid_manifest = MakeManifest({DirectSource("GAMEDATA/TEST/TEX.HOG")});

    CheckError(OpenStore(service, MakeManifest({})), LevelTextureStoreErrorCode::InvalidReference,
               "a manifest without explicit texture sources is rejected");

    DecodeLimits zero_input;
    zero_input.maximum_input_bytes = 0;
    CheckError(OpenStore(service, valid_manifest, zero_input),
               LevelTextureStoreErrorCode::InvalidConfiguration,
               "zero input capacity is invalid configuration");
    DecodeLimits zero_output;
    zero_output.maximum_output_bytes = 0;
    CheckError(OpenStore(service, valid_manifest, zero_output),
               LevelTextureStoreErrorCode::InvalidConfiguration,
               "zero output capacity is invalid configuration");
    DecodeLimits zero_items;
    zero_items.maximum_items = 0;
    CheckError(OpenStore(service, valid_manifest, zero_items),
               LevelTextureStoreErrorCode::InvalidConfiguration,
               "zero item capacity is invalid configuration");
    DecodeLimits zero_string;
    zero_string.maximum_string_bytes = 0;
    CheckError(OpenStore(service, valid_manifest, zero_string),
               LevelTextureStoreErrorCode::InvalidConfiguration,
               "zero string capacity is invalid configuration");

    auto empty_store = OpenStore(service, MakeManifest({DirectSource("GAMEDATA/TEST/EMPTY.HOG")}));
    Check(empty_store && empty_store->size() == 0,
          "a valid no-TDX container yields an empty store");
    if (empty_store)
        CheckError(empty_store->HandleAt(0), LevelTextureStoreErrorCode::InvalidHandle,
                   "an empty store exposes no handles");

    Check(tree.RemoveGameFile("EMPTY.HOG"),
          "empty source is removed before scratch preflight coverage");
    DecodeLimits zero_scratch;
    zero_scratch.maximum_scratch_bytes = 0;
    CheckError(
        OpenStore(service, MakeManifest({DirectSource("GAMEDATA/TEST/EMPTY.HOG")}), zero_scratch),
        LevelTextureStoreErrorCode::LimitExceeded,
        "source-list scratch is rejected before unavailable-source I/O");

    DecodeLimits top_level_depth;
    top_level_depth.maximum_nesting_depth = 0;
    Check(OpenStore(service, valid_manifest, top_level_depth).has_value(),
          "a top-level texture source is valid at depth zero");

    auto collision =
        OpenStore(service, MakeManifest({DirectSource("GAMEDATA/TEST/COLLISION.HOG")}));
    CheckError(collision, LevelTextureStoreErrorCode::DuplicateReference,
               "the complete directory is collision-checked before TDX filtering");
    if (!collision)
        Check(Sanitized(collision.error(), tree.root()),
              "collision diagnostics disclose no source identity");

    auto malformed =
        OpenStore(service, MakeManifest({DirectSource("GAMEDATA/TEST/NOT_A_HOG.BIN")}));
    CheckError(malformed, LevelTextureStoreErrorCode::MalformedArchive,
               "non-HOG source bytes are rejected as a malformed container");
    if (!malformed)
        Check(Sanitized(malformed.error(), tree.root()),
              "malformed-container diagnostics are sanitized");

    auto missing = OpenStore(service, MakeManifest({DirectSource("GAMEDATA/TEST/MISSING.HOG")}));
    CheckError(missing, LevelTextureStoreErrorCode::ReadFailed,
               "a missing explicit source is reported without partial store state");
    if (!missing)
        Check(Sanitized(missing.error(), tree.root()), "missing-source diagnostics are sanitized");

    auto invalid = OpenStore(service, MakeManifest({DirectSource("GAMEDATA/TEST/../ESCAPE.HOG")}));
    CheckError(invalid, LevelTextureStoreErrorCode::InvalidReference,
               "an invalid source locator is rejected before I/O");
}

enum class Boundary
{
    Input,
    Items,
    Output,
    Depth,
    Scratch,
};

std::uint64_t BoundaryUsage(const LevelTextureOperationUsage& usage, const Boundary boundary)
{
    switch (boundary)
    {
    case Boundary::Input:
        return usage.input_bytes;
    case Boundary::Items:
        return usage.items;
    case Boundary::Output:
        return usage.logical_output_bytes;
    case Boundary::Depth:
        return usage.archive_depth;
    case Boundary::Scratch:
        return usage.peak_scratch_bytes;
    }
    return 0;
}

void SetBoundary(DecodeLimits& limits, const Boundary boundary, const std::uint64_t value)
{
    switch (boundary)
    {
    case Boundary::Input:
        limits.maximum_input_bytes = value;
        return;
    case Boundary::Items:
        limits.maximum_items = value;
        return;
    case Boundary::Output:
        limits.maximum_output_bytes = value;
        return;
    case Boundary::Depth:
        limits.maximum_nesting_depth = static_cast<std::uint32_t>(value);
        return;
    case Boundary::Scratch:
        limits.maximum_scratch_bytes = value;
        return;
    }
}

std::string_view BoundaryName(const Boundary boundary)
{
    switch (boundary)
    {
    case Boundary::Input:
        return "input";
    case Boundary::Items:
        return "items";
    case Boundary::Output:
        return "output";
    case Boundary::Depth:
        return "depth";
    case Boundary::Scratch:
        return "scratch";
    }
    return "unknown";
}

struct NestedFixture
{
    std::vector<std::byte> tdx;
    std::vector<std::byte> texture_hog;
    std::vector<std::byte> pack_hog;
    LevelManifestIR manifest;
};

NestedFixture MakeNestedFixture(const std::uint8_t seed)
{
    NestedFixture fixture;
    fixture.tdx = MakeDirect24Tdx(seed);
    fixture.texture_hog = MakeHog({
        HogMember{.name = "Only.TDX", .payload = fixture.tdx},
    });
    fixture.pack_hog = MakeHog({
        HogMember{.name = "Textures.HOG", .payload = fixture.texture_hog},
    });
    SourceLocator source = DirectSource("GAMEDATA/TEST/PACK.HOG");
    source.hog_entries.push_back("textures.hog");
    fixture.manifest = MakeManifest({std::move(source)});
    return fixture;
}

void CheckOpenAndLoadBudgets()
{
    TempTree tree("operation-budgets");
    const NestedFixture fixture = MakeNestedFixture(0x33);
    std::string long_member_name(96, 'L');
    long_member_name += ".TDX";
    const auto direct_scratch_tdx = MakeDirect24Tdx(0x35);
    const auto direct_scratch_hog = MakeHog({
        HogMember{.name = long_member_name, .payload = direct_scratch_tdx},
    });
    Check(tree.ready() && tree.WriteGameFile("PACK.HOG", fixture.pack_hog) &&
              tree.WriteGameFile("DIRECT.HOG", direct_scratch_hog),
          "nested explicit texture-source fixture is written");
    auto service_open = OpenService(tree);
    Check(service_open.has_value(), "operation-budget service opens");
    if (!service_open)
        return;
    GameDataService service = std::move(*service_open);

    auto baseline_store = OpenStore(service, fixture.manifest);
    Check(baseline_store && baseline_store->size() == 1,
          "internal defaults preserve one-edge synthetic nested-source support");
    if (!baseline_store)
        return;
    const LevelTextureOperationUsage open_usage = baseline_store->open_usage();
    Check(open_usage.input_bytes == fixture.pack_hog.size() + fixture.texture_hog.size(),
          "Open counts the ancestor and terminal texture containers once");
    Check(open_usage.items == 3, "Open counts two directory entries and one stored locator");
    Check(open_usage.archive_depth == 1, "Open counts the explicit terminal-container edge");
    Check(open_usage.logical_output_bytes != 0 && open_usage.peak_scratch_bytes != 0,
          "Open reports owned store output and bounded directory scratch");

    const std::array<Boundary, 5> open_boundaries{
        Boundary::Input, Boundary::Items, Boundary::Output, Boundary::Depth, Boundary::Scratch};
    for (const Boundary boundary : open_boundaries)
    {
        const std::uint64_t exact = BoundaryUsage(open_usage, boundary);
        Check(exact != 0, "Open boundary fixture has a non-zero exact usage");
        DecodeLimits exact_limits;
        SetBoundary(exact_limits, boundary, exact);
        auto exact_store = OpenStore(service, fixture.manifest, exact_limits);
        Check(exact_store.has_value(),
              std::string("Open succeeds at exact ") + std::string(BoundaryName(boundary)));
        if (exact_store)
        {
            Check(SameUsage(exact_store->open_usage(), open_usage),
                  "exact Open boundary preserves measured usage");
            if (boundary == Boundary::Depth)
            {
                auto exact_handle = exact_store->HandleAt(0);
                Check(exact_handle && exact_store->Load(service, *exact_handle).has_value(),
                      "Load succeeds at the exact shared archive-depth boundary");
            }
        }

        DecodeLimits below_limits;
        SetBoundary(below_limits, boundary, exact - 1U);
        CheckError(OpenStore(service, fixture.manifest, below_limits),
                   LevelTextureStoreErrorCode::LimitExceeded,
                   std::string("Open rejects one below exact ") +
                       std::string(BoundaryName(boundary)));
    }

    auto handle = baseline_store->HandleAt(0);
    auto baseline_load =
        handle ? baseline_store->Load(service, *handle)
               : std::expected<omega::content::LoadedLevelTexture, LevelTextureStoreError>(
                     std::unexpected(LevelTextureStoreError{}));
    Check(baseline_load.has_value(), "operation-budget baseline texture loads");
    if (!baseline_load)
        return;
    const LevelTextureOperationUsage load_usage = baseline_load->usage;
    auto standalone = omega::retail::DecodeTdxTextureStorageMeasured(fixture.tdx);
    Check(standalone.has_value(), "budget fixture TDX decodes standalone");
    if (standalone)
    {
        Check(load_usage.input_bytes ==
                  fixture.pack_hog.size() + fixture.texture_hog.size() + fixture.tdx.size(),
              "Load reports exact ancestor and decoder input");
        Check(load_usage.items == 2U + standalone->decoded_items,
              "Load reports exact ancestor and decoder items");
        Check(load_usage.logical_output_bytes == standalone->logical_output_bytes,
              "Load reports exact decoder output");
    }
    Check(load_usage.archive_depth == open_usage.archive_depth,
          "Open and Load share the same container-edge depth");
    Check(load_usage.peak_scratch_bytes != 0,
          "Load reports the normalized locator plus ancestor-directory scratch "
          "peak");

    const std::array<Boundary, 3> load_boundaries{Boundary::Input, Boundary::Items,
                                                  Boundary::Output};
    for (const Boundary boundary : load_boundaries)
    {
        const std::uint64_t exact = BoundaryUsage(load_usage, boundary);
        Check(exact > BoundaryUsage(open_usage, boundary),
              "budget fixture isolates a larger Load boundary from Open");
        DecodeLimits exact_limits;
        SetBoundary(exact_limits, boundary, exact);
        auto exact_store = OpenStore(service, fixture.manifest, exact_limits);
        Check(exact_store.has_value(), std::string("Load exact ") +
                                           std::string(BoundaryName(boundary)) +
                                           " limit permits Open");
        if (exact_store)
        {
            auto exact_handle = exact_store->HandleAt(0);
            Check(exact_handle && exact_store->Load(service, *exact_handle).has_value(),
                  std::string("Load succeeds at exact ") + std::string(BoundaryName(boundary)));
        }

        DecodeLimits below_limits;
        SetBoundary(below_limits, boundary, exact - 1U);
        auto below_store = OpenStore(service, fixture.manifest, below_limits);
        Check(below_store.has_value(), std::string("Load one-below ") +
                                           std::string(BoundaryName(boundary)) +
                                           " limit still permits Open");
        if (below_store)
        {
            auto below_handle = below_store->HandleAt(0);
            if (below_handle)
                CheckError(below_store->Load(service, *below_handle),
                           LevelTextureStoreErrorCode::LimitExceeded,
                           std::string("Load rejects one below exact ") +
                               std::string(BoundaryName(boundary)));
        }
    }

    const std::uint64_t shared_scratch =
        std::max(open_usage.peak_scratch_bytes, load_usage.peak_scratch_bytes);
    Check(shared_scratch != 0, "the composed source and directory path reports non-zero scratch");
    DecodeLimits exact_shared_scratch;
    exact_shared_scratch.maximum_scratch_bytes = shared_scratch;
    auto scratch_store = OpenStore(service, fixture.manifest, exact_shared_scratch);
    Check(scratch_store.has_value(), "Open succeeds at the exact maximum composed scratch peak");
    if (scratch_store)
    {
        auto scratch_handle = scratch_store->HandleAt(0);
        Check(scratch_handle && scratch_store->Load(service, *scratch_handle).has_value(),
              "Load succeeds at the exact maximum composed scratch peak");
    }
    if (load_usage.peak_scratch_bytes > open_usage.peak_scratch_bytes)
    {
        DecodeLimits below_load_scratch;
        below_load_scratch.maximum_scratch_bytes = load_usage.peak_scratch_bytes - 1U;
        auto below_store = OpenStore(service, fixture.manifest, below_load_scratch);
        Check(below_store.has_value(),
              "one-below Load scratch still permits the smaller Open peak");
        if (below_store)
        {
            auto below_handle = below_store->HandleAt(0);
            if (below_handle)
                CheckError(below_store->Load(service, *below_handle),
                           LevelTextureStoreErrorCode::LimitExceeded,
                           "Load rejects one below its exact scratch peak");
        }
    }
    else
    {
        Check(open_usage.peak_scratch_bytes >= load_usage.peak_scratch_bytes,
              "immutable store limits make Open the governing scratch peak");
    }

    const auto direct_manifest = MakeManifest({DirectSource("GAMEDATA/TEST/DIRECT.HOG")});
    auto direct_store = OpenStore(service, direct_manifest);
    auto direct_handle = direct_store ? direct_store->HandleAt(0)
                                      : std::expected<LevelTextureHandle, LevelTextureStoreError>(
                                            std::unexpected(LevelTextureStoreError{}));
    auto direct_load =
        direct_store && direct_handle
            ? direct_store->Load(service, *direct_handle)
            : std::expected<omega::content::LoadedLevelTexture, LevelTextureStoreError>(
                  std::unexpected(LevelTextureStoreError{}));
    Check(direct_store && direct_handle && direct_load,
          "direct long-name fixture establishes a Load scratch baseline");
    if (direct_store && direct_handle && direct_load)
    {
        const std::uint64_t exact_load_scratch = direct_load->usage.peak_scratch_bytes;
        Check(exact_load_scratch > direct_store->open_usage().peak_scratch_bytes,
              "direct long-name fixture isolates Load scratch above Open scratch");

        DecodeLimits exact_load_scratch_limits;
        exact_load_scratch_limits.maximum_scratch_bytes = exact_load_scratch;
        auto exact_load_store = OpenStore(service, direct_manifest, exact_load_scratch_limits);
        Check(exact_load_store.has_value(),
              "exact Load scratch limit still permits direct-source Open");
        if (exact_load_store)
        {
            auto exact_handle = exact_load_store->HandleAt(0);
            Check(exact_handle && exact_load_store->Load(service, *exact_handle).has_value(),
                  "Load succeeds at its exact resolver scratch peak");
        }

        DecodeLimits below_load_scratch_limits;
        below_load_scratch_limits.maximum_scratch_bytes = exact_load_scratch - 1U;
        auto below_load_store = OpenStore(service, direct_manifest, below_load_scratch_limits);
        Check(below_load_store.has_value(),
              "one-below Load scratch still permits direct-source Open");
        if (below_load_store)
        {
            auto below_handle = below_load_store->HandleAt(0);
            if (below_handle)
                CheckError(below_load_store->Load(service, *below_handle),
                           LevelTextureStoreErrorCode::LimitExceeded,
                           "Load rejects one byte below its exact resolver scratch peak");
        }
    }
}

void CheckHandleAndStorageOwnership()
{
    TempTree tree("handle-ownership");
    const auto tdx = MakeDirect24Tdx(0x44);
    const auto hog = MakeHog({HogMember{.name = "Owned.TDX", .payload = tdx}});
    Check(tree.ready() && tree.WriteGameFile("TEX.HOG", hog),
          "handle-ownership fixture is written");
    auto service_open = OpenService(tree);
    Check(service_open.has_value(), "handle-ownership service opens");
    if (!service_open)
        return;
    GameDataService service = std::move(*service_open);
    const auto manifest = MakeManifest({DirectSource("GAMEDATA/TEST/TEX.HOG")});
    auto store_open = OpenStore(service, manifest);
    Check(store_open.has_value(), "handle-ownership store opens");
    if (!store_open)
        return;
    LevelTextureStore store = std::move(*store_open);
    auto valid_handle = store.HandleAt(0);
    Check(valid_handle.has_value(), "valid handle is available");
    if (!valid_handle)
        return;

    LevelTextureHandle default_handle;
    Check(default_handle.expired(), "a default handle has no live store identity");
    CheckError(store.Load(service, default_handle), LevelTextureStoreErrorCode::InvalidHandle,
               "a default handle is rejected");
    CheckError(store.HandleAt(store.size()), LevelTextureStoreErrorCode::InvalidHandle,
               "out-of-range handle creation is rejected");

    auto second_store = OpenStore(service, manifest);
    Check(second_store.has_value(), "second store on the same service opens");
    if (second_store)
    {
        auto second_handle = second_store->HandleAt(0);
        if (second_handle)
            CheckError(store.Load(service, *second_handle),
                       LevelTextureStoreErrorCode::InvalidHandle,
                       "a cross-store handle is rejected");
    }

    LevelTextureHandle stale_handle;
    {
        auto temporary = OpenStore(service, manifest);
        if (temporary)
        {
            auto temporary_handle = temporary->HandleAt(0);
            if (temporary_handle)
                stale_handle = *temporary_handle;
        }
    }
    Check(stale_handle.expired(), "a handle expires with its owning store");
    CheckError(store.Load(service, stale_handle), LevelTextureStoreErrorCode::InvalidHandle,
               "an expired handle is rejected");

    auto moving_store = OpenStore(service, manifest);
    Check(moving_store.has_value(), "move-source store opens");
    if (moving_store)
    {
        auto moving_handle = moving_store->HandleAt(0);
        LevelTextureStore moved = std::move(*moving_store);
        Check(moving_store->size() == 0, "a moved-from store exposes no handles");
        Check(moving_handle && moved.Load(service, *moving_handle).has_value(),
              "move construction preserves existing handle validity");
        if (moving_handle)
            CheckError(moving_store->Load(service, *moving_handle),
                       LevelTextureStoreErrorCode::InvalidHandle,
                       "a moved-from store rejects the transferred handle");
    }

    auto first_load = store.Load(service, *valid_handle);
    auto second_load = store.Load(service, *valid_handle);
    Check(first_load && second_load && first_load->storage == second_load->storage,
          "repeated loads return equal canonical storage");
    if (first_load && second_load && !first_load->storage.blocks.empty() &&
        !first_load->storage.blocks.front().planes.empty() &&
        !first_load->storage.blocks.front().planes.front().bytes.empty())
    {
        first_load->storage.blocks.front().planes.front().bytes.front() = std::byte{0};
        Check(FirstStorageByte(second_load->storage) == 0x44,
              "each Load result owns independent storage bytes");
        auto third_load = store.Load(service, *valid_handle);
        Check(third_load && FirstStorageByte(third_load->storage) == 0x44,
              "caller mutation cannot affect future loads");
    }
}

void CheckServiceIdentityBeforeIo()
{
    const auto tdx = MakeDirect24Tdx(0x51);
    const auto hog = MakeHog({HogMember{.name = "Identity.TDX", .payload = tdx}});
    const auto manifest = MakeManifest({DirectSource("GAMEDATA/TEST/TEX.HOG")});

    TempTree owner_tree("identity-owner");
    TempTree foreign_tree("identity-foreign");
    Check(owner_tree.ready() && foreign_tree.ready() && owner_tree.WriteGameFile("TEX.HOG", hog) &&
              foreign_tree.WriteGameFile("TEX.HOG", hog),
          "owner and foreign service fixtures are written");
    auto owner_open = OpenService(owner_tree);
    auto foreign_open = OpenService(foreign_tree);
    Check(owner_open && foreign_open, "owner and foreign services open");
    if (!owner_open || !foreign_open)
        return;
    GameDataService owner = std::move(*owner_open);
    GameDataService foreign = std::move(*foreign_open);
    auto store = OpenStore(owner, manifest);
    auto handle = store ? store->HandleAt(0)
                        : std::expected<LevelTextureHandle, LevelTextureStoreError>(
                              std::unexpected(LevelTextureStoreError{}));
    Check(store && handle, "owner store and handle open before source removal");
    if (!store || !handle)
        return;

    Check(owner_tree.RemoveGameFile("TEX.HOG") && foreign_tree.RemoveGameFile("TEX.HOG"),
          "owner and foreign source files are removed after Open");
    auto rejected_foreign = store->Load(foreign, *handle);
    CheckError(rejected_foreign, LevelTextureStoreErrorCode::ForeignService,
               "a foreign service is rejected before unavailable-file I/O");
    if (!rejected_foreign)
        Check(Sanitized(rejected_foreign.error(), foreign_tree.root()),
              "foreign-service diagnostics are sanitized");
    CheckError(store->Load(owner, *handle), LevelTextureStoreErrorCode::ReadFailed,
               "the bound service reaches I/O and observes the removed file");

    TempTree expired_tree("identity-expired");
    Check(expired_tree.ready() && expired_tree.WriteGameFile("TEX.HOG", hog),
          "expired-service fixture is written");
    std::optional<LevelTextureStore> expired_store;
    LevelTextureHandle expired_service_handle;
    {
        auto temporary_service = OpenService(expired_tree);
        if (temporary_service)
        {
            auto temporary_store = OpenStore(*temporary_service, manifest);
            if (temporary_store)
            {
                auto temporary_handle = temporary_store->HandleAt(0);
                if (temporary_handle)
                    expired_service_handle = *temporary_handle;
                expired_store.emplace(std::move(*temporary_store));
            }
        }
    }
    Check(expired_store.has_value(), "store can outlive its non-owning service binding");
    Check(expired_tree.RemoveGameFile("TEX.HOG"),
          "expired-service source is unavailable before validation");
    if (expired_store)
        CheckError(expired_store->Load(foreign, expired_service_handle),
                   LevelTextureStoreErrorCode::ForeignService,
                   "an expired service binding is rejected before I/O");

    TempTree moving_tree("identity-moving");
    Check(moving_tree.ready() && moving_tree.WriteGameFile("TEX.HOG", hog),
          "moving-service fixture is written");
    auto moving_service = OpenService(moving_tree);
    if (moving_service)
    {
        auto moving_store = OpenStore(*moving_service, manifest);
        auto moving_handle = moving_store
                                 ? moving_store->HandleAt(0)
                                 : std::expected<LevelTextureHandle, LevelTextureStoreError>(
                                       std::unexpected(LevelTextureStoreError{}));
        GameDataService moved_service = std::move(*moving_service);
        Check(moving_store && moving_handle &&
                  moving_store->Load(moved_service, *moving_handle).has_value(),
              "GameDataService move construction preserves the source identity");
        Check(moving_tree.RemoveGameFile("TEX.HOG"),
              "moving-service source is removed after the successful load");
        if (moving_store && moving_handle)
        {
            CheckError(moving_store->Load(*moving_service, *moving_handle),
                       LevelTextureStoreErrorCode::ForeignService,
                       "a moved-from service is rejected before I/O");
            CheckError(moving_store->Load(moved_service, *moving_handle),
                       LevelTextureStoreErrorCode::ReadFailed,
                       "the moved-to service retains the binding and reaches I/O");
        }
    }
}

void CheckMutableFilesAndSanitizedDecodeErrors()
{
    TempTree tree("mutable-files");
    const auto first_tdx = MakeDirect24Tdx(0x61);
    const auto second_tdx = MakeDirect24Tdx(0x62);
    const auto first_hog = MakeHog({
        HogMember{.name = "Mutable.TDX", .payload = first_tdx},
    });
    const auto second_hog = MakeHog({
        HogMember{.name = "Mutable.TDX", .payload = second_tdx},
    });
    Check(tree.ready() && tree.WriteGameFile("TEX.HOG", first_hog),
          "mutable source fixture is written");
    auto service_open = OpenService(tree);
    Check(service_open.has_value(), "mutable-file service opens");
    if (!service_open)
        return;
    GameDataService service = std::move(*service_open);
    const auto manifest = MakeManifest({DirectSource("GAMEDATA/TEST/TEX.HOG")});
    auto store = OpenStore(service, manifest);
    auto handle = store ? store->HandleAt(0)
                        : std::expected<LevelTextureHandle, LevelTextureStoreError>(
                              std::unexpected(LevelTextureStoreError{}));
    Check(store && handle, "mutable-file store and handle open");
    if (!store || !handle)
        return;

    Check(tree.WriteGameFile("TEX.HOG", second_hog), "source container is replaced after Open");
    auto replaced = store->Load(service, *handle);
    Check(replaced && FirstStorageByte(replaced->storage) == 0x62,
          "Load resolves current replacement bytes without caching retail views");

    const auto renamed_hog = MakeHog({
        HogMember{.name = "Replacement.TDX", .payload = second_tdx},
    });
    Check(tree.WriteGameFile("TEX.HOG", renamed_hog),
          "source member is replaced after handle creation");
    auto replaced_member = store->Load(service, *handle);
    CheckError(replaced_member, LevelTextureStoreErrorCode::InvalidReference,
               "a replaced member invalidates its old locator without partial output");
    if (!replaced_member)
        Check(Sanitized(replaced_member.error(), tree.root()),
              "replaced-member diagnostics are sanitized");

    Check(tree.RemoveGameFile("TEX.HOG"), "source container is removed after handle creation");
    auto removed = store->Load(service, *handle);
    CheckError(removed, LevelTextureStoreErrorCode::ReadFailed,
               "a removed source container fails Load without cached bytes");
    if (!removed)
        Check(Sanitized(removed.error(), tree.root()), "removed-source diagnostics are sanitized");

    TempTree decode_tree("decode-error");
    const std::vector<std::byte> malformed_tdx(32, std::byte{0x55});
    const auto malformed_hog = MakeHog({
        HogMember{.name = "Broken.TDX", .payload = malformed_tdx},
    });
    Check(decode_tree.ready() && decode_tree.WriteGameFile("TEX.HOG", malformed_hog),
          "malformed semantic payload fixture is written");
    auto decode_service = OpenService(decode_tree);
    if (decode_service)
    {
        auto decode_store = OpenStore(*decode_service, manifest);
        Check(decode_store && decode_store->size() == 1,
              "Open remains passive over malformed terminal TDX bytes");
        if (decode_store)
        {
            auto decode_handle = decode_store->HandleAt(0);
            if (decode_handle)
            {
                auto decoded = decode_store->Load(*decode_service, *decode_handle);
                CheckError(decoded, LevelTextureStoreErrorCode::DecodeFailed,
                           "malformed TDX fails atomically during Load");
                if (!decoded)
                    Check(Sanitized(decoded.error(), decode_tree.root()),
                          "semantic decode diagnostics omit paths, names, offsets, and "
                          "payloads");
            }
        }
    }
}

void CheckConcurrentLoads()
{
    TempTree tree("concurrent-loads");
    const auto tdx = MakeDirect24Tdx(0x72);
    const auto hog = MakeHog({HogMember{.name = "Concurrent.TDX", .payload = tdx}});
    Check(tree.ready() && tree.WriteGameFile("TEX.HOG", hog), "concurrent-load fixture is written");
    auto service_open = OpenService(tree);
    Check(service_open.has_value(), "concurrent-load service opens");
    if (!service_open)
        return;
    GameDataService service = std::move(*service_open);
    auto store = OpenStore(service, MakeManifest({DirectSource("GAMEDATA/TEST/TEX.HOG")}));
    auto handle = store ? store->HandleAt(0)
                        : std::expected<LevelTextureHandle, LevelTextureStoreError>(
                              std::unexpected(LevelTextureStoreError{}));
    auto baseline = store && handle
                        ? store->Load(service, *handle)
                        : std::expected<omega::content::LoadedLevelTexture, LevelTextureStoreError>(
                              std::unexpected(LevelTextureStoreError{}));
    Check(store && handle && baseline, "concurrent-load baseline is valid");
    if (!store || !handle || !baseline)
        return;

    constexpr std::size_t thread_count = 8;
    constexpr std::size_t loads_per_thread = 16;
    std::atomic<std::uint32_t> thread_failures{0};
    std::vector<std::thread> threads;
    threads.reserve(thread_count);
    for (std::size_t thread_index = 0; thread_index < thread_count; ++thread_index)
    {
        threads.emplace_back(
            [&store, &service, &handle, &baseline, &thread_failures]()
            {
                for (std::size_t iteration = 0; iteration < loads_per_thread; ++iteration)
                {
                    auto loaded = store->Load(service, *handle);
                    if (!loaded || loaded->storage != baseline->storage ||
                        !SameUsage(loaded->usage, baseline->usage))
                        thread_failures.fetch_add(1, std::memory_order_relaxed);
                }
            });
    }
    for (auto& thread : threads)
        thread.join();
    Check(thread_failures.load(std::memory_order_relaxed) == 0,
          "reentrant concurrent loads return independent deterministic results");
}
} // namespace

int main()
{
    CheckInternalDefaultLimits();
    CheckCanonicalOrderingAndDeduplication();
    CheckEmptyInvalidAndFailureSurfaces();
    CheckOpenAndLoadBudgets();
    CheckHandleAndStorageOwnership();
    CheckServiceIdentityBeforeIo();
    CheckMutableFilesAndSanitizedDecodeErrors();
    CheckConcurrentLoads();

    Check(omega::content::LevelTextureStoreErrorCodeName(
              LevelTextureStoreErrorCode::InvalidConfiguration) == "invalid-configuration" &&
              omega::content::LevelTextureStoreErrorCodeName(
                  LevelTextureStoreErrorCode::LimitExceeded) == "limit-exceeded" &&
              omega::content::LevelTextureStoreErrorCodeName(
                  LevelTextureStoreErrorCode::InvalidReference) == "invalid-reference" &&
              omega::content::LevelTextureStoreErrorCodeName(
                  LevelTextureStoreErrorCode::DuplicateReference) == "duplicate-reference" &&
              omega::content::LevelTextureStoreErrorCodeName(
                  LevelTextureStoreErrorCode::ReadFailed) == "read-failed" &&
              omega::content::LevelTextureStoreErrorCodeName(
                  LevelTextureStoreErrorCode::MalformedArchive) == "malformed-archive" &&
              omega::content::LevelTextureStoreErrorCodeName(
                  LevelTextureStoreErrorCode::DecodeFailed) == "decode-failed" &&
              omega::content::LevelTextureStoreErrorCodeName(
                  LevelTextureStoreErrorCode::ForeignService) == "foreign-service" &&
              omega::content::LevelTextureStoreErrorCodeName(
                  LevelTextureStoreErrorCode::InvalidHandle) == "invalid-handle",
          "every public store error code has a stable diagnostic name");

    if (failures == 0)
        std::cout << "omega_level_texture_store_tests: all checks passed\n";
    return failures == 0 ? 0 : 1;
}
