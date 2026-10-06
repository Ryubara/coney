// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "animation/anim_math.h"
#include "world_objects/doors.h"
#include "world_objects/glass.h"
#include "world_objects/lock_pick.h"
#include "world_objects/object_services.h"

namespace coney::world_objects {

/// A level's breakable glass and doors, and the world they change: what the level scripts' bindings fill
/// (`SpawnBreakableGlass`, `SpawnDoor`, the door commands) and what play mode steps and hits. The glass type table and
/// the lock-pick handlers are global configuration and survive clear().
///
/// Play mode sets `world` once the level's collision mesh and path data are loaded, calls tick() on every 60 Hz tick
/// (twice per 1/30 s step), and sends a human's landed hit or a thrown object's to humanHit() / thrownHit() with the
/// handle objectOfTriangle() finds for the triangle it struck.
///
/// Research: docs/research/objects.md
class LevelObjects {
  public:
    ObjectWorld world;         ///< What the panes and doors change.
    GlassPanes glass;          ///< The glass types and panes.
    Doors doors;               ///< The doors and barriers.
    LockPickHandlers lockPick; ///< `CfgSetLockPickHandler`'s and `CfgSetLockPickStageFailHandler`'s functions.

    /// A human's landed hit on `object` (`Strike_Contact`): a pane breaks (with its alarm, window link, flags and
    /// statistic); a door or barrier takes the hit, and a `TYPE_BREAKANDENTER_DOOR` also reports a break-in at the
    /// attacker. Returns whether the object took it.
    /// @orig 0x0021b290 Strike_Contact (unknown)
    bool humanHit(double object, const ObjectHit& hit);
    /// A thrown object's hit on `object`, `hit.attacker` its thrower and `hit.kind` from thrownHitKind(): as humanHit()
    /// without the break-in.
    /// @orig 0x00393538 Thrown_HitObject (unknown)
    bool thrownHit(double object, const ObjectHit& hit);

    /// The pane or door one of whose collision triangles is `triangle`; nothing when none.
    [[nodiscard]] std::optional<double> objectOfTriangle(std::uint32_t triangle) const;
    /// Where the pane (its centre), door or leaf `object` is; nothing for another handle.
    [[nodiscard]] std::optional<anim::Vec3> positionOf(double object) const;

    /// One 60 Hz tick of the doors (the panes' update does nothing).
    void tick() { doors.tick(world); }
    /// Forgets the level's panes and doors (its unload).
    void clear();
};

/// The hit kind of a thrown object of `objectType` in animation set `animSet`: 1, but 3 for a `TYPE_MISSIONTV` and 0
/// for an object of animation set 5 other than a molotov.
/// @orig 0x00393538 Thrown_HitObject (unknown)
[[nodiscard]] HitKind thrownHitKind(int objectType, int animSet, bool molotov);

/// The hit kind of a human's hit: 3 while airborne, else 2 while his attack record has any of `0x1400000` (the run
/// attack, the charge or the dive), else 0.
/// @orig 0x0021b290 Strike_Contact (unknown)
[[nodiscard]] HitKind humanHitKind(bool runAttackOrCharge, bool airborne);

} // namespace coney::world_objects
