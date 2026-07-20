#pragma once

#include "omega/asset/decode.h"
#include "omega/asset/par_text_envelope_ir.h"

#include <cstddef>
#include <span>

namespace omega::retail
{
// [any worker thread; reentrant] Decodes only the observed bounded PAR text
// envelope. It preserves body text opaquely and assigns no keys, values,
// comments, fields, paths, particle behavior, or version-compatibility
// defaults.
[[nodiscard]] asset::DecodeResult<asset::ParTextEnvelopeIR> DecodeParTextEnvelope(
    std::span<const std::byte> bytes, asset::DecodeLimits limits = {});
} // namespace omega::retail
