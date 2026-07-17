#pragma once

#include "omega/asset/decode.h"
#include "omega/asset/material_catalog_ir.h"

#include <cstddef>
#include <span>

namespace omega::retail
{
// [any worker thread; reentrant] Converts the independently documented VUMS name preamble and
// fixed MTRL table into canonical owned relationships. Geometry payloads and unproven material
// fields are validated only at their proven structural boundaries and are not exposed.
[[nodiscard]] asset::DecodeResult<asset::MaterialCatalogIR> DecodeVumMaterialCatalog(
    std::span<const std::byte> bytes, asset::DecodeLimits limits = {});
} // namespace omega::retail
