#include "omega/content/game_data_service.h"

#include <array>
#include <bit>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
constexpr std::size_t kTdxHeaderBytes = 64U;
constexpr std::size_t kTdxPrimaryObject = kTdxHeaderBytes + 0xC0U;
constexpr std::size_t kTdxPaletteObject = kTdxHeaderBytes + 0x40U;
constexpr std::size_t kTdxPaletteData = kTdxHeaderBytes + 0x120U;
constexpr std::size_t kTdxPrimaryData = kTdxHeaderBytes + 0x520U;
constexpr std::string_view kAtlasMember = "SYNTH001.TDX";

int failures = 0;

void Check(const bool condition, const std::string_view message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

[[nodiscard]] std::optional<std::filesystem::path> PrivateRetailDataPath()
{
#if defined(_WIN32)
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
    const wchar_t* const value = _wgetenv(L"OPENOMEGA_PRIVATE_RETAIL_DATA");
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
    if (value == nullptr || value[0] == L'\0')
        return std::nullopt;
    return std::filesystem::path(value);
#else
    const char* const value = std::getenv("OPENOMEGA_PRIVATE_RETAIL_DATA");
    if (value == nullptr || value[0] == '\0')
        return std::nullopt;
    return std::filesystem::path(value);
#endif
}

void AppendU16(std::vector<std::byte>& bytes, const std::uint16_t value)
{
    bytes.push_back(static_cast<std::byte>(value & 0xFFU));
    bytes.push_back(static_cast<std::byte>((value >> 8U) & 0xFFU));
}

void AppendU32(std::vector<std::byte>& bytes, const std::uint32_t value)
{
    for (unsigned shift = 0; shift < 32U; shift += 8U)
        bytes.push_back(static_cast<std::byte>((value >> shift) & 0xFFU));
}

void WriteU16(
    std::vector<std::byte>& bytes, const std::size_t offset, const std::uint16_t value)
{
    bytes[offset] = static_cast<std::byte>(value & 0xFFU);
    bytes[offset + 1U] = static_cast<std::byte>((value >> 8U) & 0xFFU);
}

void WriteU32(
    std::vector<std::byte>& bytes, const std::size_t offset, const std::uint32_t value)
{
    for (unsigned shift = 0; shift < 32U; shift += 8U)
        bytes[offset + shift / 8U] = static_cast<std::byte>((value >> shift) & 0xFFU);
}

void WriteU64(
    std::vector<std::byte>& bytes, const std::size_t offset, const std::uint64_t value)
{
    WriteU32(bytes, offset, static_cast<std::uint32_t>(value));
    WriteU32(bytes, offset + 4U, static_cast<std::uint32_t>(value >> 32U));
}

void AppendF32(std::vector<std::byte>& bytes, const float value)
{
    AppendU32(bytes, std::bit_cast<std::uint32_t>(value));
}

void WriteF32(std::vector<std::byte>& bytes, const std::size_t offset, const float value)
{
    WriteU32(bytes, offset, std::bit_cast<std::uint32_t>(value));
}

void AppendString(std::vector<std::byte>& bytes, const std::string_view value)
{
    for (const char character : value)
        bytes.push_back(static_cast<std::byte>(character));
    bytes.push_back(std::byte{0});
}

void Align4(std::vector<std::byte>& bytes)
{
    while ((bytes.size() & 3U) != 0U)
        bytes.push_back(std::byte{0xA5});
}

void Align16Zero(std::vector<std::byte>& bytes)
{
    while ((bytes.size() & 15U) != 0U)
        bytes.push_back(std::byte{0});
}

[[nodiscard]] std::vector<std::byte> Bytes(const std::string_view text)
{
    std::vector<std::byte> bytes;
    bytes.reserve(text.size());
    for (const char character : text)
        bytes.push_back(static_cast<std::byte>(character));
    return bytes;
}

struct HogMember final
{
    std::string name;
    std::vector<std::byte> payload;
};

[[nodiscard]] std::vector<std::byte> MakeHog(const std::vector<HogMember>& members)
{
    const std::size_t names_offset = 0x14U + 4U * (members.size() + 1U);
    std::size_t names_end = names_offset;
    std::size_t payload_size = 0;
    for (const auto& member : members)
    {
        names_end += member.name.size() + 1U;
        payload_size += member.payload.size();
    }
    const std::size_t data_offset = (names_end + 15U) & ~std::size_t{15U};
    std::vector<std::byte> bytes(data_offset, std::byte{0});
    WriteU32(bytes, 0x00U, 0x4052673DU);
    WriteU32(bytes, 0x04U, static_cast<std::uint32_t>(members.size()));
    WriteU32(bytes, 0x08U, 0x14U);
    WriteU32(bytes, 0x0CU, static_cast<std::uint32_t>(names_offset));
    WriteU32(bytes, 0x10U, static_cast<std::uint32_t>(data_offset));

    std::size_t name_cursor = names_offset;
    std::uint32_t payload_cursor = 0;
    for (std::size_t index = 0; index < members.size(); ++index)
    {
        WriteU32(bytes, 0x14U + 4U * index, payload_cursor);
        for (const char character : members[index].name)
            bytes[name_cursor++] = static_cast<std::byte>(character);
        ++name_cursor;
        payload_cursor += static_cast<std::uint32_t>(members[index].payload.size());
    }
    WriteU32(bytes, 0x14U + 4U * members.size(), payload_cursor);
    bytes.reserve(data_offset + payload_size);
    for (const auto& member : members)
        bytes.insert(bytes.end(), member.payload.begin(), member.payload.end());
    return bytes;
}

struct GuiNode final
{
    std::string factory;
    std::string identifier;
    std::string text_reference;
    std::string font_reference;
    bool decorated = false;
    std::string scope_reference;
    std::string resource_reference;
    std::vector<GuiNode> children;
};

void AppendGuiNode(std::vector<std::byte>& bytes, const GuiNode& node)
{
    AppendString(bytes, node.factory);
    AppendString(bytes, node.decorated ? "GuiInterfaceDecorator" : "");
    AppendString(bytes, node.identifier);
    Align4(bytes);
    for (unsigned index = 0; index < 4U; ++index)
        AppendF32(bytes, static_cast<float>(index + 1U));
    AppendU16(bytes, 1U);
    AppendU16(bytes, 0U);
    if (node.factory == "GuiTextWidget" || node.factory == "GuiButtonWidget")
    {
        AppendString(bytes, node.text_reference);
        AppendString(bytes, node.font_reference);
        Align4(bytes);
        AppendF32(bytes, 0.25F);
        AppendF32(bytes, 0.5F);
        AppendF32(bytes, 0.75F);
        AppendU32(bytes, 2U);
    }
    if (node.decorated)
    {
        AppendString(bytes, node.scope_reference);
        AppendString(bytes, node.resource_reference);
        Align4(bytes);
        for (unsigned index = 0; index < 12U; ++index)
            AppendF32(bytes, static_cast<float>(index));
        AppendU16(bytes, 0U);
    }
    AppendU16(bytes, static_cast<std::uint16_t>(node.children.size()));
    for (const auto& child : node.children)
        AppendGuiNode(bytes, child);
}

[[nodiscard]] std::vector<std::byte> MakeGui(
    const std::string_view root_identifier,
    const std::string_view localized_reference = "$CreateAgent",
    const bool unsafe_scope = false, const bool wrong_resource_case = false,
    const std::size_t extra_scope_count = 0U)
{
    const GuiNode localized{
        .factory = "GuiTextWidget",
        .identifier = "first",
        .text_reference = std::string(localized_reference),
        .font_reference = "",
        .decorated = true,
    };
    const GuiNode escaped{
        .factory = "GuiTextWidget",
        .identifier = "escaped",
        .text_reference = "$$LiteralDollar",
        .font_reference = "default",
    };
    std::string external_resource(root_identifier);
    external_resource.append("_external");
    std::string authored_resource = external_resource;
    if (wrong_resource_case && !authored_resource.empty())
    {
        const char value = authored_resource.front();
        authored_resource.front() = value >= 'A' && value <= 'Z'
            ? static_cast<char>(value + ('a' - 'A'))
            : static_cast<char>(value - ('a' - 'A'));
    }
    std::vector<GuiNode> children{
        localized,
        escaped,
        GuiNode{
            .factory = "GuiWidget",
            .identifier = "external_binding_a",
            .decorated = true,
            .scope_reference = unsafe_scope ? "../UNTRUSTED_MEMBER" : "sharedscope",
            .resource_reference = authored_resource,
        },
        GuiNode{
            .factory = "GuiWidget",
            .identifier = "external_binding_b",
            .decorated = true,
            .scope_reference = "SharedScope",
            .resource_reference = authored_resource,
        },
    };
    for (std::size_t index = 0; index < extra_scope_count; ++index)
    {
        children.push_back(GuiNode{
            .factory = "GuiWidget",
            .identifier = "extra_binding_" + std::to_string(index),
            .decorated = true,
            .scope_reference = "EXTRA_SCOPE_" + std::to_string(index),
            .resource_reference = "unused",
        });
    }
    const GuiNode root{
        .factory = "GuiWidget",
        .identifier = std::string(root_identifier),
        .decorated = true,
        .children = std::move(children),
    };
    std::vector<std::byte> bytes{
        std::byte{'G'}, std::byte{'U'}, std::byte{'I'},
        std::byte{0x7E}, std::byte{0x34}, std::byte{0x12}};
    AppendGuiNode(bytes, root);
    Align16Zero(bytes);
    return bytes;
}

struct IeNode final
{
    std::string identifier;
    std::string texture_basename;
    std::vector<IeNode> children;
};

void AppendIeNode(std::vector<std::byte>& bytes, const IeNode& node)
{
    AppendString(bytes, node.identifier);
    AppendString(bytes, node.texture_basename);
    Align4(bytes);
    for (unsigned index = 0; index < 12U; ++index)
        AppendF32(bytes, static_cast<float>(index));
    for (unsigned index = 0; index < 4U; ++index)
        AppendU32(bytes, 0U);
    AppendU32(bytes, 0U);
    AppendU32(bytes, 0U);
    AppendU32(bytes, static_cast<std::uint32_t>(node.children.size()));
    for (const auto& child : node.children)
        AppendIeNode(bytes, child);
}

[[nodiscard]] std::vector<std::byte> MakeIe(
    const std::string_view root_identifier, const std::string_view texture_basename,
    const bool unsafe_reference = false)
{
    const std::string first_texture = unsafe_reference
        ? "../UNTRUSTED_MEMBER"
        : std::string(texture_basename);
    std::string folded(texture_basename);
    for (char& value : folded)
    {
        if (value >= 'A' && value <= 'Z')
            value = static_cast<char>(value + ('a' - 'A'));
    }
    const IeNode root{
        .identifier = std::string(root_identifier) + "_root",
        .children = {
            IeNode{.identifier = "first", .texture_basename = first_texture},
            IeNode{.identifier = "duplicate", .texture_basename = folded},
        },
    };
    std::vector<std::byte> bytes{
        std::byte{0x11}, std::byte{0x22}, std::byte{0x33}, std::byte{0x44}};
    AppendIeNode(bytes, root);
    Align16Zero(bytes);
    return bytes;
}

void WriteTdxPacket(std::vector<std::byte>& bytes, const std::size_t object,
    const std::uint16_t base_pointer, const std::uint16_t buffer_width,
    const std::uint16_t width, const std::uint16_t height,
    const std::uint32_t payload_bytes)
{
    const std::uint32_t qword_count = payload_bytes / 16U;
    WriteU32(bytes, object + 0x00U, 0U);
    WriteU32(bytes, object + 0x04U,
        base_pointer | (static_cast<std::uint32_t>(buffer_width) << 16U));
    WriteU64(bytes, object + 0x08U, 0x50U);
    WriteU64(bytes, object + 0x10U, 0U);
    WriteU64(bytes, object + 0x18U, 0x51U);
    WriteU32(bytes, object + 0x20U, width);
    WriteU32(bytes, object + 0x24U, height);
    WriteU64(bytes, object + 0x28U, 0x52U);
    WriteU64(bytes, object + 0x30U, 0U);
    WriteU64(bytes, object + 0x38U, 0x53U);
    WriteU32(bytes, object + 0x40U, qword_count);
    WriteU32(bytes, object + 0x44U, 0x08000000U);
    WriteU64(bytes, object + 0x48U, 0U);
    WriteU32(bytes, object + 0x50U, 0x30000000U | qword_count);
    WriteU32(bytes, object + 0x54U, 0U);
    WriteU64(bytes, object + 0x58U, 0U);
}

void WriteTdxTransferControlPrefix(std::vector<std::byte>& bytes, const std::size_t prefix)
{
    constexpr std::uint32_t kDmaCntTag = (1U << 28U) | 6U;
    constexpr std::uint64_t kPackedAdGifTag = (1ULL << 60U) | 4U;
    constexpr std::uint64_t kAdRegisterDescriptor = 0x0EULL;
    WriteU32(bytes, prefix + 0x00U, kDmaCntTag);
    WriteU32(bytes, prefix + 0x04U, 0U);
    WriteU64(bytes, prefix + 0x08U, 0U);
    WriteU64(bytes, prefix + 0x10U, kPackedAdGifTag);
    WriteU64(bytes, prefix + 0x18U, kAdRegisterDescriptor);
}

[[nodiscard]] std::vector<std::byte> MakeTdx(const std::uint8_t seed)
{
    constexpr std::uint16_t width = 32U;
    constexpr std::uint16_t height = 32U;
    constexpr std::uint16_t upload_width = width / 2U;
    constexpr std::uint16_t upload_height = height / 2U;
    constexpr std::uint32_t primary_bytes = upload_width * upload_height * 4U;
    constexpr std::uint32_t palette_bytes = 16U * 16U * 4U;
    constexpr std::uint32_t stride = 0x520U + primary_bytes;

    std::vector<std::byte> bytes(kTdxHeaderBytes + stride, std::byte{0});
    WriteU16(bytes, 0x00U, 5U);
    WriteU16(bytes, 0x02U, 5U);
    WriteU16(bytes, 0x04U, width);
    WriteU16(bytes, 0x06U, height);
    WriteU16(bytes, 0x08U, 8U);
    WriteU16(bytes, 0x0AU, 0x13U);
    WriteU16(bytes, 0x0CU, 2U);
    WriteU16(bytes, 0x0EU, 4U);
    WriteU16(bytes, 0x10U, 16U);
    WriteU16(bytes, 0x12U, 16U);
    WriteU16(bytes, 0x14U, 32U);
    WriteU16(bytes, 0x16U, 0U);
    WriteU16(bytes, 0x18U, 1U);
    WriteU16(bytes, 0x1AU, 4U);
    WriteU16(bytes, 0x22U, 1U);
    WriteU16(bytes, 0x24U, 1U);
    WriteU16(bytes, 0x26U, 1U);
    WriteU16(bytes, 0x34U, 128U);
    WriteU16(bytes, 0x36U, 128U);
    WriteU32(bytes, 0x38U, stride);
    WriteU32(bytes, kTdxHeaderBytes + 0x00U, 0x20U);
    WriteU32(bytes, kTdxHeaderBytes + 0x14U, 0x20U);
    WriteU32(bytes, kTdxHeaderBytes + 0x18U, 0xA0U);
    WriteU32(bytes, kTdxHeaderBytes + 0x1CU, 0x20U);
    WriteTdxTransferControlPrefix(bytes, kTdxHeaderBytes + 0x20U);
    WriteTdxTransferControlPrefix(bytes, kTdxHeaderBytes + 0xA0U);
    WriteTdxPacket(bytes, kTdxPrimaryObject, 0U, 1U,
        upload_width, upload_height, primary_bytes);
    WriteTdxPacket(bytes, kTdxPaletteObject, 0U, 1U, 16U, 16U, palette_bytes);

    for (std::uint32_t index = 0; index < 256U; ++index)
    {
        bytes[kTdxPaletteData + index * 4U] = static_cast<std::byte>(index & 0xFFU);
        bytes[kTdxPaletteData + index * 4U + 1U] = static_cast<std::byte>(seed);
        bytes[kTdxPaletteData + index * 4U + 2U] = std::byte{0x33};
        bytes[kTdxPaletteData + index * 4U + 3U] = std::byte{0x80};
    }
    for (std::uint32_t index = 0; index < primary_bytes; ++index)
    {
        bytes[kTdxPrimaryData + index] =
            static_cast<std::byte>((73U * index + seed) & 0xFFU);
    }
    return bytes;
}

[[nodiscard]] std::vector<std::byte> MakeFnt()
{
    constexpr std::uint8_t glyph_count = 1U;
    constexpr std::size_t glyph_offset = 19U;
    constexpr std::size_t structural_end = glyph_offset + 16U + 2U;
    std::vector<std::byte> bytes(structural_end + 11U, std::byte{0});
    WriteU16(bytes, 0U, 3U);
    bytes[2U] = std::byte{13};
    for (std::size_t index = 0; index < kAtlasMember.size(); ++index)
        bytes[3U + index] = static_cast<std::byte>(kAtlasMember[index]);
    bytes[15U] = std::byte{0};
    bytes[16U] = std::byte{4};
    bytes[17U] = std::byte{9};
    bytes[18U] = static_cast<std::byte>(glyph_count);
    WriteF32(bytes, glyph_offset + 0U, 0.0F);
    WriteF32(bytes, glyph_offset + 4U, 0.25F);
    WriteF32(bytes, glyph_offset + 8U, 0.0F);
    WriteF32(bytes, glyph_offset + 12U, 0.5F);
    bytes[glyph_offset + 16U] = std::byte{5};
    bytes[glyph_offset + 17U] = std::byte{0};
    return bytes;
}

struct StringEntry final
{
    std::string_view key;
    std::string_view value;
};

[[nodiscard]] std::vector<std::byte> MakeStringTable(
    const std::span<const StringEntry> entries)
{
    std::vector<std::byte> bytes;
    AppendU32(bytes, static_cast<std::uint32_t>(entries.size()));
    for (const auto& entry : entries)
    {
        Align4(bytes);
        const std::size_t key_offset = bytes.size();
        bytes.resize(key_offset + 16U, std::byte{0});
        for (std::size_t index = 0; index < entry.key.size(); ++index)
            bytes[key_offset + index] = static_cast<std::byte>(entry.key[index]);
        AppendU32(bytes, static_cast<std::uint32_t>(entry.value.size() + 1U));
        for (const char character : entry.value)
            bytes.push_back(static_cast<std::byte>(character));
        bytes.push_back(std::byte{0});
        Align4(bytes);
    }
    return bytes;
}

[[nodiscard]] std::vector<std::byte> MakeStringTable()
{
    constexpr std::array entries{
        StringEntry{.key = "CreateAgent", .value = "Generated Create Agent"},
    };
    return MakeStringTable(entries);
}

struct ScreenSpec final
{
    omega::content::FrontEndScreenKey key;
    std::string_view stem;
    std::string_view texture_basename;
    std::uint8_t seed;
};

constexpr std::array kScreens{
    ScreenSpec{omega::content::FrontEndScreenKey::Title, "TITLESCR", "TITLE01", 0x11U},
    ScreenSpec{omega::content::FrontEndScreenKey::CreateAgent, "AGENTNEW", "CREATE01", 0x22U},
    ScreenSpec{omega::content::FrontEndScreenKey::LoadAgent, "AGENTOPN", "LOAD0001", 0x33U},
};

struct FrontEndFixtureOptions final
{
    std::size_t changed_screen = kScreens.size();
    bool omit_texture = false;
    bool malformed_gui = false;
    bool unsafe_texture_reference = false;
    std::string_view localized_reference = "$CreateAgent";
    bool unsafe_scope = false;
    bool wrong_resource_case = false;
    std::size_t extra_scope_count = 0U;
    bool omit_scoped_texture = false;
    bool unsafe_scoped_texture_reference = false;
    bool scoped_name_collision = false;
    bool omit_scope_archive = false;
    bool omit_scoped_visual = false;
};

[[nodiscard]] std::vector<std::byte> MakeScreenHog(const ScreenSpec& screen,
    const FrontEndFixtureOptions& options, const bool changed)
{
    std::vector<HogMember> members{
        HogMember{.name = std::string(screen.stem) + ".GUI",
            .payload = changed && options.malformed_gui
                ? Bytes("not-gui")
                : MakeGui(screen.stem,
                      changed ? options.localized_reference : "$CreateAgent",
                      changed && options.unsafe_scope,
                      changed && options.wrong_resource_case,
                      changed ? options.extra_scope_count : 0U)},
        HogMember{.name = std::string(screen.stem) + ".IE",
            .payload = MakeIe(screen.stem, screen.texture_basename,
                changed && options.unsafe_texture_reference)},
        HogMember{.name = "UNUSED.TDX", .payload = Bytes("malformed unreferenced member")},
    };
    if (!changed || !options.omit_texture)
    {
        members.push_back(HogMember{
            .name = std::string(screen.texture_basename) + ".TDX",
            .payload = MakeTdx(screen.seed),
        });
    }
    return MakeHog(members);
}

[[nodiscard]] std::vector<std::byte> MakeSharedScopeHog(
    const FrontEndFixtureOptions& options)
{
    IeNode root{.identifier = "shared_root"};
    std::vector<HogMember> members;
    for (const auto& screen : kScreens)
    {
        std::string resource(screen.stem);
        resource.append("_external");
        root.children.push_back(IeNode{
            .identifier = resource,
            .texture_basename = options.unsafe_scoped_texture_reference
                ? "../UNTRUSTED_MEMBER"
                : std::string(screen.texture_basename),
        });
        root.children.push_back(IeNode{
            .identifier = resource,
            .texture_basename = "UNRESOLVED_DUPLICATE",
        });
        if (!options.omit_scoped_texture)
        {
            members.push_back(HogMember{
                .name = std::string(screen.texture_basename) + ".TDX",
                .payload = MakeTdx(static_cast<std::uint8_t>(screen.seed + 0x40U)),
            });
        }
    }
    root.children.push_back(IeNode{
        .identifier = "unbound_resource",
        .texture_basename = "UNRESOLVED_UNBOUND",
    });
    std::vector<std::byte> document{
        std::byte{0x11}, std::byte{0x22}, std::byte{0x33}, std::byte{0x44}};
    AppendIeNode(document, root);
    Align16Zero(document);
    if (!options.omit_scoped_visual)
    {
        members.insert(members.begin(),
            HogMember{.name = "SHAREDSCOPE.IE", .payload = std::move(document)});
    }
    if (options.scoped_name_collision)
    {
        members.push_back(HogMember{
            .name = "title01.tdx",
            .payload = MakeTdx(0x77U),
        });
    }
    return MakeHog(members);
}

[[nodiscard]] std::vector<std::byte> MakeFrontEndHog(
    const FrontEndFixtureOptions options = {})
{
    std::vector<HogMember> members;
    for (std::size_t index = 0; index < kScreens.size(); ++index)
    {
        const bool changed = index == options.changed_screen;
        members.push_back(HogMember{
            .name = std::string(kScreens[index].stem) + ".HOG",
            .payload = MakeScreenHog(kScreens[index], options, changed),
        });
    }
    if (!options.omit_scope_archive)
    {
        members.push_back(HogMember{
            .name = "SHAREDSCOPE.HOG",
            .payload = MakeSharedScopeHog(options),
        });
    }
    return MakeHog(members);
}

[[nodiscard]] std::vector<std::byte> MakeFontHog(const bool normalized_collision = false)
{
    std::vector<HogMember> members{
        HogMember{.name = "DEFAULT.FNT", .payload = MakeFnt()},
        HogMember{.name = std::string(kAtlasMember), .payload = MakeTdx(0x44U)},
        HogMember{.name = "UNUSED2.TDX", .payload = Bytes("malformed unreferenced atlas")},
    };
    if (normalized_collision)
        members.push_back(HogMember{.name = "default.fnt", .payload = MakeFnt()});
    return MakeHog(members);
}

[[nodiscard]] bool WriteBytes(
    const std::filesystem::path& path, const std::span<const std::byte> bytes)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output)
        return false;
    if (!bytes.empty())
    {
        output.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
    }
    return output.good();
}

[[nodiscard]] bool WriteText(
    const std::filesystem::path& path, const std::string_view text)
{
    return WriteBytes(path, std::as_bytes(std::span(text.data(), text.size())));
}

struct FixturePaths final
{
    std::filesystem::path front_end_hog;
    std::filesystem::path font_hog;
    std::filesystem::path strings;
};

[[nodiscard]] std::optional<FixturePaths> MakeTree(const std::filesystem::path& root)
{
    std::error_code error;
    std::filesystem::create_directories(root / "GAMEDATA" / "FRONTEND", error);
    if (error)
        return std::nullopt;
    std::filesystem::create_directories(root / "GAMEDATA" / "COMMON", error);
    if (error)
        return std::nullopt;
    const FixturePaths paths{
        .front_end_hog = root / "GAMEDATA" / "FRONTEND" / "NTSC.HOG",
        .font_hog = root / "GAMEDATA" / "COMMON" / "FONTS.HOG",
        .strings = root / "GAMEDATA" / "COMMON" / "STRINGS.DAT",
    };
    if (!WriteText(root / "SYSTEM.CNF", "BOOT2 = cdrom0:\\SCUS_972.64;1\r\n") ||
        !WriteText(root / "SCUS_972.64", "generated executable marker") ||
        !WriteBytes(paths.front_end_hog, MakeFrontEndHog()) ||
        !WriteBytes(paths.font_hog, MakeFontHog()) ||
        !WriteBytes(paths.strings, MakeStringTable()))
    {
        return std::nullopt;
    }
    return paths;
}

template <typename Value>
void CheckDecodeError(const std::expected<Value, omega::content::GameDataError>& result,
    const omega::asset::DecodeErrorCode code, const std::string_view message)
{
    Check(!result && result.error().code == omega::content::GameDataErrorCode::DecodeFailed &&
              result.error().decode_error && result.error().decode_error->code == code,
        message);
    if (!result)
    {
        Check(result.error().message.find("UNTRUSTED_MEMBER") == std::string::npos &&
                  result.error().message.find('/') == std::string::npos &&
                  result.error().message.find('\\') == std::string::npos,
            "front-end dependency errors do not expose source or owner identity");
    }
}

[[nodiscard]] std::expected<omega::content::FrontEndScreenBundle,
    omega::content::GameDataError>
Load(const std::filesystem::path& root, const omega::content::FrontEndScreenKey key,
    const std::optional<omega::asset::DecodeLimits> limits = std::nullopt)
{
    omega::content::GameDataServiceConfig config{.root = root};
    if (limits)
        config.front_end_decode_limits = *limits;
    auto service = omega::content::GameDataService::Open(std::move(config));
    if (!service)
        return std::unexpected(service.error());
    return service->LoadFrontEndScreen(key);
}
} // namespace

int main()
{
    using omega::content::FrontEndScreenBundle;
    using omega::content::FrontEndScreenKey;
    using omega::content::FrontEndVisualScope;

    static_assert(!std::is_default_constructible_v<FrontEndScreenBundle>);
    static_assert(std::is_move_constructible_v<FrontEndScreenBundle>);
    static_assert(std::is_move_assignable_v<FrontEndScreenBundle>);
    static_assert(!std::is_copy_constructible_v<FrontEndScreenBundle>);
    static_assert(!std::is_copy_assignable_v<FrontEndScreenBundle>);
    static_assert(!std::is_default_constructible_v<FrontEndVisualScope>);
    static_assert(std::is_move_constructible_v<FrontEndVisualScope>);
    static_assert(!std::is_copy_constructible_v<FrontEndVisualScope>);
    static_assert(std::same_as<decltype(std::declval<const FrontEndScreenBundle&>()
                                            .screen_textures()),
        const FrontEndScreenBundle::TextureMap&>);
    static_assert(std::same_as<decltype(std::declval<const FrontEndScreenBundle&>().fonts()),
        const FrontEndScreenBundle::FontMap&>);
    static_assert(std::same_as<decltype(std::declval<const FrontEndScreenBundle&>()
                                            .visual_scopes()),
        const FrontEndScreenBundle::VisualScopeMap&>);

    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path() /
        ("openomega-front-end-bundle-tests-" + std::to_string(nonce));
    const auto paths = MakeTree(root);
    Check(paths.has_value(), "generated canonical front-end fixture tree is written");
    if (!paths)
        return 1;

    for (const auto& screen : kScreens)
    {
        auto loaded = Load(root, screen.key);
        Check(loaded.has_value(), "each fixed canonical front-end route loads");
        if (!loaded)
            continue;
        const std::string texture_name = std::string(screen.texture_basename) + ".TDX";
        Check(loaded->key() == screen.key &&
                  loaded->widget_document().root.identifier == screen.stem &&
                  loaded->presentation_capability().valid(),
            "a loaded route owns its canonical documents and valid retail capability");
        Check(!loaded->primary_scope().empty() &&
                  loaded->visual_scopes().count(loaded->primary_scope()) == 1U &&
                  loaded->visual_scopes().size() == 2U,
            "the private constructor publishes exactly one nonempty primary scope");
        Check(loaded->screen_textures().size() == 1U &&
                  loaded->screen_textures().contains(texture_name) &&
                  !loaded->screen_textures().contains("UNUSED.TDX"),
            "screen textures are recursively derived, normalized, deduplicated, and exact");
        std::string folded_primary = loaded->primary_scope();
        for (char& value : folded_primary)
        {
            if (value >= 'A' && value <= 'Z')
                value = static_cast<char>(value + ('a' - 'A'));
        }
        const auto* primary_scope = loaded->FindVisualScope("");
        const auto* authored_primary = loaded->FindVisualScope(folded_primary);
        const auto* shared_scope = loaded->FindVisualScope("sHaReDsCoPe");
        Check(primary_scope != nullptr && primary_scope == authored_primary &&
                  shared_scope != nullptr &&
                  loaded->FindVisualScope("missing_scope") == nullptr,
            "scope lookup applies empty-primary and ASCII-case-insensitive cache semantics");
        Check(shared_scope != nullptr &&
                  shared_scope->textures().size() == 1U &&
                  shared_scope->textures().contains(texture_name) &&
                  !(shared_scope->textures().at(texture_name) ==
                      loaded->screen_textures().at(texture_name)),
            "archive-local texture maps retain same-named primary and scoped images independently");

        const auto& widget_root = loaded->widget_document().root;
        const auto* root_resource = loaded->ResolveVisualBinding(widget_root, true);
        const auto* inherited_resource = loaded->ResolveVisualBinding(
            widget_root.children.at(0U), false);
        const auto* external_a = loaded->ResolveVisualBinding(
            widget_root.children.at(2U), false);
        const auto* external_b = loaded->ResolveVisualBinding(
            widget_root.children.at(3U), false);
        Check(root_resource != nullptr &&
                  root_resource->identifier == std::string(screen.stem) + "_root" &&
                  loaded->ResolveVisualBinding(widget_root, false) == nullptr &&
                  inherited_resource != nullptr && inherited_resource->identifier == "first",
            "binding resolution applies inherited scope/resource and parentless root suffix rules");
        Check(external_a != nullptr && external_a == external_b &&
                  external_a->texture_member && *external_a->texture_member == texture_name,
            "case-folded scopes resolve one exact-case DFS-first visual resource");
        std::string external_resource(screen.stem);
        external_resource.append("_external");
        std::string wrong_resource_case = external_resource;
        wrong_resource_case.front() = static_cast<char>(
            wrong_resource_case.front() + ('a' - 'A'));
        Check(shared_scope != nullptr &&
                  shared_scope->FindResource(external_resource) == external_a &&
                  shared_scope->FindResource(wrong_resource_case) == nullptr &&
                  shared_scope->FindResource("unbound_resource") == nullptr,
            "resource lookup is exact-case DFS-first and hides decoded but unbound nodes");
        Check(widget_root.children.at(0U).font_reference &&
                  widget_root.children.at(0U).font_reference->empty(),
            "an authored empty font remains in canonical IR for renderer inheritance");
        Check(loaded->fonts().size() == 1U && loaded->fonts().contains("DEFAULT.FNT") &&
                  loaded->font_atlases().size() == 1U &&
                  loaded->font_atlases().contains(kAtlasMember),
            "font basenames and decoded atlas references select exact canonical dependencies");
        const auto* localized = loaded->strings().Find("CREATEAGENT");
        Check(localized && localized->value == "Generated Create Agent",
            "the bundle owns the decoded NTSC-U localization table");
    }

    auto move_source = Load(root, FrontEndScreenKey::Title);
    Check(move_source.has_value(), "move-ownership fixture loads");
    if (move_source)
    {
        FrontEndScreenBundle moved = std::move(*move_source);
        Check(!move_source->presentation_capability().valid() &&
                  moved.presentation_capability().valid() &&
                  moved.screen_textures().contains("TITLE01.TDX"),
            "moving a bundle transfers owned content and invalidates only the source capability");
    }

    auto forged_service = omega::content::GameDataService::Open({.root = root});
    Check(forged_service.has_value(), "invalid-key fixture service opens");
    if (forged_service)
    {
        Check(WriteText(paths->front_end_hog, "unreadable after service open"),
            "front-end file is corrupted after opening the invalid-key fixture service");
        const auto forged = forged_service->LoadFrontEndScreen(
            static_cast<FrontEndScreenKey>(0xFFU));
        Check(!forged &&
                  forged.error().code == omega::content::GameDataErrorCode::InvalidConfiguration &&
                  forged.error().message == "unknown front-end screen key",
            "an invalid enum fails before VFS access and never falls back to Title");
        Check(WriteBytes(paths->front_end_hog, MakeFrontEndHog()),
            "valid front-end archive is restored after invalid-key coverage");
    }

    Check(WriteBytes(paths->front_end_hog,
              MakeFrontEndHog({.changed_screen = 0U, .omit_texture = true})),
        "missing derived texture fixture is written");
    CheckDecodeError(Load(root, FrontEndScreenKey::Title),
        omega::asset::DecodeErrorCode::InvalidReference,
        "a missing referenced texture fails the whole bundle without fallback");

    Check(WriteBytes(paths->front_end_hog,
              MakeFrontEndHog({.changed_screen = 0U, .malformed_gui = true})),
        "malformed GUI fixture is written");
    const auto malformed_gui = Load(root, FrontEndScreenKey::Title);
    Check(!malformed_gui &&
              malformed_gui.error().code == omega::content::GameDataErrorCode::DecodeFailed,
        "a malformed canonical GUI fails closed");

    Check(WriteBytes(paths->front_end_hog,
              MakeFrontEndHog(
                  {.changed_screen = 0U, .unsafe_texture_reference = true})),
        "unsafe derived reference fixture is written");
    CheckDecodeError(Load(root, FrontEndScreenKey::Title),
        omega::asset::DecodeErrorCode::InvalidReference,
        "an unsafe retail-derived member reference fails with a sanitized typed error");

    Check(WriteBytes(paths->front_end_hog,
              MakeFrontEndHog({.changed_screen = 0U, .unsafe_scope = true})),
        "unsafe visual-scope fixture is written");
    CheckDecodeError(Load(root, FrontEndScreenKey::Title),
        omega::asset::DecodeErrorCode::InvalidReference,
        "a traversal-shaped visual scope is rejected without relaxing path safety");

    Check(WriteBytes(paths->front_end_hog,
              MakeFrontEndHog(
                  {.changed_screen = 0U, .wrong_resource_case = true})),
        "wrong-case visual-resource fixture is written");
    CheckDecodeError(Load(root, FrontEndScreenKey::Title),
        omega::asset::DecodeErrorCode::InvalidReference,
        "visual-resource lookup remains exact and case-sensitive");

    Check(WriteBytes(paths->front_end_hog,
              MakeFrontEndHog(
                  {.changed_screen = 0U, .extra_scope_count = 19U})),
        "visual-scope cache overflow fixture is written");
    CheckDecodeError(Load(root, FrontEndScreenKey::Title),
        omega::asset::DecodeErrorCode::LimitExceeded,
        "the primary scope plus twenty external scopes exceed the fixed cache limit");

    Check(WriteBytes(paths->front_end_hog,
              MakeFrontEndHog({.omit_scope_archive = true})),
        "missing scoped archive fixture is written");
    CheckDecodeError(Load(root, FrontEndScreenKey::Title),
        omega::asset::DecodeErrorCode::InvalidReference,
        "a missing derived scope archive fails without a current-screen fallback");

    Check(WriteBytes(paths->front_end_hog,
              MakeFrontEndHog({.omit_scoped_visual = true})),
        "missing scoped visual document fixture is written");
    CheckDecodeError(Load(root, FrontEndScreenKey::Title),
        omega::asset::DecodeErrorCode::InvalidReference,
        "a missing derived scoped IE document fails without fallback");

    Check(WriteBytes(paths->front_end_hog,
              MakeFrontEndHog({.omit_scoped_texture = true})),
        "missing scoped texture fixture is written");
    CheckDecodeError(Load(root, FrontEndScreenKey::Title),
        omega::asset::DecodeErrorCode::InvalidReference,
        "a missing archive-relative scoped texture fails the complete bundle");

    Check(WriteBytes(paths->front_end_hog,
              MakeFrontEndHog({.unsafe_scoped_texture_reference = true})),
        "unsafe scoped texture fixture is written");
    CheckDecodeError(Load(root, FrontEndScreenKey::Title),
        omega::asset::DecodeErrorCode::InvalidReference,
        "a scoped IE cannot escape its own archive through a texture reference");

    Check(WriteBytes(paths->front_end_hog,
              MakeFrontEndHog({.scoped_name_collision = true})),
        "normalized scoped-archive collision fixture is written");
    CheckDecodeError(Load(root, FrontEndScreenKey::Title),
        omega::asset::DecodeErrorCode::DuplicateReference,
        "a scoped archive with duplicate normalized names fails before selection");

    Check(WriteBytes(paths->front_end_hog, MakeFrontEndHog()),
        "valid front-end archive is restored after document and reference failures");
    Check(WriteBytes(paths->font_hog, MakeFontHog(true)),
        "normalized font-archive collision fixture is written");
    CheckDecodeError(Load(root, FrontEndScreenKey::Title),
        omega::asset::DecodeErrorCode::DuplicateReference,
        "duplicate normalized archive names fail before ambiguous dependency selection");
    Check(WriteBytes(paths->font_hog, MakeFontHog()),
        "valid font archive is restored after collision coverage");

    constexpr std::array missing_entries{
        StringEntry{.key = "OtherKey", .value = "Generated other value"},
    };
    Check(WriteBytes(paths->strings, MakeStringTable(missing_entries)),
        "missing localization-key fixture is written");
    CheckDecodeError(Load(root, FrontEndScreenKey::Title),
        omega::asset::DecodeErrorCode::InvalidReference,
        "a missing single-dollar localization key fails the whole bundle");
    Check(WriteBytes(paths->strings, MakeStringTable()),
        "valid localization table is restored after missing-key coverage");

    Check(WriteBytes(paths->front_end_hog,
              MakeFrontEndHog(
                  {.changed_screen = 0U, .localized_reference = "$"})),
        "empty localization-key fixture is written");
    CheckDecodeError(Load(root, FrontEndScreenKey::Title),
        omega::asset::DecodeErrorCode::InvalidReference,
        "an empty single-dollar localization key fails closed");
    Check(WriteBytes(paths->front_end_hog, MakeFrontEndHog()),
        "valid front-end archive is restored after empty-key coverage");

    omega::asset::DecodeLimits tight_limits;
    tight_limits.maximum_items = 1U;
    CheckDecodeError(Load(root, FrontEndScreenKey::Title, tight_limits),
        omega::asset::DecodeErrorCode::LimitExceeded,
        "caller-tightened aggregate item limits cannot be reset by child decoders");

    if (const auto private_data = PrivateRetailDataPath())
    {
        auto service = omega::content::GameDataService::Open(
            {.root = *private_data});
        Check(service.has_value(),
            "the optional owner-supplied retail source mounts without exposing its identity");
        if (service)
        {
            for (const auto& screen : kScreens)
            {
                auto loaded = service->LoadFrontEndScreen(screen.key);
                Check(loaded.has_value(),
                    "each fixed route loads from the optional owner-supplied retail source");
                if (!loaded)
                {
                    std::cerr << "PRIVATE_SMOKE: "
                              << omega::content::GameDataErrorCodeName(loaded.error().code)
                              << ": " << loaded.error().message;
                    if (loaded.error().decode_error)
                    {
                        std::cerr << " (decode-code="
                                  << static_cast<unsigned>(
                                         loaded.error().decode_error->code);
                        if (loaded.error().decode_error->byte_offset)
                            std::cerr << ", offset="
                                      << *loaded.error().decode_error->byte_offset;
                        std::cerr << ", detail="
                                  << loaded.error().decode_error->message << ')';
                    }
                    std::cerr << '\n';
                    continue;
                }
                Check(loaded->key() == screen.key &&
                          loaded->presentation_capability().valid() &&
                          !loaded->widget_document().root.identifier.empty() &&
                          !loaded->visual_document().root.identifier.empty() &&
                          !loaded->screen_textures().empty() && !loaded->fonts().empty() &&
                          !loaded->font_atlases().empty() && !loaded->strings().entries.empty(),
                    "an owner-supplied route yields a complete owned retail presentation bundle");
            }
        }
    }

    std::error_code cleanup_error;
    std::filesystem::remove_all(root, cleanup_error);
    Check(!cleanup_error, "generated front-end fixture tree is removed");
    return failures;
}
