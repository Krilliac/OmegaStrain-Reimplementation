#pragma once

#include "omega/asset/level_content_ir.h"
#include "omega/asset/material_catalog_ir.h"
#include "omega/asset/scene_ir.h"
#include "omega/runtime/canonical_level_scene.h"

#include <cstdint>
#include <expected>
#include <string>
#include <vector>

namespace omega::runtime
{
struct CanonicalLevelSceneMaterialLimits
{
    CanonicalLevelSceneLimits scene;
    std::uint64_t maximum_catalogs = 4096U;
    std::uint64_t maximum_names = 1ULL << 20U;
    std::uint64_t maximum_materials = 1ULL << 20U;
    std::uint64_t maximum_name_bytes = 64ULL * 1024ULL * 1024ULL;
    std::uint64_t maximum_name_length = 4096U;
    // ABI-local logical host-footprint budget for owned values and payloads.
    // This is neither a serialization size nor an exact heap-allocation cap;
    // allocator metadata and implementation-defined capacity are excluded.
    std::uint64_t maximum_output_bytes = 256ULL * 1024ULL * 1024ULL;
};

struct CanonicalLevelCellMaterialCatalog
{
    SourceCellOrdinal source_cell_ordinal;
    asset::MaterialCatalogIR catalog;

    bool operator==(const CanonicalLevelCellMaterialCatalog&) const = default;
};

// A canonical level scene paired with a material/name catalog carrying the same
// explicit source ordinal as each canonical cell. This preserves only
// LevelContentIR's documented positional relationship; it does not
// independently prove how a caller constructed that aggregate and it asserts no
// mesh-to-material, triangle, vertex, texture, or visibility binding.
struct CanonicalLevelSceneWithMaterials
{
    CanonicalLevelScene scene;
    std::vector<CanonicalLevelCellMaterialCatalog> material_catalogs;

    bool operator==(const CanonicalLevelSceneWithMaterials&) const = default;
};

// [any worker thread; reentrant] Validates output cardinality, exact
// typed-ordinal pairing, canonical scene geometry, material references, and
// every tighten-only scene/material/output budget without allocating output.
[[nodiscard]] std::expected<void, std::string> ValidateCanonicalLevelSceneMaterialAssociation(
    const CanonicalLevelSceneWithMaterials& content,
    const CanonicalLevelSceneMaterialLimits& limits = CanonicalLevelSceneMaterialLimits{});

// [any worker thread; reentrant] Builds and explicitly tags the positional
// relationship documented by LevelContentIR. Limits are fixed safety maxima:
// callers may tighten but never widen them.
[[nodiscard]] std::expected<CanonicalLevelSceneWithMaterials, std::string>
BuildCanonicalLevelSceneWithMaterials(
    const asset::LevelContentIR& content,
    const CanonicalLevelSceneMaterialLimits& limits = CanonicalLevelSceneMaterialLimits{});
} // namespace omega::runtime
