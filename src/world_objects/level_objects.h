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
    /// attacker. A hit the object took is damage done by the attacker (ObjectServices::damageDone()). Returns whether
    /// the object took it.
    /// @orig 0x0021b290 Strike_Contact (unknown)
    bool humanHit(double object, const ObjectHit& hit);
    /// A thrown object's hit on `object`, `hit.attacker` its thrower and `hit.kind` from thrownHitKind(): as humanHit()
    /// without the break-in; a hit taken is the thrower's damage done.
    /// @orig 0x00393538 Thrown_HitObject (unknown)
    bool thrownHit(double object, const ObjectHit& hit);

    /// The pane or door one of whose collision triangles is `triangle`; nothing when none.
    [[nodiscard]] std::optional<double> objectOfTriangle(std::uint32_t triangle) const;
    /// Where the pane (its centre), door or leaf `object` is; nothing for another handle.
    [[nodiscard]] std::optional<anim::Vec3> positionOf(double object) const;

    /// `CamGhostDoor(door)`: the camera passes through the door from now on: its two collision triangles take type bit
    /// `0x200`, which the camera's rays skip, until the level's mesh is loaded again. Only a door of the door kinds
    /// (`TYPE_DOOR`, `TYPE_BREAKABLE_DOOR`, `TYPE_BREAKANDENTER_DOOR`, `TYPE_SLIDING_DOOR`); false for another handle.
    /// Research: docs/references/bindings/camera.md#camghostdoor
    /// @orig 0x003973a0 Door_SetCameraGhost (unknown)
    bool ghostForCamera(double door);
    /// One 60 Hz tick of the doors (the panes' update does nothing).
    void tick() { doors.tick(world); }
    /// Forgets the level's panes and doors (its unload).
    void clear();

  private:
    // humanHit() without the damage message: the pane, door or barrier takes the hit.
    bool takeHumanHit(double object, const ObjectHit& hit);
};

/// The collision triangles' type bit the camera's rays skip (`CamGhostDoor`).
inline constexpr std::uint16_t kCameraGhostBit = 0x200;

/// Whether an object of `objectType` (`TYPE_*`) is a door: 15, 25, 30 or 33.
/// @orig 0x00395020 ObjType_IsDoorKind (unknown)
[[nodiscard]] constexpr bool isDoorKind(int objectType) {
    return objectType == 15 || objectType == 25 || objectType == 30 || objectType == 33;
}

/// The hit kind of a thrown object of `objectType` in animation set `animSet`: 1, but 3 for a `TYPE_MISSIONTV` and 0
/// for an object of animation set 5 other than a molotov.
/// @orig 0x00393538 Thrown_HitObject (unknown)
[[nodiscard]] HitKind thrownHitKind(int objectType, int animSet, bool molotov);

/// The hit kind of a human's hit: 3 while airborne, else 2 while his attack record has any of `0x1400000` (the run
/// attack, the charge or the dive), else 0.
/// @orig 0x0021b290 Strike_Contact (unknown)
[[nodiscard]] HitKind humanHitKind(bool runAttackOrCharge, bool airborne);

} // namespace coney::world_objects
