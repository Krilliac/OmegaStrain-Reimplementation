#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace omega::vfs
{
inline constexpr std::uint64_t kDefaultMaximumReadBytes = 512ULL * 1024ULL * 1024ULL;

// Game paths are ASCII, slash-separated, relative, and case-insensitive.
[[nodiscard]] std::expected<std::string, std::string> NormalizeGamePath(std::string_view path);

class VirtualFileSystem final
{
public:
    // [game thread] Owns all mounts exclusively; non-hot-reloadable after Freeze().
    VirtualFileSystem();
    // [game thread, after all readers have joined]
    ~VirtualFileSystem();
    // [game thread, before Freeze()] The moved-from object may only be destroyed or assigned.
    VirtualFileSystem(VirtualFileSystem&&) noexcept;
    VirtualFileSystem& operator=(VirtualFileSystem&&) noexcept;
    VirtualFileSystem(const VirtualFileSystem&) = delete;
    VirtualFileSystem& operator=(const VirtualFileSystem&) = delete;

    // [game thread, before mount freeze] Newer mounts override older mounts.
    [[nodiscard]] std::expected<void, std::string> MountDirectory(
        const std::filesystem::path& root);

    // [game thread, before mount freeze] Exposes archive entries below virtual_root.
    [[nodiscard]] std::expected<void, std::string> MountHog(
        std::string_view virtual_root, const std::filesystem::path& archive_path);

    // [game thread] Publishes the immutable mount table. Idempotent.
    void Freeze() noexcept;
    [[nodiscard]] bool frozen() const noexcept;

    // [any thread after Freeze(); thread-safe] Reads an independently owned, bounded payload.
    [[nodiscard]] std::expected<std::vector<std::byte>, std::string> Read(
        std::string_view game_path,
        std::uint64_t maximum_bytes = kDefaultMaximumReadBytes) const;

    // [any thread after Freeze(); thread-safe]
    [[nodiscard]] bool Contains(std::string_view game_path) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace omega::vfs
