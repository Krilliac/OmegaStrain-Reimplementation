#include "omega/vfs/virtual_file_system.h"

#include "omega/archive/hog_archive.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <limits>
#include <unordered_map>
#include <utility>

namespace omega::vfs
{
namespace
{
constexpr std::size_t kMaximumGamePathLength = 4096;
constexpr std::size_t kMaximumDirectoryMountEntries = 1U << 20U;

[[nodiscard]] std::expected<std::vector<std::byte>, std::string> ReadRange(
    const std::filesystem::path& path, const std::uint64_t offset, const std::uint64_t size,
    const std::uint64_t maximum_bytes)
{
    if (size > maximum_bytes)
        return std::unexpected("file range exceeds caller's read limit: " + path.string());
    if (size > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()) ||
        offset > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max()) ||
        size > static_cast<std::uint64_t>(std::numeric_limits<std::streamsize>::max()))
        return std::unexpected("file range is too large for this process: " + path.string());

    std::ifstream stream(path, std::ios::binary);
    if (!stream)
        return std::unexpected("unable to open file: " + path.string());
    stream.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!stream)
        return std::unexpected("unable to seek file: " + path.string());

    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    if (!bytes.empty() && !stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
        return std::unexpected("unable to read complete file range: " + path.string());
    return bytes;
}

class Mount
{
public:
    virtual ~Mount() = default;
    [[nodiscard]] virtual bool Contains(std::string_view normalized_path) const = 0;
    [[nodiscard]] virtual std::expected<std::vector<std::byte>, std::string> Read(
        std::string_view normalized_path, std::uint64_t maximum_bytes) const = 0;
};

class DirectoryMount final : public Mount
{
public:
    [[nodiscard]] static std::expected<std::unique_ptr<DirectoryMount>, std::string> Create(
        const std::filesystem::path& root)
    {
        std::error_code error;
        const std::filesystem::path canonical_root = std::filesystem::weakly_canonical(root, error);
        const bool is_directory = !error && std::filesystem::is_directory(canonical_root, error);
        if (error || !is_directory)
            return std::unexpected("mount root is not a readable directory: " + root.string());

        auto mount = std::unique_ptr<DirectoryMount>(new DirectoryMount(canonical_root));
        for (std::filesystem::recursive_directory_iterator iterator(canonical_root, error), end;
             iterator != end && !error; iterator.increment(error))
        {
            std::error_code entry_error;
            const auto status = iterator->symlink_status(entry_error);
            if (entry_error)
                return std::unexpected("unable to inspect mounted entry: " + entry_error.message());
            if (std::filesystem::is_symlink(status))
                return std::unexpected("symbolic links are not supported in directory mounts: " +
                                       iterator->path().string());
            if (!std::filesystem::is_regular_file(status))
                continue;
            const auto relative = std::filesystem::relative(iterator->path(), canonical_root, error);
            if (error)
                break;
            auto normalized = NormalizeGamePath(relative.generic_string());
            if (!normalized)
                return std::unexpected("unable to normalize mounted path: " + relative.string());
            if (mount->files_.size() >= kMaximumDirectoryMountEntries)
                return std::unexpected("directory mount exceeds entry safety limit");
            if (!mount->files_.emplace(std::move(*normalized), iterator->path()).second)
                return std::unexpected("directory contains duplicate case-insensitive game path: " +
                                       relative.string());
        }
        if (error)
            return std::unexpected("unable to enumerate mount root: " + error.message());
        return mount;
    }

    bool Contains(const std::string_view normalized_path) const override
    {
        return files_.contains(std::string(normalized_path));
    }

    std::expected<std::vector<std::byte>, std::string> Read(
        const std::string_view normalized_path, const std::uint64_t maximum_bytes) const override
    {
        const auto iterator = files_.find(std::string(normalized_path));
        if (iterator == files_.end())
            return std::unexpected("path is not present in directory mount");

        std::error_code error;
        const auto canonical_path = std::filesystem::weakly_canonical(iterator->second, error);
        if (error || !IsWithinRoot(canonical_path))
            return std::unexpected("mounted file no longer resolves within its root: " +
                                   iterator->second.string());
        const auto size = std::filesystem::file_size(canonical_path, error);
        if (error)
            return std::unexpected("unable to determine file size: " + canonical_path.string());
        return ReadRange(canonical_path, 0, size, maximum_bytes);
    }

private:
    explicit DirectoryMount(std::filesystem::path root) : root_(std::move(root)) {}

    [[nodiscard]] bool IsWithinRoot(const std::filesystem::path& path) const
    {
        auto root_part = root_.begin();
        auto path_part = path.begin();
        for (; root_part != root_.end(); ++root_part, ++path_part)
        {
            if (path_part == path.end() || *root_part != *path_part)
                return false;
        }
        return true;
    }

    std::filesystem::path root_;
    std::unordered_map<std::string, std::filesystem::path> files_;
};

class HogMount final : public Mount
{
public:
    [[nodiscard]] static std::expected<std::unique_ptr<HogMount>, std::string> Create(
        const std::string_view virtual_root, const std::filesystem::path& archive_path)
    {
        auto root = NormalizeGamePath(virtual_root);
        if (!root)
            return std::unexpected("invalid HOG virtual root: " + root.error());

        auto index = archive::HogIndex::Open(archive_path);
        if (!index)
            return std::unexpected(index.error());

        auto mount = std::unique_ptr<HogMount>(new HogMount(archive_path));
        for (const auto& entry : index->entries())
        {
            auto entry_path = NormalizeGamePath(entry.name);
            if (!entry_path)
                return std::unexpected("invalid HOG entry path: " + entry.name);
            if (root->size() + 1U + entry_path->size() > kMaximumGamePathLength)
                return std::unexpected("combined HOG virtual path exceeds safety limit");
            const std::string full_path = *root + "/" + *entry_path;
            if (!mount->entries_.emplace(full_path, entry).second)
                return std::unexpected("duplicate normalized HOG entry: " + full_path);
        }
        return mount;
    }

    bool Contains(const std::string_view normalized_path) const override
    {
        return entries_.contains(std::string(normalized_path));
    }

    std::expected<std::vector<std::byte>, std::string> Read(
        const std::string_view normalized_path, const std::uint64_t maximum_bytes) const override
    {
        const auto iterator = entries_.find(std::string(normalized_path));
        if (iterator == entries_.end())
            return std::unexpected("path is not present in HOG mount");
        return ReadRange(archive_path_, iterator->second.offset, iterator->second.size, maximum_bytes);
    }

private:
    explicit HogMount(std::filesystem::path archive_path) : archive_path_(std::move(archive_path)) {}

    std::filesystem::path archive_path_;
    std::unordered_map<std::string, archive::HogEntry> entries_;
};
} // namespace

struct VirtualFileSystem::Impl
{
    std::vector<std::unique_ptr<Mount>> mounts;
    bool frozen = false;
};

std::expected<std::string, std::string> NormalizeGamePath(const std::string_view path)
{
    if (path.empty())
        return std::unexpected("path is empty");
    if (path.size() > kMaximumGamePathLength)
        return std::unexpected("game path exceeds safety limit");
    if (path.front() == '/' || path.front() == '\\' ||
        (path.size() >= 2 && std::isalpha(static_cast<unsigned char>(path[0])) && path[1] == ':'))
        return std::unexpected("absolute game paths are not allowed");

    std::string result;
    result.reserve(path.size());
    std::string component;
    const auto flush = [&result, &component]() -> bool {
        if (component.empty())
            return true;
        if (component == "." || component == "..")
            return false;
        if (!result.empty())
            result.push_back('/');
        result += component;
        component.clear();
        return true;
    };

    for (const unsigned char value : path)
    {
        if (value == '/' || value == '\\')
        {
            if (!flush())
                return std::unexpected("relative path components are not allowed");
            continue;
        }
        if (value < 0x20U || value > 0x7EU)
            return std::unexpected("game path is not printable ASCII");
        component.push_back(static_cast<char>(std::toupper(value)));
    }
    if (!flush())
        return std::unexpected("relative path components are not allowed");
    if (result.empty())
        return std::unexpected("path has no usable components");
    return result;
}

VirtualFileSystem::VirtualFileSystem() : impl_(std::make_unique<Impl>()) {}
VirtualFileSystem::~VirtualFileSystem() = default;
VirtualFileSystem::VirtualFileSystem(VirtualFileSystem&&) noexcept = default;
VirtualFileSystem& VirtualFileSystem::operator=(VirtualFileSystem&&) noexcept = default;

std::expected<void, std::string> VirtualFileSystem::MountDirectory(const std::filesystem::path& root)
{
    if (!impl_)
        return std::unexpected("cannot use a moved-from VFS");
    if (impl_->frozen)
        return std::unexpected("cannot add a mount after the VFS is frozen");
    auto mount = DirectoryMount::Create(root);
    if (!mount)
        return std::unexpected(mount.error());
    impl_->mounts.push_back(std::move(*mount));
    return {};
}

std::expected<void, std::string> VirtualFileSystem::MountHog(
    const std::string_view virtual_root, const std::filesystem::path& archive_path)
{
    if (!impl_)
        return std::unexpected("cannot use a moved-from VFS");
    if (impl_->frozen)
        return std::unexpected("cannot add a mount after the VFS is frozen");
    auto mount = HogMount::Create(virtual_root, archive_path);
    if (!mount)
        return std::unexpected(mount.error());
    impl_->mounts.push_back(std::move(*mount));
    return {};
}

void VirtualFileSystem::Freeze() noexcept
{
    if (impl_)
        impl_->frozen = true;
}

bool VirtualFileSystem::frozen() const noexcept
{
    return impl_ && impl_->frozen;
}

std::expected<std::vector<std::byte>, std::string> VirtualFileSystem::Read(
    const std::string_view game_path, const std::uint64_t maximum_bytes) const
{
    if (!impl_)
        return std::unexpected("cannot use a moved-from VFS");
    if (!impl_->frozen)
        return std::unexpected("VFS reads require Freeze() after all mounts are configured");
    auto normalized = NormalizeGamePath(game_path);
    if (!normalized)
        return std::unexpected(normalized.error());

    for (auto iterator = impl_->mounts.rbegin(); iterator != impl_->mounts.rend(); ++iterator)
    {
        if ((*iterator)->Contains(*normalized))
            return (*iterator)->Read(*normalized, maximum_bytes);
    }
    return std::unexpected("game path was not found: " + *normalized);
}

bool VirtualFileSystem::Contains(const std::string_view game_path) const
{
    if (!impl_)
        return false;
    if (!impl_->frozen)
        return false;
    auto normalized = NormalizeGamePath(game_path);
    if (!normalized)
        return false;
    return std::ranges::any_of(impl_->mounts,
        [&normalized](const auto& mount) { return mount->Contains(*normalized); });
}
} // namespace omega::vfs
