#pragma once

#include "omega/asset/decode.h"
#include "omega/asset/geometry_ir.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace omega::retail
{
// One decoded enemy/actor spawn from the POP GOB: NPC: section: the retail
// handle id, the model/type name (e.g. "minsk_metaMIB.skl"), and the world
// position. Carries no behavior -- only placement + type.
struct PopNpcSpawn
{
    std::uint32_t id = 0U;
    std::string model;
    asset::Float3IR position;

    bool operator==(const PopNpcSpawn&) const = default;
};

// One adjacency edge of a nav node: the neighbor node index and its edge weight.
struct PopNavLink
{
    std::uint32_t neighbor = 0U;
    std::uint32_t weight = 0U;

    bool operator==(const PopNavLink&) const = default;
};

// One decoded NOD: nav-graph node: the retail handle id, world position, and its
// adjacency list. Neighbor indices are into the level's full node array (some may
// reference variant records this Stage-2 walk skipped; consumers must bounds-check).
struct PopNavNode
{
    std::uint32_t id = 0U;
    asset::Float3IR position;
    std::vector<PopNavLink> links;

    bool operator==(const PopNavNode&) const = default;
};

// Decoded POP game objects. NPC: enemy/actor spawns (placement + type) and the
// NOD: nav-graph nodes actually decoded, plus the declared section counts (a
// node may be skipped when its variant layout is not cleanly walkable, so
// nav_nodes.size() <= nav_node_count). BOX: is still a count only.
struct PopGameObjectsIR
{
    std::vector<PopNpcSpawn> npc_spawns;
    std::vector<PopNavNode> nav_nodes;
    std::uint32_t npc_section_count = 0U;
    std::uint32_t nav_node_count = 0U;
    std::uint32_t hotbox_count = 0U;

    bool operator==(const PopGameObjectsIR&) const = default;
};

// [any worker thread; reentrant] Decodes the POP file's post-TER GOB game-object
// sections. Locates the fixed typed sections (each: 4-char tag + u32 count) and
// extracts the NPC: spawns. Each NPC record is
//   class(u32==12) + id(u32) + NUL-terminated model name + params + f32 pos[3] + orientation
// so the world position is read at align4(8 + name_len + 1) + 12 from the record
// start. The walk splits on the class delimiter and only emits records that carry
// a valid model name and a finite position -- fail-soft: variant / nameless
// records are skipped, never aborting the decode. Also reports the NOD: and BOX:
// declared counts. Validated against MINSK/DATA.POP (authentic enemy roster +
// positions in the level's collision bounds).
[[nodiscard]] asset::DecodeResult<PopGameObjectsIR> DecodePopGameObjects(
    std::span<const std::byte> pop_bytes, asset::DecodeLimits limits = {});
} // namespace omega::retail
