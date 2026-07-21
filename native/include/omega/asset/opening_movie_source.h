#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace omega::asset
{
inline constexpr std::uint64_t kOpeningMovieMaximumSourceBytes =
    512ULL * 1024ULL * 1024ULL;

// Owned, identity-free bytes for one explicitly selected opening movie. The value is a one-way
// ownership handoff from bounded content loading to the app/media boundary; it retains no host
// path, archive path, or member name.
class OpeningMovieSource final
{
public:
    [[nodiscard]] static std::optional<OpeningMovieSource> Create(
        std::vector<std::byte> bytes) noexcept
    {
        if (bytes.size() > kOpeningMovieMaximumSourceBytes)
            return std::nullopt;
        return OpeningMovieSource(std::move(bytes));
    }

    OpeningMovieSource(const OpeningMovieSource&) = delete;
    OpeningMovieSource& operator=(const OpeningMovieSource&) = delete;
    OpeningMovieSource(OpeningMovieSource&& other) noexcept
        : bytes_(std::exchange(other.bytes_, std::vector<std::byte>{}))
    {
    }
    OpeningMovieSource& operator=(OpeningMovieSource&&) noexcept = delete;
    ~OpeningMovieSource() = default;

    [[nodiscard]] std::span<const std::byte> bytes() const noexcept { return bytes_; }
    [[nodiscard]] bool empty() const noexcept { return bytes_.empty(); }

    [[nodiscard]] std::vector<std::byte> TakeBytes() && noexcept
    {
        return std::exchange(bytes_, std::vector<std::byte>{});
    }

private:
    explicit OpeningMovieSource(std::vector<std::byte> bytes) noexcept
        : bytes_(std::move(bytes))
    {
    }

    std::vector<std::byte> bytes_;
};
} // namespace omega::asset
