#pragma once

#include "omega/asset/decode.h"
#include "omega/asset/material_catalog_ir.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace omega::retail
{
struct DecodedVumMaterialCatalog
{
    asset::MaterialCatalogIR catalog;
    std::uint64_t decoded_items = 0;
    std::uint64_t logical_output_bytes = 0;
};

// [any worker thread; reentrant] Converts the independently documented VUMS name preamble and
// fixed MTRL table into canonical owned relationships. Geometry payloads and unproven material
// fields are validated only at their proven structural boundaries and are not exposed.
[[nodiscard]] asset::DecodeResult<asset::MaterialCatalogIR> DecodeVumMaterialCatalog(
    std::span<const std::byte> bytes, asset::DecodeLimits limits = {});

// [any worker thread; reentrant] Performs the same decode while publishing exact operation-budget
// usage for a caller composing multiple catalogs under one shared DecodeLimits context. The usage
// values describe the complete standalone catalog result, including its root value.
[[nodiscard]] asset::DecodeResult<DecodedVumMaterialCatalog>
DecodeVumMaterialCatalogMeasured(
    std::span<const std::byte> bytes, asset::DecodeLimits limits = {});
} // namespace omega::retail
