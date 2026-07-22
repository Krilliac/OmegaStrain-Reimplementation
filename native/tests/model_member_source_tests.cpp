#include "omega/content/model_member_source.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{
using omega::content::DiscoverModelMembers;
using omega::content::kMaximumModelMemberCandidates;
using omega::content::ModelMemberErrorCode;
using omega::content::ModelMemberKind;

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

// Minimal well-formed two-chunk SKM fixture, mirroring skm_container_descriptor_tests.cpp's
// MakeSkm helper (each test file owns its own fixture builder per project convention).
[[nodiscard]] std::vector<std::byte> MakeValidSkm()
{
    constexpr std::uint8_t chunk_count = 2;
    constexpr std::size_t header_bytes = 16; // align16(2 + 2*2)
    constexpr std::size_t logical_bytes = header_bytes + 16 * (4 + 5);
    std::vector<std::byte> bytes(logical_bytes, std::byte{0});
    WriteU8(bytes, 0, chunk_count);
    WriteU8(bytes, 1, 3); // format version
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

// Minimal well-formed SKA fixture: word_0x04=1, word_0x08=88, word_0x10=1, version=3, mirroring
// ska_container_descriptor_tests.cpp's default MakeSka spec.
[[nodiscard]] std::vector<std::byte> MakeValidSka()
{
    constexpr std::uint32_t word_0x04 = 1;
    constexpr std::uint32_t word_0x08 = 88;
    const std::size_t logical_bytes = 112U + 4ULL * word_0x08 * word_0x04;
    std::vector<std::byte> bytes(logical_bytes, std::byte{0});
    WriteU32(bytes, 0, 3); // version
    WriteU32(bytes, 4, word_0x04);
    WriteU32(bytes, 8, word_0x08);
    WriteU32(bytes, 16, 1); // word_0x10
    for (std::size_t offset = 112; offset < logical_bytes; ++offset)
        bytes[offset] = static_cast<std::byte>((offset * 29U + 7U) & 0xFFU);
    return bytes;
}

// Minimal well-formed SKAS fixture at the exact lower boundary (5,129 logical bytes, 3 padding
// bytes, 5,132 physical bytes; 67 single-colon lines, 5 blank lines), mirroring the MakeLogical/
// MakePhysical helpers in skas_text_envelope_decoder_tests.cpp.
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

    omega::vfs::VirtualFileSystem vfs;
    Check(vfs.MountDirectory(root).has_value(), "model member source test directory mounts");
    vfs.Freeze();

    const std::vector<std::string> candidates{
        "MODELS/HERO.SKM",
        "MODELS/HERO.SKA",
        "MODELS/HERO.SKAS",
        "MODELS/HERO.TXT",  // unrecognized suffix; never read
        "MODELS/MISSING.SKM", // recognized suffix, absent from the mount
        "../escape.skm", // rejected by NormalizeGamePath before any suffix check
    };

    const auto discovered = DiscoverModelMembers(vfs, candidates);
    Check(discovered.has_value(), "discovery over a mixed valid/invalid candidate list succeeds structurally");
    if (discovered)
    {
        Check(discovered->size() == candidates.size(),
            "discovery produces exactly one result per candidate");
        for (std::size_t index = 0; index < discovered->size() && index < candidates.size(); ++index)
        {
            Check((*discovered)[index].game_path == candidates[index],
                "discovery preserves source order and the caller's exact candidate string");
        }

        const auto& skm_outcome = (*discovered)[0].outcome;
        Check(skm_outcome.has_value() && skm_outcome->kind == ModelMemberKind::Skm &&
                  skm_outcome->skm.has_value() && skm_outcome->skm->format_version == 3,
            "a valid SKM candidate routes to InspectSkmContainer and decodes");

        const auto& ska_outcome = (*discovered)[1].outcome;
        Check(ska_outcome.has_value() && ska_outcome->kind == ModelMemberKind::Ska &&
                  ska_outcome->ska.has_value() && ska_outcome->ska->format_version == 3,
            "a valid SKA candidate routes to InspectSkaContainer and decodes");

        const auto& skas_outcome = (*discovered)[2].outcome;
        Check(skas_outcome.has_value() && skas_outcome->kind == ModelMemberKind::Skas &&
                  skas_outcome->skas.has_value() && skas_outcome->skas->blank_line_count == 5,
            "a valid SKAS candidate routes to DecodeSkasTextEnvelope using its own widened "
            "default string budget");

        const auto& unrecognized_outcome = (*discovered)[3].outcome;
        Check(!unrecognized_outcome &&
                  unrecognized_outcome.error().code == ModelMemberErrorCode::UnrecognizedSuffix,
            "an unrecognized suffix is rejected without a VFS read");

        const auto& missing_outcome = (*discovered)[4].outcome;
        Check(!missing_outcome && missing_outcome.error().code == ModelMemberErrorCode::ReadFailed,
            "a recognized suffix absent from the mount reports a typed read failure");

        const auto& invalid_path_outcome = (*discovered)[5].outcome;
        Check(!invalid_path_outcome &&
                  invalid_path_outcome.error().code == ModelMemberErrorCode::InvalidGamePath,
            "a path rejected by NormalizeGamePath reports a typed error, not a crash");
    }

    // Explicit caller limits apply uniformly; an input-byte budget too small for the SKM fixture
    // surfaces as a per-candidate DecodeFailed, not a discovery-wide failure.
    {
        omega::asset::DecodeLimits tight_limits;
        tight_limits.maximum_input_bytes = 4;
        const std::vector<std::string> single{"MODELS/HERO.SKM"};
        const auto tight = DiscoverModelMembers(vfs, single, tight_limits);
        Check(tight.has_value() && tight->size() == 1 && !(*tight)[0].outcome &&
                  (*tight)[0].outcome.error().code == ModelMemberErrorCode::DecodeFailed &&
                  (*tight)[0].outcome.error().decode_error.has_value(),
            "an explicit undersized caller limit surfaces as a typed per-candidate decode failure");
    }

    // The fixed candidate-count ceiling is a discovery-wide, not per-candidate, rejection.
    {
        const std::vector<std::string> oversized(kMaximumModelMemberCandidates + 1, "MODELS/HERO.SKM");
        const auto rejected = DiscoverModelMembers(vfs, oversized);
        Check(!rejected && rejected.error().code == ModelMemberErrorCode::TooManyCandidates,
            "a candidate list above the fixed ceiling is rejected before any VFS read");
    }

    // Discovery is deterministic across repeated calls with the same input.
    {
        const std::vector<std::string> single{"MODELS/HERO.SKA"};
        const auto first = DiscoverModelMembers(vfs, single);
        const auto second = DiscoverModelMembers(vfs, single);
        Check(first.has_value() && second.has_value() && first->size() == 1 && second->size() == 1 &&
                  (*first)[0].outcome.has_value() && (*second)[0].outcome.has_value() &&
                  (*first)[0].outcome->ska == (*second)[0].outcome->ska,
            "discovery is deterministic across repeated calls with identical input");
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
