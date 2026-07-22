#pragma once

#include "omega/asset/decode.h"
#include "omega/asset/skas_text_envelope_ir.h"
#include "omega/retail/ska_container_descriptor.h"
#include "omega/retail/skm_container_descriptor.h"
#include "omega/vfs/virtual_file_system.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace omega::content
{
// Fixed authoring ceiling on candidates per DiscoverModelMembers call. This bounds the caller's
// own candidate list; it is not an observation about how many SKM/SKA/SKAS members exist in any
// tracked archive.
inline constexpr std::size_t kMaximumModelMemberCandidates = 4096;

enum class ModelMemberKind : std::uint8_t
{
    Skm,
    Ska,
    Skas,
};

enum class ModelMemberErrorCode : std::uint8_t
{
    TooManyCandidates,
    InvalidGamePath,
    UnrecognizedSuffix,
    ReadFailed,
    DecodeFailed,
};

struct ModelMemberError
{
    ModelMemberErrorCode code = ModelMemberErrorCode::ReadFailed;
    // Populated only when code == DecodeFailed; the underlying decoder's own typed error.
    std::optional<asset::DecodeError> decode_error;
    // Path-free, generic diagnostic text. Never echoes a VirtualFileSystem diagnostic or a host
    // filesystem path, matching the discipline GameDataError documents for the same reason.
    std::string message;
};

// Exactly one member is populated, matching kind. No relationship between SKM, SKA, and SKAS is
// claimed by grouping them here; this type only lets one caller-supplied candidate list route
// each member to its own already-existing passive/structural decoder.
struct ModelMemberDescriptor
{
    ModelMemberKind kind = ModelMemberKind::Skm;
    std::optional<retail::SkmContainerDescriptor> skm;
    std::optional<retail::SkaContainerDescriptor> ska;
    std::optional<asset::SkasTextEnvelopeIR> skas;
};

struct ModelMemberResult
{
    // The caller-supplied candidate string, unmodified. It is an already game-relative path, not
    // an owner host filesystem path.
    std::string game_path;
    std::expected<ModelMemberDescriptor, ModelMemberError> outcome;
};

// [any worker thread after vfs.Freeze(); thread-safe, reentrant] Resolves each caller-supplied
// candidate game path in source order through the frozen VFS, classifies it by its normalized
// .SKM/.SKA/.SKAS suffix, and routes it to the matching existing passive/structural decoder
// (InspectSkmContainer/InspectSkaContainer/DecodeSkasTextEnvelope). It assigns no retail role,
// naming rule, or selection policy to any path; the caller supplies the exact candidate set and
// its order, and every candidate produces exactly one ModelMemberResult in that same order. A
// per-candidate read or decode failure does not stop or reorder the remaining candidates.
//
// When limits is not supplied, each candidate uses its own decoder's ordinary default limits
// (including SKAS's widened default string budget) rather than one shared value that could
// silently under-budget a different format. When limits is supplied, every candidate uses it
// uniformly; the caller is then responsible for a budget wide enough for every format it expects
// to encounter.
//
// This function depends only on VirtualFileSystem and the retail SKM/SKA/SKAS decoders; it does
// not depend on or call into GameDataService or any level-composition type.
[[nodiscard]] std::expected<std::vector<ModelMemberResult>, ModelMemberError> DiscoverModelMembers(
    const vfs::VirtualFileSystem& vfs, std::span<const std::string> candidate_game_paths,
    std::optional<asset::DecodeLimits> limits = std::nullopt,
    std::uint64_t maximum_read_bytes = vfs::kDefaultMaximumReadBytes);
} // namespace omega::content
