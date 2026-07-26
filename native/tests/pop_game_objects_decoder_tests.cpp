#include "omega/retail/pop_game_objects_decoder.h"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
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

void CheckLimitExceeded(const omega::asset::DecodeResult<omega::retail::PopGameObjectsIR>& result,
                        const std::string_view message)
{
    Check(!result && result.error().code == omega::asset::DecodeErrorCode::LimitExceeded, message);
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

// Appends one NOD nav-node record: class + id + two fields + position(3 f32) +
// a 6-word transform + link_count + (neighbor,weight) pairs -- the fixed layout
// the decoder walks (link_count lands at record+52).
void AppendNodRecord(std::vector<std::byte>& body, const std::uint32_t id,
    const float x, const float y, const float z,
    const std::vector<std::pair<std::uint32_t, std::uint32_t>>& links)
{
    AppendU32(body, 12U); // class
    AppendU32(body, id);
    AppendU32(body, 0U); // field_a
    AppendU32(body, 0U); // field_b
    AppendF32(body, x);  // position at record+16
    AppendF32(body, y);
    AppendF32(body, z);
    for (int i = 0; i < 6; ++i) // 6-word transform -> link_count at record+52
        AppendF32(body, 0.0F);
    AppendU32(body, static_cast<std::uint32_t>(links.size()));
    for (const auto& link : links)
    {
        AppendU32(body, link.first);  // neighbor index
        AppendU32(body, link.second); // weight
    }
}

// A POP whose NOD: section carries `nod_body` and is terminated by GEN: (the
// section the decoder walks the NOD body up to).
[[nodiscard]] std::vector<std::byte> BuildSyntheticPopWithNav(
    const std::vector<std::byte>& nod_body, const std::uint32_t nod_count)
{
    std::vector<std::byte> pop;
    AppendU32(pop, 70U);
    AppendTag(pop, "TER:");
    AppendU32(pop, 0U);
    AppendTag(pop, "NPC:");
    AppendU32(pop, 0U);
    AppendTag(pop, "WPN:");
    AppendU32(pop, 0U);
    AppendTag(pop, "NOD:");
    AppendU32(pop, nod_count);
    pop.insert(pop.end(), nod_body.begin(), nod_body.end());
    AppendTag(pop, "GEN:");
    AppendU32(pop, 0U);
    AppendTag(pop, "BOX:");
    AppendU32(pop, 0U);
    return pop;
}

[[nodiscard]] std::vector<std::byte> BuildSyntheticPopWithObjects(
    const std::vector<std::byte>& npc_body, const std::uint32_t npc_count,
    const std::vector<std::byte>& nod_body, const std::uint32_t nod_count,
    const std::uint32_t box_count)
{
    std::vector<std::byte> pop;
    AppendU32(pop, 70U);
    AppendTag(pop, "TER:");
    AppendU32(pop, 0U);
    AppendTag(pop, "NPC:");
    AppendU32(pop, npc_count);
    pop.insert(pop.end(), npc_body.begin(), npc_body.end());
    AppendTag(pop, "WPN:");
    AppendU32(pop, 0U);
    AppendTag(pop, "NOD:");
    AppendU32(pop, nod_count);
    pop.insert(pop.end(), nod_body.begin(), nod_body.end());
    AppendTag(pop, "GEN:");
    AppendU32(pop, 0U);
    AppendTag(pop, "BOX:");
    AppendU32(pop, box_count);
    return pop;
}

template <typename SetLimit>
void CheckLimitBoundary(const std::span<const std::byte> pop, const std::uint64_t exact,
                        SetLimit set_limit, const std::string_view label)
{
    auto limits = omega::asset::DecodeLimits{};
    set_limit(limits, 0U);
    CheckLimitExceeded(omega::retail::DecodePopGameObjects(pop, limits),
                       std::string(label) + " zero limit fails closed");

    limits = omega::asset::DecodeLimits{};
    set_limit(limits, exact - 1U);
    CheckLimitExceeded(omega::retail::DecodePopGameObjects(pop, limits),
                       std::string(label) + " tight limit fails closed");

    limits = omega::asset::DecodeLimits{};
    set_limit(limits, exact);
    Check(omega::retail::DecodePopGameObjects(pop, limits).has_value(),
          std::string(label) + " exact limit succeeds");

    limits = omega::asset::DecodeLimits{};
    set_limit(limits, exact + 1U);
    Check(omega::retail::DecodePopGameObjects(pop, limits).has_value(),
          std::string(label) + " plus-one limit succeeds");
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
                // All three components: leaving .y unchecked would let a
                // dropped or mis-strided second-record Y pass.
                Check(b.id == 0x0802U && b.model == "minsk_sniper.skl" &&
                          b.position.x == -5.0F && b.position.y == -6.0F &&
                          b.position.z == -7.0F,
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

    // NOD: nav nodes decode with positions + adjacency (incl. weight-0 links).
    {
        std::vector<std::byte> nod;
        AppendNodRecord(nod, 4777U, -20.1F, -4.2F, 0.8F,
            {{1U, 2U}, {3U, 2U}, {18U, 0U}});
        AppendNodRecord(nod, 4778U, 5.0F, 6.0F, 7.0F, {{0U, 1U}});
        const auto pop = BuildSyntheticPopWithNav(nod, 1613U);
        const auto decoded = DecodePopGameObjects(pop);
        Check(decoded && decoded->nav_node_count == 1613U,
            "NOD: declared count still read with a body present");
        Check(decoded && decoded->nav_nodes.size() == 2U, "two nav nodes decoded");
        if (decoded && decoded->nav_nodes.size() == 2U)
        {
            const auto& n0 = decoded->nav_nodes[0];
            // All three components, as for the NPC spawns above: an x-only
            // check would let a dropped or mis-strided Y/Z pass. The fixture
            // writes these literals bit-for-bit and the decoder reads them back
            // unchanged, so the exact compares hold.
            Check(n0.id == 4777U && n0.position.x == -20.1F &&
                      n0.position.y == -4.2F && n0.position.z == 0.8F &&
                      n0.links.size() == 3U,
                "nav node 0 id / position / link count");
            Check(n0.links.size() == 3U && n0.links[0].neighbor == 1U &&
                      n0.links[0].weight == 2U && n0.links[2].neighbor == 18U &&
                      n0.links[2].weight == 0U,
                "nav node 0 adjacency incl. a weight-0 link");
            Check(decoded->nav_nodes[1].links.size() == 1U &&
                      decoded->nav_nodes[1].links[0].neighbor == 0U,
                "nav node 1 single link to node 0");
            // The second record's own id and position were previously unchecked,
            // so only its link survived as evidence that the walker strided onto
            // it correctly.
            const auto& n1 = decoded->nav_nodes[1];
            Check(n1.id == 4778U && n1.position.x == 5.0F &&
                      n1.position.y == 6.0F && n1.position.z == 7.0F,
                "nav node 1 id / position");
        }
    }

    // A NOD record with an out-of-range neighbor is skipped fail-soft.
    {
        std::vector<std::byte> nod;
        AppendNodRecord(nod, 1U, 0.0F, 0.0F, 0.0F, {{9999U, 1U}}); // >= count(10)
        AppendNodRecord(nod, 2U, 1.0F, 1.0F, 1.0F, {{0U, 1U}});
        const auto pop = BuildSyntheticPopWithNav(nod, 10U);
        const auto decoded = DecodePopGameObjects(pop);
        Check(decoded && decoded->nav_nodes.size() == 1U &&
                  decoded->nav_nodes[0].id == 2U,
            "out-of-range-neighbor nav node skipped; the valid one is kept");
    }

    // DecodeLimits are one preflight contract over the complete result. The
    // fixture intentionally combines two NPCs (and their two model strings),
    // two nav nodes, and three links so separate per-vector caps cannot pass.
    {
        constexpr std::string_view first_model = "alpha.skl";
        constexpr std::string_view second_model = "baker.skl";
        std::vector<std::byte> npc;
        AppendNpcRecord(npc, 0x0B01U, first_model, 1.0F, 2.0F, 3.0F);
        AppendNpcRecord(npc, 0x0B02U, second_model, 4.0F, 5.0F, 6.0F);
        std::vector<std::byte> nod;
        AppendNodRecord(nod, 1U, 7.0F, 8.0F, 9.0F, {{0U, 1U}, {1U, 2U}});
        AppendNodRecord(nod, 2U, 10.0F, 11.0F, 12.0F, {{0U, 3U}});
        const auto pop = BuildSyntheticPopWithObjects(npc, 2U, nod, 2U, 0U);

        constexpr std::uint64_t exact_items = 2U + // NPC objects
                                              2U + // model strings
                                              2U + // nav-node objects
                                              3U;  // nav links
        constexpr std::uint64_t exact_child_bytes =
            2U * sizeof(omega::retail::PopNpcSpawn) + first_model.size() + second_model.size() +
            2U * sizeof(omega::retail::PopNavNode) + 3U * sizeof(omega::retail::PopNavLink);
        constexpr std::uint64_t exact_output_bytes =
            sizeof(omega::retail::PopGameObjectsIR) + exact_child_bytes;
        constexpr std::uint64_t exact_string_bytes =
            first_model.size() > second_model.size() ? first_model.size() : second_model.size();

        CheckLimitBoundary(
            pop, pop.size(), [](omega::asset::DecodeLimits& limits, const std::uint64_t value)
            { limits.maximum_input_bytes = value; }, "input-byte");
        CheckLimitBoundary(
            pop, exact_output_bytes,
            [](omega::asset::DecodeLimits& limits, const std::uint64_t value)
            { limits.maximum_output_bytes = value; }, "output-byte");
        CheckLimitBoundary(
            pop, exact_child_bytes,
            [](omega::asset::DecodeLimits& limits, const std::uint64_t value)
            { limits.maximum_scratch_bytes = value; }, "scratch-byte");
        CheckLimitBoundary(
            pop, exact_string_bytes,
            [](omega::asset::DecodeLimits& limits, const std::uint64_t value)
            { limits.maximum_string_bytes = static_cast<std::uint32_t>(value); }, "string-byte");
        CheckLimitBoundary(
            pop, exact_items, [](omega::asset::DecodeLimits& limits, const std::uint64_t value)
            { limits.maximum_items = value; }, "combined-item");
    }

    // Root output accounting is still enforced when no semantic objects exist.
    {
        const std::vector<std::byte> empty;
        auto limits = omega::asset::DecodeLimits{};
        limits.maximum_input_bytes = 0U;
        limits.maximum_items = 0U;
        limits.maximum_scratch_bytes = 0U;
        limits.maximum_string_bytes = 0U;
        limits.maximum_output_bytes = sizeof(omega::retail::PopGameObjectsIR);
        Check(DecodePopGameObjects(empty, limits).has_value(),
              "empty POP accepts exact root output and zero remaining budgets");
        --limits.maximum_output_bytes;
        CheckLimitExceeded(DecodePopGameObjects(empty, limits),
                           "empty POP rejects a tight root output budget");
    }

    if (failures != 0)
    {
        std::cerr << failures << " POP game-object decoder test(s) failed\n";
        return 1;
    }
    std::cout << "POP game-object decoder tests passed\n";
    return 0;
}
