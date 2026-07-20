#include "omega/runtime/packed24_transfer_debug_image.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <iostream>
#include <limits>
#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
using omega::asset::TexturePaletteStorageIR;
using omega::asset::TextureSampleEncoding;
using omega::asset::TextureStorageBlockIR;
using omega::asset::TextureStorageIR;
using omega::asset::TextureStoragePlaneIR;
using omega::asset::TextureTransferElementEncoding;
using omega::runtime::DebugImage;
using omega::runtime::Packed24TransferDebugImageError;
using omega::runtime::Packed24TransferDebugImageErrorCode;
using omega::runtime::Packed24TransferDebugImageLimits;

using BuilderSignature = std::expected<DebugImage, Packed24TransferDebugImageError> (*)(
    const TextureStorageIR&, const Packed24TransferDebugImageLimits&);
static_assert(std::is_same_v<
    decltype(&omega::runtime::BuildPacked24TransferDebugImage), BuilderSignature>);
static_assert(noexcept(omega::runtime::Packed24TransferDebugImageErrorCodeName(
    Packed24TransferDebugImageErrorCode::InvalidTextureDimensions)));
static_assert(noexcept(omega::runtime::Packed24TransferDebugImageErrorMessage(
    Packed24TransferDebugImageErrorCode::InvalidTextureDimensions)));
static_assert(omega::runtime::Packed24TransferDebugImageErrorCodeName(
                  Packed24TransferDebugImageErrorCode::InvalidTextureDimensions) ==
              "invalid-texture-dimensions");
static_assert(omega::runtime::Packed24TransferDebugImageErrorMessage(
                  Packed24TransferDebugImageErrorCode::InvalidTextureDimensions) ==
              "packed-24 transfer debug image requires nonzero texture dimensions");

int failures = 0;

void Check(const bool condition, const std::string_view message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

[[nodiscard]] TextureStorageIR MakeStorage(const std::uint8_t seed = 0x21U)
{
    constexpr std::uint32_t width = 16U;
    constexpr std::uint32_t height = 16U;
    std::vector<std::byte> bytes(static_cast<std::size_t>(width) * height * 3U);
    for (std::size_t index = 0U; index < bytes.size(); ++index)
    {
        bytes[index] =
            static_cast<std::byte>(static_cast<std::uint8_t>(seed + index));
    }
    return TextureStorageIR{
        .width = width,
        .height = height,
        .sample_encoding = TextureSampleEncoding::Packed24,
        .blocks = {
            TextureStorageBlockIR{
                .planes = {
                    TextureStoragePlaneIR{
                        .width = width,
                        .height = height,
                        .element_encoding = TextureTransferElementEncoding::Packed24,
                        .bytes = std::move(bytes),
                    },
                },
            },
        },
    };
}

[[nodiscard]] std::uint64_t Fnv1a64(const DebugImage& image) noexcept
{
    std::uint64_t hash = 14695981039346656037ULL;
    for (const std::byte value : image.rgba8_pixels)
    {
        hash ^= std::to_integer<std::uint8_t>(value);
        hash *= 1099511628211ULL;
    }
    return hash;
}

void CheckError(const TextureStorageIR& storage,
    const Packed24TransferDebugImageErrorCode code, const std::string_view message,
    const Packed24TransferDebugImageLimits& limits = {})
{
    const std::expected<DebugImage, Packed24TransferDebugImageError> result =
        omega::runtime::BuildPacked24TransferDebugImage(storage, limits);
    Check(!result && result.error().code == code &&
              result.error().message ==
                  omega::runtime::Packed24TransferDebugImageErrorMessage(code),
        message);
}

void CheckErrorContract()
{
    struct ErrorContract
    {
        Packed24TransferDebugImageErrorCode code;
        std::string_view name;
        std::string_view message;
    };
    constexpr std::array contracts{
        ErrorContract{Packed24TransferDebugImageErrorCode::InvalidTextureDimensions,
            "invalid-texture-dimensions",
            "packed-24 transfer debug image requires nonzero texture dimensions"},
        ErrorContract{Packed24TransferDebugImageErrorCode::InvalidSampleEncoding,
            "invalid-sample-encoding",
            "packed-24 transfer debug image sample encoding is invalid"},
        ErrorContract{Packed24TransferDebugImageErrorCode::UnsupportedSampleEncoding,
            "unsupported-sample-encoding",
            "packed-24 transfer debug image requires packed-24 sample encoding"},
        ErrorContract{Packed24TransferDebugImageErrorCode::BlockCountMismatch,
            "block-count-mismatch",
            "packed-24 transfer debug image requires exactly one block"},
        ErrorContract{Packed24TransferDebugImageErrorCode::PlaneCountMismatch,
            "plane-count-mismatch",
            "packed-24 transfer debug image requires exactly one plane"},
        ErrorContract{Packed24TransferDebugImageErrorCode::UnexpectedPalette,
            "unexpected-palette",
            "packed-24 transfer debug image does not accept a palette"},
        ErrorContract{Packed24TransferDebugImageErrorCode::InvalidPlaneDimensions,
            "invalid-plane-dimensions",
            "packed-24 transfer debug image requires nonzero plane dimensions"},
        ErrorContract{Packed24TransferDebugImageErrorCode::InvalidTransferElementEncoding,
            "invalid-transfer-element-encoding",
            "packed-24 transfer debug image transfer-element encoding is invalid"},
        ErrorContract{Packed24TransferDebugImageErrorCode::UnsupportedTransferElementEncoding,
            "unsupported-transfer-element-encoding",
            "packed-24 transfer debug image requires packed-24 transfer-element encoding"},
        ErrorContract{Packed24TransferDebugImageErrorCode::TexturePlaneDimensionMismatch,
            "texture-plane-dimension-mismatch",
            "packed-24 transfer debug image texture and plane dimensions do not match"},
        ErrorContract{Packed24TransferDebugImageErrorCode::SourceByteSizeOverflow,
            "source-byte-size-overflow",
            "packed-24 transfer debug image source byte size overflows"},
        ErrorContract{Packed24TransferDebugImageErrorCode::OutputByteSizeOverflow,
            "output-byte-size-overflow",
            "packed-24 transfer debug image output byte size overflows"},
        ErrorContract{Packed24TransferDebugImageErrorCode::SourceByteSizeMismatch,
            "source-byte-size-mismatch",
            "packed-24 transfer debug image source byte size does not match the transfer rectangle"},
        ErrorContract{Packed24TransferDebugImageErrorCode::SourceByteLimitExceeded,
            "source-byte-limit-exceeded",
            "packed-24 transfer debug image exceeds the source-byte limit"},
        ErrorContract{Packed24TransferDebugImageErrorCode::OutputByteLimitExceeded,
            "output-byte-limit-exceeded",
            "packed-24 transfer debug image exceeds the output-byte limit"},
        ErrorContract{Packed24TransferDebugImageErrorCode::AllocationFailed,
            "allocation-failed", "packed-24 transfer debug image allocation failed"},
    };

    bool exact = true;
    for (std::size_t index = 0U; index < contracts.size(); ++index)
    {
        const ErrorContract& contract = contracts[index];
        exact = exact && static_cast<std::size_t>(contract.code) == index &&
                omega::runtime::Packed24TransferDebugImageErrorCodeName(contract.code) ==
                    contract.name &&
                omega::runtime::Packed24TransferDebugImageErrorMessage(contract.code) ==
                    contract.message;
    }
    Check(exact, "all sixteen error ordinals, names, and fixed messages are frozen");

    constexpr auto unknown = static_cast<Packed24TransferDebugImageErrorCode>(255);
    Check(omega::runtime::Packed24TransferDebugImageErrorCodeName(unknown) == "unknown" &&
              omega::runtime::Packed24TransferDebugImageErrorMessage(unknown) ==
                  "packed-24 transfer debug image error is unknown",
        "unknown error values use fixed identity-free fallbacks");

    constexpr Packed24TransferDebugImageLimits defaults;
    Check(defaults.maximum_source_bytes == 48ULL * 1024ULL * 1024ULL &&
              defaults.maximum_output_bytes == 64ULL * 1024ULL * 1024ULL,
        "the independent synthetic source and output defaults are frozen");
}

void CheckValidationPriority()
{
    {
        TextureStorageIR storage = MakeStorage();
        storage.width = 0U;
        storage.sample_encoding = static_cast<TextureSampleEncoding>(255U);
        storage.blocks.clear();
        CheckError(storage, Packed24TransferDebugImageErrorCode::InvalidTextureDimensions,
            "texture dimensions have first validation priority");
    }
    {
        TextureStorageIR storage = MakeStorage();
        storage.sample_encoding = static_cast<TextureSampleEncoding>(255U);
        storage.blocks.clear();
        CheckError(storage, Packed24TransferDebugImageErrorCode::InvalidSampleEncoding,
            "an unknown sample enum fails before structural validation");
    }
    for (const TextureSampleEncoding encoding : {TextureSampleEncoding::Indexed4,
             TextureSampleEncoding::Indexed8, TextureSampleEncoding::Packed32})
    {
        TextureStorageIR storage = MakeStorage();
        storage.sample_encoding = encoding;
        storage.blocks.clear();
        CheckError(storage, Packed24TransferDebugImageErrorCode::UnsupportedSampleEncoding,
            "every known non-Packed24 sample encoding is unsupported before structure");
    }
    {
        TextureStorageIR storage = MakeStorage();
        storage.blocks.clear();
        CheckError(storage, Packed24TransferDebugImageErrorCode::BlockCountMismatch,
            "zero blocks fail the exact cardinality contract");
        storage = MakeStorage();
        storage.blocks.push_back(TextureStorageBlockIR{});
        CheckError(storage, Packed24TransferDebugImageErrorCode::BlockCountMismatch,
            "multiple blocks fail without selecting a first block");
    }
    {
        TextureStorageIR storage = MakeStorage();
        storage.blocks.front().planes.clear();
        storage.blocks.front().palette.emplace();
        CheckError(storage, Packed24TransferDebugImageErrorCode::PlaneCountMismatch,
            "plane cardinality precedes palette validation");
        storage = MakeStorage();
        storage.blocks.front().planes.push_back(TextureStoragePlaneIR{});
        CheckError(storage, Packed24TransferDebugImageErrorCode::PlaneCountMismatch,
            "multiple planes fail without selecting a first plane");
    }
    {
        TextureStorageIR storage = MakeStorage();
        storage.blocks.front().palette.emplace(TexturePaletteStorageIR{});
        storage.blocks.front().planes.front().width = 0U;
        CheckError(storage, Packed24TransferDebugImageErrorCode::UnexpectedPalette,
            "palette rejection precedes plane validation");
    }
    {
        TextureStorageIR storage = MakeStorage();
        storage.blocks.front().planes.front().width = 0U;
        storage.blocks.front().planes.front().element_encoding =
            static_cast<TextureTransferElementEncoding>(255U);
        CheckError(storage, Packed24TransferDebugImageErrorCode::InvalidPlaneDimensions,
            "plane dimensions precede transfer-element validation");
    }
    {
        TextureStorageIR storage = MakeStorage();
        storage.blocks.front().planes.front().element_encoding =
            static_cast<TextureTransferElementEncoding>(255U);
        storage.blocks.front().planes.front().width = 15U;
        CheckError(storage,
            Packed24TransferDebugImageErrorCode::InvalidTransferElementEncoding,
            "an unknown transfer enum fails before dimension matching");
    }
    for (const TextureTransferElementEncoding encoding : {
             TextureTransferElementEncoding::Packed4,
             TextureTransferElementEncoding::Packed8,
             TextureTransferElementEncoding::Packed32})
    {
        TextureStorageIR storage = MakeStorage();
        storage.blocks.front().planes.front().element_encoding = encoding;
        storage.blocks.front().planes.front().width = 15U;
        CheckError(storage,
            Packed24TransferDebugImageErrorCode::UnsupportedTransferElementEncoding,
            "every known non-Packed24 transfer encoding is unsupported before dimensions");
    }
    {
        TextureStorageIR storage = MakeStorage();
        storage.blocks.front().planes.front().width = 15U;
        CheckError(storage,
            Packed24TransferDebugImageErrorCode::TexturePlaneDimensionMismatch,
            "texture and plane rectangles must match exactly");
    }
    {
        TextureStorageIR storage = MakeStorage();
        storage.width = std::numeric_limits<std::uint32_t>::max();
        storage.height = std::numeric_limits<std::uint32_t>::max();
        storage.blocks.front().planes.front().width = storage.width;
        storage.blocks.front().planes.front().height = storage.height;
        storage.blocks.front().planes.front().bytes.clear();
        CheckError(storage, Packed24TransferDebugImageErrorCode::SourceByteSizeOverflow,
            "source-byte multiplication has an independent no-allocation overflow oracle");
    }
    {
        TextureStorageIR storage = MakeStorage();
        storage.width = std::numeric_limits<std::uint32_t>::max();
        storage.height = 0x40000001U;
        storage.blocks.front().planes.front().width = storage.width;
        storage.blocks.front().planes.front().height = storage.height;
        storage.blocks.front().planes.front().bytes.clear();
        CheckError(storage, Packed24TransferDebugImageErrorCode::OutputByteSizeOverflow,
            "output-byte multiplication has an independent no-allocation overflow oracle");
    }
    {
        TextureStorageIR storage = MakeStorage();
        storage.blocks.front().planes.front().bytes.pop_back();
        const Packed24TransferDebugImageLimits zero_limits{
            .maximum_source_bytes = 0U,
            .maximum_output_bytes = 0U,
        };
        CheckError(storage, Packed24TransferDebugImageErrorCode::SourceByteSizeMismatch,
            "source cardinality mismatch precedes both independent budgets", zero_limits);

        storage = MakeStorage();
        storage.blocks.front().planes.front().bytes.push_back(std::byte{0x00});
        CheckError(storage, Packed24TransferDebugImageErrorCode::SourceByteSizeMismatch,
            "one extra source byte also fails exact transfer-rectangle cardinality");
    }
    {
        const TextureStorageIR storage = MakeStorage();
        const Packed24TransferDebugImageLimits source_tight{
            .maximum_source_bytes = 767U,
            .maximum_output_bytes = 1023U,
        };
        CheckError(storage, Packed24TransferDebugImageErrorCode::SourceByteLimitExceeded,
            "source-byte budget precedes the output-byte budget", source_tight);
        const Packed24TransferDebugImageLimits output_tight{
            .maximum_source_bytes = 768U,
            .maximum_output_bytes = 1023U,
        };
        CheckError(storage, Packed24TransferDebugImageErrorCode::OutputByteLimitExceeded,
            "the one-byte-short output budget has its typed failure", output_tight);
    }
}

void CheckMappingAndOwnership()
{
    TextureStorageIR source_21 = MakeStorage(0x21U);
    const TextureStorageIR source_before = source_21;
    auto first = omega::runtime::BuildPacked24TransferDebugImage(source_21);
    auto repeated = omega::runtime::BuildPacked24TransferDebugImage(source_21);
    TextureStorageIR source_61 = MakeStorage(0x61U);
    auto second_seed = omega::runtime::BuildPacked24TransferDebugImage(source_61);
    Check(first && repeated && second_seed,
        "both generated seeds and a repeated call build owned diagnostic images");
    Check(source_21 == source_before,
        "building a transfer diagnostic does not mutate borrowed storage");
    if (!first || !repeated || !second_seed)
        return;

    Check(first->width == 16U && first->height == 16U &&
              first->rgba8_pixels.size() == 1024U && second_seed->width == 16U &&
              second_seed->height == 16U && second_seed->rgba8_pixels.size() == 1024U,
        "both fixtures map 768 source slots into an owned 16x16 1024-byte image");
    Check(Fnv1a64(*first) == 0x4abb645f50f5a325ULL &&
              Fnv1a64(*second_seed) == 0x36590f25eee3ab25ULL &&
              first->rgba8_pixels != second_seed->rgba8_pixels,
        "the two payload-sensitive fixtures have their independent frozen signatures");

    constexpr std::array first_21{
        std::byte{0x21}, std::byte{0x22}, std::byte{0x23}, std::byte{0xff},
        std::byte{0x24}, std::byte{0x25}, std::byte{0x26}, std::byte{0xff},
        std::byte{0x27}, std::byte{0x28}, std::byte{0x29}, std::byte{0xff},
        std::byte{0x2a}, std::byte{0x2b}, std::byte{0x2c}, std::byte{0xff},
    };
    constexpr std::array final_21{
        std::byte{0x15}, std::byte{0x16}, std::byte{0x17}, std::byte{0xff},
        std::byte{0x18}, std::byte{0x19}, std::byte{0x1a}, std::byte{0xff},
        std::byte{0x1b}, std::byte{0x1c}, std::byte{0x1d}, std::byte{0xff},
        std::byte{0x1e}, std::byte{0x1f}, std::byte{0x20}, std::byte{0xff},
    };
    constexpr std::array first_61{
        std::byte{0x61}, std::byte{0x62}, std::byte{0x63}, std::byte{0xff},
        std::byte{0x64}, std::byte{0x65}, std::byte{0x66}, std::byte{0xff},
        std::byte{0x67}, std::byte{0x68}, std::byte{0x69}, std::byte{0xff},
        std::byte{0x6a}, std::byte{0x6b}, std::byte{0x6c}, std::byte{0xff},
    };
    constexpr std::array final_61{
        std::byte{0x55}, std::byte{0x56}, std::byte{0x57}, std::byte{0xff},
        std::byte{0x58}, std::byte{0x59}, std::byte{0x5a}, std::byte{0xff},
        std::byte{0x5b}, std::byte{0x5c}, std::byte{0x5d}, std::byte{0xff},
        std::byte{0x5e}, std::byte{0x5f}, std::byte{0x60}, std::byte{0xff},
    };
    Check(std::equal(first_21.begin(), first_21.end(), first->rgba8_pixels.begin()) &&
              std::equal(final_21.begin(), final_21.end(),
                  first->rgba8_pixels.end() -
                      static_cast<std::ptrdiff_t>(final_21.size())) &&
              std::equal(first_61.begin(), first_61.end(),
                  second_seed->rgba8_pixels.begin()) &&
              std::equal(final_61.begin(), final_61.end(),
                  second_seed->rgba8_pixels.end() -
                      static_cast<std::ptrdiff_t>(final_61.size())),
        "both seeds preserve the exact first and final source-slot groups");

    bool exact_mapping = true;
    std::size_t synthetic_ff_slots = 0U;
    const std::vector<std::byte>& source_bytes =
        source_21.blocks.front().planes.front().bytes;
    for (std::size_t element = 0U; element < 256U; ++element)
    {
        const std::size_t source_offset = element * 3U;
        const std::size_t output_offset = element * 4U;
        exact_mapping = exact_mapping &&
                        first->rgba8_pixels[output_offset] == source_bytes[source_offset] &&
                        first->rgba8_pixels[output_offset + 1U] ==
                            source_bytes[source_offset + 1U] &&
                        first->rgba8_pixels[output_offset + 2U] ==
                            source_bytes[source_offset + 2U] &&
                        first->rgba8_pixels[output_offset + 3U] == std::byte{0xff};
        synthetic_ff_slots +=
            first->rgba8_pixels[output_offset + 3U] == std::byte{0xff} ? 1U : 0U;
    }
    Check(exact_mapping && synthetic_ff_slots == 256U,
        "every source triple maps in order and all 256 synthetic fourth slots are FF");
    Check(first->rgba8_pixels == repeated->rgba8_pixels &&
              first->rgba8_pixels.data() != repeated->rgba8_pixels.data(),
        "repeated results are deterministic and independently allocated");

    const Packed24TransferDebugImageLimits exact_limits{
        .maximum_source_bytes = 768U,
        .maximum_output_bytes = 1024U,
    };
    auto exact =
        omega::runtime::BuildPacked24TransferDebugImage(source_21, exact_limits);
    Check(exact && exact->rgba8_pixels == first->rgba8_pixels,
        "the exact independent source and output limits succeed");

    const std::vector<std::byte> owned_before_source_change = first->rgba8_pixels;
    std::ranges::fill(
        source_21.blocks.front().planes.front().bytes, std::byte{0x00});
    source_21.blocks.clear();
    Check(first->rgba8_pixels == owned_before_source_change,
        "the returned image survives source mutation and destruction of source-owned bytes");
}
} // namespace

int main()
{
    CheckErrorContract();
    CheckValidationPriority();
    CheckMappingAndOwnership();

    if (failures == 0)
        std::cout << "omega_packed24_transfer_debug_image_tests: all checks passed\n";
    return failures == 0 ? 0 : 1;
}
