#include "omega/asset/level_ir.h"
#include "omega/content/game_data_service.h"
#include "omega/content/level_texture_store.h"
#include "omega/runtime/asset_service.h"
#include "omega/runtime/job_service.h"
#include "omega/runtime/level_texture_topology_preview.h"
#include "omega/runtime/texture_storage_topology_debug_image.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <latch>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace
{
using omega::asset::LevelManifestIR;
using omega::asset::SourceLocator;
using omega::content::GameDataService;
using omega::content::LevelTextureHandle;
using omega::content::LevelTextureStore;
using omega::content::LevelTextureStoreError;
using omega::content::LevelTextureStoreErrorCode;
using omega::runtime::AssetService;
using omega::runtime::AssetServiceConfig;
using omega::runtime::AssetServiceError;
using omega::runtime::AssetServiceErrorCode;
using omega::runtime::AssetServiceSnapshot;
using omega::runtime::JobService;
using omega::runtime::JobServiceConfig;
using omega::runtime::LevelTextureTopologyPreviewError;
using omega::runtime::LevelTextureTopologyPreviewErrorCode;
using omega::runtime::Packed24TransferDebugImageErrorCode;
using omega::runtime::TextureAssetHandle;
using omega::runtime::TextureAssetState;
using omega::runtime::TextureStorageTopologyDebugImageErrorCode;

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
bool CheckError(const std::expected<Value, AssetServiceError>& result,
    const AssetServiceErrorCode code, const std::string_view message)
{
    const bool matches = !result && result.error().code == code;
    Check(matches, message);
    return matches;
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

bool ContainsForbidden(const std::string_view message, const std::filesystem::path& root)
{
    const std::array<std::string, 10> forbidden{
        root.string(), root.generic_string(), "GAMEDATA", "TEX.HOG", "A_READY.TDX",
        "A_BROKEN.TDX", "B_READY.TDX", "Z_BROKEN.TDX", "SCUS_972.64",
        "omega-asset-service-"};
    const std::string comparable_message = PrivacyComparable(message);
    return std::ranges::any_of(
        forbidden, [&comparable_message](const std::string& value)
        {
            const std::string comparable_value = PrivacyComparable(value);
            return !comparable_value.empty() &&
                   comparable_message.find(comparable_value) != std::string::npos;
        });
}

bool Sanitized(const AssetServiceError& error, const std::filesystem::path& root)
{
    if (ContainsForbidden(error.message, root))
        return false;
    if (!error.level_texture_error)
        return true;
    if (ContainsForbidden(error.level_texture_error->message, root))
        return false;
    return !error.level_texture_error->decode_error ||
           (!error.level_texture_error->decode_error->byte_offset &&
               !ContainsForbidden(error.level_texture_error->decode_error->message, root));
}

void WriteU16(std::vector<std::byte>& bytes, const std::size_t offset,
    const std::uint16_t value)
{
    bytes[offset] = static_cast<std::byte>(value & 0xFFU);
    bytes[offset + 1U] = static_cast<std::byte>((value >> 8U) & 0xFFU);
}

void WriteU32(std::vector<std::byte>& bytes, const std::size_t offset,
    const std::uint32_t value)
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
    std::size_t payload_bytes = 0U;
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
    std::uint32_t payload_cursor = 0U;
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
        const auto stamp = static_cast<std::uint64_t>(
            std::chrono::steady_clock::now().time_since_epoch().count());
        root_ = std::filesystem::temp_directory_path() /
                ("omega-asset-service-" + std::string(label) + "-" +
                    std::to_string(stamp) + "-" + std::to_string(next.fetch_add(1U)));
        std::error_code error;
        std::filesystem::create_directories(root_ / "GAMEDATA" / "TEST", error);
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

    [[nodiscard]] bool WriteGameFile(const std::string_view name,
        const std::span<const std::byte> bytes) const
    {
        return WriteBytes(root_ / "GAMEDATA" / "TEST" / std::string(name), bytes);
    }

private:
    std::filesystem::path root_;
    bool ready_ = false;
};

SourceLocator DirectSource(const std::string_view game_path)
{
    return SourceLocator{.game_path = std::string(game_path), .hog_entries = {}};
}

LevelManifestIR MakeManifest()
{
    LevelManifestIR manifest;
    manifest.data_hog_source = DirectSource("GAMEDATA/TEST/UNUSED.HOG");
    manifest.texture_sources = {DirectSource("GAMEDATA/TEST/TEX.HOG")};
    return manifest;
}

enum class ContentFixtureKind
{
    Standard,
    EmptyInventory,
    MalformedFirst,
};

class ContentFixture final
{
public:
    explicit ContentFixture(const std::string_view label,
        const ContentFixtureKind kind = ContentFixtureKind::Standard)
        : tree_(label), manifest_(MakeManifest())
    {
        std::vector<HogMember> members;
        std::size_t expected_store_size = 0U;
        switch (kind)
        {
        case ContentFixtureKind::Standard:
            members = {
                HogMember{.name = "A_READY.TDX", .payload = MakeDirect24Tdx(0x21)},
                HogMember{.name = "B_READY.TDX", .payload = MakeDirect24Tdx(0x61)},
                HogMember{.name = "Z_BROKEN.TDX",
                    .payload = std::vector<std::byte>(32U, std::byte{0x55})},
            };
            expected_store_size = 3U;
            break;
        case ContentFixtureKind::EmptyInventory:
            members = {
                HogMember{.name = "README.BIN",
                    .payload = std::vector<std::byte>{std::byte{0x01}}},
            };
            break;
        case ContentFixtureKind::MalformedFirst:
            members = {
                HogMember{.name = "A_BROKEN.TDX",
                    .payload = std::vector<std::byte>(32U, std::byte{0x55})},
                HogMember{.name = "B_READY.TDX", .payload = MakeDirect24Tdx(0x21)},
            };
            expected_store_size = 2U;
            break;
        }

        const auto hog = MakeHog(members);
        if (!tree_.ready() || !tree_.WriteGameFile("TEX.HOG", hog))
            return;

        auto opened = GameDataService::Open({.root = tree_.root()});
        if (!opened)
            return;
        game_data_.emplace(std::move(*opened));
        auto store = LevelTextureStore::Open(*game_data_, manifest_);
        if (!store || store->size() != expected_store_size)
            return;
        texture_store_.emplace(std::move(*store));
        ready_ = true;
    }

    ContentFixture(const ContentFixture&) = delete;
    ContentFixture& operator=(const ContentFixture&) = delete;

    [[nodiscard]] bool ready() const noexcept { return ready_; }
    [[nodiscard]] const std::filesystem::path& root() const noexcept { return tree_.root(); }
    [[nodiscard]] const LevelManifestIR& manifest() const noexcept { return manifest_; }
    [[nodiscard]] GameDataService& game_data() noexcept { return *game_data_; }
    [[nodiscard]] const GameDataService& game_data() const noexcept { return *game_data_; }
    [[nodiscard]] LevelTextureStore& texture_store() noexcept { return *texture_store_; }
    [[nodiscard]] const LevelTextureStore& texture_store() const noexcept
    {
        return *texture_store_;
    }

    [[nodiscard]] std::expected<LevelTextureHandle, LevelTextureStoreError> HandleAt(
        const std::size_t index) const
    {
        return texture_store_->HandleAt(index);
    }

private:
    TempTree tree_;
    LevelManifestIR manifest_;
    std::optional<GameDataService> game_data_;
    std::optional<LevelTextureStore> texture_store_;
    bool ready_ = false;
};

std::expected<JobService, std::string> MakeJobs(
    const std::size_t pending = 64U, const std::size_t workers = 1U)
{
    return JobService::Create(
        JobServiceConfig{.worker_count = workers, .max_pending_jobs = pending});
}

std::expected<std::unique_ptr<AssetService>, AssetServiceError> MakeAssets(JobService& jobs,
    const ContentFixture& fixture, const AssetServiceConfig config)
{
    return AssetService::Create(
        jobs, fixture.game_data(), fixture.texture_store(), config);
}

AssetServiceConfig Config(const std::size_t slots, const std::size_t in_flight,
    const std::uint64_t resident)
{
    return AssetServiceConfig{.slot_capacity = slots,
        .maximum_in_flight_requests = in_flight,
        .maximum_resident_logical_bytes = resident};
}

bool IsEmptySnapshot(const AssetServiceSnapshot& snapshot, const std::size_t capacity)
{
    return snapshot.slot_capacity == capacity && snapshot.free_slots == capacity &&
           snapshot.active_slots == 0U && snapshot.retired_slots == 0U &&
           snapshot.queued == 0U && snapshot.loading == 0U && snapshot.ready == 0U &&
           snapshot.failed == 0U && snapshot.in_flight_requests == 0U &&
           snapshot.resident_logical_bytes == 0U;
}

bool SameSnapshot(
    const AssetServiceSnapshot& left, const AssetServiceSnapshot& right) noexcept
{
    return left.slot_capacity == right.slot_capacity &&
           left.free_slots == right.free_slots &&
           left.active_slots == right.active_slots &&
           left.retired_slots == right.retired_slots && left.queued == right.queued &&
           left.loading == right.loading && left.ready == right.ready &&
           left.failed == right.failed &&
           left.in_flight_requests == right.in_flight_requests &&
           left.resident_logical_bytes == right.resident_logical_bytes;
}

std::uint8_t FirstStorageByte(const omega::asset::TextureStorageIR& storage)
{
    if (storage.blocks.empty() || storage.blocks.front().planes.empty() ||
        storage.blocks.front().planes.front().bytes.empty())
        return 0U;
    return std::to_integer<std::uint8_t>(
        storage.blocks.front().planes.front().bytes.front());
}

std::uint64_t Fnv1a64(const std::span<const std::byte> bytes) noexcept
{
    std::uint64_t hash = 14695981039346656037ULL;
    for (const std::byte value : bytes)
    {
        hash ^= std::to_integer<std::uint8_t>(value);
        hash *= 1099511628211ULL;
    }
    return hash;
}

bool MatchesPreviewError(
    const std::expected<omega::runtime::DebugImage, LevelTextureTopologyPreviewError>& result,
    const LevelTextureTopologyPreviewErrorCode code, const std::string_view message,
    const std::optional<LevelTextureStoreErrorCode> texture_store_error_code = std::nullopt,
    const std::optional<AssetServiceErrorCode> asset_error_code = std::nullopt,
    const std::optional<TextureStorageTopologyDebugImageErrorCode> image_error_code =
        std::nullopt) noexcept
{
    return !result && result.error().code == code && result.error().message == message &&
           result.error().texture_store_error_code == texture_store_error_code &&
           result.error().asset_error_code == asset_error_code &&
           result.error().image_error_code == image_error_code;
}

std::optional<std::uint64_t> ResidentBytes(
    const ContentFixture& fixture, const LevelTextureHandle& handle)
{
    auto loaded = fixture.texture_store().Load(fixture.game_data(), handle);
    if (!loaded)
        return std::nullopt;
    return loaded->usage.logical_output_bytes;
}

void CheckConfigurationAndNames(const ContentFixture& fixture)
{
    Check(omega::runtime::kMaximumAssetServiceSlotCapacity == 8'192U,
        "the fixed pool hard maximum is pinned to 8192 slots");
    const AssetServiceConfig defaults;
    Check(defaults.slot_capacity == 64U && defaults.maximum_in_flight_requests == 64U &&
              defaults.maximum_resident_logical_bytes == 64ULL * 1024ULL * 1024ULL,
        "the internal default budgets are explicit and stable");

    auto jobs = MakeJobs();
    Check(jobs.has_value(), "configuration test job service is created");
    if (!jobs)
        return;

    const auto invalid = [&](const AssetServiceConfig config, const std::string_view message)
    {
        CheckError(MakeAssets(*jobs, fixture, config),
            AssetServiceErrorCode::InvalidConfiguration, message);
    };
    invalid(Config(0U, 1U, 1U), "zero slot capacity is rejected");
    invalid(Config(1U, 0U, 1U), "zero in-flight capacity is rejected");
    invalid(Config(1U, 1U, 0U), "zero resident capacity is rejected");
    invalid(Config(1U, 2U, 1U), "in-flight capacity cannot exceed the slot pool");
    invalid(Config(omega::runtime::kMaximumAssetServiceSlotCapacity + 1U, 1U, 1U),
        "one slot above the 8192 hard maximum is rejected");

    auto maximum = MakeAssets(*jobs, fixture,
        Config(omega::runtime::kMaximumAssetServiceSlotCapacity, 1U, 1U));
    Check(maximum.has_value(), "the exact 8192-slot hard maximum is accepted");
    if (maximum)
        Check(IsEmptySnapshot((*maximum)->Snapshot(),
                  omega::runtime::kMaximumAssetServiceSlotCapacity),
            "the maximum pool is completely preallocated and initially empty");

    auto move_source = MakeJobs();
    Check(move_source.has_value(), "moved dependency source is created");
    if (move_source)
    {
        JobService moved = std::move(*move_source);
        CheckError(MakeAssets(*move_source, fixture, Config(1U, 1U, 1U)),
            AssetServiceErrorCode::InvalidDependency,
            "a moved-from job dependency is rejected before publication");
        moved.WaitForIdle();
    }

    using Pair = std::pair<AssetServiceErrorCode, std::string_view>;
    const std::array<Pair, 13> names{
        Pair{AssetServiceErrorCode::InvalidConfiguration, "invalid-configuration"},
        Pair{AssetServiceErrorCode::InvalidDependency, "invalid-dependency"},
        Pair{AssetServiceErrorCode::InvalidSourceHandle, "invalid-source-handle"},
        Pair{AssetServiceErrorCode::InvalidHandle, "invalid-handle"},
        Pair{AssetServiceErrorCode::SlotCapacityExceeded, "slot-capacity-exceeded"},
        Pair{AssetServiceErrorCode::InFlightLimitExceeded, "in-flight-limit-exceeded"},
        Pair{AssetServiceErrorCode::SubmissionRejected, "submission-rejected"},
        Pair{AssetServiceErrorCode::NotReady, "not-ready"},
        Pair{AssetServiceErrorCode::Busy, "busy"},
        Pair{AssetServiceErrorCode::LoadFailed, "load-failed"},
        Pair{AssetServiceErrorCode::ResidentBudgetExceeded, "resident-budget-exceeded"},
        Pair{AssetServiceErrorCode::WorkerFailed, "worker-failed"},
        Pair{AssetServiceErrorCode::AllocationFailed, "allocation-failed"},
    };
    Check(std::ranges::all_of(names,
              [](const Pair& pair)
              {
                  return omega::runtime::AssetServiceErrorCodeName(pair.first) == pair.second;
              }),
        "every public asset-service error code has a stable diagnostic name");
}

void CheckQueuedBusyAndInFlight(const ContentFixture& fixture)
{
    auto source_a = fixture.HandleAt(0U);
    auto source_b = fixture.HandleAt(1U);
    auto jobs = MakeJobs(8U);
    Check(source_a && source_b && jobs, "queued-state dependencies are created");
    if (!source_a || !source_b || !jobs)
        return;

    std::latch blocker_started(1);
    std::latch blocker_release(1);
    Check(jobs->Submit(
              [&blocker_started, &blocker_release]
              {
                  blocker_started.count_down();
                  blocker_release.wait();
              })
              .has_value(),
        "the queue gate is accepted");
    blocker_started.wait();

    auto created = MakeAssets(*jobs, fixture, Config(2U, 1U, 1ULL << 20U));
    Check(created.has_value(), "queued-state asset service is created");
    if (!created)
    {
        blocker_release.count_down();
        jobs->WaitForIdle();
        return;
    }
    std::unique_ptr<AssetService> assets = std::move(*created);
    auto requested = assets->Request(*source_a);
    Check(requested.has_value(), "a request queues behind the deterministic gate");
    if (!requested)
    {
        blocker_release.count_down();
        jobs->WaitForIdle();
        return;
    }

    const auto queued = assets->Snapshot();
    Check(queued.slot_capacity == 2U && queued.free_slots == 1U && queued.active_slots == 1U &&
              queued.queued == 1U && queued.loading == 0U && queued.ready == 0U &&
              queued.failed == 0U && queued.in_flight_requests == 1U &&
              queued.resident_logical_bytes == 0U,
        "the queued snapshot exactly accounts one accepted request");
    auto state = assets->State(*requested);
    Check(state && *state == TextureAssetState::Queued,
        "a gated request exposes the queued state");
    CheckError(assets->Get(*requested), AssetServiceErrorCode::NotReady,
        "queued storage cannot be observed");
    CheckError(assets->Release(*requested), AssetServiceErrorCode::Busy,
        "a queued slot cannot be cancelled in v0");
    auto over_in_flight = assets->Request(*source_b);
    CheckError(over_in_flight, AssetServiceErrorCode::InFlightLimitExceeded,
        "the in-flight budget rejects a second queued request without consuming a slot");
    Check(assets->Snapshot().free_slots == 1U && assets->Snapshot().active_slots == 1U,
        "in-flight rejection leaves the fixed pool unchanged");
    if (!over_in_flight)
        Check(Sanitized(over_in_flight.error(), fixture.root()),
            "in-flight diagnostics disclose no source identity");

    blocker_release.count_down();
    assets->WaitForIdle();
    const auto ready = assets->Snapshot();
    Check(ready.queued == 0U && ready.loading == 0U && ready.ready == 1U &&
              ready.failed == 0U && ready.in_flight_requests == 0U &&
              ready.resident_logical_bytes != 0U,
        "the completed snapshot publishes one resident ready asset");
    state = assets->State(*requested);
    Check(state && *state == TextureAssetState::Ready,
        "the gated request becomes ready after release");
    Check(assets->Get(*requested).has_value(), "ready storage is observable");
    Check(assets->Release(*requested).has_value(), "a ready slot releases explicitly");
    Check(IsEmptySnapshot(assets->Snapshot(), 2U),
        "ready release restores the exact empty snapshot");
    jobs->WaitForIdle();
}

void CheckDuplicatesPoolReuseAndAccounting(const ContentFixture& fixture)
{
    auto source_a = fixture.HandleAt(0U);
    auto source_b = fixture.HandleAt(1U);
    Check(source_a && source_b, "duplicate-request source handles are available");
    if (!source_a || !source_b)
        return;
    const auto resident = ResidentBytes(fixture, *source_a);
    Check(resident && *resident != 0U, "synthetic ready storage has nonzero logical residency");
    if (!resident)
        return;

    auto jobs = MakeJobs(8U);
    Check(jobs.has_value(), "duplicate-request job service is created");
    if (!jobs)
        return;
    auto created = MakeAssets(*jobs, fixture, Config(2U, 2U, *resident * 2U));
    Check(created.has_value(), "two-slot duplicate-request service is created");
    if (!created)
        return;
    std::unique_ptr<AssetService> assets = std::move(*created);

    auto first = assets->Request(*source_a);
    auto second = assets->Request(*source_a);
    Check(first && second, "duplicate source requests allocate independent slots");
    if (!first || !second)
        return;
    auto exhausted = assets->Request(*source_b);
    CheckError(exhausted, AssetServiceErrorCode::SlotCapacityExceeded,
        "the first request beyond the fixed slot pool is rejected");
    if (!exhausted)
        Check(Sanitized(exhausted.error(), fixture.root()),
            "pool-exhaustion diagnostics disclose no source identity");

    assets->WaitForIdle();
    auto first_view = assets->Get(*first);
    auto second_view = assets->Get(*second);
    Check(first_view && second_view, "both duplicate requests become independently ready");
    if (first_view && second_view)
    {
        Check(&first_view->storage.get() != &second_view->storage.get() &&
                  first_view->storage.get() == second_view->storage.get(),
            "duplicate requests own distinct equal immutable storage values");
    }
    const auto full = assets->Snapshot();
    Check(full.free_slots == 0U && full.active_slots == 2U && full.ready == 2U &&
              full.failed == 0U && full.in_flight_requests == 0U &&
              full.resident_logical_bytes == *resident * 2U,
        "duplicate residency and slot accounting are exact");

    Check(assets->Release(*first).has_value(), "one duplicate releases independently");
    const auto half = assets->Snapshot();
    Check(half.free_slots == 1U && half.active_slots == 1U && half.ready == 1U &&
              half.resident_logical_bytes == *resident,
        "releasing one duplicate decrements only its residency");
    CheckError(assets->State(*first), AssetServiceErrorCode::InvalidHandle,
        "a released handle is immediately stale");
    CheckError(assets->Get(*first), AssetServiceErrorCode::InvalidHandle,
        "stale storage cannot be observed");
    CheckError(assets->Release(*first), AssetServiceErrorCode::InvalidHandle,
        "a stale handle cannot release a reused slot");
    Check(assets->Get(*second).has_value(), "the other duplicate remains independently live");
    Check(assets->Release(*second).has_value(), "the second duplicate releases normally");
    Check(IsEmptySnapshot(assets->Snapshot(), 2U), "both duplicate slots return to the pool");

    auto reused = assets->Request(*source_b);
    Check(reused.has_value(), "a released slot is reused for a new source");
    if (reused)
    {
        CheckError(assets->State(*first), AssetServiceErrorCode::InvalidHandle,
            "slot reuse never revives the prior generation");
        assets->WaitForIdle();
        auto view = assets->Get(*reused);
        Check(view && FirstStorageByte(view->storage.get()) == 0x61U,
            "slot reuse publishes the new source, not prior storage");
        Check(assets->Release(*reused).has_value(), "the reused slot releases normally");
    }
    Check(IsEmptySnapshot(assets->Snapshot(), 2U),
        "reuse leaves no residency or retired slots behind");
}

void CheckOwnedTopologyImageComposition(const ContentFixture& fixture)
{
    auto source_a = fixture.HandleAt(0U);
    auto source_b = fixture.HandleAt(1U);
    auto jobs = MakeJobs(8U);
    Check(source_a && source_b && jobs,
        "topology-composition dependencies are created from public synthetic data");
    if (!source_a || !source_b || !jobs)
        return;

    auto created = MakeAssets(*jobs, fixture, Config(2U, 2U, 1ULL << 20U));
    Check(created.has_value(), "the topology-composition asset service is created");
    if (!created)
        return;
    std::unique_ptr<AssetService> assets = std::move(*created);

    auto first_handle = assets->Request(*source_a);
    auto second_handle = assets->Request(*source_b);
    Check(first_handle && second_handle,
        "two distinct synthetic payloads are accepted for asynchronous loading");
    if (!first_handle || !second_handle)
        return;

    assets->WaitForIdle();
    std::optional<omega::runtime::DebugImage> first_image;
    std::optional<omega::runtime::DebugImage> second_image;
    {
        auto first_view = assets->Get(*first_handle);
        auto second_view = assets->Get(*second_handle);
        Check(first_view && second_view,
            "both asynchronous loads publish canonical immutable texture storage");
        if (!first_view || !second_view)
            return;
        Check(FirstStorageByte(first_view->storage.get()) !=
                  FirstStorageByte(second_view->storage.get()),
            "the two canonical inputs retain their intentionally distinct payload bytes");

        auto first_built = omega::runtime::BuildTextureStorageTopologyDebugImage(
            first_view->storage.get());
        auto second_built = omega::runtime::BuildTextureStorageTopologyDebugImage(
            second_view->storage.get());
        Check(first_built && second_built,
            "ready asset views compose directly with the portable topology adapter");
        if (!first_built || !second_built)
            return;
        first_image.emplace(std::move(*first_built));
        second_image.emplace(std::move(*second_built));
    }

    const bool exact_dimensions = first_image->width == 32U && first_image->height == 32U &&
                                  second_image->width == 32U &&
                                  second_image->height == 32U &&
                                  first_image->rgba8_pixels.size() == 32U * 32U * 4U &&
                                  second_image->rgba8_pixels.size() == 32U * 32U * 4U;
    Check(exact_dimensions,
        "both one-block public fixtures produce complete owned topology tiles");
    Check(Fnv1a64(first_image->pixels()) == 0x666d00371feff88dULL &&
              Fnv1a64(second_image->pixels()) == 0x666d00371feff88dULL,
        "both composed one-block images have the independently frozen signature");
    Check(first_image->rgba8_pixels == second_image->rgba8_pixels,
        "different payload bytes cannot change metadata-only topology pixels");
    Check(!first_image->rgba8_pixels.empty() && !second_image->rgba8_pixels.empty() &&
              first_image->rgba8_pixels.data() != second_image->rgba8_pixels.data(),
        "the topology results own distinct nonempty pixel allocations");
    const std::vector<std::byte> first_pixels_before_release = first_image->rgba8_pixels;
    const std::vector<std::byte> second_pixels_before_release = second_image->rgba8_pixels;

    const auto released_first = assets->Release(*first_handle);
    const auto released_second = assets->Release(*second_handle);
    Check(released_first.has_value(), "the first source asset releases after image construction");
    Check(released_second.has_value(), "the second source asset releases independently");
    Check(IsEmptySnapshot(assets->Snapshot(), 2U),
        "topology composition leaves no asset residency or active slots");
    Check(first_image->rgba8_pixels == first_pixels_before_release &&
              second_image->rgba8_pixels == second_pixels_before_release,
        "the independently owned topology images survive source-slot recycling");
}

void CheckFirstLevelTextureTopologyPreview(const ContentFixture& fixture)
{
    Check(static_cast<std::uint8_t>(
              LevelTextureTopologyPreviewErrorCode::AssetServiceNotEmpty) == 0U &&
              static_cast<std::uint8_t>(
                  LevelTextureTopologyPreviewErrorCode::EmptyTextureInventory) == 1U &&
              static_cast<std::uint8_t>(
                  LevelTextureTopologyPreviewErrorCode::SourceHandleFailed) == 2U &&
              static_cast<std::uint8_t>(
                  LevelTextureTopologyPreviewErrorCode::AssetRequestFailed) == 3U &&
              static_cast<std::uint8_t>(
                  LevelTextureTopologyPreviewErrorCode::AssetGetFailed) == 4U &&
              static_cast<std::uint8_t>(
                  LevelTextureTopologyPreviewErrorCode::ImageBuildFailed) == 5U &&
              static_cast<std::uint8_t>(
                  LevelTextureTopologyPreviewErrorCode::AssetReleaseFailed) == 6U &&
              static_cast<std::uint8_t>(
                  LevelTextureTopologyPreviewErrorCode::ResidualAssetState) == 7U,
        "the level-texture topology preview error ordinals are frozen");

    {
        auto jobs = MakeJobs(8U);
        Check(jobs.has_value(), "the first-topology-preview job service is created");
        if (jobs)
        {
            auto created = MakeAssets(*jobs, fixture, Config(1U, 1U, 1ULL << 20U));
            Check(created.has_value(), "the first-topology-preview asset service is created");
            if (created)
            {
                std::unique_ptr<AssetService> assets = std::move(*created);
                auto first = omega::runtime::BuildFirstLevelTextureTopologyPreview(
                    *assets, fixture.texture_store());
                Check(first.has_value(),
                    "canonical texture zero builds the first topology preview");
                Check(IsEmptySnapshot(assets->Snapshot(), 1U),
                    "the first topology preview restores an exact empty asset service");
                auto second = omega::runtime::BuildFirstLevelTextureTopologyPreview(
                    *assets, fixture.texture_store());
                Check(second.has_value(),
                    "a second first-topology preview call succeeds after exact cleanup");
                Check(IsEmptySnapshot(assets->Snapshot(), 1U),
                    "the repeated topology preview again restores an exact empty service");
                if (first && second)
                {
                    Check(first->width == 32U && first->height == 32U &&
                              first->rgba8_pixels.size() == 4096U && second->width == 32U &&
                              second->height == 32U && second->rgba8_pixels.size() == 4096U,
                        "the adapter returns complete owned 32x32 RGBA8 topology images");
                    Check(Fnv1a64(first->pixels()) == 0x666d00371feff88dULL &&
                              Fnv1a64(second->pixels()) == 0x666d00371feff88dULL,
                        "the first canonical topology preview has the frozen signature");
                    Check(first->rgba8_pixels == second->rgba8_pixels &&
                              first->rgba8_pixels.data() != second->rgba8_pixels.data(),
                        "repeated preview calls are byte-identical and independently owned");
                }

                auto diagnostic_first =
                    omega::runtime::BuildFirstLevelTextureDiagnosticPreview(
                        *assets, fixture.texture_store());
                auto diagnostic_second =
                    omega::runtime::BuildFirstLevelTextureDiagnosticPreview(
                        *assets, fixture.texture_store());
                Check(diagnostic_first && diagnostic_second &&
                          IsEmptySnapshot(assets->Snapshot(), 1U),
                    "combined diagnostic previews repeat after exact source cleanup");
                if (diagnostic_first && diagnostic_second)
                {
                    const auto& first_transfer =
                        diagnostic_first->packed24_transfer_image;
                    const auto& second_transfer =
                        diagnostic_second->packed24_transfer_image;
                    Check(!diagnostic_first->packed24_transfer_error_code &&
                              !diagnostic_second->packed24_transfer_error_code &&
                              first_transfer && second_transfer &&
                              diagnostic_first->topology_image.width == 32U &&
                              diagnostic_first->topology_image.height == 32U &&
                              Fnv1a64(diagnostic_first->topology_image.pixels()) ==
                                  0x666d00371feff88dULL &&
                              first_transfer->width == 16U &&
                              first_transfer->height == 16U &&
                              first_transfer->rgba8_pixels.size() == 1024U &&
                              Fnv1a64(first_transfer->pixels()) ==
                                  0x4abb645f50f5a325ULL,
                        "one combined call yields the frozen topology and Packed24 diagnostics");
                    Check(first_transfer && second_transfer &&
                              first_transfer->rgba8_pixels ==
                                  second_transfer->rgba8_pixels &&
                              first_transfer->rgba8_pixels.data() !=
                                  second_transfer->rgba8_pixels.data() &&
                              diagnostic_first->topology_image.rgba8_pixels ==
                                  diagnostic_second->topology_image.rgba8_pixels &&
                              diagnostic_first->topology_image.rgba8_pixels.data() !=
                                  diagnostic_second->topology_image.rgba8_pixels.data(),
                        "combined outputs are byte-stable and independently owned after release");
                }

                omega::runtime::LevelTextureDiagnosticPreviewLimits transfer_tight;
                transfer_tight.packed24_transfer.maximum_output_bytes = 1023U;
                auto transfer_rejected =
                    omega::runtime::BuildFirstLevelTextureDiagnosticPreview(
                        *assets, fixture.texture_store(), transfer_tight);
                Check(transfer_rejected &&
                          !transfer_rejected->packed24_transfer_image &&
                          transfer_rejected->packed24_transfer_error_code ==
                              Packed24TransferDebugImageErrorCode::OutputByteLimitExceeded &&
                          Fnv1a64(transfer_rejected->topology_image.pixels()) ==
                              0x666d00371feff88dULL &&
                          IsEmptySnapshot(assets->Snapshot(), 1U),
                    "Packed24 rejection is typed and nonfatal while topology and cleanup survive");

                omega::runtime::TextureStorageTopologyDebugImageLimits tight_limits;
                tight_limits.maximum_output_bytes = 4095U;
                auto limited = omega::runtime::BuildFirstLevelTextureTopologyPreview(
                    *assets, fixture.texture_store(), tight_limits);
                Check(MatchesPreviewError(limited,
                          LevelTextureTopologyPreviewErrorCode::ImageBuildFailed,
                          "level texture topology preview image build failed", std::nullopt,
                          std::nullopt,
                          TextureStorageTopologyDebugImageErrorCode::OutputByteLimitExceeded),
                    "an output limit failure retains only the topology-image category");
                Check(IsEmptySnapshot(assets->Snapshot(), 1U),
                    "an image-build failure releases the loaded source exactly");
            }
        }
    }

    {
        ContentFixture empty_fixture(
            "empty-topology-preview", ContentFixtureKind::EmptyInventory);
        auto jobs = MakeJobs(4U);
        Check(empty_fixture.ready() && jobs,
            "the empty-inventory preview dependencies are created");
        if (empty_fixture.ready() && jobs)
        {
            auto created = MakeAssets(
                *jobs, empty_fixture, Config(1U, 1U, 1ULL << 20U));
            Check(created.has_value(), "an empty-inventory asset service is created");
            if (created)
            {
                std::unique_ptr<AssetService> assets = std::move(*created);
                auto preview = omega::runtime::BuildFirstLevelTextureTopologyPreview(
                    *assets, empty_fixture.texture_store());
                Check(MatchesPreviewError(preview,
                          LevelTextureTopologyPreviewErrorCode::EmptyTextureInventory,
                          "level texture topology preview requires at least one texture"),
                    "an empty inventory fails before scheduling with no nested category");
                Check(IsEmptySnapshot(assets->Snapshot(), 1U),
                    "the empty-inventory rejection performs no asset work");
            }
        }
    }

    {
        ContentFixture malformed_fixture(
            "malformed-first-topology-preview", ContentFixtureKind::MalformedFirst);
        auto jobs = MakeJobs(4U);
        Check(malformed_fixture.ready() && jobs,
            "the malformed-first preview dependencies are created");
        if (malformed_fixture.ready() && jobs)
        {
            auto created = MakeAssets(
                *jobs, malformed_fixture, Config(1U, 1U, 1ULL << 20U));
            Check(created.has_value(), "the malformed-first asset service is created");
            if (created)
            {
                std::unique_ptr<AssetService> assets = std::move(*created);
                auto preview = omega::runtime::BuildFirstLevelTextureTopologyPreview(
                    *assets, malformed_fixture.texture_store());
                Check(MatchesPreviewError(preview,
                          LevelTextureTopologyPreviewErrorCode::AssetGetFailed,
                          "level texture topology preview asset get failed",
                          LevelTextureStoreErrorCode::DecodeFailed,
                          AssetServiceErrorCode::LoadFailed),
                    "a malformed canonical first TDX retains the typed nested load categories");
                Check(!preview && !ContainsForbidden(preview.error().message,
                                      malformed_fixture.root()),
                    "the malformed-first adapter diagnostic contains no source identity");
                Check(IsEmptySnapshot(assets->Snapshot(), 1U),
                    "the malformed-first failed slot releases exactly");
            }
        }
    }

    {
        ContentFixture foreign_fixture("foreign-first-topology-preview");
        auto jobs = MakeJobs(4U);
        Check(foreign_fixture.ready() && jobs,
            "the foreign-store preview dependencies are created");
        if (foreign_fixture.ready() && jobs)
        {
            auto created = MakeAssets(*jobs, fixture, Config(1U, 1U, 1ULL << 20U));
            Check(created.has_value(), "the foreign-store probe asset service is created");
            if (created)
            {
                std::unique_ptr<AssetService> assets = std::move(*created);
                auto preview = omega::runtime::BuildFirstLevelTextureTopologyPreview(
                    *assets, foreign_fixture.texture_store());
                Check(MatchesPreviewError(preview,
                          LevelTextureTopologyPreviewErrorCode::AssetGetFailed,
                          "level texture topology preview asset get failed",
                          LevelTextureStoreErrorCode::InvalidHandle,
                          AssetServiceErrorCode::LoadFailed),
                    "a foreign first handle retains the typed invalid-handle load categories");
                Check(IsEmptySnapshot(assets->Snapshot(), 1U),
                    "the foreign-source failed slot releases exactly");
            }
        }
    }

    {
        auto source = fixture.HandleAt(0U);
        auto jobs = MakeJobs(4U);
        Check(source && jobs, "the preoccupied-preview dependencies are created");
        if (source && jobs)
        {
            auto created = MakeAssets(*jobs, fixture, Config(1U, 1U, 1ULL << 20U));
            Check(created.has_value(), "the preoccupied-preview asset service is created");
            if (created)
            {
                std::unique_ptr<AssetService> assets = std::move(*created);
                auto occupied = assets->Request(*source);
                Check(occupied.has_value(), "one source preoccupies the preview asset service");
                if (occupied)
                {
                    assets->WaitForIdle();
                    const AssetServiceSnapshot before = assets->Snapshot();
                    auto preview = omega::runtime::BuildFirstLevelTextureTopologyPreview(
                        *assets, fixture.texture_store());
                    Check(MatchesPreviewError(preview,
                              LevelTextureTopologyPreviewErrorCode::AssetServiceNotEmpty,
                              "level texture topology preview requires an empty asset service"),
                        "a preoccupied service fails before work with no nested category");
                    Check(SameSnapshot(before, assets->Snapshot()),
                        "the preoccupied-service rejection leaves every snapshot field unchanged");
                    Check(assets->Release(*occupied).has_value(),
                        "the manually preoccupied source releases after the rejection probe");
                }
                Check(IsEmptySnapshot(assets->Snapshot(), 1U),
                    "the preoccupied-service probe ends exactly empty");
            }
        }
    }

    {
        auto jobs = MakeJobs(1U);
        Check(jobs.has_value(), "the preview submission-rejection job service is created");
        if (jobs)
        {
            std::latch blocker_started(1);
            std::latch blocker_release(1);
            auto blocker = jobs->Submit(
                [&blocker_started, &blocker_release]
                {
                    blocker_started.count_down();
                    blocker_release.wait();
                });
            Check(blocker.has_value(), "the preview rejection gate is accepted");
            if (blocker)
            {
                blocker_started.wait();
                auto filler = jobs->Submit([] {});
                Check(filler.has_value() && jobs->pending_job_count() == 1U,
                    "the preview probe fills the pending-job budget deterministically");
                if (filler)
                {
                    auto created = MakeAssets(
                        *jobs, fixture, Config(1U, 1U, 1ULL << 20U));
                    Check(created.has_value(),
                        "the preview submission-rejection asset service is created");
                    if (created)
                    {
                        std::unique_ptr<AssetService> assets = std::move(*created);
                        auto preview = omega::runtime::BuildFirstLevelTextureTopologyPreview(
                            *assets, fixture.texture_store());
                        Check(MatchesPreviewError(preview,
                                  LevelTextureTopologyPreviewErrorCode::AssetRequestFailed,
                                  "level texture topology preview asset request failed",
                                  std::nullopt, AssetServiceErrorCode::SubmissionRejected),
                            "a full queue reports only the typed request-rejection category");
                        Check(IsEmptySnapshot(assets->Snapshot(), 1U),
                            "request rejection transactionally restores the exact entry snapshot");
                    }
                }
                blocker_release.count_down();
                jobs->WaitForIdle();
            }
        }
    }
}

void CheckHandleIdentityAndSourceValidation(const ContentFixture& fixture)
{
    auto source = fixture.HandleAt(0U);
    auto jobs = MakeJobs(16U);
    Check(source && jobs, "handle-identity dependencies are created");
    if (!source || !jobs)
        return;

    auto first_created = MakeAssets(*jobs, fixture, Config(2U, 2U, 1ULL << 20U));
    auto second_created = MakeAssets(*jobs, fixture, Config(1U, 1U, 1ULL << 20U));
    Check(first_created && second_created, "independent asset services are created");
    if (!first_created || !second_created)
        return;
    std::unique_ptr<AssetService> first_service = std::move(*first_created);
    std::unique_ptr<AssetService> second_service = std::move(*second_created);

    TextureAssetHandle default_handle;
    Check(default_handle.expired(), "a default asset handle has no live service token");
    CheckError(first_service->State(default_handle), AssetServiceErrorCode::InvalidHandle,
        "a default asset handle has no state");
    CheckError(first_service->Get(default_handle), AssetServiceErrorCode::InvalidHandle,
        "a default asset handle has no storage");
    CheckError(first_service->Release(default_handle), AssetServiceErrorCode::InvalidHandle,
        "a default asset handle cannot release a slot");

    auto first_handle = first_service->Request(*source);
    Check(first_handle.has_value(), "the owner service accepts its source request");
    if (first_handle)
    {
        first_service->WaitForIdle();
        CheckError(second_service->State(*first_handle), AssetServiceErrorCode::InvalidHandle,
            "a foreign service cannot observe handle state");
        CheckError(second_service->Get(*first_handle), AssetServiceErrorCode::InvalidHandle,
            "a foreign service cannot observe handle storage");
        CheckError(second_service->Release(*first_handle), AssetServiceErrorCode::InvalidHandle,
            "a foreign service cannot release an owner slot");
        Check(first_service->Get(*first_handle).has_value(),
            "foreign rejection leaves the owner slot unchanged");
        Check(first_service->Release(*first_handle).has_value(),
            "the owner releases its handle after foreign probes");
    }

    TextureAssetHandle expired_asset;
    {
        auto temporary_created = MakeAssets(*jobs, fixture, Config(1U, 1U, 1ULL << 20U));
        Check(temporary_created.has_value(), "temporary asset service is created");
        if (temporary_created)
        {
            std::unique_ptr<AssetService> temporary = std::move(*temporary_created);
            auto temporary_handle = temporary->Request(*source);
            if (temporary_handle)
            {
                temporary->WaitForIdle();
                expired_asset = *temporary_handle;
            }
        }
    }
    Check(expired_asset.expired(), "an asset handle expires with its service identity");

    LevelTextureHandle default_source;
    Check(default_source.expired(), "a default source handle has no live store identity");
    CheckError(first_service->Request(default_source),
        AssetServiceErrorCode::InvalidSourceHandle,
        "a default source handle is rejected before slot allocation");

    LevelTextureHandle expired_source;
    {
        auto temporary_store =
            LevelTextureStore::Open(fixture.game_data(), fixture.manifest());
        Check(temporary_store.has_value(), "temporary source store opens");
        if (temporary_store)
        {
            auto temporary_handle = temporary_store->HandleAt(0U);
            if (temporary_handle)
                expired_source = *temporary_handle;
        }
    }
    Check(expired_source.expired(), "a source handle expires with its texture store");
    CheckError(first_service->Request(expired_source),
        AssetServiceErrorCode::InvalidSourceHandle,
        "an expired source handle is rejected before slot allocation");
    Check(IsEmptySnapshot(first_service->Snapshot(), 2U),
        "invalid source handles leave the pool empty");

    auto foreign_store = LevelTextureStore::Open(fixture.game_data(), fixture.manifest());
    auto foreign_source = foreign_store
                              ? foreign_store->HandleAt(0U)
                              : std::expected<LevelTextureHandle, LevelTextureStoreError>(
                                    std::unexpected(LevelTextureStoreError{}));
    Check(foreign_store && foreign_source, "a foreign source-store handle is available");
    if (foreign_source)
    {
        auto accepted = first_service->Request(*foreign_source);
        Check(accepted.has_value(),
            "a live foreign source token is accepted for deferred worker validation");
        if (accepted)
        {
            first_service->WaitForIdle();
            auto state = first_service->State(*accepted);
            Check(state && *state == TextureAssetState::Failed,
                "the bound store fails a foreign source handle on the worker");
            auto failed = first_service->Get(*accepted);
            CheckError(failed, AssetServiceErrorCode::LoadFailed,
                "foreign source validation publishes a sanitized load failure");
            if (!failed)
            {
                Check(failed.error().level_texture_error &&
                          failed.error().level_texture_error->code ==
                              LevelTextureStoreErrorCode::InvalidHandle,
                    "foreign source failure retains only the typed store category");
                Check(Sanitized(failed.error(), fixture.root()),
                    "foreign source failure discloses no identity");
            }
            Check(first_service->Release(*accepted).has_value(),
                "a failed foreign-source slot releases explicitly");
        }
    }
    Check(IsEmptySnapshot(first_service->Snapshot(), 2U),
        "source-validation probes leave no active slots or residency");
    jobs->WaitForIdle();
}

void CheckFailedReleaseAndPrivacy(const ContentFixture& fixture)
{
    auto broken_source = fixture.HandleAt(2U);
    auto jobs = MakeJobs(8U);
    Check(broken_source && jobs, "failed-load dependencies are created");
    if (!broken_source || !jobs)
        return;
    auto created = MakeAssets(*jobs, fixture, Config(1U, 1U, 1ULL << 20U));
    Check(created.has_value(), "failed-load service is created");
    if (!created)
        return;
    std::unique_ptr<AssetService> assets = std::move(*created);

    auto requested = assets->Request(*broken_source);
    Check(requested.has_value(), "a malformed source is accepted for asynchronous loading");
    if (!requested)
        return;
    assets->WaitForIdle();
    auto state = assets->State(*requested);
    Check(state && *state == TextureAssetState::Failed,
        "a malformed source publishes the failed state");
    const auto snapshot = assets->Snapshot();
    Check(snapshot.active_slots == 1U && snapshot.failed == 1U && snapshot.ready == 0U &&
              snapshot.in_flight_requests == 0U && snapshot.resident_logical_bytes == 0U,
        "a failed load retains one recyclable slot but no residency");

    auto failed = assets->Get(*requested);
    CheckError(failed, AssetServiceErrorCode::LoadFailed,
        "Get returns the retained typed load failure");
    if (!failed)
    {
        Check(failed.error().level_texture_error &&
                  failed.error().level_texture_error->code ==
                      LevelTextureStoreErrorCode::DecodeFailed,
            "the asset error retains the decoder failure category");
        Check(Sanitized(failed.error(), fixture.root()),
            "nested load diagnostics omit paths, names, locators, offsets, and payloads");
    }
    Check(assets->Release(*requested).has_value(), "a failed slot releases explicitly");
    CheckError(assets->State(*requested), AssetServiceErrorCode::InvalidHandle,
        "failed-slot release invalidates the prior generation");
    Check(IsEmptySnapshot(assets->Snapshot(), 1U),
        "failed release restores all capacity and zero residency");
}

void CheckSubmissionRejectionRollback(const ContentFixture& fixture)
{
    auto source = fixture.HandleAt(0U);
    auto jobs = MakeJobs(1U);
    Check(source && jobs, "submission-rejection dependencies are created");
    if (!source || !jobs)
        return;

    std::latch blocker_started(1);
    std::latch blocker_release(1);
    Check(jobs->Submit(
              [&blocker_started, &blocker_release]
              {
                  blocker_started.count_down();
                  blocker_release.wait();
              })
              .has_value(),
        "the submission-rejection gate is accepted");
    blocker_started.wait();
    Check(jobs->Submit([] {}).has_value(),
        "one filler occupies the complete pending-job budget");
    Check(jobs->pending_job_count() == 1U,
        "the job-service pending budget is deterministically full");

    auto created = MakeAssets(*jobs, fixture, Config(1U, 1U, 1ULL << 20U));
    Check(created.has_value(), "submission-rejection service is created");
    if (!created)
    {
        blocker_release.count_down();
        jobs->WaitForIdle();
        return;
    }
    std::unique_ptr<AssetService> assets = std::move(*created);
    auto rejected = assets->Request(*source);
    CheckError(rejected, AssetServiceErrorCode::SubmissionRejected,
        "a full job queue rejects asset submission without blocking");
    if (!rejected)
        Check(Sanitized(rejected.error(), fixture.root()),
            "submission diagnostics disclose no source identity or queue text");
    Check(IsEmptySnapshot(assets->Snapshot(), 1U),
        "submission rejection transactionally restores slot and in-flight capacity");

    blocker_release.count_down();
    jobs->WaitForIdle();
    auto retry = assets->Request(*source);
    Check(retry.has_value(), "the rolled-back slot accepts a later request");
    if (retry)
    {
        assets->WaitForIdle();
        Check(assets->Get(*retry).has_value(),
            "the post-rollback request loads normally");
        Check(assets->Release(*retry).has_value(),
            "the post-rollback request releases normally");
    }
    Check(IsEmptySnapshot(assets->Snapshot(), 1U),
        "post-rollback reuse ends with an exact empty snapshot");
}

void CheckResidentBoundaries(const ContentFixture& fixture)
{
    auto source_a = fixture.HandleAt(0U);
    auto source_b = fixture.HandleAt(1U);
    Check(source_a && source_b, "resident-boundary source handles are available");
    if (!source_a || !source_b)
        return;
    const auto resident = ResidentBytes(fixture, *source_a);
    Check(resident && *resident > 1U, "resident-boundary fixture has measurable output");
    if (!resident || *resident <= 1U)
        return;

    auto jobs = MakeJobs(16U);
    Check(jobs.has_value(), "resident-boundary job service is created");
    if (!jobs)
        return;

    auto exact_created = MakeAssets(*jobs, fixture, Config(1U, 1U, *resident));
    Check(exact_created.has_value(), "exact-resident-budget service is created");
    if (exact_created)
    {
        std::unique_ptr<AssetService> exact = std::move(*exact_created);
        auto handle = exact->Request(*source_a);
        Check(handle.has_value(), "exact-resident request is accepted");
        if (handle)
        {
            exact->WaitForIdle();
            auto view = exact->Get(*handle);
            Check(view && view->load_usage.logical_output_bytes == *resident,
                "a request succeeds at the exact resident boundary");
            Check(exact->Snapshot().ready == 1U &&
                      exact->Snapshot().resident_logical_bytes == *resident,
                "exact-boundary residency is charged once");
            Check(exact->Release(*handle).has_value(),
                "exact-boundary storage releases normally");
            Check(IsEmptySnapshot(exact->Snapshot(), 1U),
                "exact-boundary release returns every logical byte");
        }
    }

    auto below_created = MakeAssets(*jobs, fixture, Config(1U, 1U, *resident - 1U));
    Check(below_created.has_value(), "below-resident-budget service is created");
    if (below_created)
    {
        std::unique_ptr<AssetService> below = std::move(*below_created);
        auto handle = below->Request(*source_a);
        Check(handle.has_value(), "below-resident request is scheduled");
        if (handle)
        {
            below->WaitForIdle();
            auto state = below->State(*handle);
            Check(state && *state == TextureAssetState::Failed,
                "one byte below exact residency fails atomically");
            auto failed = below->Get(*handle);
            CheckError(failed, AssetServiceErrorCode::ResidentBudgetExceeded,
                "the below-boundary failure names only the resident budget category");
            if (!failed)
                Check(Sanitized(failed.error(), fixture.root()),
                    "resident-budget diagnostics disclose no source identity");
            Check(below->Snapshot().failed == 1U &&
                      below->Snapshot().resident_logical_bytes == 0U,
                "a rejected publication charges no resident bytes");
            Check(below->Release(*handle).has_value(),
                "a resident-budget failure remains recyclable");
        }
    }

    auto excess_created = MakeAssets(*jobs, fixture, Config(2U, 2U, *resident));
    Check(excess_created.has_value(), "aggregate-resident-budget service is created");
    if (excess_created)
    {
        std::unique_ptr<AssetService> excess = std::move(*excess_created);
        auto first = excess->Request(*source_a);
        auto second = excess->Request(*source_b);
        Check(first && second, "two sequential resident requests are accepted");
        if (first && second)
        {
            excess->WaitForIdle();
            auto first_state = excess->State(*first);
            auto second_state = excess->State(*second);
            Check(first_state && *first_state == TextureAssetState::Ready && second_state &&
                      *second_state == TextureAssetState::Failed,
                "single-worker FIFO publishes the first asset and rejects aggregate excess");
            CheckError(excess->Get(*second), AssetServiceErrorCode::ResidentBudgetExceeded,
                "aggregate excess retains the resident-budget failure");
            const auto snapshot = excess->Snapshot();
            Check(snapshot.ready == 1U && snapshot.failed == 1U &&
                      snapshot.resident_logical_bytes == *resident,
                "aggregate budget accounting retains exactly one ready asset");
            Check(excess->Release(*first).has_value() &&
                      excess->Release(*second).has_value(),
                "ready and budget-failed slots both recycle");
            Check(IsEmptySnapshot(excess->Snapshot(), 2U),
                "aggregate-budget release returns every slot and byte");
        }
    }
    jobs->WaitForIdle();
}

void CheckCapacityOneSequentialReuse(const ContentFixture& fixture)
{
    auto source_a = fixture.HandleAt(0U);
    auto source_b = fixture.HandleAt(1U);
    Check(source_a && source_b, "capacity-one sources are available");
    if (!source_a || !source_b)
        return;
    const auto resident = ResidentBytes(fixture, *source_a);
    if (!resident)
        return;
    auto jobs = MakeJobs(4U);
    Check(jobs.has_value(), "capacity-one job service is created");
    if (!jobs)
        return;
    auto created = MakeAssets(*jobs, fixture, Config(1U, 1U, *resident));
    Check(created.has_value(), "capacity-one asset service is created");
    if (!created)
        return;
    std::unique_ptr<AssetService> assets = std::move(*created);

    std::optional<TextureAssetHandle> previous;
    for (std::size_t iteration = 0U; iteration < 24U; ++iteration)
    {
        if (previous)
            CheckError(assets->State(*previous), AssetServiceErrorCode::InvalidHandle,
                "every prior generation stays stale before sequential reuse");
        const LevelTextureHandle& source = iteration % 2U == 0U ? *source_a : *source_b;
        auto current = assets->Request(source);
        Check(current.has_value(), "capacity-one slot accepts the next sequential request");
        if (!current)
            break;
        assets->WaitForIdle();
        auto view = assets->Get(*current);
        const std::uint8_t expected_seed = iteration % 2U == 0U ? 0x21U : 0x61U;
        Check(view && FirstStorageByte(view->storage.get()) == expected_seed,
            "capacity-one reuse never publishes storage from another generation");
        Check(assets->Release(*current).has_value(),
            "capacity-one generation releases before the next request");
        Check(IsEmptySnapshot(assets->Snapshot(), 1U),
            "capacity-one sequential release restores the fixed pool exactly");
        previous = *current;
    }
    if (previous)
    {
        Check(!previous->expired(),
            "a released handle retains the live service token while remaining stale");
        CheckError(assets->Get(*previous), AssetServiceErrorCode::InvalidHandle,
            "the final released generation remains inaccessible");
    }
}

void CheckWaitAndDestructionIsolation(const ContentFixture& fixture)
{
    auto source = fixture.HandleAt(0U);
    Check(source.has_value(), "wait-isolation source handle is available");
    if (!source)
        return;

    // AssetService::WaitForIdle must not delegate to the shared JobService. The asset job is
    // submitted first on a single worker; reaching the later gate proves that asset job is done
    // while an unrelated accepted job is still running.
    {
        auto jobs = MakeJobs(8U);
        Check(jobs.has_value(), "wait-isolation job service is created");
        if (jobs)
        {
            auto created = MakeAssets(*jobs, fixture, Config(1U, 1U, 1ULL << 20U));
            Check(created.has_value(), "wait-isolation asset service is created");
            if (created)
            {
                std::unique_ptr<AssetService> assets = std::move(*created);
                auto handle = assets->Request(*source);
                Check(handle.has_value(), "wait-isolation asset request is accepted first");

                std::latch blocker_started(1);
                std::latch blocker_release(1);
                Check(jobs->Submit(
                          [&blocker_started, &blocker_release]
                          {
                              blocker_started.count_down();
                              blocker_release.wait();
                          })
                          .has_value(),
                    "the unrelated post-asset gate is accepted");
                blocker_started.wait();
                Check(assets->Snapshot().ready == 1U &&
                          assets->Snapshot().in_flight_requests == 0U,
                    "reaching the unrelated gate proves the asset request is complete");

                std::promise<void> wait_returned;
                auto wait_future = wait_returned.get_future();
                std::jthread waiter(
                    [&assets, &wait_returned]
                    {
                        assets->WaitForIdle();
                        wait_returned.set_value();
                    });
                const bool asset_only =
                    wait_future.wait_for(std::chrono::milliseconds(500)) ==
                    std::future_status::ready;
                Check(asset_only,
                    "AssetService WaitForIdle returns while an unrelated job remains blocked");
                blocker_release.count_down();
                if (!asset_only)
                    wait_future.wait();
                waiter.join();
                if (handle)
                    Check(assets->Release(*handle).has_value(),
                        "the wait-isolation asset releases normally");
                jobs->WaitForIdle();

                std::atomic<std::uint64_t> probe{0U};
                Check(jobs->Submit([&probe] { probe.fetch_add(1U); }).has_value(),
                    "the shared job service accepts work after asset-only waiting");
                jobs->WaitForIdle();
                Check(probe.load() == 1U,
                    "the shared job service executes work after asset-only waiting");
            }
        }
    }

    // An idle AssetService destructor likewise ignores an unrelated running job.
    {
        auto jobs = MakeJobs(8U);
        Check(jobs.has_value(), "destructor-isolation job service is created");
        if (jobs)
        {
            auto created = MakeAssets(*jobs, fixture, Config(1U, 1U, 1ULL << 20U));
            Check(created.has_value(), "destructor-isolation asset service is created");
            if (created)
            {
                std::unique_ptr<AssetService> assets = std::move(*created);
                auto handle = assets->Request(*source);
                Check(handle.has_value(), "destructor-isolation asset request is accepted first");

                std::latch blocker_started(1);
                std::latch blocker_release(1);
                Check(jobs->Submit(
                          [&blocker_started, &blocker_release]
                          {
                              blocker_started.count_down();
                              blocker_release.wait();
                          })
                          .has_value(),
                    "the unrelated destructor gate is accepted");
                blocker_started.wait();
                Check(assets->Snapshot().ready == 1U,
                    "the asset is idle before destructor isolation is tested");

                std::promise<void> destroyed;
                auto destroyed_future = destroyed.get_future();
                std::jthread destroyer(
                    [owned = std::move(assets), &destroyed]() mutable
                    {
                        owned.reset();
                        destroyed.set_value();
                    });
                const bool asset_only =
                    destroyed_future.wait_for(std::chrono::milliseconds(500)) ==
                    std::future_status::ready;
                Check(asset_only,
                    "an idle AssetService destructor ignores unrelated blocked jobs");
                blocker_release.count_down();
                if (!asset_only)
                    destroyed_future.wait();
                destroyer.join();
                if (handle)
                    Check(handle->expired(), "destruction expires the service-scoped handle");
                jobs->WaitForIdle();

                std::atomic<std::uint64_t> probe{0U};
                Check(jobs->Submit([&probe] { probe.fetch_add(1U); }).has_value(),
                    "the shared job service accepts work after AssetService destruction");
                jobs->WaitForIdle();
                Check(probe.load() == 1U,
                    "the shared job service remains usable after AssetService destruction");
            }
        }
    }

    // Destruction must still wait for this service's own accepted queued job. A gate submitted
    // before Request keeps that job queued until the test explicitly releases it.
    {
        auto jobs = MakeJobs(8U);
        Check(jobs.has_value(), "own-drain job service is created");
        if (jobs)
        {
            std::latch blocker_started(1);
            std::latch blocker_release(1);
            Check(jobs->Submit(
                      [&blocker_started, &blocker_release]
                      {
                          blocker_started.count_down();
                          blocker_release.wait();
                      })
                      .has_value(),
                "the own-drain gate is accepted");
            blocker_started.wait();

            auto created = MakeAssets(*jobs, fixture, Config(1U, 1U, 1ULL << 20U));
            Check(created.has_value(), "own-drain asset service is created");
            if (created)
            {
                std::unique_ptr<AssetService> assets = std::move(*created);
                auto handle = assets->Request(*source);
                Check(handle.has_value(), "the own-drain asset request queues behind the gate");

                std::promise<void> destruction_started;
                std::promise<void> destroyed;
                auto started_future = destruction_started.get_future();
                auto destroyed_future = destroyed.get_future();
                std::jthread destroyer(
                    [owned = std::move(assets), &destruction_started, &destroyed]() mutable
                    {
                        destruction_started.set_value();
                        owned.reset();
                        destroyed.set_value();
                    });
                started_future.wait();
                Check(destroyed_future.wait_for(std::chrono::milliseconds(25)) ==
                          std::future_status::timeout,
                    "destruction waits while its own accepted asset job remains queued");
                blocker_release.count_down();
                const bool drained =
                    destroyed_future.wait_for(std::chrono::seconds(2)) ==
                    std::future_status::ready;
                Check(drained,
                    "destruction returns after its own accepted asset job finishes");
                if (!drained)
                    destroyed_future.wait();
                destroyer.join();
                if (handle)
                    Check(handle->expired(), "own-drain destruction expires its handle");
            }
            else
            {
                blocker_release.count_down();
            }
            jobs->WaitForIdle();
            std::atomic<std::uint64_t> probe{0U};
            Check(jobs->Submit([&probe] { probe.fetch_add(1U); }).has_value(),
                "the job service accepts work after destructor-owned draining");
            jobs->WaitForIdle();
            Check(probe.load() == 1U,
                "the job service executes work after destructor-owned draining");
        }
    }
}
} // namespace

int main()
{
    ContentFixture fixture("all-synthetic-cases");
    Check(fixture.ready(), "the synthetic AssetService fixture opens without owner data");
    if (fixture.ready())
    {
        CheckConfigurationAndNames(fixture);
        CheckQueuedBusyAndInFlight(fixture);
        CheckDuplicatesPoolReuseAndAccounting(fixture);
        CheckOwnedTopologyImageComposition(fixture);
        CheckFirstLevelTextureTopologyPreview(fixture);
        CheckHandleIdentityAndSourceValidation(fixture);
        CheckFailedReleaseAndPrivacy(fixture);
        CheckSubmissionRejectionRollback(fixture);
        CheckResidentBoundaries(fixture);
        CheckCapacityOneSequentialReuse(fixture);
        CheckWaitAndDestructionIsolation(fixture);
    }

    if (failures == 0)
        std::cout << "omega_asset_service_tests: all checks passed\n";
    return failures == 0 ? 0 : 1;
}
