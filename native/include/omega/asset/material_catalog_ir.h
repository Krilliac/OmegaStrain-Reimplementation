#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace omega::asset
{
struct MaterialCatalogEntryIR
{
    // Dense source-order indices into MaterialCatalogIR::names. Each referenced name's role is
    // deliberately unassigned until the render/material binding grammar is proven.
    std::array<std::uint32_t, 3> name_indices{};
    std::uint8_t name_count = 0;

    bool operator==(const MaterialCatalogEntryIR&) const = default;
};

// Canonical, fully owned material/name relationships. Retail record words, byte offsets,
// packet data, console instructions, and renderer/GPU objects are intentionally absent.
struct MaterialCatalogIR
{
    std::vector<std::string> names;
    std::vector<MaterialCatalogEntryIR> materials;

    bool operator==(const MaterialCatalogIR&) const = default;
};
} // namespace omega::asset
