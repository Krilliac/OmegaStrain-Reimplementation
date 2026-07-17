#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <string>

namespace omega::asset
{
struct DecodeLimits
{
    // Per top-level decoder call. Composed decoders must debit a shared operation context rather
    // than resetting these maxima for every child; that context will be introduced with the first
    // nested semantic asset pipeline.
    std::uint64_t maximum_input_bytes = 64ULL * 1024ULL * 1024ULL;
    // Logical owned result: value/vector/string objects plus their character or payload bytes.
    std::uint64_t maximum_output_bytes = 256ULL * 1024ULL * 1024ULL;
    // Conservative decoder-owned transient storage, separate from caller-owned input and output.
    std::uint64_t maximum_scratch_bytes = 256ULL * 1024ULL * 1024ULL;
    std::uint64_t maximum_items = 1ULL << 20U;
    std::uint32_t maximum_string_bytes = 4096;
    std::uint32_t maximum_nesting_depth = 8;
};

enum class DecodeErrorCode
{
    Truncated,
    Malformed,
    Overflow,
    LimitExceeded,
    UnsupportedVariant,
    InvalidReference,
    DuplicateReference,
};

struct DecodeError
{
    DecodeErrorCode code = DecodeErrorCode::Malformed;
    std::optional<std::uint64_t> byte_offset;
    std::string message;
};

template <typename Value>
using DecodeResult = std::expected<Value, DecodeError>;
} // namespace omega::asset
