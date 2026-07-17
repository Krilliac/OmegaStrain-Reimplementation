#include "omega/retail/container_descriptors.h"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

namespace
{
void WriteU16(
    std::vector<std::byte>& bytes, const std::size_t offset, const std::uint16_t value)
{
    bytes[offset] = static_cast<std::byte>(value & 0xFFU);
    bytes[offset + 1] = static_cast<std::byte>((value >> 8U) & 0xFFU);
}

void WriteU32(
    std::vector<std::byte>& bytes, const std::size_t offset, const std::uint32_t value)
{
    for (unsigned shift = 0; shift < 32; shift += 8)
        bytes[offset + shift / 8U] = static_cast<std::byte>((value >> shift) & 0xFFU);
}

std::vector<std::byte> MakeCol()
{
    std::vector<std::byte> bytes(128, std::byte{0x5A});
    bytes[0] = std::byte{'C'};
    bytes[1] = std::byte{'O'};
    bytes[2] = std::byte{'L'};
    bytes[3] = std::byte{5};
    WriteU32(bytes, 4, 1);
    WriteU32(bytes, 8, 48);
    WriteU32(bytes, 12, 0);
    WriteU32(bytes, 16, 112);
    WriteU32(bytes, 20, 0);
    WriteU32(bytes, 24, 112);
    WriteU32(bytes, 28, 0);
    WriteU32(bytes, 32, 112);
    WriteU32(bytes, 36, 112);
    WriteU32(bytes, 40, 1);
    WriteU32(bytes, 44, 112);
    return bytes;
}

std::vector<std::byte> MakeVum()
{
    std::vector<std::byte> bytes(128, std::byte{0});
    bytes[0] = std::byte{'V'};
    bytes[1] = std::byte{'U'};
    bytes[2] = std::byte{'M'};
    bytes[3] = std::byte{'S'};
    WriteU32(bytes, 4, 2);
    WriteU32(bytes, 28, 0);
    WriteU32(bytes, 80, 92);
    WriteU32(bytes, 84, 92);
    WriteU32(bytes, 88, 96);
    return bytes;
}

std::vector<std::byte> MakeTdx()
{
    std::vector<std::byte> bytes(320, std::byte{0});
    WriteU16(bytes, 0, 5);
    WriteU16(bytes, 2, 1);
    WriteU16(bytes, 4, 16);
    WriteU16(bytes, 6, 16);
    WriteU16(bytes, 8, 8);
    WriteU16(bytes, 10, 0x13);
    WriteU16(bytes, 12, 2);
    WriteU16(bytes, 14, 1);
    WriteU16(bytes, 16, 16);
    WriteU16(bytes, 18, 16);
    WriteU16(bytes, 20, 32);
    WriteU16(bytes, 24, 1);
    WriteU16(bytes, 26, 4);
    WriteU16(bytes, 34, 1);
    WriteU16(bytes, 36, 1);
    WriteU16(bytes, 38, 1);
    WriteU16(bytes, 52, 128);
    WriteU16(bytes, 54, 128);
    WriteU32(bytes, 56, 256);
    WriteU32(bytes, 60, 0);
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

template <typename Result>
void CheckError(
    const Result& result, const omega::asset::DecodeErrorCode code,
    const std::string_view message)
{
    Check(!result && result.error().code == code, message);
}
} // namespace

int ContainerDescriptorFailureCount()
{
    auto col_bytes = MakeCol();
    auto col = omega::retail::InspectColContainer(col_bytes);
    Check(col.has_value(), "synthetic COL counted-table structure is accepted");
    if (col)
    {
        Check(col->format_version == 5 && col->header_bytes == 48,
            "COL publishes only the supported version and observed header size");
        Check(col->observed_record_counts[0] == 1 &&
                  col->counted_tables[0].offset == 48 &&
                  col->counted_tables[0].size == 64,
            "COL first counted table preserves its proven arithmetic extent");
        Check(col->uncounted_table_region.offset == 112 &&
                  col->uncounted_table_region.size == 0,
            "COL final table-region endpoint is kept separate from counted tables");
        Check(col->described_tables_extent.relation ==
                  omega::retail::ObservedExtentRelation::NonzeroTail,
            "COL table-region end is not misreported as the container length");
    }

    auto version_three_col = MakeCol();
    version_three_col[3] = std::byte{3};
    Check(omega::retail::InspectColContainer(version_three_col).has_value(),
        "observed COL version three remains structurally supported");
    for (std::size_t size = 0; size < 48U; ++size)
        CheckError(omega::retail::InspectColContainer(
                       std::span<const std::byte>(col_bytes.data(), size)),
            omega::asset::DecodeErrorCode::Truncated,
            "every short COL header is a truncation error");
    auto bad_col = MakeCol();
    bad_col[0] = std::byte{'X'};
    CheckError(omega::retail::InspectColContainer(bad_col),
        omega::asset::DecodeErrorCode::Malformed, "COL rejects an invalid prefix");
    bad_col = MakeCol();
    bad_col[3] = std::byte{4};
    CheckError(omega::retail::InspectColContainer(bad_col),
        omega::asset::DecodeErrorCode::UnsupportedVariant,
        "COL rejects an unobserved format version as unsupported");
    bad_col = MakeCol();
    WriteU32(bad_col, 8, 64);
    CheckError(omega::retail::InspectColContainer(bad_col),
        omega::asset::DecodeErrorCode::UnsupportedVariant,
        "COL rejects an unobserved header layout as unsupported");
    bad_col = MakeCol();
    WriteU32(bad_col, 16, 96);
    CheckError(omega::retail::InspectColContainer(bad_col),
        omega::asset::DecodeErrorCode::Malformed,
        "COL rejects a counted-table endpoint that contradicts its count");
    bad_col = MakeCol();
    WriteU32(bad_col, 44, 96);
    CheckError(omega::retail::InspectColContainer(bad_col),
        omega::asset::DecodeErrorCode::Malformed,
        "COL rejects a non-monotonic final table endpoint");
    bad_col = MakeCol();
    WriteU32(bad_col, 44, 144);
    CheckError(omega::retail::InspectColContainer(bad_col),
        omega::asset::DecodeErrorCode::Truncated,
        "COL distinguishes a coherent endpoint beyond the supplied input");
    bad_col = MakeCol();
    bad_col.resize(127);
    CheckError(omega::retail::InspectColContainer(bad_col),
        omega::asset::DecodeErrorCode::Malformed,
        "COL rejects a container span that is not 16-byte aligned");
    auto cutoff_col = MakeCol();
    WriteU32(cutoff_col, 44, 128);
    CheckError(omega::retail::InspectColContainer(
                   std::span<const std::byte>(cutoff_col.data(), 127)),
        omega::asset::DecodeErrorCode::Truncated,
        "COL classifies an exact table-region cutoff before input alignment");

    auto vum_bytes = MakeVum();
    auto vum = omega::retail::InspectVumContainer(vum_bytes);
    Check(vum.has_value(), "synthetic VUM prefix and ordered boundaries are accepted");
    if (vum)
    {
        Check(vum->observed_variant == 2 && vum->observed_word_0x1c == 0,
            "VUM preserves opaque observed words without assigning semantics");
        Check(vum->observed_boundaries[0] == 92 && vum->observed_boundaries[2] == 96,
            "VUM publishes only the proven ordered prefix boundaries");
        Check(vum->primary_extent.relation ==
                  omega::retail::ObservedExtentRelation::ZeroPaddedTail,
            "VUM recognizes a bounded primary section followed by zero padding");
    }
    auto exact_vum = MakeVum();
    exact_vum.resize(96);
    auto exact_vum_result = omega::retail::InspectVumContainer(exact_vum);
    Check(exact_vum_result && exact_vum_result->primary_extent.relation ==
                                  omega::retail::ObservedExtentRelation::Exact,
        "VUM exact primary-section boundary is preserved");
    auto trailing_vum = MakeVum();
    WriteU32(trailing_vum, 28, 2);
    trailing_vum[96] = std::byte{1};
    auto trailing_vum_result = omega::retail::InspectVumContainer(trailing_vum);
    Check(trailing_vum_result && trailing_vum_result->primary_extent.relation ==
                                     omega::retail::ObservedExtentRelation::NonzeroTail,
        "VUM accepts and reports the observed nonzero trailing-region family");
    for (std::size_t size = 0; size < 92U; ++size)
        CheckError(omega::retail::InspectVumContainer(
                       std::span<const std::byte>(vum_bytes.data(), size)),
            omega::asset::DecodeErrorCode::Truncated,
            "every short VUM prefix is a truncation error");
    auto bad_vum = MakeVum();
    bad_vum[0] = std::byte{'X'};
    CheckError(omega::retail::InspectVumContainer(bad_vum),
        omega::asset::DecodeErrorCode::Malformed, "VUM rejects an invalid prefix");
    bad_vum = MakeVum();
    WriteU32(bad_vum, 84, 88);
    CheckError(omega::retail::InspectVumContainer(bad_vum),
        omega::asset::DecodeErrorCode::Malformed,
        "VUM rejects non-monotonic observed boundaries");
    bad_vum = MakeVum();
    WriteU32(bad_vum, 88, 144);
    CheckError(omega::retail::InspectVumContainer(bad_vum),
        omega::asset::DecodeErrorCode::Truncated,
        "VUM distinguishes a primary boundary beyond the supplied input");
    bad_vum = MakeVum();
    bad_vum.resize(127);
    CheckError(omega::retail::InspectVumContainer(bad_vum),
        omega::asset::DecodeErrorCode::Malformed,
        "VUM rejects a container span that is not 16-byte aligned");
    auto cutoff_vum = MakeVum();
    cutoff_vum.resize(96);
    CheckError(omega::retail::InspectVumContainer(
                   std::span<const std::byte>(cutoff_vum.data(), 95)),
        omega::asset::DecodeErrorCode::Truncated,
        "VUM classifies an exact primary-section cutoff before input alignment");

    auto tdx_bytes = MakeTdx();
    auto tdx = omega::retail::InspectTdxContainer(tdx_bytes);
    Check(tdx.has_value(), "synthetic TDX v5 header is accepted");
    if (tdx)
    {
        Check(tdx->width == 16 && tdx->height == 16 && tdx->bits_per_pixel == 8 &&
                  tdx->observed_storage_format_code == 0x13,
            "TDX publishes structural dimensions and opaque storage-format code");
        Check(tdx->observed_width_unit_word == 2 &&
                  tdx->storage_word_matches_area_bit_formula,
            "TDX validates width units and reports the observed area/bit formula");
        Check(tdx->primary_extent.relation == omega::retail::ObservedExtentRelation::Exact &&
                  tdx->bounded_primary_region && tdx->bounded_primary_region->offset == 64 &&
                  tdx->bounded_primary_region->size == 256,
            "TDX exposes a bounded primary body only for a safe extent relation");
    }
    auto padded_tdx = MakeTdx();
    padded_tdx.resize(336);
    auto padded = omega::retail::InspectTdxContainer(padded_tdx);
    Check(padded && padded->primary_extent.relation ==
                        omega::retail::ObservedExtentRelation::ZeroPaddedTail &&
              padded->bounded_primary_region.has_value(),
        "TDX accepts an observed primary body followed only by zero padding");
    padded_tdx.back() = std::byte{1};
    auto extra_tdx = omega::retail::InspectTdxContainer(padded_tdx);
    Check(extra_tdx && extra_tdx->primary_extent.relation ==
                           omega::retail::ObservedExtentRelation::NonzeroTail &&
              !extra_tdx->bounded_primary_region,
        "TDX nonzero trailing data remains opaque and has no bounded body view");
    auto exceeding_tdx = MakeTdx();
    WriteU32(exceeding_tdx, 56, 512);
    auto exceeding = omega::retail::InspectTdxContainer(exceeding_tdx);
    Check(exceeding && exceeding->primary_extent.relation ==
                          omega::retail::ObservedExtentRelation::ExceedsInput &&
              !exceeding->bounded_primary_region,
        "TDX accepts the observed size-word-exceeds-span family without exposing payload");
    for (std::size_t size = 0; size < 64U; ++size)
        CheckError(omega::retail::InspectTdxContainer(
                       std::span<const std::byte>(tdx_bytes.data(), size)),
            omega::asset::DecodeErrorCode::Truncated,
            "every short TDX header is a truncation error");
    auto bad_tdx = MakeTdx();
    WriteU16(bad_tdx, 0, 4);
    CheckError(omega::retail::InspectTdxContainer(bad_tdx),
        omega::asset::DecodeErrorCode::UnsupportedVariant,
        "TDX rejects an unobserved format version as unsupported");
    bad_tdx = MakeTdx();
    WriteU16(bad_tdx, 2, 8);
    CheckError(omega::retail::InspectTdxContainer(bad_tdx),
        omega::asset::DecodeErrorCode::UnsupportedVariant,
        "TDX rejects flags outside the observed family");
    bad_tdx = MakeTdx();
    WriteU16(bad_tdx, 2, 2);
    CheckError(omega::retail::InspectTdxContainer(bad_tdx),
        omega::asset::DecodeErrorCode::UnsupportedVariant,
        "TDX rejects a flag and bit-depth combination absent from the observed family");
    bad_tdx = MakeTdx();
    WriteU16(bad_tdx, 4, 0);
    CheckError(omega::retail::InspectTdxContainer(bad_tdx),
        omega::asset::DecodeErrorCode::Malformed, "TDX rejects zero dimensions as malformed");
    bad_tdx = MakeTdx();
    WriteU16(bad_tdx, 4, 24);
    CheckError(omega::retail::InspectTdxContainer(bad_tdx),
        omega::asset::DecodeErrorCode::UnsupportedVariant,
        "TDX rejects non-power-of-two dimensions as unsupported");
    bad_tdx = MakeTdx();
    WriteU16(bad_tdx, 10, 0x14);
    CheckError(omega::retail::InspectTdxContainer(bad_tdx),
        omega::asset::DecodeErrorCode::UnsupportedVariant,
        "TDX rejects an unobserved bit-depth and format-code pair");
    bad_tdx = MakeTdx();
    WriteU16(bad_tdx, 12, 1);
    CheckError(omega::retail::InspectTdxContainer(bad_tdx),
        omega::asset::DecodeErrorCode::UnsupportedVariant,
        "TDX rejects a width-unit word that contradicts the observed formula");
    bad_tdx = MakeTdx();
    WriteU16(bad_tdx, 16, 8);
    CheckError(omega::retail::InspectTdxContainer(bad_tdx),
        omega::asset::DecodeErrorCode::UnsupportedVariant,
        "TDX keeps unobserved opaque layout signatures unsupported");
    bad_tdx = MakeTdx();
    WriteU16(bad_tdx, 52, 0);
    CheckError(omega::retail::InspectTdxContainer(bad_tdx),
        omega::asset::DecodeErrorCode::UnsupportedVariant,
        "TDX rejects an opaque word relation absent from the observed corpus");
    bad_tdx = MakeTdx();
    WriteU32(bad_tdx, 60, 1);
    CheckError(omega::retail::InspectTdxContainer(bad_tdx),
        omega::asset::DecodeErrorCode::UnsupportedVariant,
        "TDX keeps nonzero reserved-word layouts unsupported");
    bad_tdx = MakeTdx();
    WriteU16(bad_tdx, 22, 1);
    CheckError(omega::retail::InspectTdxContainer(bad_tdx),
        omega::asset::DecodeErrorCode::UnsupportedVariant,
        "TDX keeps unobserved nonzero layout words unsupported");
    bad_tdx = MakeTdx();
    bad_tdx.resize(319);
    CheckError(omega::retail::InspectTdxContainer(bad_tdx),
        omega::asset::DecodeErrorCode::Malformed,
        "TDX rejects a container span that is not 16-byte aligned");

    auto limits = omega::asset::DecodeLimits{};
    limits.maximum_input_bytes = col_bytes.size() - 1U;
    CheckError(omega::retail::InspectColContainer(col_bytes, limits),
        omega::asset::DecodeErrorCode::LimitExceeded,
        "fixed descriptors enforce the caller input-byte limit");
    limits = omega::asset::DecodeLimits{};
    limits.maximum_output_bytes = sizeof(omega::retail::TdxContainerDescriptor) - 1U;
    CheckError(omega::retail::InspectTdxContainer(tdx_bytes, limits),
        omega::asset::DecodeErrorCode::LimitExceeded,
        "fixed descriptors enforce their logical output-byte limit");
    limits = omega::asset::DecodeLimits{};
    limits.maximum_items = 0;
    CheckError(omega::retail::InspectVumContainer(vum_bytes, limits),
        omega::asset::DecodeErrorCode::LimitExceeded,
        "fixed descriptors enforce the caller item limit");

    return failures;
}
