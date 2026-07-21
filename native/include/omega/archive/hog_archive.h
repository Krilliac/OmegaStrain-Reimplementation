#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace omega::archive
{
inline constexpr std::uint64_t kDefaultMaximumArchiveLoadBytes = 512ULL * 1024ULL * 1024ULL;

struct HogFileRange
{
    std::uint64_t offset = 0;
    std::uint64_t size = 0;
};

// Caller-owned, synchronous random-access byte source. The callback and context
// are borrowed only for the duration of an Open/OpenRange call and are never
// retained. This lets a caller bind parsing to an already-open no-follow file
// handle instead of reopening an untrusted pathname.
struct HogReadSource
{
    using ReadExact = std::expected<void, std::string> (*)(
        void* context, std::uint64_t offset, std::span<std::byte> output);

    std::uint64_t size = 0;
    void* context = nullptr;
    ReadExact read_exact = nullptr;
};

struct HogHeader
{
    std::uint32_t tag = 0;
    std::uint32_t count = 0;
    std::uint32_t offsets_offset = 0;
    std::uint32_t names_offset = 0;
    std::uint32_t data_offset = 0;
};

struct HogEntry
{
    std::string name;
    std::uint64_t offset = 0;
    std::uint64_t size = 0;
};

class HogIndex final
{
public:
    // [any thread] Reads only a bounded directory prefix; payload bytes remain on disk.
    // The indexed top-level archive must end exactly at its terminal payload offset.
    [[nodiscard]] static std::expected<HogIndex, std::string> Open(
        const std::filesystem::path& path);

    // [any thread; caller-synchronized source] Indexes an exact top-level
    // archive through one identity-bound random-access source.
    [[nodiscard]] static std::expected<HogIndex, std::string> Open(
        const HogReadSource& source);

    // [any thread] Indexes one independently bounded nested archive span without loading its
    // payload. Entry offsets remain relative to the start of the nested span. Only an all-zero
    // tail between logical_size() and archive_size() is accepted.
    [[nodiscard]] static std::expected<HogIndex, std::string> OpenRange(
        const std::filesystem::path& path,
        HogFileRange range,
        std::uint64_t maximum_bytes = kDefaultMaximumArchiveLoadBytes);

    // [any thread; caller-synchronized source] Indexes one independently
    // bounded nested archive span through the same identity-bound source.
    [[nodiscard]] static std::expected<HogIndex, std::string> OpenRange(
        const HogReadSource& source,
        HogFileRange range,
        std::uint64_t maximum_bytes = kDefaultMaximumArchiveLoadBytes);

    // [any thread after publication; immutable]
    [[nodiscard]] const HogHeader& header() const noexcept { return header_; }
    [[nodiscard]] std::span<const HogEntry> entries() const noexcept { return entries_; }
    [[nodiscard]] std::uint64_t archive_size() const noexcept { return archive_size_; }
    [[nodiscard]] std::uint64_t logical_size() const noexcept { return logical_size_; }
    [[nodiscard]] std::uint64_t padding_size() const noexcept
    {
        return archive_size_ - logical_size_;
    }
    [[nodiscard]] const HogEntry* find(std::string_view name) const noexcept;

private:
    HogHeader header_;
    std::vector<HogEntry> entries_;
    std::uint64_t archive_size_ = 0;
    std::uint64_t logical_size_ = 0;
};

class HogArchive final
{
public:
    // [any thread] Produces an independent immutable top-level archive using a bounded read.
    // The file must end exactly at the archive's terminal payload offset.
    [[nodiscard]] static std::expected<HogArchive, std::string> Open(
        const std::filesystem::path& path,
        std::uint64_t maximum_bytes = kDefaultMaximumArchiveLoadBytes);

    // [any thread] Reads and owns exactly one bounded nested archive range. Bytes outside the
    // range are never read. Only an all-zero tail inside the range is accepted.
    [[nodiscard]] static std::expected<HogArchive, std::string> OpenRange(
        const std::filesystem::path& path,
        HogFileRange range,
        std::uint64_t maximum_bytes = kDefaultMaximumArchiveLoadBytes);

    // [any thread] Takes ownership of a complete top-level archive. Trailing bytes are rejected.
    [[nodiscard]] static std::expected<HogArchive, std::string> FromBytes(
        std::vector<std::byte> bytes);

    // [any thread] Copies and owns one caller-bounded nested archive span. An all-zero tail is
    // accepted; non-zero trailing bytes are rejected.
    [[nodiscard]] static std::expected<HogArchive, std::string> FromSpan(
        std::span<const std::byte> bytes,
        std::uint64_t maximum_bytes = kDefaultMaximumArchiveLoadBytes);

    // [any thread after publication; immutable]
    [[nodiscard]] const HogHeader& header() const noexcept { return header_; }
    [[nodiscard]] std::span<const HogEntry> entries() const noexcept { return entries_; }
    [[nodiscard]] std::span<const std::byte> bytes() const noexcept { return storage_; }
    [[nodiscard]] std::uint64_t logical_size() const noexcept { return logical_size_; }
    [[nodiscard]] std::uint64_t padding_size() const noexcept
    {
        return static_cast<std::uint64_t>(storage_.size()) - logical_size_;
    }
    [[nodiscard]] std::span<const std::byte> payload(const HogEntry& entry) const noexcept;
    [[nodiscard]] const HogEntry* find(std::string_view name) const noexcept;

private:
    enum class TrailingDataPolicy
    {
        ExactEnd,
        ZeroPadding,
    };

    [[nodiscard]] static std::expected<HogArchive, std::string> FromOwnedBytes(
        std::vector<std::byte> bytes,
        TrailingDataPolicy trailing_policy);

    HogHeader header_;
    std::vector<HogEntry> entries_;
    std::vector<std::byte> storage_;
    std::uint64_t logical_size_ = 0;
};
} // namespace omega::archive
