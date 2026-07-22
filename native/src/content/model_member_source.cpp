#include "omega/content/model_member_source.h"

#include "omega/retail/ska_container_descriptor.h"
#include "omega/retail/skas_text_envelope_decoder.h"
#include "omega/retail/skm_container_descriptor.h"

#include <algorithm>
#include <limits>
#include <optional>
#include <string_view>
#include <utility>

namespace omega::content
{
namespace
{
enum class ModelMemberKind : std::uint8_t
{
    Skm,
    Ska,
    Skas,
};

struct Usage
{
    std::uint64_t items = 0;
    std::uint64_t output_bytes = 0;
};

struct DecodedMember
{
    ModelMemberSummary summary;
    Usage usage;
};

[[nodiscard]] ModelMemberError Error(
    const ModelMemberErrorCode code,
    const std::optional<asset::DecodeError>& decode_error = std::nullopt)
{
    std::optional<ModelMemberDecodeFailure> failure;
    if (decode_error.has_value())
    {
        failure = ModelMemberDecodeFailure{
            .code = decode_error->code,
            .byte_offset = decode_error->byte_offset,
        };
    }
    return ModelMemberError{
        .code = code,
        .decode_failure = failure,
    };
}

[[nodiscard]] bool Add(
    const std::uint64_t left, const std::uint64_t right, std::uint64_t& result) noexcept
{
    if (right > std::numeric_limits<std::uint64_t>::max() - left)
        return false;
    result = left + right;
    return true;
}

[[nodiscard]] bool Multiply(
    const std::uint64_t left, const std::uint64_t right, std::uint64_t& result) noexcept
{
    if (left != 0 && right > std::numeric_limits<std::uint64_t>::max() / left)
        return false;
    result = left * right;
    return true;
}

[[nodiscard]] bool HasSuffix(const std::string& normalized_path, const std::string_view suffix)
{
    return normalized_path.size() >= suffix.size() &&
           normalized_path.compare(
               normalized_path.size() - suffix.size(), suffix.size(), suffix) == 0;
}

[[nodiscard]] std::optional<ModelMemberKind> ClassifySuffix(const std::string& normalized_path)
{
    if (HasSuffix(normalized_path, ".SKAS"))
        return ModelMemberKind::Skas;
    if (HasSuffix(normalized_path, ".SKM"))
        return ModelMemberKind::Skm;
    if (HasSuffix(normalized_path, ".SKA"))
        return ModelMemberKind::Ska;
    return std::nullopt;
}

[[nodiscard]] asset::DecodeLimits OrdinaryLimits(const ModelMemberKind kind)
{
    if (kind == ModelMemberKind::Skas)
        return retail::DefaultSkasDecodeLimits();
    return {};
}

[[nodiscard]] asset::DecodeLimits TightenedDecoderLimits(const ModelMemberKind kind,
    const ModelMemberDiscoveryLimits& operation_limits, const std::uint64_t remaining_read_bytes,
    const std::uint64_t remaining_output_bytes, const std::uint64_t remaining_items)
{
    auto effective = OrdinaryLimits(kind);
    if (operation_limits.decoder_limits.has_value())
    {
        const asset::DecodeLimits& requested = *operation_limits.decoder_limits;
        effective.maximum_input_bytes =
            std::min(effective.maximum_input_bytes, requested.maximum_input_bytes);
        effective.maximum_output_bytes =
            std::min(effective.maximum_output_bytes, requested.maximum_output_bytes);
        effective.maximum_scratch_bytes =
            std::min(effective.maximum_scratch_bytes, requested.maximum_scratch_bytes);
        effective.maximum_items = std::min(effective.maximum_items, requested.maximum_items);
        effective.maximum_string_bytes =
            std::min(effective.maximum_string_bytes, requested.maximum_string_bytes);
        effective.maximum_nesting_depth =
            std::min(effective.maximum_nesting_depth, requested.maximum_nesting_depth);
    }
    effective.maximum_input_bytes = std::min(
        {effective.maximum_input_bytes, operation_limits.maximum_read_bytes_per_candidate,
            remaining_read_bytes});
    effective.maximum_output_bytes =
        std::min(effective.maximum_output_bytes, remaining_output_bytes);
    effective.maximum_items = std::min(effective.maximum_items, remaining_items);
    return effective;
}

[[nodiscard]] std::expected<DecodedMember, ModelMemberError> DecodeMember(
    const ModelMemberKind kind, const std::vector<std::byte>& bytes,
    const asset::DecodeLimits& limits)
{
    switch (kind)
    {
        case ModelMemberKind::Skm:
        {
            auto result = retail::InspectSkmContainer(bytes, limits);
            if (!result)
                return std::unexpected(Error(ModelMemberErrorCode::DecodeFailed, result.error()));

            std::uint64_t chunk_bytes = 0;
            std::uint64_t output_bytes = sizeof(retail::SkmContainerDescriptor);
            if (!Multiply(result->chunks.size(), sizeof(retail::SkmChunkDescriptor), chunk_bytes) ||
                !Add(output_bytes, chunk_bytes, output_bytes))
            {
                return std::unexpected(Error(ModelMemberErrorCode::LimitExceeded));
            }
            return DecodedMember{
                .summary = SkmMemberSummary{
                    .format_version = result->format_version,
                    .chunk_count = static_cast<std::uint32_t>(result->chunks.size()),
                    .observed_logical_bytes = result->logical_extent.observed_bytes,
                    .input_bytes = result->logical_extent.input_bytes,
                },
                .usage = Usage{
                    .items = 1U + result->chunks.size(),
                    .output_bytes = output_bytes,
                },
            };
        }
        case ModelMemberKind::Ska:
        {
            auto result = retail::InspectSkaContainer(bytes, limits);
            if (!result)
                return std::unexpected(Error(ModelMemberErrorCode::DecodeFailed, result.error()));
            return DecodedMember{
                .summary = SkaMemberSummary{
                    .format_version = result->format_version,
                    .observed_word_0x04 = result->observed_word_0x04,
                    .observed_word_0x08 = result->observed_word_0x08,
                    .observed_word_0x10 = result->observed_word_0x10,
                    .observed_logical_bytes = result->logical_extent.observed_bytes,
                    .input_bytes = result->logical_extent.input_bytes,
                },
                .usage = Usage{
                    .items = 1,
                    .output_bytes = sizeof(retail::SkaContainerDescriptor),
                },
            };
        }
        case ModelMemberKind::Skas:
        {
            auto result = retail::DecodeSkasTextEnvelope(bytes, limits);
            if (!result)
                return std::unexpected(Error(ModelMemberErrorCode::DecodeFailed, result.error()));

            std::uint64_t line_bytes = 0;
            std::uint64_t output_bytes = sizeof(asset::SkasTextEnvelopeIR);
            if (!Multiply(result->lines.size(), sizeof(asset::SkasOpaqueTextLineIR), line_bytes) ||
                !Add(output_bytes, result->logical_text.size(), output_bytes) ||
                !Add(output_bytes, line_bytes, output_bytes))
            {
                return std::unexpected(Error(ModelMemberErrorCode::LimitExceeded));
            }
            return DecodedMember{
                .summary = SkasMemberSummary{
                    .line_count = static_cast<std::uint32_t>(result->lines.size()),
                    .blank_line_count = result->blank_line_count,
                    .single_colon_line_count = result->single_colon_line_count,
                    .padding_bytes = result->padding_bytes,
                    .logical_text_bytes = result->logical_text.size(),
                    .input_bytes = bytes.size(),
                },
                .usage = Usage{
                    .items = 1U + result->lines.size(),
                    .output_bytes = output_bytes,
                },
            };
        }
    }
    return std::unexpected(Error(ModelMemberErrorCode::UnrecognizedSuffix));
}
} // namespace

std::expected<std::vector<ModelMemberResult>, ModelMemberError> DiscoverModelMembers(
    const vfs::VirtualFileSystem& file_system,
    const std::span<const std::string> candidate_game_paths,
    ModelMemberDiscoveryLimits limits)
{
    limits.maximum_candidates =
        std::min(limits.maximum_candidates, kMaximumModelMemberCandidates);
    limits.maximum_total_read_bytes =
        std::min(limits.maximum_total_read_bytes, kMaximumModelMemberReadBytes);
    limits.maximum_total_output_bytes =
        std::min(limits.maximum_total_output_bytes, kMaximumModelMemberOutputBytes);
    limits.maximum_total_path_bytes =
        std::min(limits.maximum_total_path_bytes, kMaximumModelMemberPathBytes);
    limits.maximum_total_items =
        std::min(limits.maximum_total_items, kMaximumModelMemberItems);
    limits.maximum_read_bytes_per_candidate = std::min(
        limits.maximum_read_bytes_per_candidate, kMaximumModelMemberReadBytesPerCandidate);

    if (candidate_game_paths.size() > limits.maximum_candidates)
        return std::unexpected(Error(ModelMemberErrorCode::TooManyCandidates));

    std::uint64_t result_bytes = 0;
    std::uint64_t output_used = sizeof(std::vector<ModelMemberResult>);
    std::uint64_t items_used = 1;
    if (!Multiply(candidate_game_paths.size(), sizeof(ModelMemberResult), result_bytes) ||
        !Add(output_used, result_bytes, output_used) ||
        !Add(items_used, candidate_game_paths.size(), items_used) ||
        output_used > limits.maximum_total_output_bytes ||
        items_used > limits.maximum_total_items)
    {
        return std::unexpected(Error(ModelMemberErrorCode::LimitExceeded));
    }

    std::uint64_t read_used = 0;
    std::uint64_t path_used = 0;
    std::vector<ModelMemberResult> results;
    results.reserve(candidate_game_paths.size());
    for (std::size_t candidate_index = 0; candidate_index < candidate_game_paths.size();
         ++candidate_index)
    {
        ModelMemberResult entry{
            .candidate_index = candidate_index,
            .normalized_game_path = std::nullopt,
            .outcome = std::unexpected(Error(ModelMemberErrorCode::InvalidGamePath)),
        };

        auto normalized = vfs::NormalizeGamePath(candidate_game_paths[candidate_index]);
        if (!normalized)
        {
            results.push_back(std::move(entry));
            continue;
        }

        std::uint64_t next_path_used = 0;
        std::uint64_t next_output_used = 0;
        if (!Add(path_used, normalized->size(), next_path_used) ||
            !Add(output_used, normalized->size(), next_output_used) ||
            next_path_used > limits.maximum_total_path_bytes ||
            next_output_used > limits.maximum_total_output_bytes)
        {
            entry.outcome = std::unexpected(Error(ModelMemberErrorCode::LimitExceeded));
            results.push_back(std::move(entry));
            continue;
        }
        path_used = next_path_used;
        output_used = next_output_used;
        entry.normalized_game_path = std::move(*normalized);

        const auto kind = ClassifySuffix(*entry.normalized_game_path);
        if (!kind.has_value())
        {
            entry.outcome = std::unexpected(Error(ModelMemberErrorCode::UnrecognizedSuffix));
            results.push_back(std::move(entry));
            continue;
        }

        const std::uint64_t remaining_read_bytes = limits.maximum_total_read_bytes - read_used;
        const std::uint64_t remaining_output_bytes = limits.maximum_total_output_bytes - output_used;
        const std::uint64_t remaining_items = limits.maximum_total_items - items_used;
        const asset::DecodeLimits decoder_limits = TightenedDecoderLimits(
            *kind, limits, remaining_read_bytes, remaining_output_bytes, remaining_items);

        auto bytes = file_system.Read(
            *entry.normalized_game_path, decoder_limits.maximum_input_bytes);
        if (!bytes)
        {
            entry.outcome = std::unexpected(Error(ModelMemberErrorCode::ReadFailed));
            results.push_back(std::move(entry));
            continue;
        }
        if (!Add(read_used, bytes->size(), read_used))
        {
            entry.outcome = std::unexpected(Error(ModelMemberErrorCode::LimitExceeded));
            results.push_back(std::move(entry));
            continue;
        }

        auto decoded = DecodeMember(*kind, *bytes, decoder_limits);
        if (!decoded)
        {
            entry.outcome = std::unexpected(decoded.error());
            results.push_back(std::move(entry));
            continue;
        }

        if (!Add(items_used, decoded->usage.items, items_used) ||
            !Add(output_used, decoded->usage.output_bytes, output_used) ||
            items_used > limits.maximum_total_items ||
            output_used > limits.maximum_total_output_bytes)
        {
            entry.outcome = std::unexpected(Error(ModelMemberErrorCode::LimitExceeded));
            results.push_back(std::move(entry));
            continue;
        }
        entry.outcome = std::move(decoded->summary);
        results.push_back(std::move(entry));
    }
    return results;
}
} // namespace omega::content
