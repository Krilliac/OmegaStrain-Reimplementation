#include "omega/content/model_member_source.h"

#include "omega/retail/skas_text_envelope_decoder.h"

#include <optional>
#include <string_view>
#include <utility>

namespace omega::content
{
namespace
{
[[nodiscard]] ModelMemberError Error(
    const ModelMemberErrorCode code, std::string message,
    std::optional<asset::DecodeError> decode_error = std::nullopt)
{
    return ModelMemberError{
        .code = code,
        .decode_error = std::move(decode_error),
        .message = std::move(message),
    };
}

[[nodiscard]] bool HasSuffix(const std::string& normalized_path, const std::string_view suffix)
{
    return normalized_path.size() >= suffix.size() &&
           normalized_path.compare(
               normalized_path.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// Checked longest-suffix-first: ".SKAS" must be tested before ".SKA" so a genuine SKAS path is
// never misclassified as SKA by matching only its shorter trailing substring.
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

[[nodiscard]] std::expected<ModelMemberDescriptor, ModelMemberError> DecodeMember(
    const ModelMemberKind kind, const std::vector<std::byte>& bytes,
    const std::optional<asset::DecodeLimits>& limits)
{
    ModelMemberDescriptor descriptor;
    descriptor.kind = kind;
    switch (kind)
    {
        case ModelMemberKind::Skm:
        {
            auto result = limits.has_value() ? retail::InspectSkmContainer(bytes, *limits)
                                              : retail::InspectSkmContainer(bytes);
            if (!result)
            {
                return std::unexpected(Error(ModelMemberErrorCode::DecodeFailed,
                    "SKM member failed structural inspection", result.error()));
            }
            descriptor.skm = std::move(*result);
            return descriptor;
        }
        case ModelMemberKind::Ska:
        {
            auto result = limits.has_value() ? retail::InspectSkaContainer(bytes, *limits)
                                              : retail::InspectSkaContainer(bytes);
            if (!result)
            {
                return std::unexpected(Error(ModelMemberErrorCode::DecodeFailed,
                    "SKA member failed structural inspection", result.error()));
            }
            descriptor.ska = std::move(*result);
            return descriptor;
        }
        case ModelMemberKind::Skas:
        {
            auto result = limits.has_value() ? retail::DecodeSkasTextEnvelope(bytes, *limits)
                                              : retail::DecodeSkasTextEnvelope(bytes);
            if (!result)
            {
                return std::unexpected(Error(ModelMemberErrorCode::DecodeFailed,
                    "SKAS member failed structural inspection", result.error()));
            }
            descriptor.skas = std::move(*result);
            return descriptor;
        }
    }
    return std::unexpected(Error(
        ModelMemberErrorCode::UnrecognizedSuffix, "model member kind is not recognized"));
}
} // namespace

std::expected<std::vector<ModelMemberResult>, ModelMemberError> DiscoverModelMembers(
    const vfs::VirtualFileSystem& vfs, const std::span<const std::string> candidate_game_paths,
    const std::optional<asset::DecodeLimits> limits, const std::uint64_t maximum_read_bytes)
{
    if (candidate_game_paths.size() > kMaximumModelMemberCandidates)
    {
        return std::unexpected(Error(ModelMemberErrorCode::TooManyCandidates,
            "model member candidate count exceeds the fixed discovery ceiling"));
    }

    std::vector<ModelMemberResult> results;
    results.reserve(candidate_game_paths.size());
    for (const std::string& candidate : candidate_game_paths)
    {
        ModelMemberResult entry;
        entry.game_path = candidate;

        auto normalized = vfs::NormalizeGamePath(candidate);
        if (!normalized)
        {
            entry.outcome = std::unexpected(Error(ModelMemberErrorCode::InvalidGamePath,
                "model member candidate path is invalid"));
            results.push_back(std::move(entry));
            continue;
        }

        const auto kind = ClassifySuffix(*normalized);
        if (!kind.has_value())
        {
            entry.outcome = std::unexpected(Error(ModelMemberErrorCode::UnrecognizedSuffix,
                "model member candidate suffix is not one of SKM/SKA/SKAS"));
            results.push_back(std::move(entry));
            continue;
        }

        auto bytes = vfs.Read(*normalized, maximum_read_bytes);
        if (!bytes)
        {
            entry.outcome = std::unexpected(
                Error(ModelMemberErrorCode::ReadFailed, "unable to read model member"));
            results.push_back(std::move(entry));
            continue;
        }

        entry.outcome = DecodeMember(*kind, *bytes, limits);
        results.push_back(std::move(entry));
    }
    return results;
}
} // namespace omega::content
