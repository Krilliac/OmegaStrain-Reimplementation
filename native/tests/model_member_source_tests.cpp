#include "omega/content/model_member_source.h"

#include "omega/retail/ska_container_descriptor.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace
{
using omega::content::DiscoverModelMembers;
using omega::content::kMaximumModelMemberCandidates;
using omega::content::ModelMemberDiscoveryLimits;
using omega::content::ModelMemberErrorCode;
using omega::content::ModelMemberResult;
using omega::content::SkaMemberSummary;
using omega::content::SkasMemberSummary;
using omega::content::SkmMemberSummary;

int failures = 0;

void Check(const bool condition, const std::string_view message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

void WriteFile(const std::filesystem::path& path, const std::span<const std::byte> bytes)
{
    std::ofstream output(path, std::ios::binary);
    if (!bytes.empty())
        output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

void WriteU8(std::vector<std::byte>& bytes, const std::size_t offset, const std::uint8_t value)
{
    bytes[offset] = static_cast<std::byte>(value);
}

[[nodiscard]] std::vector<std::byte> MakeValidSkm()
{
    constexpr std::uint8_t chunk_count = 2;
    constexpr std::size_t header_bytes = 16;
    constexpr std::size_t logical_bytes = header_bytes + 16 * (4 + 5);
    std::vector<std::byte> bytes(logical_bytes, std::byte{0});
    WriteU8(bytes, 0, chunk_count);
    WriteU8(bytes, 1, 3);
    WriteU8(bytes, 2, 4);
    WriteU8(bytes, 3, 1);
    WriteU8(bytes, 4, 5);
    WriteU8(bytes, 5, 2);
    return bytes;
}

void WriteU32(std::vector<std::byte>& bytes, const std::size_t offset, const std::uint32_t value)
{
    for (unsigned shift = 0; shift < 32; shift += 8)
        bytes[offset + shift / 8U] = static_cast<std::byte>((value >> shift) & 0xFFU);
}

[[nodiscard]] std::vector<std::byte> MakeValidSka()
{
    constexpr std::uint32_t word_0x04 = 1;
    constexpr std::uint32_t word_0x08 = 88;
    const std::size_t logical_bytes = 112U + 4ULL * word_0x08 * word_0x04;
    std::vector<std::byte> bytes(logical_bytes, std::byte{0});
    WriteU32(bytes, 0, 3);
    WriteU32(bytes, 4, word_0x04);
    WriteU32(bytes, 8, word_0x08);
    WriteU32(bytes, 16, 1);
    for (std::size_t offset = 112; offset < logical_bytes; ++offset)
        bytes[offset] = static_cast<std::byte>((offset * 29U + 7U) & 0xFFU);
    return bytes;
}

[[nodiscard]] std::vector<std::byte> MakeValidSkas()
{
    constexpr std::size_t nonblank_lines = 67;
    constexpr std::size_t blank_lines = 5;
    constexpr std::size_t logical_bytes = 5129;
    constexpr std::size_t padding_bytes = 3;
    constexpr std::size_t base_bytes = nonblank_lines * 5U + blank_lines * 2U;

    std::string logical;
    logical.reserve(logical_bytes);
    logical.append("K:V");
    logical.append(logical_bytes - base_bytes, 'X');
    logical.append("\r\n");
    for (std::size_t line = 1; line < nonblank_lines; ++line)
        logical.append("K:V\r\n");
    for (std::size_t line = 0; line < blank_lines; ++line)
        logical.append("\r\n");

    std::vector<std::byte> physical(logical.size() + padding_bytes, std::byte{0});
    for (std::size_t index = 0; index < logical.size(); ++index)
        physical[index] = static_cast<std::byte>(static_cast<unsigned char>(logical[index]));
    return physical;
}

[[nodiscard]] bool HasError(const ModelMemberResult& result, const ModelMemberErrorCode code)
{
    return !result.outcome && result.outcome.error().code == code;
}

[[nodiscard]] std::uint64_t BaseOutputBytes(const std::size_t candidates)
{
    return sizeof(std::vector<ModelMemberResult>) + candidates * sizeof(ModelMemberResult);
}
} // namespace

void RunModelMemberSourceTests()
{
    const auto unique_suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path() /
                      ("omega-model-member-source-tests-" + std::to_string(unique_suffix));
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root / "MODELS", error);
    Check(!error, "temporary test directory is created");

    const auto skm_bytes = MakeValidSkm();
    const auto ska_bytes = MakeValidSka();
    const auto skas_bytes = MakeValidSkas();
    WriteFile(root / "MODELS" / "HERO.SKM", skm_bytes);
    WriteFile(root / "MODELS" / "HERO.SKA", ska_bytes);
    WriteFile(root / "MODELS" / "HERO.SKAS", skas_bytes);
    WriteFile(root / "MODELS" / "BROKEN.SKM", std::vector<std::byte>{std::byte{0}});

    omega::vfs::VirtualFileSystem vfs;
    Check(vfs.MountDirectory(root).has_value(), "model member source test directory mounts");
    vfs.Freeze();

    const std::string oversized_invalid_path(8192, 'X');
    const std::vector<std::string> candidates{
        "models\\hero.skm",
        "MODELS/HERO.SKA",
        "MODELS/HERO.SKAS",
        "MODELS/HERO.TXT",
        "MODELS/MISSING.SKM",
        "../escape.skm",
        oversized_invalid_path,
        "MODELS/BROKEN.SKM",
    };

    const auto discovered = DiscoverModelMembers(vfs, candidates);
    Check(discovered.has_value(), "mixed model-member discovery succeeds structurally");
    if (discovered)
    {
        Check(discovered->size() == candidates.size(),
            "discovery produces exactly one result per candidate");
        for (std::size_t index = 0; index < discovered->size(); ++index)
            Check((*discovered)[index].candidate_index == index,
                "discovery preserves source correlation by bounded candidate index");

        const auto* skm = (*discovered)[0].outcome
                              ? std::get_if<SkmMemberSummary>(&*(*discovered)[0].outcome)
                              : nullptr;
        Check((*discovered)[0].normalized_game_path == "MODELS/HERO.SKM" && skm != nullptr &&
                  skm->format_version == 3 && skm->chunk_count == 2,
            "SKM discovery normalizes identity and returns only its project-owned summary variant");

        const auto* ska = (*discovered)[1].outcome
                              ? std::get_if<SkaMemberSummary>(&*(*discovered)[1].outcome)
                              : nullptr;
        Check(ska != nullptr && ska->format_version == 3 && ska->observed_word_0x08 == 88,
            "SKA discovery returns its distinct project-owned summary variant");

        const auto* skas = (*discovered)[2].outcome
                               ? std::get_if<SkasMemberSummary>(&*(*discovered)[2].outcome)
                               : nullptr;
        Check(skas != nullptr && skas->line_count == 72 && skas->blank_line_count == 5 &&
                  skas->logical_text_bytes == 5129,
            "SKAS discovery summarizes structure without retaining opaque source text");

        Check(HasError((*discovered)[3], ModelMemberErrorCode::UnrecognizedSuffix) &&
                  (*discovered)[3].normalized_game_path == "MODELS/HERO.TXT",
            "unrecognized suffix retains only its bounded normalized identity");
        Check(HasError((*discovered)[4], ModelMemberErrorCode::ReadFailed),
            "missing recognized member reports a path-free read failure");
        Check(HasError((*discovered)[5], ModelMemberErrorCode::InvalidGamePath) &&
                  !(*discovered)[5].normalized_game_path.has_value(),
            "relative escape is rejected without echoing its invalid candidate");
        Check(HasError((*discovered)[6], ModelMemberErrorCode::InvalidGamePath) &&
                  !(*discovered)[6].normalized_game_path.has_value(),
            "oversized invalid path is rejected before any owned copy is retained");
        Check(HasError((*discovered)[7], ModelMemberErrorCode::DecodeFailed) &&
                  (*discovered)[7].outcome.error().decode_failure.has_value(),
            "malformed source reports only a typed path-free decoder failure summary");
    }

    // Decoder input limits cap the VFS read itself: the full member is never allocated merely to
    // produce a predictable decoder limit failure.
    {
        ModelMemberDiscoveryLimits limits;
        limits.decoder_limits = omega::asset::DecodeLimits{};
        limits.decoder_limits->maximum_input_bytes = 4;
        const std::vector<std::string> single{"MODELS/HERO.SKM"};
        const auto tight = DiscoverModelMembers(vfs, single, limits);
        Check(tight.has_value() && tight->size() == 1 &&
                  HasError((*tight)[0], ModelMemberErrorCode::ReadFailed),
            "tight decoder input limit is applied before the VFS allocates the member");
    }

    // Caller and hard candidate ceilings are both tighten-only and checked before any reads.
    {
        ModelMemberDiscoveryLimits limits;
        limits.maximum_candidates = 0;
        const std::vector<std::string> single{"MODELS/HERO.SKM"};
        const auto rejected = DiscoverModelMembers(vfs, single, limits);
        Check(!rejected && rejected.error().code == ModelMemberErrorCode::TooManyCandidates,
            "caller can tighten the operation candidate ceiling");
    }
    {
        ModelMemberDiscoveryLimits limits;
        limits.maximum_candidates = std::numeric_limits<std::size_t>::max();
        const std::vector<std::string> oversized(
            kMaximumModelMemberCandidates + 1, "MODELS/HERO.SKM");
        const auto rejected = DiscoverModelMembers(vfs, oversized, limits);
        Check(!rejected && rejected.error().code == ModelMemberErrorCode::TooManyCandidates,
            "caller cannot widen the fixed candidate ceiling");
    }

    // Retained result storage is preflighted before path normalization or VFS reads.
    {
        ModelMemberDiscoveryLimits limits;
        limits.maximum_total_output_bytes = BaseOutputBytes(1) - 1;
        const std::vector<std::string> single{"MODELS/HERO.SKA"};
        const auto rejected = DiscoverModelMembers(vfs, single, limits);
        Check(!rejected && rejected.error().code == ModelMemberErrorCode::LimitExceeded,
            "operation rejects one byte below retained result storage preflight");
    }
    {
        ModelMemberDiscoveryLimits limits;
        limits.maximum_total_items = 1; // result vector root only; one result needs one more item
        const std::vector<std::string> single{"MODELS/HERO.SKA"};
        const auto rejected = DiscoverModelMembers(vfs, single, limits);
        Check(!rejected && rejected.error().code == ModelMemberErrorCode::LimitExceeded,
            "operation rejects one item below retained result storage preflight");
    }

    // Path bytes are charged before storing the normalized identity and are also part of output.
    {
        constexpr std::string_view normalized = "MODELS/HERO.SKA";
        ModelMemberDiscoveryLimits limits;
        limits.maximum_total_path_bytes = normalized.size() - 1;
        const std::vector<std::string> single{"models/hero.ska"};
        const auto rejected = DiscoverModelMembers(vfs, single, limits);
        Check(rejected.has_value() && HasError((*rejected)[0], ModelMemberErrorCode::LimitExceeded) &&
                  !(*rejected)[0].normalized_game_path.has_value(),
            "path budget rejects one byte below normalized identity without retaining it");
        limits.maximum_total_path_bytes = normalized.size();
        const auto exact = DiscoverModelMembers(vfs, single, limits);
        Check(exact.has_value() && (*exact)[0].outcome.has_value(),
            "path budget succeeds at the exact normalized identity size");
    }

    // The read budget is shared by duplicate candidates. One exact read succeeds and the second
    // repeat is rejected before allocation when no operation read bytes remain.
    {
        ModelMemberDiscoveryLimits limits;
        limits.maximum_total_read_bytes = ska_bytes.size();
        const std::vector<std::string> duplicate{"MODELS/HERO.SKA", "MODELS/HERO.SKA"};
        const auto bounded = DiscoverModelMembers(vfs, duplicate, limits);
        Check(bounded.has_value() && (*bounded)[0].outcome.has_value() &&
                  HasError((*bounded)[1], ModelMemberErrorCode::ReadFailed),
            "duplicate candidates share one operation-wide read budget");
        limits.maximum_total_read_bytes = ska_bytes.size() - 1;
        const std::vector<std::string> single{"MODELS/HERO.SKA"};
        const auto one_below = DiscoverModelMembers(vfs, single, limits);
        Check(one_below.has_value() && HasError((*one_below)[0], ModelMemberErrorCode::ReadFailed),
            "member read is rejected one byte below its exact operation read budget");
    }

    // Cumulative decoder item/output production is also shared rather than reset per child.
    {
        const std::vector<std::string> duplicate{"MODELS/HERO.SKA", "MODELS/HERO.SKA"};
        const auto path_bytes = duplicate[0].size() + duplicate[1].size();
        ModelMemberDiscoveryLimits limits;
        limits.maximum_total_read_bytes = ska_bytes.size() * 2U;
        limits.maximum_total_items = 1U + duplicate.size() + 1U;
        limits.maximum_total_output_bytes = BaseOutputBytes(duplicate.size()) + path_bytes +
                                            sizeof(omega::retail::SkaContainerDescriptor);
        const auto bounded = DiscoverModelMembers(vfs, duplicate, limits);
        Check(bounded.has_value() && (*bounded)[0].outcome.has_value() &&
                  HasError((*bounded)[1], ModelMemberErrorCode::DecodeFailed) &&
                  (*bounded)[1].outcome.error().decode_failure.has_value() &&
                  (*bounded)[1].outcome.error().decode_failure->code ==
                      omega::asset::DecodeErrorCode::LimitExceeded,
            "duplicate decoders share exact operation item and logical-output budgets");
    }

    // Determinism includes normalized identities, variants, indices, and sanitized errors.
    {
        const std::vector<std::string> single{"models/hero.ska"};
        const auto first = DiscoverModelMembers(vfs, single);
        const auto second = DiscoverModelMembers(vfs, single);
        Check(first.has_value() && second.has_value() && *first == *second,
            "model-member discovery is deterministic across identical calls");
    }

    std::filesystem::remove_all(root, error);
    Check(!error, "temporary test directory is removed");
}

namespace
{
struct TestRegistration
{
    TestRegistration() { RunModelMemberSourceTests(); }
};

TestRegistration registration;
} // namespace

int ModelMemberSourceFailureCount()
{
    return failures;
}
