#include "level_texture_commands.h"

#include "omega/content/game_data_service.h"
#include "omega/content/level_texture_store.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace
{
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

#ifdef _WIN32
bool TryCreateUnprivilegedDirectorySymlink(const std::filesystem::path& target,
    const std::filesystem::path& link, std::error_code& error) noexcept
{
    DWORD flags = SYMBOLIC_LINK_FLAG_DIRECTORY;
#ifdef SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE
    flags |= SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;
#endif
    if (CreateSymbolicLinkW(link.c_str(), target.c_str(), flags) != FALSE)
    {
        error.clear();
        return true;
    }
    error = std::error_code(static_cast<int>(GetLastError()), std::system_category());
    return false;
}

bool IsExplicitReparseCapabilitySkip(const std::error_code& error) noexcept
{
    switch (static_cast<DWORD>(error.value()))
    {
    case ERROR_ACCESS_DENIED:
    case ERROR_INVALID_FUNCTION:
    case ERROR_NOT_SUPPORTED:
    case ERROR_INVALID_PARAMETER:
    case ERROR_CALL_NOT_IMPLEMENTED:
    case ERROR_PRIVILEGE_NOT_HELD:
        return true;
    default:
        return false;
    }
}
#endif

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

std::vector<std::byte> TextBytes(const std::string_view value)
{
    std::vector<std::byte> bytes;
    AppendText(bytes, value);
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

std::vector<std::byte> MakeDirect24Tdx(const std::uint16_t width,
    const std::uint16_t height, const std::uint8_t seed)
{
    constexpr std::uint32_t descriptor_bytes = 128;
    constexpr std::uint32_t primary_base = 0x20;
    constexpr std::uint32_t primary_start = primary_base + descriptor_bytes;
    const std::uint32_t payload_bytes =
        static_cast<std::uint32_t>(width) * static_cast<std::uint32_t>(height) * 3U;
    const std::uint32_t stride = primary_start + payload_bytes;

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
    explicit TempTree(
        const std::string_view label, const std::string_view game_data_name = "GAMEDATA")
    {
        static std::atomic<std::uint64_t> next{0};
        const auto stamp = static_cast<std::uint64_t>(
            std::chrono::steady_clock::now().time_since_epoch().count());
        root_ = std::filesystem::temp_directory_path() /
                ("omega-level-texture-command-" + std::string(label) + "-" +
                    std::to_string(stamp) + "-" + std::to_string(next.fetch_add(1)));
        game_data_ = root_ / std::string(game_data_name);
        std::error_code error;
        std::filesystem::create_directories(game_data_, error);
        ready_ = !error &&
                 WriteText(root_ / "SYSTEM.CNF",
                     "BOOT2 = cdrom0:\\SCUS_972.64;1\r\nVER = 1.00\r\nVMODE = NTSC\r\n") &&
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
    [[nodiscard]] const std::filesystem::path& game_data() const noexcept
    {
        return game_data_;
    }

    [[nodiscard]] bool AddLevel(const std::string_view code,
        const std::span<const std::byte> texture_hog,
        const std::span<const std::byte> map_texture_hog,
        const std::string_view pop_name = "DATA.POP",
        const std::string_view data_hog_name = "DATA.HOG",
        const std::string_view texture_name = "TEX.HOG",
        const std::string_view map_texture_name = "MAPTEX.HOG") const
    {
        const std::filesystem::path level = game_data_ / std::string(code);
        std::error_code error;
        std::filesystem::create_directories(level, error);
        const auto data_hog = MakeHog(
            {HogMember{.name = "CELL.HOG", .payload = TextBytes("xyz")}});
        return !error && WriteBytes(level / std::string(pop_name), MakePop()) &&
               WriteBytes(level / std::string(data_hog_name), data_hog) &&
               WriteBytes(level / std::string(texture_name), texture_hog) &&
               WriteBytes(level / std::string(map_texture_name), map_texture_hog);
    }

private:
    std::filesystem::path root_;
    std::filesystem::path game_data_;
    bool ready_ = false;
};

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

ToolRun RunTool(const std::filesystem::path& root)
{
    StreamCapture output(std::cout);
    StreamCapture error(std::cerr);
    const int exit_code = omega::tool::LevelTextureStoreVerifyTree(root);
    return ToolRun{
        .exit_code = exit_code,
        .standard_output = output.Release(),
        .standard_error = error.Release(),
    };
}

ToolRun RunAssetTool(const std::filesystem::path& root)
{
    StreamCapture output(std::cout);
    StreamCapture error(std::cerr);
    const int exit_code = omega::tool::AssetServiceVerifyTree(root);
    return ToolRun{
        .exit_code = exit_code,
        .standard_output = output.Release(),
        .standard_error = error.Release(),
    };
}

enum class ErrorIndex : std::size_t
{
    DiscoveryInvalidRoot,
    DiscoveryUnsafeEntry,
    DiscoveryLimitExceeded,
    DiscoveryIo,
    DiscoveryMissingGameData,
    DiscoveryDuplicateGameData,
    DiscoveryInvalidLevelCode,
    DiscoveryDuplicateLevelCode,
    DiscoveryDuplicateLevelMarker,
    NoLevels,
    ServiceOpen,
    ManifestLoad,
    StoreOpen,
    EmptyTextureStore,
    HandleLookup,
    TextureLoad,
    AggregateOverflow,
    Count,
};

constexpr std::array<std::string_view, static_cast<std::size_t>(ErrorIndex::Count)>
    kErrorNames{
        "discovery_invalid_root", "discovery_unsafe_entry",
        "discovery_limit_exceeded", "discovery_io", "discovery_missing_game_data",
        "discovery_duplicate_game_data", "discovery_invalid_level_code",
        "discovery_duplicate_level_code", "discovery_duplicate_level_marker", "no_levels",
        "service_open", "manifest_load", "store_open", "empty_texture_store",
        "handle_lookup", "texture_load", "aggregate_overflow",
    };

struct UsageMaxima
{
    std::uint64_t input_bytes = 0;
    std::uint64_t items = 0;
    std::uint64_t logical_output_bytes = 0;
    std::uint32_t archive_depth = 0;
    std::uint64_t peak_scratch_bytes = 0;

    void Observe(const omega::content::LevelTextureOperationUsage& usage)
    {
        input_bytes = std::max(input_bytes, usage.input_bytes);
        items = std::max(items, usage.items);
        logical_output_bytes = std::max(logical_output_bytes, usage.logical_output_bytes);
        archive_depth = std::max(archive_depth, usage.archive_depth);
        peak_scratch_bytes = std::max(peak_scratch_bytes, usage.peak_scratch_bytes);
    }
};

struct ExpectedReport
{
    std::uint64_t levels_discovered = 0;
    std::uint64_t levels_verified = 0;
    std::uint64_t texture_sources = 0;
    std::uint64_t textures = 0;
    std::uint64_t storage_blocks = 0;
    std::uint64_t storage_planes = 0;
    std::uint64_t storage_palette_entries = 0;
    std::uint64_t storage_plane_bytes = 0;
    std::uint64_t storage_palette_bytes = 0;
    std::uint64_t storage_owned_bytes = 0;
    UsageMaxima open_maxima;
    UsageMaxima load_maxima;
    std::array<std::uint64_t, static_cast<std::size_t>(ErrorIndex::Count)> errors{};
};

std::string BuildReport(const ExpectedReport& report)
{
    std::uint64_t error_total = 0;
    for (const auto value : report.errors)
        error_total += value;
    std::ostringstream output;
    output
        << "{\"schema_version\":1,"
           "\"scope\":\"native aggregate level texture store verification; independent "
           "field maxima only; no paths, names, hashes, offsets, payloads, per-level rows, "
           "identities, or bindings\","
           "\"totals\":{\"levels_discovered\":"
        << report.levels_discovered << ",\"levels_verified\":" << report.levels_verified
        << ",\"texture_sources\":" << report.texture_sources << ",\"textures\":"
        << report.textures << ",\"storage_blocks\":" << report.storage_blocks
        << ",\"storage_planes\":" << report.storage_planes
        << ",\"storage_palette_entries\":" << report.storage_palette_entries
        << ",\"storage_plane_bytes\":" << report.storage_plane_bytes
        << ",\"storage_palette_bytes\":" << report.storage_palette_bytes
        << ",\"storage_owned_bytes\":" << report.storage_owned_bytes
        << ",\"errors\":" << error_total << "},\"maxima\":{\"open\":{\"input_bytes\":"
        << report.open_maxima.input_bytes << ",\"items\":" << report.open_maxima.items
        << ",\"logical_output_bytes\":" << report.open_maxima.logical_output_bytes
        << ",\"archive_depth\":" << report.open_maxima.archive_depth
        << ",\"peak_scratch_bytes\":" << report.open_maxima.peak_scratch_bytes
        << "},\"load\":{\"input_bytes\":" << report.load_maxima.input_bytes
        << ",\"items\":" << report.load_maxima.items
        << ",\"logical_output_bytes\":" << report.load_maxima.logical_output_bytes
        << ",\"archive_depth\":" << report.load_maxima.archive_depth
        << ",\"peak_scratch_bytes\":" << report.load_maxima.peak_scratch_bytes
        << "}},\"error_categories\":{";
    for (std::size_t index = 0; index < kErrorNames.size(); ++index)
    {
        if (index != 0)
            output << ',';
        output << '\"' << kErrorNames[index] << "\":" << report.errors[index];
    }
    output << "}}\n";
    return output.str();
}

struct MeasuredExpected
{
    ExpectedReport report;
    std::vector<omega::content::LevelTextureOperationUsage> open_operations;
    std::vector<omega::content::LevelTextureOperationUsage> load_operations;
};

std::optional<MeasuredExpected> MeasureExpected(
    const std::filesystem::path& root, const std::span<const std::string_view> codes)
{
    auto service = omega::content::GameDataService::Open({.root = root});
    Check(service.has_value(), "expected-report service opens");
    if (!service)
        return std::nullopt;

    MeasuredExpected measured;
    measured.report.levels_discovered = codes.size();
    for (const std::string_view code : codes)
    {
        auto manifest = service->LoadLevelManifest(code);
        Check(manifest.has_value(), "expected-report manifest loads");
        if (!manifest)
            return std::nullopt;
        auto store = omega::content::LevelTextureStore::Open(*service, *manifest);
        Check(store.has_value() && store->size() != 0, "expected-report store opens nonempty");
        if (!store || store->size() == 0)
            return std::nullopt;

        ++measured.report.levels_verified;
        measured.report.texture_sources += manifest->texture_sources.size();
        measured.report.textures += store->size();
        measured.report.open_maxima.Observe(store->open_usage());
        measured.open_operations.push_back(store->open_usage());
        for (std::size_t index = 0; index < store->size(); ++index)
        {
            auto handle = store->HandleAt(index);
            Check(handle.has_value(), "expected-report handle is available");
            if (!handle)
                return std::nullopt;
            auto loaded = store->Load(*service, *handle);
            Check(loaded.has_value(), "expected-report texture loads");
            if (!loaded)
                return std::nullopt;
            measured.report.load_maxima.Observe(loaded->usage);
            measured.load_operations.push_back(loaded->usage);
            measured.report.storage_blocks += loaded->storage.blocks.size();
            for (const auto& block : loaded->storage.blocks)
            {
                measured.report.storage_planes += block.planes.size();
                for (const auto& plane : block.planes)
                {
                    measured.report.storage_plane_bytes += plane.bytes.size();
                    measured.report.storage_owned_bytes += plane.bytes.size();
                }
                if (block.palette)
                {
                    measured.report.storage_palette_entries += block.palette->entries.size();
                    const std::uint64_t bytes = block.palette->entries.size() *
                                                sizeof(block.palette->entries.front());
                    measured.report.storage_palette_bytes += bytes;
                    measured.report.storage_owned_bytes += bytes;
                }
            }
        }
    }
    return measured;
}

template <typename Projection>
std::size_t MaximumOperationIndex(
    const std::vector<omega::content::LevelTextureOperationUsage>& operations,
    Projection projection)
{
    return static_cast<std::size_t>(std::distance(operations.begin(),
        std::max_element(operations.begin(), operations.end(),
            [&projection](const auto& left, const auto& right) {
                return projection(left) < projection(right);
            })));
}

void CheckSuccessfulAggregateAndIndependentMaxima()
{
    TempTree tree("success-private", "gAmEdAtA");
    const auto small_a = MakeDirect24Tdx(16, 16, 0x11);
    const auto large = MakeDirect24Tdx(32, 32, 0x22);
    const auto small_b = MakeDirect24Tdx(16, 16, 0x33);
    const auto small_c = MakeDirect24Tdx(16, 16, 0x44);

    std::vector<HogMember> alpha_primary{
        HogMember{.name = "ALPHA.TDX", .payload = small_a},
        HogMember{.name = "PADDING.BIN", .payload = std::vector<std::byte>(65536)},
    };
    const auto alpha_map = MakeHog(
        {HogMember{.name = "LARGE.TDX", .payload = large}});

    std::vector<HogMember> beta_primary;
    beta_primary.push_back(HogMember{.name = "BETA.TDX", .payload = small_b});
    for (std::size_t index = 0; index < 80; ++index)
    {
        beta_primary.push_back(HogMember{
            .name = "DECOY" + std::to_string(index) + ".BIN",
            .payload = {static_cast<std::byte>(index & 0xFFU)},
        });
    }
    const auto beta_map = MakeHog(
        {HogMember{.name = "SMALL.TDX", .payload = small_c}});

    Check(tree.ready() &&
              tree.AddLevel("alpha", MakeHog(alpha_primary), alpha_map, "data.pop",
                  "Data.Hog", "tex.hog", "mapTex.hog") &&
              tree.AddLevel("BETA", MakeHog(beta_primary), beta_map),
        "mixed-case synthetic owner tree is written");
    if (!tree.ready())
        return;

    constexpr std::array<std::string_view, 2> codes{"ALPHA", "BETA"};
    auto expected = MeasureExpected(tree.root(), codes);
    if (!expected)
        return;
    const ToolRun run = RunTool(tree.root());
    Check(run.exit_code == 0, "complete texture tree succeeds");
    Check(run.standard_output == BuildReport(expected->report),
        "success output has exact fixed schema and independently measured values");
    Check(run.standard_error.empty(), "complete texture tree has no diagnostics");

    const auto open_input = MaximumOperationIndex(expected->open_operations,
        [](const auto& usage) { return usage.input_bytes; });
    const auto open_items = MaximumOperationIndex(expected->open_operations,
        [](const auto& usage) { return usage.items; });
    Check(open_input != open_items,
        "Open input and item maxima are witnessed by different levels");

    const auto load_input = MaximumOperationIndex(expected->load_operations,
        [](const auto& usage) { return usage.input_bytes; });
    const auto load_items = MaximumOperationIndex(expected->load_operations,
        [](const auto& usage) { return usage.items; });
    const auto load_output = MaximumOperationIndex(expected->load_operations,
        [](const auto& usage) { return usage.logical_output_bytes; });
    Check(load_input != load_items && load_input != load_output && load_items != load_output,
        "Load input, item, and output maxima are witnessed by different operations");
}

void CheckEmptyInputs()
{
    TempTree no_levels("empty-level-set");
    Check(no_levels.ready(), "empty synthetic owner root is written");
    ExpectedReport no_level_report;
    no_level_report.errors[static_cast<std::size_t>(ErrorIndex::NoLevels)] = 1;
    const ToolRun no_level_run = RunTool(no_levels.root());
    Check(no_level_run.exit_code != 0 &&
              no_level_run.standard_output == BuildReport(no_level_report) &&
              no_level_run.standard_error == "level-texture-store: no_levels\n",
        "empty level set fails with one fixed typed error");

    TempTree no_textures("empty-texture-store");
    const auto empty_hog = MakeHog({});
    Check(no_textures.ready() && no_textures.AddLevel("EMPTY1", empty_hog, empty_hog),
        "empty texture-container level is written");
    ExpectedReport no_texture_report;
    no_texture_report.levels_discovered = 1;
    no_texture_report.errors[
        static_cast<std::size_t>(ErrorIndex::EmptyTextureStore)] = 1;
    const ToolRun no_texture_run = RunTool(no_textures.root());
    Check(no_texture_run.exit_code != 0 &&
              no_texture_run.standard_output == BuildReport(no_texture_report) &&
              no_texture_run.standard_error ==
                  "level-texture-store: empty_texture_store\n",
        "empty texture store is rejected without committing partial level totals");
}

void CheckMalformedAndPrivateDiagnostics()
{
    constexpr std::string_view level_marker = "SECRET42";
    constexpr std::string_view member_marker = "PRIVATE_MEMBER.TDX";
    constexpr std::string_view payload_marker = "PRIVATE_PAYLOAD_MARKER";
    TempTree tree("PRIVATE_ROOT_MARKER");
    const auto malformed_texture = MakeHog({HogMember{
        .name = std::string(member_marker),
        .payload = TextBytes(payload_marker),
    }});
    Check(tree.ready() &&
              tree.AddLevel(level_marker, malformed_texture, MakeHog({})),
        "malformed private-marker texture level is written");

    ExpectedReport expected;
    expected.levels_discovered = 1;
    expected.errors[static_cast<std::size_t>(ErrorIndex::TextureLoad)] = 1;
    const ToolRun run = RunTool(tree.root());
    Check(run.exit_code != 0 && run.standard_output == BuildReport(expected) &&
              run.standard_error == "level-texture-store: texture_load\n",
        "malformed texture fails atomically with a typed load category");
    const std::array<std::string, 5> forbidden{
        tree.root().string(), tree.root().generic_string(), std::string(level_marker),
        std::string(member_marker), std::string(payload_marker),
    };
    const std::string comparable_output = PrivacyComparable(run.standard_output);
    const std::string comparable_error = PrivacyComparable(run.standard_error);
    for (const auto& marker : forbidden)
    {
        const std::string comparable_marker = PrivacyComparable(marker);
        Check(comparable_output.find(comparable_marker) == std::string::npos &&
                  comparable_error.find(comparable_marker) == std::string::npos,
            "aggregate diagnostics do not disclose private identity markers");
    }

    TempTree malformed_archive("malformed-archive");
    const auto invalid_hog = std::vector<std::byte>(64, std::byte{0});
    Check(malformed_archive.ready() &&
              malformed_archive.AddLevel("BROKEN1", invalid_hog, MakeHog({})),
        "malformed texture archive level is written");
    ExpectedReport archive_expected;
    archive_expected.levels_discovered = 1;
    archive_expected.errors[static_cast<std::size_t>(ErrorIndex::StoreOpen)] = 1;
    const ToolRun archive_run = RunTool(malformed_archive.root());
    Check(archive_run.exit_code != 0 &&
              archive_run.standard_output == BuildReport(archive_expected) &&
              archive_run.standard_error == "level-texture-store: store_open\n",
        "malformed texture archive fails at the fixed store-open stage");
}

enum class UnsafeLinkCoverage
{
    Exercised,
    ExplicitlySkipped,
    FixtureFailure,
};

UnsafeLinkCoverage CheckUnsafeLinksAreRejected()
{
    TempTree tree("unsafe-link");
    const std::filesystem::path link = tree.root() / "linked-data";
    std::error_code error;
    std::filesystem::create_directory_symlink(tree.game_data(), link, error);
#ifdef _WIN32
    if (error)
    {
        std::error_code cleanup_error;
        std::filesystem::remove(link, cleanup_error);
        if (!TryCreateUnprivilegedDirectorySymlink(tree.game_data(), link, error))
        {
            const bool explicit_skip = IsExplicitReparseCapabilitySkip(error);
            Check(explicit_skip,
                "unsafe-link fixture fails only for a recognized Windows reparse capability");
            if (explicit_skip)
            {
                std::cout << "SKIP: Windows unprivileged reparse fixture is unavailable\n";
                return UnsafeLinkCoverage::ExplicitlySkipped;
            }
            return UnsafeLinkCoverage::FixtureFailure;
        }
    }
#else
    if (error)
    {
        Check(false, "unsafe-link fixture is created on this platform");
        return UnsafeLinkCoverage::FixtureFailure;
    }
#endif

    ExpectedReport expected;
    expected.errors[
        static_cast<std::size_t>(ErrorIndex::DiscoveryUnsafeEntry)] = 1;
    const ToolRun run = RunTool(tree.root());
    Check(run.exit_code != 0 && run.standard_output == BuildReport(expected) &&
              run.standard_error ==
                  "level-texture-store: discovery_unsafe_entry\n",
        "discovery rejects links without following them");
    return UnsafeLinkCoverage::Exercised;
}

enum class AssetErrorIndex : std::size_t
{
    DiscoveryInvalidRoot,
    DiscoveryUnsafeEntry,
    DiscoveryLimitExceeded,
    DiscoveryIo,
    DiscoveryMissingGameData,
    DiscoveryDuplicateGameData,
    DiscoveryInvalidLevelCode,
    DiscoveryDuplicateLevelCode,
    DiscoveryDuplicateLevelMarker,
    NoLevels,
    ServiceOpen,
    JobServiceCreate,
    ManifestLoad,
    StoreOpen,
    EmptyTextureStore,
    HandleLookup,
    AssetServiceCreate,
    GateSubmission,
    AssetRequest,
    AssetTerminalState,
    AssetGet,
    AssetRelease,
    StaleHandleCheck,
    SnapshotInvariant,
    AggregateOverflow,
    UnexpectedFailure,
    Count,
};

constexpr std::array<std::string_view, static_cast<std::size_t>(AssetErrorIndex::Count)>
    kAssetErrorNames{
        "discovery_invalid_root",
        "discovery_unsafe_entry",
        "discovery_limit_exceeded",
        "discovery_io",
        "discovery_missing_game_data",
        "discovery_duplicate_game_data",
        "discovery_invalid_level_code",
        "discovery_duplicate_level_code",
        "discovery_duplicate_level_marker",
        "no_levels",
        "service_open",
        "job_service_create",
        "manifest_load",
        "store_open",
        "empty_texture_store",
        "handle_lookup",
        "asset_service_create",
        "gate_submission",
        "asset_request",
        "asset_terminal_state",
        "asset_get",
        "asset_release",
        "stale_handle_check",
        "snapshot_invariant",
        "aggregate_overflow",
        "unexpected_failure",
    };

struct ExpectedAssetReport
{
    std::uint64_t levels_discovered = 0;
    std::uint64_t levels_verified = 0;
    std::uint64_t texture_sources = 0;
    std::uint64_t texture_occurrences = 0;
    std::uint64_t requests = 0;
    std::uint64_t ready = 0;
    std::uint64_t gets = 0;
    std::uint64_t releases = 0;
    std::uint64_t stale_handle_rejections = 0;
    std::uint64_t zero_residual_releases = 0;
    std::uint64_t storage_blocks = 0;
    std::uint64_t storage_planes = 0;
    std::uint64_t storage_palette_entries = 0;
    std::uint64_t storage_plane_bytes = 0;
    std::uint64_t storage_palette_bytes = 0;
    std::uint64_t storage_owned_bytes = 0;
    std::uint64_t maximum_active_slots = 0;
    std::uint64_t maximum_in_flight_requests = 0;
    std::uint64_t maximum_resident_logical_bytes = 0;
    std::array<std::uint64_t, static_cast<std::size_t>(AssetErrorIndex::Count)> errors{};
};

std::string BuildAssetReport(const ExpectedAssetReport& report)
{
    std::uint64_t error_total = 0;
    for (const std::uint64_t value : report.errors)
        error_total += value;

    std::ostringstream output;
    output
        << "{\"schema_version\":1,"
           "\"scope\":\"native aggregate asset service verification; fixed capacity-one "
           "sequential lifecycle; no paths, names, hashes, offsets, payloads, per-level rows, "
           "identities, bindings, messages, or exception text\","
           "\"limits\":{\"worker_count\":1,\"max_pending_jobs\":1,\"slot_capacity\":1,"
           "\"maximum_in_flight_requests\":1,"
           "\"maximum_resident_logical_bytes\":524288},"
           "\"totals\":{\"levels_discovered\":"
        << report.levels_discovered << ",\"levels_verified\":" << report.levels_verified
        << ",\"texture_sources\":" << report.texture_sources
        << ",\"texture_occurrences\":" << report.texture_occurrences
        << ",\"requests\":" << report.requests << ",\"ready\":" << report.ready
        << ",\"gets\":" << report.gets << ",\"releases\":" << report.releases
        << ",\"stale_handle_rejections\":" << report.stale_handle_rejections
        << ",\"zero_residual_releases\":" << report.zero_residual_releases
        << ",\"storage_blocks\":" << report.storage_blocks
        << ",\"storage_planes\":" << report.storage_planes
        << ",\"storage_palette_entries\":" << report.storage_palette_entries
        << ",\"storage_plane_bytes\":" << report.storage_plane_bytes
        << ",\"storage_palette_bytes\":" << report.storage_palette_bytes
        << ",\"storage_owned_bytes\":" << report.storage_owned_bytes
        << ",\"errors\":" << error_total << "},\"maxima\":{\"active_slots\":"
        << report.maximum_active_slots << ",\"in_flight_requests\":"
        << report.maximum_in_flight_requests << ",\"resident_logical_bytes\":"
        << report.maximum_resident_logical_bytes << "},\"error_categories\":{";
    for (std::size_t index = 0; index < kAssetErrorNames.size(); ++index)
    {
        if (index != 0U)
            output << ',';
        output << '\"' << kAssetErrorNames[index] << "\":" << report.errors[index];
    }
    output << "}}\n";
    return output.str();
}

ExpectedAssetReport SuccessfulAssetReport(const MeasuredExpected& measured)
{
    ExpectedAssetReport report;
    report.levels_discovered = measured.report.levels_discovered;
    report.levels_verified = measured.report.levels_verified;
    report.texture_sources = measured.report.texture_sources;
    report.texture_occurrences = measured.report.textures;
    report.requests = measured.report.textures;
    report.ready = measured.report.textures;
    report.gets = measured.report.textures;
    report.releases = measured.report.textures;
    report.stale_handle_rejections = measured.report.textures;
    report.zero_residual_releases = measured.report.textures;
    report.storage_blocks = measured.report.storage_blocks;
    report.storage_planes = measured.report.storage_planes;
    report.storage_palette_entries = measured.report.storage_palette_entries;
    report.storage_plane_bytes = measured.report.storage_plane_bytes;
    report.storage_palette_bytes = measured.report.storage_palette_bytes;
    report.storage_owned_bytes = measured.report.storage_owned_bytes;
    if (report.texture_occurrences != 0U)
    {
        report.maximum_active_slots = 1U;
        report.maximum_in_flight_requests = 1U;
        report.maximum_resident_logical_bytes =
            measured.report.load_maxima.logical_output_bytes;
    }
    return report;
}

void CheckAssetSuccessfulAggregateAndLegacyRegression()
{
    TempTree tree("asset-success-private", "gAmEdAtA");
    const auto small_a = MakeDirect24Tdx(16, 16, 0x15);
    const auto large = MakeDirect24Tdx(32, 32, 0x35);
    const auto small_b = MakeDirect24Tdx(16, 16, 0x55);
    const auto small_c = MakeDirect24Tdx(16, 16, 0x75);
    const auto alpha_primary = MakeHog(
        {HogMember{.name = "ALPHA.TDX", .payload = small_a}});
    const auto alpha_map = MakeHog(
        {HogMember{.name = "LARGE.TDX", .payload = large}});
    const auto beta_primary = MakeHog(
        {HogMember{.name = "BETA.TDX", .payload = small_b}});
    const auto beta_map = MakeHog(
        {HogMember{.name = "SMALL.TDX", .payload = small_c}});

    Check(tree.ready() &&
              tree.AddLevel("alpha", alpha_primary, alpha_map, "data.pop", "Data.Hog",
                  "tex.hog", "mapTex.hog") &&
              tree.AddLevel("BETA", beta_primary, beta_map),
        "multi-level multi-texture AssetService tree is written");
    if (!tree.ready())
        return;

    constexpr std::array<std::string_view, 2> codes{"ALPHA", "BETA"};
    auto measured = MeasureExpected(tree.root(), codes);
    if (!measured)
        return;
    const ExpectedAssetReport expected = SuccessfulAssetReport(*measured);
    Check(expected.texture_occurrences == 4U && expected.requests == 4U &&
              expected.requests == expected.ready && expected.ready == expected.gets &&
              expected.gets == expected.releases &&
              expected.releases == expected.stale_handle_rejections &&
              expected.stale_handle_rejections == expected.zero_residual_releases,
        "capacity-one lifecycle counters cover every texture occurrence exactly once");

    const ToolRun legacy_before = RunTool(tree.root());
    const ToolRun asset = RunAssetTool(tree.root());
    const ToolRun legacy_after = RunTool(tree.root());
    Check(asset.exit_code == 0 && asset.standard_output == BuildAssetReport(expected) &&
              asset.standard_error.empty(),
        "AssetService verifier emits the exact fixed limits, lifecycle, storage, maxima, and "
        "zero-error report");
    Check(legacy_before.exit_code == 0 && legacy_after.exit_code == 0 &&
              legacy_before.standard_output == BuildReport(measured->report) &&
              legacy_after.standard_output == legacy_before.standard_output &&
              legacy_before.standard_error.empty() && legacy_after.standard_error.empty(),
        "AssetService verification leaves the legacy level-texture-store schema and output "
        "byte-unchanged");
}

void CheckAssetEmptyInputs()
{
    TempTree no_levels("asset-empty-level-set");
    Check(no_levels.ready(), "empty AssetService owner root is written");
    ExpectedAssetReport no_level_report;
    no_level_report.errors[static_cast<std::size_t>(AssetErrorIndex::NoLevels)] = 1U;
    const ToolRun no_level_run = RunAssetTool(no_levels.root());
    Check(no_level_run.exit_code != 0 &&
              no_level_run.standard_output == BuildAssetReport(no_level_report) &&
              no_level_run.standard_error == "asset-service: no_levels\n",
        "AssetService verifier rejects an empty level set with one fixed typed error");

    TempTree no_textures("asset-empty-texture-store");
    const auto empty_hog = MakeHog({});
    Check(no_textures.ready() && no_textures.AddLevel("EMPTY1", empty_hog, empty_hog),
        "empty AssetService texture-container level is written");
    ExpectedAssetReport no_texture_report;
    no_texture_report.levels_discovered = 1U;
    no_texture_report.errors[
        static_cast<std::size_t>(AssetErrorIndex::EmptyTextureStore)] = 1U;
    const ToolRun no_texture_run = RunAssetTool(no_textures.root());
    Check(no_texture_run.exit_code != 0 &&
              no_texture_run.standard_output == BuildAssetReport(no_texture_report) &&
              no_texture_run.standard_error == "asset-service: empty_texture_store\n",
        "AssetService verifier rejects an empty store without partial lifecycle totals");
}

void CheckAssetMalformedAndPrivateDiagnostics()
{
    constexpr std::string_view level_marker = "ASSETSECRET42";
    constexpr std::string_view member_marker = "PRIVATE_ASSET_MEMBER.TDX";
    constexpr std::string_view payload_marker = "PRIVATE_ASSET_PAYLOAD_MARKER";
    TempTree tree("PRIVATE_ASSET_ROOT_MARKER");
    const auto malformed_texture = MakeHog({HogMember{
        .name = std::string(member_marker),
        .payload = TextBytes(payload_marker),
    }});
    Check(tree.ready() && tree.AddLevel(level_marker, malformed_texture, MakeHog({})),
        "malformed private-marker AssetService level is written");

    ExpectedAssetReport expected;
    expected.levels_discovered = 1U;
    expected.errors[
        static_cast<std::size_t>(AssetErrorIndex::AssetTerminalState)] = 1U;
    const ToolRun run = RunAssetTool(tree.root());
    Check(run.exit_code != 0 && run.standard_output == BuildAssetReport(expected) &&
              run.standard_error == "asset-service: asset_terminal_state\n",
        "malformed asset fails atomically at the fixed terminal-state category");
    const std::array<std::string, 5> forbidden{
        tree.root().string(), tree.root().generic_string(), std::string(level_marker),
        std::string(member_marker), std::string(payload_marker),
    };
    const std::string comparable_output = PrivacyComparable(run.standard_output);
    const std::string comparable_error = PrivacyComparable(run.standard_error);
    for (const auto& marker : forbidden)
    {
        const std::string comparable_marker = PrivacyComparable(marker);
        Check(comparable_output.find(comparable_marker) == std::string::npos &&
                  comparable_error.find(comparable_marker) == std::string::npos,
            "AssetService aggregate diagnostics disclose no private identity markers");
    }

    TempTree malformed_archive("asset-malformed-archive");
    const auto invalid_hog = std::vector<std::byte>(64U, std::byte{0});
    Check(malformed_archive.ready() &&
              malformed_archive.AddLevel("BROKEN1", invalid_hog, MakeHog({})),
        "malformed AssetService texture archive is written");
    ExpectedAssetReport archive_expected;
    archive_expected.levels_discovered = 1U;
    archive_expected.errors[static_cast<std::size_t>(AssetErrorIndex::StoreOpen)] = 1U;
    const ToolRun archive_run = RunAssetTool(malformed_archive.root());
    Check(archive_run.exit_code != 0 &&
              archive_run.standard_output == BuildAssetReport(archive_expected) &&
              archive_run.standard_error == "asset-service: store_open\n",
        "malformed AssetService texture archive fails at the store-open stage");
}

UnsafeLinkCoverage CheckAssetUnsafeLinksAreRejected()
{
    TempTree tree("asset-unsafe-link");
    const std::filesystem::path link = tree.root() / "linked-asset-data";
    std::error_code error;
    std::filesystem::create_directory_symlink(tree.game_data(), link, error);
#ifdef _WIN32
    if (error)
    {
        std::error_code cleanup_error;
        std::filesystem::remove(link, cleanup_error);
        if (!TryCreateUnprivilegedDirectorySymlink(tree.game_data(), link, error))
        {
            const bool explicit_skip = IsExplicitReparseCapabilitySkip(error);
            Check(explicit_skip,
                "AssetService unsafe-link fixture fails only for a recognized Windows reparse "
                "capability");
            return explicit_skip ? UnsafeLinkCoverage::ExplicitlySkipped
                                 : UnsafeLinkCoverage::FixtureFailure;
        }
    }
#else
    if (error)
    {
        Check(false, "AssetService unsafe-link fixture is created on this platform");
        return UnsafeLinkCoverage::FixtureFailure;
    }
#endif

    ExpectedAssetReport expected;
    expected.errors[
        static_cast<std::size_t>(AssetErrorIndex::DiscoveryUnsafeEntry)] = 1U;
    const ToolRun run = RunAssetTool(tree.root());
    Check(run.exit_code != 0 && run.standard_output == BuildAssetReport(expected) &&
              run.standard_error == "asset-service: discovery_unsafe_entry\n",
        "AssetService discovery rejects unsafe entries without following them");
    return UnsafeLinkCoverage::Exercised;
}
} // namespace

int main()
{
    CheckSuccessfulAggregateAndIndependentMaxima();
    CheckEmptyInputs();
    CheckMalformedAndPrivateDiagnostics();
    const UnsafeLinkCoverage unsafe_link_coverage = CheckUnsafeLinksAreRejected();
#ifdef _WIN32
    Check(unsafe_link_coverage == UnsafeLinkCoverage::Exercised ||
              unsafe_link_coverage == UnsafeLinkCoverage::ExplicitlySkipped,
        "Windows unsafe-link coverage is exercised or emits an explicit capability skip");
#else
    Check(unsafe_link_coverage == UnsafeLinkCoverage::Exercised,
        "unsafe-link coverage is exercised on platforms with unprivileged symlinks");
#endif

    CheckAssetSuccessfulAggregateAndLegacyRegression();
    CheckAssetEmptyInputs();
    CheckAssetMalformedAndPrivateDiagnostics();
    const UnsafeLinkCoverage asset_unsafe_link_coverage = CheckAssetUnsafeLinksAreRejected();
#ifdef _WIN32
    Check(asset_unsafe_link_coverage == UnsafeLinkCoverage::Exercised ||
              asset_unsafe_link_coverage == UnsafeLinkCoverage::ExplicitlySkipped,
        "Windows AssetService unsafe-link coverage is exercised or explicitly skipped");
#else
    Check(asset_unsafe_link_coverage == UnsafeLinkCoverage::Exercised,
        "AssetService unsafe-link coverage is exercised on platforms with unprivileged "
        "symlinks");
#endif

    if (failures == 0)
    {
        std::cout << "level texture command tests passed\n";
        return 0;
    }
    std::cerr << failures << " level texture command test(s) failed\n";
    return 1;
}
