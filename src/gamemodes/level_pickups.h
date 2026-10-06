// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>
#include <vector>

#include "animation/anim_math.h"
#include "scripting/script_system.h"
#include "warriors/game_state.h"
#include "world_objects/object_types.h"
#include "world_objects/pickups.h"
#include "world_objects/spawn_records.h"

// A level's loose objects as a player picks them up: the triangle search over the spawn records, and the take.
// Research: docs/research/combat.md#breakables, docs/research/player-state.md#pickup-callback

namespace coney {

/// What the search chose: the object and where it is.
struct PickupChoice {
    double handle = 0;
    anim::Vec3 position{};
    int clip = 0; ///< The pick-up clip for its height above the feet (world_objects::pickupClip()).
};

/// The pick-up over a level's spawn records, its object types, its scripts and the players' state, all of which must
/// outlive it.
class LevelPickups {
  public:
    /// The game's money multiplier for loot (game state `+0x380`, 1.0 in mission 1; what sets it is open).
    static constexpr float kLootMoneyFactor = 1.0F;

    LevelPickups(script::ScriptSystem& scripts, GameState& state, world_objects::SpawnRecords& records,
                 const world_objects::ObjectTypes& types)
        : m_scripts(scripts), m_state(state), m_records(records), m_types(types) {}

    /// The triangle search (world_objects::searchPickup()) for a human at `feet` facing `facing`, over the records that
    /// are not removed, hidden or in a disabled zone, whose type's class is pickable. Nothing when none qualifies.
    [[nodiscard]] std::optional<PickupChoice> search(anim::Vec3 feet, anim::Vec3 facing,
                                                     const world_objects::SightBlocked& blocked) const;

    /// Player `player` (0 or 1) takes object `handle`, at its pick-up clip's event. A `TYPE_SPECIAL` adds item 10
    /// (loot) ×1 with notify, then its value × kLootMoneyFactor in money without, and the record is removed for good.
    /// Returns whether the handle named a record still there. **Coney choices**: the named mission items (model hashes
    /// the original checks first) are not listed, so every `TYPE_SPECIAL` is loot; item 10's pickup sound is not
    /// played; any other kind is only removed.
    /// @orig 0x0023bf00 Human_PickUpObject (unknown)
    bool take(double handle, int player);

  private:
    script::ScriptSystem& m_scripts;
    GameState& m_state;
    world_objects::SpawnRecords& m_records;
    const world_objects::ObjectTypes& m_types;
};

} // namespace coney
