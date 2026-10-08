// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string_view>
#include <vector>

#include "animation/anim_math.h"
#include "world_objects/object_services.h"
#include "world_objects/object_types.h"

// The breakable street props (newsstands, crate stacks, benches) and the other world objects a strike can land on:
// the two hit counters every world object keeps, what a strike does to them, and how a `dyn_masks` prop breaks and
// goes. Research: docs/research/objects.md#breakable-props, docs/research/combat.md#targets

namespace coney::world_objects {

/// The class of the breakable street props.
inline constexpr std::string_view kDynMasksClass = "dyn_masks";

/// A collision body's flags from its type's word `+0x5e`: bit `0x1` gives `0x2`, `0x2` → `0x4`, `0x4` → `0x10`,
/// `0x8` → `0x8`, `0x10` → `0x40`, `0x20` → `0x20`, `0x40` → `0x2000`, `0x80` → `0x10000`, `0x100` → `0x20000`, on top
/// of `0x80000500`. The kinds that add bits of their own first are left out (none of them is a strike target here).
/// @orig 0x00391d48 Obj_CreatePhysicsBody (unknown)
[[nodiscard]] std::uint32_t bodyFlagsOf(std::uint16_t typeWord);

/// Whether a body with `bodyFlags` is something a strike picks: any of `0x8`, `0x10`, `0x20`.
/// @orig 0x00396710 Object_IsStrikeTarget (unknown)
[[nodiscard]] constexpr bool isStrikeTarget(std::uint32_t bodyFlags) { return (bodyFlags & 0x38U) != 0; }

/// Whether a sphere at `centre` of `radius` touches the collision body of an object of `type` standing at `position`
/// turned by `rotation` (game axes): a box of the type's size about its centre offset (`PHYS.OBB`), or a sphere of half
/// its size's x (`PHYS.SPHERE`), as `Obj_CreatePhysicsBody` makes them. **Coney's stand-ins**: the size is the box's
/// whole extents (the sphere's half x reads it so), and a cylinder or capsule body is tested as its box. No shape
/// (`PHYS.NONE`) or no size: no body.
/// Research: docs/research/objects.md#spawning, docs/research/combat.md#moving-strikes
[[nodiscard]] bool bodyTouches(const ObjectType& type, anim::Vec3 position, anim::Quat rotation, anim::Vec3 centre,
                               float radius);

/// Where a struck prop stands: its object's position and rotation (game axes).
struct PropPose {
    anim::Vec3 position;
    anim::Quat rotation;
};

/// What one strike did to a prop.
struct PropStrike {
    bool intactBefore = false; ///< Neither counter was 0 before it: the strike was damage done (message 6).
    bool broke = false;        ///< A `dyn_masks` prop broke on it.
};

/// The world objects strikes have landed on: each one's two hit counters (`+0x10d`, `+0x10e`) and, for a `dyn_masks`
/// prop, its own hit points and hits, until it breaks and goes. An object's state is made from its type on its first
/// strike and kept by handle while the level lasts.
///
/// **Coney's stand-ins** where the research is open or Coney lacks the system: the noise, the statistic and the
/// burning near a fire are not built; the newsstands' paper debris is left out; the dust's colour, view test and
/// particle room are the services' (ObjectServices::dust()); a piece rests where it is spawned (it is not knocked
/// flying); the crate piece's random turn is drawn in steps of 1/1000 of pi; and a broken prop goes 20 ticks after
/// the break (the original removes it at its next update, up to 20 ticks later). None of the riot props has a damaged
/// model, so none is swapped.
///
/// Research: docs/research/objects.md#breakable-props, docs/research/objects.md#riot-prop-breaks
class Props {
  public:
    /// The ticks after a break until a broken prop goes: its update interval.
    static constexpr int kRemovalTicks = 20;
    /// The splinters a bench breaks into.
    static constexpr int kBenchSplinters = 25;
    /// The two dust bursts every hit on a `dyn_masks` prop raises at the hit point, metres.
    static constexpr float kDustRadius = 3.75F;
    static constexpr float kSecondDustRadius = 2.75F;

    /// A strike from `hit.attacker` (of `hit.kind`, landing at `hit.point`) on the world object `handle` of `type`, at
    /// `pose`
    /// (`Strike_Contact` on a world object): takes the counters (`WorldObject_TakeHit`), plays the impact sound,
    /// tells the boxes the attacker stands in when the object was intact (ObjectServices::damageDone()), then gives a
    /// `dyn_masks` prop its hit (`DynMasks_OnHit`). A broken prop takes nothing more.
    /// @orig 0x0021b290 Strike_Contact (unknown)
    PropStrike strike(double handle, const ObjectType& type, const ObjectHit& hit, const PropPose& pose,
                      ObjectWorld& world);

    /// Whether the prop `handle` is broken (it no longer offers itself to a strike).
    [[nodiscard]] bool broken(double handle) const;
    /// The prop's counter `+0x10d`; nothing for an object no strike has landed on.
    [[nodiscard]] std::optional<std::uint8_t> counter(double handle) const;

    /// One 60 Hz tick: a broken prop's wait for its removal.
    void tick();
    /// The broken props whose time came since the last call: their objects go for good (message 2, spawn record bit
    /// `0x40000`).
    /// @orig 0x003bf548 DynMasks_Update (unknown)
    [[nodiscard]] std::vector<double> takeRemoved();

    /// Forgets every prop (the level is unloaded).
    void clear() {
        m_props.clear();
        m_removed.clear();
    }

  private:
    // One struck object.
    struct Prop {
        std::uint8_t counter = 0;       // +0x10d
        std::uint8_t secondCounter = 0; // +0x10e
        bool onePoint = false;          // +0x128: each hit takes one point
        bool masks = false;             // a dyn_masks prop, with the data below
        int hitpoints = -1;             // its data +0x0c: -1 for none
        int hits = -1;                  // its data +0x10: -1 for none
        bool broken = false;            // its data +0x00
        int removalIn = 0;              // ticks until it goes, once broken
        std::uint32_t modelHash = 0;
    };

    // The state of `handle`, made from `type` on its first strike.
    // @orig 0x003b7538 DynMasks_Init (unknown)
    Prop& stateOf(double handle, const ObjectType& type);
    // A dyn_masks prop's message 1: its own hit points or hits, the material sound, the dust and, when it breaks, its
    // splinters and piece.
    // @orig 0x003b7a88 DynMasks_OnHit (unknown)
    static bool masksHit(Prop& prop, const ObjectType& type, const ObjectHit& hit, const PropPose& pose,
                         ObjectWorld& world);

    std::map<double, Prop> m_props;
    std::vector<double> m_removed;
};

/// `WorldObject_TakeHit` on the counters `counter` (`+0x10d`) and `secondCounter` (`+0x10e`) of an object hit with
/// kind `kind` (−1 a flying or held object's contact): `+0x10e` loses one first while `flyingOrHeld`; otherwise
/// `+0x10d` loses 1 when `kind` is −1 or `onePoint`, else 4 + 6 × kind, floored at 0. A counter at 0 or `0xff`
/// (unbreakable) is left alone.
/// @orig 0x00393450 WorldObject_TakeHit (unknown)
void takeHit(std::uint8_t& counter, std::uint8_t& secondCounter, int kind, bool onePoint, bool flyingOrHeld);

} // namespace coney::world_objects
