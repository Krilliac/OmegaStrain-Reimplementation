#include "omega/retail/vum_material_catalog_decoder.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace
{
constexpr std::uint32_t kNamesEnd = 132;
constexpr std::uint32_t kMaterialsEnd = 316;
constexpr std::uint32_t kPayloadA = 368;
constexpr std::uint32_t kPayloadB = 384;
constexpr std::uint32_t kPrimaryEnd = 432;
constexpr std::uint32_t kFirstMaterial = kNamesEnd;
constexpr std::uint32_t kSecondMaterial = kNamesEnd + 92U;
constexpr std::uint32_t kMetadataT = kMaterialsEnd;
constexpr std::uint32_t kMetadataQ = kMaterialsEnd + 16U;
constexpr std::uint32_t kMetadataP = kMaterialsEnd + 32U;

void WriteU32(
    std::vector<std::byte>& bytes, const std::size_t offset, const std::uint32_t value)
{
    for (unsigned shift = 0; shift < 32; shift += 8)
        bytes[offset + shift / 8U] = static_cast<std::byte>((value >> shift) & 0xFFU);
}

void WriteText(
    std::vector<std::byte>& bytes, const std::size_t offset, const std::string_view value)
{
    for (std::size_t index = 0; index < value.size(); ++index)
        bytes[offset + index] = static_cast<std::byte>(value[index]);
}

void WriteMaterial(
    std::vector<std::byte>& bytes, const std::size_t offset,
    const std::vector<std::uint32_t>& name_indices)
{
    WriteText(bytes, offset, "MTRL");
    WriteU32(bytes, offset + 68U, 0xFFFFFFFFU);
    WriteU32(bytes, offset + 84U, 0xFFFFFFFFU);
    constexpr std::uint32_t inactive = 0xFFFFFFFFU;
    for (std::size_t slot = 0; slot < 3; ++slot)
    {
        WriteU32(bytes, offset + 56U + slot * 4U,
            slot < name_indices.size() ? name_indices[slot] : inactive);
        WriteU32(bytes, offset + 72U + slot * 4U, inactive);
    }
    if (name_indices.size() == 1)
        WriteU32(bytes, offset + 72U, 2);
    else if (name_indices.size() == 2)
    {
        WriteU32(bytes, offset + 72U, 2);
        WriteU32(bytes, offset + 76U, 11);
    }
    else
    {
        WriteU32(bytes, offset + 72U, 2);
        WriteU32(bytes, offset + 76U, 12);
        WriteU32(bytes, offset + 80U, 14);
    }
    WriteU32(bytes, offset + 88U, static_cast<std::uint32_t>(name_indices.size()));
}

std::vector<std::byte> MakeVumCatalog()
{
    std::vector<std::byte> bytes(448, std::byte{0});
    WriteText(bytes, 0, "VUMS");
    WriteU32(bytes, 4, 2);
    WriteU32(bytes, 12, 3);
    WriteU32(bytes, 16, 1);
    WriteU32(bytes, 20, 2);
    WriteU32(bytes, 24, 2);
    WriteU32(bytes, 28, 1);
    WriteU32(bytes, 80, kNamesEnd);
    WriteU32(bytes, 84, kMaterialsEnd);
    WriteU32(bytes, 88, kPrimaryEnd);
    WriteU32(bytes, 92, kPayloadA);
    WriteU32(bytes, 96, kPayloadB);
    WriteText(bytes, 112, "BASE.TDX");
    WriteText(bytes, 121, "DETAIL.TDX");
    WriteMaterial(bytes, kFirstMaterial, {0});
    WriteMaterial(bytes, kSecondMaterial, {1, 0, 1});
    WriteU32(bytes, kMetadataT + 8U, kMetadataQ);
    WriteU32(bytes, kMetadataQ + 4U, kPayloadA);
    WriteU32(bytes, kPayloadA + 4U, kPayloadB + 16U);
    WriteU32(bytes, kMetadataQ + 12U, kPayloadB + 32U);
    WriteU32(bytes, kMetadataP, kPayloadB + 36U);
    WriteU32(bytes, kMetadataP + 8U, kPayloadB + 40U);
    WriteU32(bytes, kMetadataP + 12U, kPayloadB + 44U);
    std::fill(bytes.begin() + kPrimaryEnd, bytes.end(), std::byte{0xA5});
    return bytes;
}

int failures = 0;

void Check(const bool condition, const std::string_view message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

template <typename Value>
void CheckError(const omega::asset::DecodeResult<Value>& result,
    const omega::asset::DecodeErrorCode code, const std::string_view message)
{
    Check(!result && result.error().code == code, message);
}
} // namespace

int VumMaterialCatalogDecoderFailureCount()
{
    auto bytes = MakeVumCatalog();
    const auto catalog = omega::retail::DecodeVumMaterialCatalog(bytes);
    Check(catalog && catalog->names == std::vector<std::string>{"BASE.TDX", "DETAIL.TDX"},
        "VUM catalog owns the declared source-order name table");
    Check(catalog && catalog->materials.size() == 2 &&
              catalog->materials[0].name_count == 1 &&
              catalog->materials[0].name_indices[0] == 0 &&
              catalog->materials[1].name_count == 3 &&
              catalog->materials[1].name_indices == std::array<std::uint32_t, 3>{1, 0, 1},
        "VUM catalog publishes all proven dense MTRL-to-name relationships");

    auto ownership_bytes = bytes;
    auto owned_catalog = omega::retail::DecodeVumMaterialCatalog(ownership_bytes);
    std::fill(ownership_bytes.begin(), ownership_bytes.end(), std::byte{0});
    Check(owned_catalog && owned_catalog->names[1] == "DETAIL.TDX" &&
              owned_catalog->materials[1].name_indices[2] == 1,
        "VUM catalog remains valid after its source storage is replaced");

    auto opaque_bytes = bytes;
    WriteU32(opaque_bytes, 4, 0x12345678U);
    WriteU32(opaque_bytes, kFirstMaterial + 4U, 0xA5A55A5AU);
    WriteU32(opaque_bytes, kSecondMaterial + 8U, 0x3F800000U);
    opaque_bytes[kMetadataT] = std::byte{0x5A};
    opaque_bytes[kPayloadA] = std::byte{0xC3};
    const auto opaque_catalog = omega::retail::DecodeVumMaterialCatalog(opaque_bytes);
    Check(catalog && opaque_catalog && *catalog == *opaque_catalog,
        "VUM opaque header, record, geometry, and trailing data never enter catalog IR");

    auto bad = bytes;
    bad.resize(96);
    CheckError(omega::retail::DecodeVumMaterialCatalog(bad),
        omega::asset::DecodeErrorCode::Truncated,
        "VUM catalog rejects a truncated fixed preamble");
    bad = bytes;
    bad[0] = std::byte{'X'};
    CheckError(omega::retail::DecodeVumMaterialCatalog(bad),
        omega::asset::DecodeErrorCode::Malformed, "VUM catalog rejects the wrong prefix");
    bad = bytes;
    bad.pop_back();
    CheckError(omega::retail::DecodeVumMaterialCatalog(bad),
        omega::asset::DecodeErrorCode::Malformed,
        "VUM catalog rejects a non-aligned container span");
    bad = bytes;
    WriteU32(bad, 88, 464);
    CheckError(omega::retail::DecodeVumMaterialCatalog(bad),
        omega::asset::DecodeErrorCode::Truncated,
        "VUM catalog rejects a primary boundary beyond the input");
    bad = bytes;
    WriteU32(bad, 92, kMaterialsEnd + 2U);
    CheckError(omega::retail::DecodeVumMaterialCatalog(bad),
        omega::asset::DecodeErrorCode::Malformed,
        "VUM catalog rejects a misaligned payload boundary");
    bad = bytes;
    WriteU32(bad, 92, kPayloadB);
    WriteU32(bad, 96, kPayloadA);
    CheckError(omega::retail::DecodeVumMaterialCatalog(bad),
        omega::asset::DecodeErrorCode::Malformed,
        "VUM catalog rejects reversed payload boundaries");
    bad = bytes;
    bad[100] = std::byte{1};
    CheckError(omega::retail::DecodeVumMaterialCatalog(bad),
        omega::asset::DecodeErrorCode::UnsupportedVariant,
        "VUM catalog rejects an unknown preamble layout");
    bad = bytes;
    WriteU32(bad, 24, 3);
    CheckError(omega::retail::DecodeVumMaterialCatalog(bad),
        omega::asset::DecodeErrorCode::Malformed,
        "VUM catalog rejects a contradictory fixed-record count");
    bad = bytes;
    bad[kNamesEnd - 1U] = std::byte{'X'};
    CheckError(omega::retail::DecodeVumMaterialCatalog(bad),
        omega::asset::DecodeErrorCode::Malformed,
        "VUM catalog rejects an unterminated name region");
    bad = bytes;
    bad[112] = std::byte{1};
    CheckError(omega::retail::DecodeVumMaterialCatalog(bad),
        omega::asset::DecodeErrorCode::UnsupportedVariant,
        "VUM catalog rejects an unsupported name character");
    bad = bytes;
    WriteU32(bad, 20, 3);
    CheckError(omega::retail::DecodeVumMaterialCatalog(bad),
        omega::asset::DecodeErrorCode::Malformed,
        "VUM catalog rejects a contradictory name count");
    bad = bytes;
    bad[kFirstMaterial] = std::byte{'X'};
    CheckError(omega::retail::DecodeVumMaterialCatalog(bad),
        omega::asset::DecodeErrorCode::Malformed,
        "VUM catalog rejects a fixed record without MTRL magic");
    bad = bytes;
    bad[kFirstMaterial + 16U] = std::byte{1};
    CheckError(omega::retail::DecodeVumMaterialCatalog(bad),
        omega::asset::DecodeErrorCode::UnsupportedVariant,
        "VUM catalog rejects an unknown fixed-record reserved layout");
    bad = bytes;
    WriteU32(bad, kFirstMaterial + 56U, 2);
    CheckError(omega::retail::DecodeVumMaterialCatalog(bad),
        omega::asset::DecodeErrorCode::InvalidReference,
        "VUM catalog rejects an out-of-range name index");
    bad = bytes;
    WriteU32(bad, kFirstMaterial + 60U, 0);
    CheckError(omega::retail::DecodeVumMaterialCatalog(bad),
        omega::asset::DecodeErrorCode::UnsupportedVariant,
        "VUM catalog rejects a populated inactive reference slot");
    bad = bytes;
    WriteU32(bad, kFirstMaterial + 72U, 99);
    CheckError(omega::retail::DecodeVumMaterialCatalog(bad),
        omega::asset::DecodeErrorCode::UnsupportedVariant,
        "VUM catalog rejects an unknown dense-reference usage family");
    bad = bytes;
    WriteU32(bad, kFirstMaterial + 88U, 0);
    CheckError(omega::retail::DecodeVumMaterialCatalog(bad),
        omega::asset::DecodeErrorCode::UnsupportedVariant,
        "VUM catalog rejects an unsupported active-reference count");
    bad = bytes;
    WriteU32(bad, kFirstMaterial + 8U, 0x7FC00000U);
    CheckError(omega::retail::DecodeVumMaterialCatalog(bad),
        omega::asset::DecodeErrorCode::UnsupportedVariant,
        "VUM catalog rejects a non-finite fixed-record scalar");
    bad = bytes;
    WriteU32(bad, kMetadataT + 8U, kMetadataP);
    CheckError(omega::retail::DecodeVumMaterialCatalog(bad),
        omega::asset::DecodeErrorCode::InvalidReference,
        "VUM metadata rejects a T reference that does not target Q");
    bad = bytes;
    constexpr std::uint32_t metadata_end = kMetadataP + 16U;
    WriteU32(bad, kMetadataT + 8U, metadata_end);
    WriteU32(bad, metadata_end + 4U, kPayloadA);
    WriteU32(bad, metadata_end + 12U, kPayloadB + 32U);
    CheckError(omega::retail::DecodeVumMaterialCatalog(bad),
        omega::asset::DecodeErrorCode::UnsupportedVariant,
        "VUM metadata rejects a Q-like T target beyond the metadata region");
    bad = bytes;
    WriteU32(bad, kMetadataT + 8U, kMaterialsEnd + 8U * 16U);
    CheckError(omega::retail::DecodeVumMaterialCatalog(bad),
        omega::asset::DecodeErrorCode::UnsupportedVariant,
        "VUM metadata rejects a T target whose record would extend past the input");
    bad = bytes;
    WriteU32(bad, kMetadataQ + 4U, kPayloadA + 32U);
    CheckError(omega::retail::DecodeVumMaterialCatalog(bad),
        omega::asset::DecodeErrorCode::InvalidReference,
        "VUM metadata rejects a T target whose Q partition no longer starts at payload A");
    bad = bytes;
    WriteU32(bad, kMetadataP, kPayloadB);
    CheckError(omega::retail::DecodeVumMaterialCatalog(bad),
        omega::asset::DecodeErrorCode::UnsupportedVariant,
        "VUM metadata rejects a P reference on the final-region boundary");

    auto limits = omega::asset::DecodeLimits{};
    limits.maximum_string_bytes = 10;
    Check(omega::retail::DecodeVumMaterialCatalog(bytes, limits).has_value(),
        "VUM catalog accepts the exact longest-name byte limit");
    limits.maximum_string_bytes = 9;
    CheckError(omega::retail::DecodeVumMaterialCatalog(bytes, limits),
        omega::asset::DecodeErrorCode::LimitExceeded,
        "VUM catalog rejects one byte below the longest-name limit");

    limits = omega::asset::DecodeLimits{};
    limits.maximum_input_bytes = bytes.size();
    Check(omega::retail::DecodeVumMaterialCatalog(bytes, limits).has_value(),
        "VUM catalog accepts the exact input-byte limit");
    limits.maximum_input_bytes = bytes.size() - 1U;
    CheckError(omega::retail::DecodeVumMaterialCatalog(bytes, limits),
        omega::asset::DecodeErrorCode::LimitExceeded,
        "VUM catalog rejects one byte below the input-byte limit");

    limits = omega::asset::DecodeLimits{};
    limits.maximum_items = 12;
    Check(omega::retail::DecodeVumMaterialCatalog(bytes, limits).has_value(),
        "VUM catalog accepts the exact root-plus-entry item limit");
    limits.maximum_items = 11;
    CheckError(omega::retail::DecodeVumMaterialCatalog(bytes, limits),
        omega::asset::DecodeErrorCode::LimitExceeded,
        "VUM catalog rejects one item below its exact decoded item count");

    constexpr std::uint64_t output_bytes = sizeof(omega::asset::MaterialCatalogIR) +
        2U * sizeof(std::string) + 2U * sizeof(omega::asset::MaterialCatalogEntryIR) + 18U;
    limits = omega::asset::DecodeLimits{};
    limits.maximum_output_bytes = output_bytes;
    limits.maximum_scratch_bytes = 0;
    Check(omega::retail::DecodeVumMaterialCatalog(bytes, limits).has_value(),
        "VUM catalog accepts exact output and zero-scratch limits");
    limits.maximum_output_bytes = output_bytes - 1U;
    CheckError(omega::retail::DecodeVumMaterialCatalog(bytes, limits),
        omega::asset::DecodeErrorCode::LimitExceeded,
        "VUM catalog rejects one byte below its exact owned-output limit");

    return failures;
}
