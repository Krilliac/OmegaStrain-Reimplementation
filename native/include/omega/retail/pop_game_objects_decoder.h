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

// Decoded POP game objects. Stage 1: the NPC: enemy/actor spawns (placement +
// type), plus the declared counts of the NOD: nav-node graph and BOX: hotbox
// trigger sections (their full per-record decode is a documented follow-up).
struct PopGameObjectsIR
{
    std::vector<PopNpcSpawn> npc_spawns;
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
