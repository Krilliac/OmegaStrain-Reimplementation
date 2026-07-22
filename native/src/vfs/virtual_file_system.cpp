#include "omega/vfs/virtual_file_system.h"

#include "omega/archive/hog_archive.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <exception>
#include <fstream>
#include <limits>
#include <new>
#include <span>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
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

constexpr std::uint64_t kIso9660SectorBytes = 2048;
constexpr std::uint64_t kIso9660FirstDescriptorSector = 16;
constexpr std::size_t kMaximumIso9660Descriptors = 64;
constexpr std::size_t kMaximumIso9660Records = 131'072;
constexpr std::size_t kMaximumIso9660Directories = 32'768;
constexpr std::uint32_t kMaximumIso9660Depth = 32;
constexpr std::uint64_t kMaximumIso9660DirectoryBytes = 16ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kMaximumIso9660CumulativeDirectoryBytes = 64ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kMaximumIso9660NameBytes = 16ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kMaximumIso9660IndexBytes = 64ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kIso9660FileIndexOverheadBytes = 64;
constexpr std::uint64_t kIso9660DirectoryIndexOverheadBytes = 96;

[[nodiscard]] bool CheckedAdd(const std::uint64_t left, const std::uint64_t right,
    std::uint64_t& result) noexcept
{
    if (right > std::numeric_limits<std::uint64_t>::max() - left)
        return false;
    result = left + right;
    return true;
}

[[nodiscard]] bool CheckedMultiply(const std::uint64_t left, const std::uint64_t right,
    std::uint64_t& result) noexcept
{
    if (left != 0 && right > std::numeric_limits<std::uint64_t>::max() / left)
        return false;
    result = left * right;
    return true;
}

[[nodiscard]] std::uint8_t ByteValue(const std::byte value) noexcept
{
    return std::to_integer<std::uint8_t>(value);
}

[[nodiscard]] std::expected<std::uint16_t, std::string> ReadBothEndianU16(
    const std::span<const std::byte> bytes, const std::size_t offset, const std::string_view field)
{
    if (offset > bytes.size() || bytes.size() - offset < 4)
        return std::unexpected("ISO9660 " + std::string(field) + " field is truncated");
    const std::uint16_t little = static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(ByteValue(bytes[offset])) |
        static_cast<std::uint16_t>(ByteValue(bytes[offset + 1])) << 8U);
    const std::uint16_t big = static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(ByteValue(bytes[offset + 2])) << 8U |
        static_cast<std::uint16_t>(ByteValue(bytes[offset + 3])));
    if (little != big)
        return std::unexpected("ISO9660 " + std::string(field) + " endian copies disagree");
    return little;
}

[[nodiscard]] std::expected<std::uint32_t, std::string> ReadBothEndianU32(
    const std::span<const std::byte> bytes, const std::size_t offset, const std::string_view field)
{
    if (offset > bytes.size() || bytes.size() - offset < 8)
        return std::unexpected("ISO9660 " + std::string(field) + " field is truncated");
    const std::uint32_t little = static_cast<std::uint32_t>(ByteValue(bytes[offset])) |
                                 static_cast<std::uint32_t>(ByteValue(bytes[offset + 1])) << 8U |
                                 static_cast<std::uint32_t>(ByteValue(bytes[offset + 2])) << 16U |
                                 static_cast<std::uint32_t>(ByteValue(bytes[offset + 3])) << 24U;
    const std::uint32_t big = static_cast<std::uint32_t>(ByteValue(bytes[offset + 4])) << 24U |
                              static_cast<std::uint32_t>(ByteValue(bytes[offset + 5])) << 16U |
                              static_cast<std::uint32_t>(ByteValue(bytes[offset + 6])) << 8U |
                              static_cast<std::uint32_t>(ByteValue(bytes[offset + 7]));
    if (little != big)
        return std::unexpected("ISO9660 " + std::string(field) + " endian copies disagree");
    return little;
}

struct Iso9660ImageIdentity
{
    std::uint64_t size = 0;
    std::filesystem::file_time_type last_write_time{};
};

[[nodiscard]] std::expected<Iso9660ImageIdentity, std::string> QueryIso9660ImageIdentity(
    const std::filesystem::path& image_path)
{
    std::error_code error;
    const auto status = std::filesystem::symlink_status(image_path, error);
    if (error || std::filesystem::is_symlink(status) || !std::filesystem::is_regular_file(status))
        return std::unexpected("ISO9660 source is not a readable regular file");
    const auto size = std::filesystem::file_size(image_path, error);
    if (error)
        return std::unexpected("unable to determine ISO9660 image size");
    const auto last_write_time = std::filesystem::last_write_time(image_path, error);
    if (error)
        return std::unexpected("unable to determine ISO9660 image identity");
    return Iso9660ImageIdentity{.size = size, .last_write_time = last_write_time};
}

[[nodiscard]] bool SameIso9660ImageIdentity(
    const Iso9660ImageIdentity& left, const Iso9660ImageIdentity& right) noexcept
{
    return left.size == right.size && left.last_write_time == right.last_write_time;
}

[[nodiscard]] std::expected<void, std::string> ReadIso9660Bytes(std::ifstream& stream,
    const std::uint64_t offset, const std::span<std::byte> output)
{
    if (offset > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max()) ||
        output.size() > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max()))
        return std::unexpected("ISO9660 byte range is too large for this process");
    stream.clear();
    stream.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!stream)
        return std::unexpected("unable to seek within ISO9660 image");
    if (!output.empty() &&
        !stream.read(reinterpret_cast<char*>(output.data()),
            static_cast<std::streamsize>(output.size())))
        return std::unexpected("unable to read complete ISO9660 byte range");
    return {};
}

struct Iso9660DirectoryRecord
{
    std::uint32_t extent_sector = 0;
    std::uint32_t data_length = 0;
    std::uint8_t flags = 0;
    std::span<const std::byte> identifier;

    [[nodiscard]] bool is_directory() const noexcept { return (flags & 0x02U) != 0; }
};

[[nodiscard]] std::expected<Iso9660DirectoryRecord, std::string> ParseIso9660DirectoryRecord(
    const std::span<const std::byte> record)
{
    if (record.size() < 34 || ByteValue(record.front()) != record.size())
        return std::unexpected("ISO9660 directory record has an invalid length");
    if (ByteValue(record[1]) != 0)
        return std::unexpected("ISO9660 extended-attribute records are unsupported");

    auto extent = ReadBothEndianU32(record, 2, "extent");
    if (!extent)
        return std::unexpected(extent.error());
    auto data_length = ReadBothEndianU32(record, 10, "data-length");
    if (!data_length)
        return std::unexpected(data_length.error());
    if (ByteValue(record[26]) != 0 || ByteValue(record[27]) != 0)
        return std::unexpected("ISO9660 interleaved files are unsupported");
    auto volume_sequence = ReadBothEndianU16(record, 28, "volume-sequence");
    if (!volume_sequence)
        return std::unexpected(volume_sequence.error());
    if (*volume_sequence != 1)
        return std::unexpected("ISO9660 directory record uses an unsupported volume sequence");

    const std::size_t identifier_size = ByteValue(record[32]);
    if (identifier_size == 0 || identifier_size > record.size() - 33)
        return std::unexpected("ISO9660 directory identifier is truncated");
    const std::size_t minimum_size = 33 + identifier_size + (identifier_size % 2U == 0 ? 1U : 0U);
    if (minimum_size > record.size())
        return std::unexpected("ISO9660 directory record padding is truncated");
    if (identifier_size % 2U == 0 && ByteValue(record[33 + identifier_size]) != 0)
        return std::unexpected("ISO9660 directory record padding is malformed");

    const std::uint8_t flags = ByteValue(record[25]);
    if ((flags & 0x80U) != 0)
        return std::unexpected("ISO9660 multi-extent files are unsupported");
    return Iso9660DirectoryRecord{
        .extent_sector = *extent,
        .data_length = *data_length,
        .flags = flags,
        .identifier = record.subspan(33, identifier_size),
    };
}

[[nodiscard]] bool IsSpecialIso9660Identifier(const std::span<const std::byte> identifier) noexcept
{
    return identifier.size() == 1 && (ByteValue(identifier.front()) == 0 ||
                                         ByteValue(identifier.front()) == 1);
}

[[nodiscard]] std::expected<std::string, std::string> NormalizeIso9660Identifier(
    const std::span<const std::byte> identifier, const bool is_directory)
{
    std::string component;
    component.reserve(identifier.size());
    for (const std::byte raw : identifier)
    {
        const std::uint8_t value = ByteValue(raw);
        if (value < 0x20U || value > 0x7EU || value == '/' || value == '\\')
            return std::unexpected("ISO9660 identifier is not a supported ASCII game path component");
        component.push_back(static_cast<char>(value));
    }
    if (!is_directory && component.size() > 2 && component.ends_with(";1"))
        component.resize(component.size() - 2);
    auto normalized = NormalizeGamePath(component);
    if (!normalized || normalized->find('/') != std::string::npos)
        return std::unexpected("ISO9660 identifier is not a valid game path component");
    return normalized;
}

[[nodiscard]] std::expected<std::string, std::string> JoinIso9660Path(
    const std::string_view parent, const std::string_view component)
{
    std::uint64_t combined_size = component.size();
    if (!parent.empty())
    {
        if (!CheckedAdd(parent.size(), 1, combined_size) ||
            !CheckedAdd(combined_size, component.size(), combined_size))
            return std::unexpected("ISO9660 game path length overflows");
    }
    if (combined_size > kMaximumGamePathLength)
        return std::unexpected("ISO9660 game path exceeds safety limit");
    if (parent.empty())
        return std::string(component);
    std::string result;
    result.reserve(static_cast<std::size_t>(combined_size));
    result.append(parent);
    result.push_back('/');
    result.append(component);
    return result;
}

[[nodiscard]] std::expected<std::uint64_t, std::string> Iso9660ExtentOffset(
    const std::uint32_t sector)
{
    std::uint64_t offset = 0;
    if (!CheckedMultiply(sector, kIso9660SectorBytes, offset))
        return std::unexpected("ISO9660 extent offset overflows");
    return offset;
}

[[nodiscard]] std::expected<std::uint64_t, std::string> ValidateIso9660Extent(
    const std::uint32_t sector, const std::uint32_t size, const std::uint64_t volume_bytes,
    const std::uint64_t image_bytes)
{
    auto offset = Iso9660ExtentOffset(sector);
    if (!offset)
        return std::unexpected(offset.error());
    std::uint64_t end = 0;
    if (!CheckedAdd(*offset, size, end) || end > volume_bytes || end > image_bytes)
        return std::unexpected("ISO9660 extent is outside the declared image range");
    return *offset;
}

struct Iso9660PrimaryVolume
{
    std::uint64_t volume_bytes = 0;
    Iso9660DirectoryRecord root;
};

[[nodiscard]] std::expected<Iso9660PrimaryVolume, std::string> ReadIso9660PrimaryVolume(
    std::ifstream& stream, const std::uint64_t image_bytes)
{
    std::array<std::byte, kIso9660SectorBytes> descriptor{};
    bool saw_primary = false;
    bool saw_terminator = false;
    std::uint64_t descriptor_set_end = 0;
    Iso9660PrimaryVolume primary;

    for (std::size_t descriptor_index = 0;
         descriptor_index < kMaximumIso9660Descriptors; ++descriptor_index)
    {
        std::uint64_t sector = 0;
        if (!CheckedAdd(kIso9660FirstDescriptorSector, descriptor_index, sector))
            return std::unexpected("ISO9660 descriptor sector overflows");
        auto offset = Iso9660ExtentOffset(static_cast<std::uint32_t>(sector));
        if (!offset)
            return std::unexpected(offset.error());
        std::uint64_t descriptor_end = 0;
        if (!CheckedAdd(*offset, descriptor.size(), descriptor_end) || descriptor_end > image_bytes)
            return std::unexpected("ISO9660 volume descriptor set is truncated");
        auto read = ReadIso9660Bytes(stream, *offset, descriptor);
        if (!read)
            return std::unexpected(read.error());

        const std::span<const std::byte> bytes(descriptor);
        constexpr std::array<std::byte, 5> signature{
            std::byte{'C'}, std::byte{'D'}, std::byte{'0'}, std::byte{'0'}, std::byte{'1'}};
        if (!std::ranges::equal(bytes.subspan(1, signature.size()), signature) ||
            ByteValue(bytes[6]) != 1)
            return std::unexpected("ISO9660 volume descriptor signature or version is invalid");

        const std::uint8_t type = ByteValue(bytes[0]);
        if (type == 255)
        {
            saw_terminator = true;
            descriptor_set_end = descriptor_end;
            break;
        }
        if (type > 3)
            return std::unexpected("ISO9660 volume descriptor type is unsupported");
        if (type != 1)
            continue;
        if (saw_primary)
            return std::unexpected("ISO9660 image contains duplicate primary volume descriptors");

        auto volume_sectors = ReadBothEndianU32(bytes, 80, "volume-space-size");
        if (!volume_sectors)
            return std::unexpected(volume_sectors.error());
        if (*volume_sectors == 0)
            return std::unexpected("ISO9660 volume is empty");
        auto logical_block_size = ReadBothEndianU16(bytes, 128, "logical-block-size");
        if (!logical_block_size)
            return std::unexpected(logical_block_size.error());
        if (*logical_block_size != kIso9660SectorBytes)
            return std::unexpected("ISO9660 logical block size is not 2048 bytes");
        auto volume_set_size = ReadBothEndianU16(bytes, 120, "volume-set-size");
        auto volume_sequence = ReadBothEndianU16(bytes, 124, "volume-sequence-number");
        if (!volume_set_size)
            return std::unexpected(volume_set_size.error());
        if (!volume_sequence)
            return std::unexpected(volume_sequence.error());
        if (*volume_set_size != 1 || *volume_sequence != 1)
            return std::unexpected("ISO9660 multi-volume sets are unsupported");
        if (!CheckedMultiply(*volume_sectors, kIso9660SectorBytes, primary.volume_bytes) ||
            primary.volume_bytes > image_bytes)
            return std::unexpected("ISO9660 declared volume exceeds the image range");

        if (ByteValue(bytes[881]) != 1)
            return std::unexpected("ISO9660 file structure version is invalid");
        constexpr std::size_t root_length = 34;
        if (ByteValue(bytes[156]) != root_length)
            return std::unexpected("ISO9660 root directory record is malformed");
        auto root = ParseIso9660DirectoryRecord(bytes.subspan(156, root_length));
        if (!root)
            return std::unexpected(root.error());
        if (!root->is_directory() || !IsSpecialIso9660Identifier(root->identifier) ||
            ByteValue(root->identifier.front()) != 0 || root->data_length == 0)
            return std::unexpected("ISO9660 root directory record is invalid");
        primary.root = Iso9660DirectoryRecord{
            .extent_sector = root->extent_sector,
            .data_length = root->data_length,
            .flags = root->flags,
            .identifier = {},
        };
        saw_primary = true;
    }
    if (!saw_primary)
        return std::unexpected("ISO9660 primary volume descriptor is missing");
    if (!saw_terminator)
        return std::unexpected("ISO9660 volume descriptor terminator is missing");
    if (descriptor_set_end > primary.volume_bytes)
        return std::unexpected("ISO9660 descriptor set exceeds the declared volume");
    auto root_range = ValidateIso9660Extent(primary.root.extent_sector,
        primary.root.data_length, primary.volume_bytes, image_bytes);
    if (!root_range)
        return std::unexpected(root_range.error());
    return primary;
}

struct Iso9660Entry
{
    std::uint64_t offset = 0;
    std::uint64_t size = 0;
};

struct Iso9660DirectoryTask
{
    std::string path;
    std::uint32_t extent_sector = 0;
    std::uint32_t data_length = 0;
    std::uint32_t parent_extent_sector = 0;
    std::uint32_t parent_data_length = 0;
    std::uint32_t depth = 0;
};

[[nodiscard]] std::expected<std::unordered_map<std::string, Iso9660Entry>, std::string>
BuildIso9660Index(std::ifstream& stream, const Iso9660PrimaryVolume& primary,
    const std::uint64_t image_bytes)
{
    std::unordered_map<std::string, Iso9660Entry> files;
    std::unordered_set<std::string> directory_paths;
    std::unordered_set<std::uint32_t> directory_extents;
    std::vector<Iso9660DirectoryTask> directories;

    if (primary.root.data_length > kMaximumIso9660DirectoryBytes)
        return std::unexpected("ISO9660 directory exceeds the per-directory safety budget");
    directories.push_back(Iso9660DirectoryTask{
        .path = {},
        .extent_sector = primary.root.extent_sector,
        .data_length = primary.root.data_length,
        .parent_extent_sector = primary.root.extent_sector,
        .parent_data_length = primary.root.data_length,
        .depth = 0,
    });
    directory_extents.insert(primary.root.extent_sector);

    std::uint64_t cumulative_directory_bytes = primary.root.data_length;
    std::uint64_t cumulative_name_bytes = 0;
    std::uint64_t estimated_index_bytes = kIso9660DirectoryIndexOverheadBytes;
    std::size_t record_count = 0;
    std::array<std::byte, kIso9660SectorBytes> block{};

    for (std::size_t directory_index = 0; directory_index < directories.size(); ++directory_index)
    {
        const Iso9660DirectoryTask directory = directories[directory_index];
        auto directory_offset = ValidateIso9660Extent(directory.extent_sector,
            directory.data_length, primary.volume_bytes, image_bytes);
        if (!directory_offset)
            return std::unexpected(directory_offset.error());

        std::uint64_t consumed = 0;
        std::size_t directory_record_ordinal = 0;
        while (consumed < directory.data_length)
        {
            const std::uint64_t remaining = directory.data_length - consumed;
            const std::size_t block_bytes = static_cast<std::size_t>(
                std::min<std::uint64_t>(remaining, kIso9660SectorBytes));
            std::uint64_t read_offset = 0;
            if (!CheckedAdd(*directory_offset, consumed, read_offset))
                return std::unexpected("ISO9660 directory read offset overflows");
            auto read = ReadIso9660Bytes(stream, read_offset,
                std::span<std::byte>(block).first(block_bytes));
            if (!read)
                return std::unexpected(read.error());

            std::size_t position = 0;
            while (position < block_bytes)
            {
                const std::size_t record_length = ByteValue(block[position]);
                if (record_length == 0)
                {
                    const auto padding = std::span<const std::byte>(block).subspan(
                        position, block_bytes - position);
                    if (!std::ranges::all_of(padding,
                            [](const std::byte value) { return value == std::byte{0}; }))
                        return std::unexpected("ISO9660 directory padding is malformed");
                    break;
                }
                if (record_length > block_bytes - position)
                    return std::unexpected("ISO9660 directory record crosses a logical block");
                if (++record_count > kMaximumIso9660Records)
                    return std::unexpected("ISO9660 directory record count exceeds safety budget");

                auto record = ParseIso9660DirectoryRecord(
                    std::span<const std::byte>(block).subspan(position, record_length));
                if (!record)
                    return std::unexpected(record.error());
                auto record_offset = ValidateIso9660Extent(record->extent_sector,
                    record->data_length, primary.volume_bytes, image_bytes);
                if (!record_offset)
                    return std::unexpected(record_offset.error());
                if (IsSpecialIso9660Identifier(record->identifier))
                {
                    if (!record->is_directory())
                        return std::unexpected("ISO9660 special directory entry is not a directory");
                    if (directory_record_ordinal > 1 ||
                        ByteValue(record->identifier.front()) != directory_record_ordinal)
                        return std::unexpected("ISO9660 special directory entries are out of order");
                    const bool is_current_directory = directory_record_ordinal == 0;
                    const std::uint32_t expected_extent = is_current_directory
                                                              ? directory.extent_sector
                                                              : directory.parent_extent_sector;
                    const std::uint32_t expected_length = is_current_directory
                                                              ? directory.data_length
                                                              : directory.parent_data_length;
                    if (record->extent_sector != expected_extent ||
                        record->data_length != expected_length)
                        return std::unexpected(
                            "ISO9660 special directory entry does not match its hierarchy");
                    ++directory_record_ordinal;
                    position += record_length;
                    continue;
                }
                if (directory_record_ordinal < 2)
                    return std::unexpected("ISO9660 directory is missing its special entries");

                std::uint64_t next_name_bytes = 0;
                if (!CheckedAdd(cumulative_name_bytes, record->identifier.size(), next_name_bytes) ||
                    next_name_bytes > kMaximumIso9660NameBytes)
                    return std::unexpected("ISO9660 identifier bytes exceed safety budget");
                cumulative_name_bytes = next_name_bytes;
                auto component = NormalizeIso9660Identifier(
                    record->identifier, record->is_directory());
                if (!component)
                    return std::unexpected(component.error());
                auto path = JoinIso9660Path(directory.path, *component);
                if (!path)
                    return std::unexpected(path.error());

                if (files.contains(*path) || directory_paths.contains(*path))
                    return std::unexpected("ISO9660 image contains a duplicate normalized game path");

                if (record->is_directory())
                {
                    if (record->data_length == 0 ||
                        record->data_length > kMaximumIso9660DirectoryBytes)
                        return std::unexpected(
                            "ISO9660 directory exceeds the per-directory safety budget");
                    if (directory.depth >= kMaximumIso9660Depth)
                        return std::unexpected("ISO9660 directory nesting exceeds safety budget");
                    if (directories.size() >= kMaximumIso9660Directories)
                        return std::unexpected("ISO9660 directory count exceeds safety budget");
                    std::uint64_t next_directory_bytes = 0;
                    if (!CheckedAdd(cumulative_directory_bytes, record->data_length,
                            next_directory_bytes) ||
                        next_directory_bytes > kMaximumIso9660CumulativeDirectoryBytes)
                        return std::unexpected(
                            "ISO9660 directory bytes exceed cumulative safety budget");
                    if (!directory_extents.insert(record->extent_sector).second)
                        return std::unexpected("ISO9660 directory cycle or extent alias was detected");

                    std::uint64_t next_index_bytes = 0;
                    std::uint64_t path_storage_bytes = 0;
                    if (!CheckedMultiply(path->size(), 2, path_storage_bytes) ||
                        !CheckedAdd(path_storage_bytes, kIso9660DirectoryIndexOverheadBytes,
                            path_storage_bytes) ||
                        !CheckedAdd(estimated_index_bytes, path_storage_bytes, next_index_bytes) ||
                        next_index_bytes > kMaximumIso9660IndexBytes)
                        return std::unexpected("ISO9660 index exceeds memory safety budget");

                    cumulative_directory_bytes = next_directory_bytes;
                    estimated_index_bytes = next_index_bytes;
                    directory_paths.insert(*path);
                    directories.push_back(Iso9660DirectoryTask{
                        .path = std::move(*path),
                        .extent_sector = record->extent_sector,
                        .data_length = record->data_length,
                        .parent_extent_sector = directory.extent_sector,
                        .parent_data_length = directory.data_length,
                        .depth = directory.depth + 1,
                    });
                }
                else
                {
                    std::uint64_t entry_bytes = 0;
                    std::uint64_t next_index_bytes = 0;
                    if (!CheckedAdd(path->size(), kIso9660FileIndexOverheadBytes, entry_bytes) ||
                        !CheckedAdd(estimated_index_bytes, entry_bytes, next_index_bytes) ||
                        next_index_bytes > kMaximumIso9660IndexBytes)
                        return std::unexpected("ISO9660 index exceeds memory safety budget");
                    estimated_index_bytes = next_index_bytes;
                    files.emplace(std::move(*path), Iso9660Entry{
                        .offset = *record_offset,
                        .size = record->data_length,
                    });
                }
                ++directory_record_ordinal;
                position += record_length;
            }
            consumed += block_bytes;
        }
        if (directory_record_ordinal < 2)
            return std::unexpected("ISO9660 directory is missing its special entries");
    }
    return files;
}

class Iso9660Mount final : public Mount
{
public:
    [[nodiscard]] static std::expected<std::unique_ptr<Iso9660Mount>, std::string> Create(
        const std::filesystem::path& image_path)
    {
        try
        {
            auto source_identity = QueryIso9660ImageIdentity(image_path);
            if (!source_identity)
                return std::unexpected(source_identity.error());
            auto canonical_path = std::filesystem::weakly_canonical(image_path);
            auto identity = QueryIso9660ImageIdentity(canonical_path);
            if (!identity)
                return std::unexpected(identity.error());
            if (!SameIso9660ImageIdentity(*source_identity, *identity))
                return std::unexpected("ISO9660 image changed before it was indexed");
            if (identity->size < (kIso9660FirstDescriptorSector + 2) * kIso9660SectorBytes)
                return std::unexpected("ISO9660 image is too small for a descriptor set");

            std::ifstream stream(canonical_path, std::ios::binary);
            if (!stream)
                return std::unexpected("unable to open ISO9660 image");
            auto primary = ReadIso9660PrimaryVolume(stream, identity->size);
            if (!primary)
                return std::unexpected(primary.error());
            auto entries = BuildIso9660Index(stream, *primary, identity->size);
            if (!entries)
                return std::unexpected(entries.error());

            auto final_identity = QueryIso9660ImageIdentity(canonical_path);
            if (!final_identity || !SameIso9660ImageIdentity(*identity, *final_identity))
                return std::unexpected("ISO9660 image changed while it was being indexed");
            return std::unique_ptr<Iso9660Mount>(new Iso9660Mount(std::move(canonical_path),
                *identity, primary->volume_bytes, std::move(*entries)));
        }
        catch (const std::bad_alloc&)
        {
            return std::unexpected("ISO9660 index allocation exceeded process capacity");
        }
        catch (const std::length_error&)
        {
            return std::unexpected("ISO9660 index allocation exceeded container capacity");
        }
        catch (const std::filesystem::filesystem_error&)
        {
            return std::unexpected("unable to resolve ISO9660 image");
        }
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
            return std::unexpected("path is not present in ISO9660 mount");
        const Iso9660Entry& entry = iterator->second;
        if (entry.size > maximum_bytes)
            return std::unexpected("ISO9660 file exceeds caller's read limit");
        if (entry.size > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()) ||
            entry.offset > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max()) ||
            entry.size > static_cast<std::uint64_t>(std::numeric_limits<std::streamsize>::max()))
            return std::unexpected("ISO9660 file range is too large for this process");

        auto before = QueryIso9660ImageIdentity(image_path_);
        if (!before || !SameIso9660ImageIdentity(identity_, *before))
            return std::unexpected("ISO9660 image changed after it was mounted");
        std::uint64_t end = 0;
        if (!CheckedAdd(entry.offset, entry.size, end) || end > volume_bytes_ || end > before->size)
            return std::unexpected("ISO9660 file range is no longer valid");

        try
        {
            std::ifstream stream(image_path_, std::ios::binary);
            if (!stream)
                return std::unexpected("unable to reopen ISO9660 image");
            std::vector<std::byte> bytes(static_cast<std::size_t>(entry.size));
            auto read = ReadIso9660Bytes(stream, entry.offset, bytes);
            if (!read)
                return std::unexpected(read.error());
            auto after = QueryIso9660ImageIdentity(image_path_);
            if (!after || !SameIso9660ImageIdentity(identity_, *after))
                return std::unexpected("ISO9660 image changed while a file was being read");
            return bytes;
        }
        catch (const std::bad_alloc&)
        {
            return std::unexpected("ISO9660 payload allocation exceeded process capacity");
        }
        catch (const std::length_error&)
        {
            return std::unexpected("ISO9660 payload allocation exceeded container capacity");
        }
    }

private:
    Iso9660Mount(std::filesystem::path image_path, const Iso9660ImageIdentity identity,
        const std::uint64_t volume_bytes,
        std::unordered_map<std::string, Iso9660Entry> entries)
        : image_path_(std::move(image_path)), identity_(identity), volume_bytes_(volume_bytes),
          entries_(std::move(entries))
    {
    }

    const std::filesystem::path image_path_;
    const Iso9660ImageIdentity identity_;
    const std::uint64_t volume_bytes_;
    const std::unordered_map<std::string, Iso9660Entry> entries_;
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

std::expected<void, std::string> VirtualFileSystem::MountIso9660(
    const std::filesystem::path& image_path)
{
    if (!impl_)
        return std::unexpected("cannot use a moved-from VFS");
    if (impl_->frozen)
        return std::unexpected("cannot add a mount after the VFS is frozen");
    auto mount = Iso9660Mount::Create(image_path);
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
