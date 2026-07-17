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

    // [any thread after publication; immutable]
    [[nodiscard]] const HogHeader& header() const noexcept { return header_; }
    [[nodiscard]] std::span<const HogEntry> entries() const noexcept { return entries_; }
    [[nodiscard]] std::uint64_t archive_size() const noexcept { return archive_size_; }
    [[nodiscard]] const HogEntry* find(std::string_view name) const noexcept;

private:
    HogHeader header_;
    std::vector<HogEntry> entries_;
    std::uint64_t archive_size_ = 0;
};

class HogArchive final
{
public:
    // [any thread] Produces an independent immutable archive object using a bounded read.
    [[nodiscard]] static std::expected<HogArchive, std::string> Open(
        const std::filesystem::path& path,
        std::uint64_t maximum_bytes = kDefaultMaximumArchiveLoadBytes);

    // [any thread] Takes ownership of the supplied bytes and validates the complete directory.
    // Nested HOG spans may contain all-zero sector padding after their logical end; non-zero
    // trailing bytes are always rejected.
    [[nodiscard]] static std::expected<HogArchive, std::string> FromBytes(
        std::vector<std::byte> bytes);

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
    HogHeader header_;
    std::vector<HogEntry> entries_;
    std::vector<std::byte> storage_;
    std::uint64_t logical_size_ = 0;
};
} // namespace omega::archive
