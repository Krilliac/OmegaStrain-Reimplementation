#pragma once

#include "omega/archive/hog_archive.h"
#include "omega/asset/decode.h"
#include "omega/asset/level_ir.h"
#include "omega/asset/source_locator.h"

#include <cstddef>
#include <span>

namespace omega::retail
{
// [any worker thread; reentrant] Resolves the independently documented POP TER prefix against one
// DATA.HOG directory. The result owns every string and retains no input spans or retail bytes.
[[nodiscard]] asset::DecodeResult<asset::LevelManifestIR> DecodePopLevelManifest(
    std::span<const std::byte> pop_bytes,
    std::span<const archive::HogEntry> data_hog_entries,
    const asset::SourceLocator& data_hog_source,
    asset::DecodeLimits limits = {});
} // namespace omega::retail
