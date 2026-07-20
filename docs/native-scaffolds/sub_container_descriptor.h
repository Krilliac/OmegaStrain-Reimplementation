#pragma once

// SCAFFOLD - evidence-pending. This is NOT a decoder. It is NOT wired into CMakeLists.txt, the
// runtime, or any test, and it makes NO coverage claim. It lives under docs/native-scaffolds/ (not
// native/) precisely so the native-dependency gate does not treat it as an active shipping header.
//
// Format .sub - tracked aggregate occurrences: recursive-in-HOG = 42, top-level-HOG = 42.
// Tracked STRUCTURAL evidence for this format: NONE. tools/fingerprint_assets.py has no handler for
// .sub, and no analysis/formats/*.md documents its layout, so no field, offset, count, header,
// role, or alignment is known. Do not infer any - a plausible invented decoder is a regression per
// the clean-room brief.
//
// This file commits ONLY to the established bounded passive-descriptor pattern and entry point. The
// descriptor holds a single opaque whole-input extent and nothing else.
//
// Codex: to turn this into a real descriptor,
//   1. Run tools/measure_member_structural_fingerprint.py (or add a fingerprint_sub handler to
//      tools/fingerprint_assets.py) against the owner corpus and publish only the aggregate result.
//   2. Populate this struct with ObservedByteRange regions / counts ONLY for structure that aggregate
//      proves. Add a manual operator== over those members (see SkmContainerDescriptor for the pattern;
//      ObservedExtent has no defaulted ==).
//   3. Move this header to native/include/omega/retail/sub_container_descriptor.h, add
//      native/src/retail/sub_container_descriptor.cpp implementing InspectSubContainer as a
//      bounded, fail-closed, allocation-checked reader honoring DecodeLimits.
//   4. Register both in CMakeLists.txt and add native/tests/sub_container_descriptor_tests.cpp
//      (exact/malformed/truncated/limit/determinism adversarial coverage) BEFORE claiming coverage.

#include "omega/asset/decode.h"
#include "omega/retail/container_descriptors.h"

#include <cstddef>
#include <span>

namespace omega::retail
{
// Passive structural descriptor placeholder for the .sub member family. Opaque until tracked
// evidence proves internal structure; no semantics are assigned to any payload byte.
struct SubContainerDescriptor
{
    // The whole input treated as one opaque span. Replace/extend with ObservedByteRange regions and
    // counts ONLY as tracked fingerprint evidence proves them. Do not add invented named fields.
    ObservedExtent logical_extent;
};

// [any worker thread; reentrant] Passive, allocation-free structural inspector. Retains no input span
// and assigns no semantics. Implementation is intentionally absent pending tracked structural evidence.
[[nodiscard]] asset::DecodeResult<SubContainerDescriptor> InspectSubContainer(
    std::span<const std::byte> bytes, asset::DecodeLimits limits = {});
} // namespace omega::retail
