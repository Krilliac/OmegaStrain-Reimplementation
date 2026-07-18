#pragma once

#include "omega/asset/source_locator.h"

#include <cstdint>
#include <string>
#include <vector>

namespace omega::asset
{
struct LevelCellSourceIR
{
    // Preserved reference fields whose gameplay meaning is not yet established.
    std::uint32_t observed_kind = 0;
    std::uint32_t observed_index = 0;
    std::string data_hog_entry;
};

// Canonical owned manifest data. The common DATA.HOG source is stored once; each cell stores only
// its canonical member name. Texture sources are explicit owned container locations in deterministic
// discovery order only: their positions assert no priority, cell/material/name relationship, or
// asset binding. Geometry, transforms, visibility, and materials are deliberately absent until
// their source semantics are independently established.
struct LevelManifestIR
{
    SourceLocator data_hog_source;
    std::vector<SourceLocator> texture_sources;
    std::vector<LevelCellSourceIR> terrain_cells;
};
} // namespace omega::asset
