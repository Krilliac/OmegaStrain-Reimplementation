#pragma once

#include "omega/asset/decode.h"
#include "omega/vfs/virtual_file_system.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace omega::content
{
// Project-owned, tighten-only operation ceilings. They bound the whole discovery call; callers may
// lower but cannot widen them through ModelMemberDiscoveryLimits.
inline constexpr std::size_t kMaximumModelMemberCandidates = 4096;
inline constexpr std::uint64_t kMaximumModelMemberReadBytes = 512ULL * 1024ULL * 1024ULL;
inline constexpr std::uint64_t kMaximumModelMemberOutputBytes = 64ULL * 1024ULL * 1024ULL;
inline constexpr std::uint64_t kMaximumModelMemberPathBytes = 16ULL * 1024ULL * 1024ULL;
inline constexpr std::uint64_t kMaximumModelMemberItems = 1ULL << 20U;
inline constexpr std::uint64_t kMaximumModelMemberReadBytesPerCandidate =
    64ULL * 1024ULL * 1024ULL;

struct ModelMemberDiscoveryLimits
{
    std::size_t maximum_candidates = kMaximumModelMemberCandidates;
    std::uint64_t maximum_total_read_bytes = kMaximumModelMemberReadBytes;
    // Retained result storage plus cumulative logical decoder output produced during this call.
    std::uint64_t maximum_total_output_bytes = kMaximumModelMemberOutputBytes;
    std::uint64_t maximum_total_path_bytes = kMaximumModelMemberPathBytes;
    // Retained result items plus cumulative decoder items produced during this call.
    std::uint64_t maximum_total_items = kMaximumModelMemberItems;
    std::uint64_t maximum_read_bytes_per_candidate =
        kMaximumModelMemberReadBytesPerCandidate;
    // When present, every field only tightens that format decoder's ordinary defaults. SKAS keeps
    // its wider ordinary string default when this is absent.
    std::optional<asset::DecodeLimits> decoder_limits;
};

enum class ModelMemberErrorCode : std::uint8_t
{
    TooManyCandidates,
    LimitExceeded,
    InvalidGamePath,
    UnrecognizedSuffix,
    ReadFailed,
    DecodeFailed,
};

// Path-free decoder failure summary. Retail error messages and VFS diagnostics stay internal.
struct ModelMemberDecodeFailure
{
    asset::DecodeErrorCode code = asset::DecodeErrorCode::Malformed;
    std::optional<std::uint64_t> byte_offset;

    bool operator==(const ModelMemberDecodeFailure&) const = default;
};

struct ModelMemberError
{
    ModelMemberErrorCode code = ModelMemberErrorCode::ReadFailed;
    std::optional<ModelMemberDecodeFailure> decode_failure;

    bool operator==(const ModelMemberError&) const = default;
};

// Project-owned passive summaries. They intentionally retain no retail descriptor types, source
// payload bytes, inferred model semantics, or host paths.
struct SkmMemberSummary
{
    std::uint8_t format_version = 0;
    std::uint32_t chunk_count = 0;
    std::uint64_t observed_logical_bytes = 0;
    std::uint64_t input_bytes = 0;

    bool operator==(const SkmMemberSummary&) const = default;
};

struct SkaMemberSummary
{
    std::uint32_t format_version = 0;
    std::uint32_t observed_word_0x04 = 0;
    std::uint32_t observed_word_0x08 = 0;
    std::uint32_t observed_word_0x10 = 0;
    std::uint64_t observed_logical_bytes = 0;
    std::uint64_t input_bytes = 0;

    bool operator==(const SkaMemberSummary&) const = default;
};

struct SkasMemberSummary
{
    std::uint32_t line_count = 0;
    std::uint32_t blank_line_count = 0;
    std::uint32_t single_colon_line_count = 0;
    std::uint32_t padding_bytes = 0;
    std::uint64_t logical_text_bytes = 0;
    std::uint64_t input_bytes = 0;

    bool operator==(const SkasMemberSummary&) const = default;
};

using ModelMemberSummary = std::variant<SkmMemberSummary, SkaMemberSummary, SkasMemberSummary>;

struct ModelMemberResult
{
    // Stable correlation without echoing an invalid candidate. A normalized path is retained only
    // after NormalizeGamePath succeeds and the operation path/output budgets can own it.
    std::size_t candidate_index = 0;
    std::optional<std::string> normalized_game_path;
    std::expected<ModelMemberSummary, ModelMemberError> outcome;

    bool operator==(const ModelMemberResult&) const = default;
};

// [any worker thread after file_system.Freeze(); thread-safe, reentrant] Resolves caller-supplied
// candidates in source order through the frozen VFS. Each valid normalized .SKM/.SKA/.SKAS path is
// routed to its existing passive/structural decoder and reduced to a project-owned summary. Every
// candidate produces one indexed result unless the call-wide retained-result preflight itself
// fails. Per-candidate failures remain path-free and do not reorder later candidates.
//
// Every ModelMemberDiscoveryLimits field is clamped to its fixed ceiling. Each decoder limit is
// also clamped to that format's ordinary default. A read is capped by the per-candidate ceiling,
// the effective decoder input ceiling, and the remaining operation read budget before allocation.
[[nodiscard]] std::expected<std::vector<ModelMemberResult>, ModelMemberError> DiscoverModelMembers(
    const vfs::VirtualFileSystem& file_system,
    std::span<const std::string> candidate_game_paths,
    ModelMemberDiscoveryLimits limits = {});
} // namespace omega::content
