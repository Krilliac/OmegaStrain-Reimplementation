#include "omega/runtime/manifest_debug_image.h"

#include <algorithm>
#include <cstddef>
#include <iostream>
#include <string_view>

namespace
{
int failures = 0;

void Check(const bool condition, const std::string_view message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

omega::asset::LevelManifestIR MakeManifest()
{
    return omega::asset::LevelManifestIR{
        .data_hog_source = {.game_path = "GAMEDATA/MINSK/DATA.HOG", .hog_entries = {}},
        .terrain_cells = {
            {.observed_kind = 1, .observed_index = 2, .data_hog_entry = "A.HOG"},
            {.observed_kind = 3, .observed_index = 4, .data_hog_entry = "B.HOG"},
            {.observed_kind = 5, .observed_index = 6, .data_hog_entry = "C.HOG"},
        },
    };
}
} // namespace

int ManifestDebugImageFailureCount()
{
    const auto manifest = MakeManifest();
    auto first = omega::runtime::BuildManifestDebugImage(manifest);
    auto second = omega::runtime::BuildManifestDebugImage(manifest);
    Check(first && first->width == 24 && first->height == 24 &&
              first->rgba8_pixels.size() == 24U * 24U * 4U,
        "three manifest cells produce a bounded square synthetic grid");
    Check(first && second && first->rgba8_pixels == second->rgba8_pixels,
        "manifest debug pixels are deterministic");
    if (first)
    {
        bool all_alpha_opaque = true;
        for (std::size_t index = 3; index < first->rgba8_pixels.size(); index += 4U)
            all_alpha_opaque = all_alpha_opaque && first->rgba8_pixels[index] == std::byte{255};
        Check(all_alpha_opaque, "the debug image publishes fully opaque RGBA8 pixels");
    }

    auto changed_manifest = manifest;
    changed_manifest.terrain_cells[1].observed_index += 1U;
    auto changed = omega::runtime::BuildManifestDebugImage(changed_manifest);
    Check(first && changed && first->rgba8_pixels != changed->rgba8_pixels,
        "canonical manifest changes alter the synthetic debug view");

    auto empty = omega::runtime::BuildManifestDebugImage({});
    Check(empty && empty->width == 12 && empty->height == 12 &&
              empty->rgba8_pixels.size() == 12U * 12U * 4U,
        "an empty manifest still produces a safe diagnostic image");
    return failures;
}
