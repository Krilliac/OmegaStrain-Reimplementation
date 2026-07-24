#include "omega/retail/pop_game_objects_decoder.h"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <string_view>
#include <vector>

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

void AppendU32(std::vector<std::byte>& bytes, const std::uint32_t value)
{
    for (unsigned shift = 0U; shift < 32U; shift += 8U)
        bytes.push_back(static_cast<std::byte>((value >> shift) & 0xFFU));
}

void AppendF32(std::vector<std::byte>& bytes, const float value)
{
    std::uint32_t bits = 0U;
    std::memcpy(&bits, &value, 4U);
    AppendU32(bytes, bits);
}

void AppendTag(std::vector<std::byte>& bytes, const std::string_view tag)
{
    for (const char character : tag)
        bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
}

void AppendCStr(std::vector<std::byte>& bytes, const std::string_view value)
{
    AppendTag(bytes, value);
    bytes.push_back(std::byte{0});
}

// Appends one NPC record at the current end of `body`: class + id + model name +
// three parameter words + position + a short orientation tail. `body_base` is the
// offset of `body[0]` so the record aligns exactly as the decoder expects.
void AppendNpcRecord(std::vector<std::byte>& body, const std::uint32_t id,
    const std::string_view model, const float x, const float y, const float z)
{
    const std::size_t record_start = body.size();
    AppendU32(body, 12U); // class
    AppendU32(body, id);
    AppendCStr(body, model);
    while (((body.size() - record_start) % 4U) != 0U) // align4 from record start
        body.push_back(std::byte{0});
    AppendU32(body, 0xFFFFFF00U); // flags
    AppendU32(body, 200U);        // param
    AppendU32(body, 100U);        // param
    AppendF32(body, x);
    AppendF32(body, y);
    AppendF32(body, z);
    for (int i = 0; i < 4; ++i) // orientation tail
        AppendF32(body, i == 3 ? 1.0F : 0.0F);
}

[[nodiscard]] std::vector<std::byte> BuildSyntheticPop(
    const std::vector<std::byte>& npc_body, const std::uint32_t npc_count,
    const std::uint32_t nod_count, const std::uint32_t box_count)
{
    std::vector<std::byte> pop;
    AppendU32(pop, 70U);            // header word
    AppendTag(pop, "TER:");
    AppendU32(pop, 0U);
    AppendTag(pop, "NPC:");
    AppendU32(pop, npc_count);
    pop.insert(pop.end(), npc_body.begin(), npc_body.end());
    AppendTag(pop, "WPN:");
    AppendU32(pop, 0U);
    AppendTag(pop, "NOD:");
    AppendU32(pop, nod_count);
    AppendTag(pop, "BOX:");
    AppendU32(pop, box_count);
    return pop;
}
} // namespace

int main()
{
    using omega::retail::DecodePopGameObjects;

    // Two clean records + the section counts.
    {
        std::vector<std::byte> body;
        AppendNpcRecord(body, 0x0801U, "minsk_metaMIB.skl", 10.0F, 20.0F, 30.0F);
        AppendNpcRecord(body, 0x0802U, "minsk_sniper.skl", -5.0F, -6.0F, -7.0F);
        const auto pop = BuildSyntheticPop(body, 2U, 1613U, 456U);
        const auto decoded = DecodePopGameObjects(pop);
        Check(decoded.has_value(), "synthetic POP decodes");
        if (decoded)
        {
            Check(decoded->npc_section_count == 2U, "NPC: declared count read");
            Check(decoded->nav_node_count == 1613U, "NOD: declared count read");
            Check(decoded->hotbox_count == 456U, "BOX: declared count read");
            Check(decoded->npc_spawns.size() == 2U, "two NPC spawns extracted");
            if (decoded->npc_spawns.size() == 2U)
            {
                const auto& a = decoded->npc_spawns[0];
                Check(a.id == 0x0801U && a.model == "minsk_metaMIB.skl" &&
                          a.position.x == 10.0F && a.position.y == 20.0F &&
                          a.position.z == 30.0F,
                    "first spawn id/model/position");
                const auto& b = decoded->npc_spawns[1];
                Check(b.id == 0x0802U && b.model == "minsk_sniper.skl" &&
                          b.position.x == -5.0F && b.position.z == -7.0F,
                    "second spawn id/model/position");
            }
        }
    }

    // A nameless class=12 record (no model name) is skipped, not counted.
    {
        std::vector<std::byte> body;
        AppendU32(body, 12U); // class
        AppendU32(body, 0x0900U); // id
        AppendU32(body, 0U); // where a name would be: a NUL byte -> no name
        AppendF32(body, 1.0F);
        AppendNpcRecord(body, 0x0901U, "guard.skl", 3.0F, 4.0F, 5.0F);
        const auto pop = BuildSyntheticPop(body, 2U, 0U, 0U);
        const auto decoded = DecodePopGameObjects(pop);
        Check(decoded && decoded->npc_spawns.size() == 1U,
            "nameless record skipped; the named one still decodes");
        if (decoded && decoded->npc_spawns.size() == 1U)
            Check(decoded->npc_spawns[0].model == "guard.skl",
                "the surviving spawn is the named record");
    }

    // Empty / no-NPC-section input yields an empty roster, not an error.
    {
        const std::vector<std::byte> empty;
        const auto decoded = DecodePopGameObjects(empty);
        Check(decoded.has_value() && decoded->npc_spawns.empty(),
            "empty input is a valid empty roster");
    }

    // A record whose position is non-finite is skipped fail-soft.
    {
        std::vector<std::byte> body;
        const float infinity = std::numeric_limits<float>::infinity();
        AppendNpcRecord(body, 0x0A01U, "bad.skl", infinity, 0.0F, 0.0F);
        AppendNpcRecord(body, 0x0A02U, "good.skl", 1.0F, 2.0F, 3.0F);
        const auto pop = BuildSyntheticPop(body, 2U, 0U, 0U);
        const auto decoded = DecodePopGameObjects(pop);
        Check(decoded && decoded->npc_spawns.size() == 1U &&
                  decoded->npc_spawns[0].model == "good.skl",
            "non-finite position skipped, finite one kept");
    }

    if (failures != 0)
    {
        std::cerr << failures << " POP game-object decoder test(s) failed\n";
        return 1;
    }
    std::cout << "POP game-object decoder tests passed\n";
    return 0;
}
