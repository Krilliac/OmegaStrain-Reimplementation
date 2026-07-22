#pragma once

#include "omega/asset/decode.h"

#include <cstdint>
#include <expected>
#include <optional>
#include <string>

namespace omega::asset
{
// Project-owned validation/evaluation failure. Model IR has no source-byte identity, so failures
// identify the affected semantic item rather than overloading DecodeError::byte_offset.
struct ModelIrError
{
    DecodeErrorCode code = DecodeErrorCode::Malformed;
    std::optional<std::uint64_t> item_index;
    std::string message;
};

template <typename Value>
using ModelIrResult = std::expected<Value, ModelIrError>;
} // namespace omega::asset
