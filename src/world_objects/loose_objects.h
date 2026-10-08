// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <functional>
#include <map>
#include <optional>

#include "animation/anim_math.h"
#include "world_objects/object_types.h"

// Loose world objects in flight: a dropped (or thrown) object falls under gravity once per 30 Hz update, bounces off
// what its move meets, and on its first floor contact starts a "settle", a short eased turn onto its nearest face run
// on the 60 Hz physics tick; its next floor contact once the settle is done stops it. The original runs the fall in
// the world object's own update and the settle in the physics step; Coney runs both from one fixed 1/30 s step (one
// update, then two settle ticks), which gives the same sequence.
// Research: docs/research/physics.md#settle, docs/research/physics.md#movers, docs/research/physics.md#step,
// docs/research/physics.md#attributes, docs/research/objects.md#held

namespace coney::world_objects {

/// The axes a type may settle onto: the low three bits of its `CfgObj` `axis` byte (`+0x85`), bit 0 the local x
/// axis, bit 1 y, bit 2 z. Bit 3 (the `_ROUND` variants) is tested by the original but never changes the result.
inline constexpr int kSettleAxesMask = 7;

/// The local axis of `rotation` (0 x, 1 y, 2 z) whose positive direction is nearest `normal` (the largest
/// `axis · normal`, the smallest angle) among those `axisMask` allows; nothing when it allows none (`axis` 0: the type
/// never settles).
/// @orig 0x00340b38 Settle_NearestAxis (physics.cpp)
[[nodiscard]] std::optional<int> settleAxis(anim::Quat rotation, anim::Vec3 normal, int axisMask);

/// The rotation a settle ends at: `rotation` turned by the smallest angle that lines its settleAxis() up with
/// `normal`, the turn wrapped to ±90° (the axis's negative direction is lined up when it points away), so the object
/// comes to rest on its nearest face. `rotation` itself when the mask allows no axis.
/// @orig 0x00340d08 Settle_ComputeTarget (physics.cpp)
[[nodiscard]] anim::Quat settleTarget(anim::Quat rotation, anim::Vec3 normal, int axisMask);

/// One settle slot (0x30 bytes in the original): a turn from `start` to `end` whose progress accelerates by 0.02
/// a tick² from rest, so it takes 11 ticks (183 ms), slow at first, like something tipping over.
///
/// Research: docs/research/physics.md#settle-slot
struct SettleTurn {
    /// The acceleration of the progress, per 60 Hz tick² (`0x3ca3d70a`).
    static constexpr float kAcceleration = 0.02F;

    anim::Quat start;   ///< `+0x20`: the rotation when it landed.
    anim::Quat end;     ///< `+0x10`: settleTarget().
    float t = 0.0F;     ///< `+0x04`: progress, 0 to 1.
    float speed = 0.0F; ///< `+0x0c`: progress per tick.

    /// One 60 Hz tick: `t += speed`, `speed += kAcceleration`, `t` clamped to 1. True when the turn is done.
    /// @orig 0x00340918 IPhysics_Step (physics.cpp)
    bool tick();
    /// The rotation now, `slerp(start, end, t)`.
    [[nodiscard]] anim::Quat rotation() const;
};

/// A world object's physics body, as `Obj_CreatePhysicsBody` makes it from the type: a box of half-extents
/// size / 2 (`OBB`), a sphere of radius size.x / 2 (`SPHERE`), or nothing (a point), offset by the type's centre.
/// @orig 0x00391d48 Obj_CreatePhysicsBody (unknown)
struct LooseShape {
    int kind = 0;           ///< kBodyBox, kBodySphere, or 0 for a point.
    anim::Vec3 halfExtents; ///< The box's half-extents; the sphere's radius in x.
    anim::Vec3 centre;      ///< The body's offset from the object's origin, in the object's frame.

    /// The shape of `type`'s body.
    [[nodiscard]] static LooseShape of(const ObjectType& type);
    /// How far the shape reaches from its centre along the unit direction `d`, turned by `rotation`.
    [[nodiscard]] float reach(anim::Quat rotation, anim::Vec3 d) const;
};

/// What a ray met: its distance along the ray, the surface's unit normal (towards the ray's origin) and whether it
/// was a body (a human, a car, another object) rather than the level's collision mesh.
struct RayContact {
    float distance = 0.0F;
    anim::Vec3 normal{0.0F, 0.0F, 1.0F};
    bool body = false;
};

/// The test a loose object's move asks for: the first surface along the ray from `origin` in the unit direction
/// `direction`, at most `length` away; nothing for none.
using RayTest = std::function<std::optional<RayContact>(anim::Vec3 origin, anim::Vec3 direction, float length)>;

/// One loose object's motion state, as the world object keeps it (`+0x54` flags among it).
struct LooseObject {
    anim::Vec3 position;
    anim::Quat rotation;
    anim::Vec3 velocity;        ///< Metres a second.
    anim::Vec3 angularVelocity; ///< Radians a second, about the world's axes (**Coney's reading**: the frame is open).
    LooseShape shape;
    int axisMask = 0;         ///< The type's `axis` byte.
    float restitution = 0.0F; ///< The type's `+0x88`.
    bool airborne = true;     ///< Flag `0x4000000`.
    bool settling = false;    ///< Flag `0x40000`: a settle slot turns it.
    bool settled = false;     ///< Flag `0x8000000`: its settle is done; the next floor contact stops it.
    bool grounded = false;    ///< Flag `0x2000000`: at rest.
    bool firstUpdate = true;  ///< The update after the holder let go, whose move lasts 1/60 s.
    std::optional<SettleTurn> settle;

    /// The update's interval in 60 Hz ticks: 2 while airborne or settling, otherwise the resting objects' 20.
    [[nodiscard]] int interval() const { return airborne || settling ? 2 : 20; }
};

/// The loose objects in flight, by handle. Stepped once per fixed 1/30 s step; an object that comes to rest leaves.
///
/// **Coney's stand-ins**: the shape's move is a ray from its centre along the move, as long as the move plus the
/// shape's reach that way, and a contact leaves the shape kBackOff in front of the surface however shallow the move
/// went into it (the original sweeps the shape, and leaves a contact under 0.01 m deep where it is). An object of a
/// type with `axis` 0 never settles, so it never stops (as in the original, inferred); the scripts' throwable ones
/// break on their first contact.
///
/// Research: docs/research/physics.md#settle, docs/research/physics.md#movers
class LooseObjects {
  public:
    /// Gravity on a flying object, m/s².
    static constexpr float kGravity = 15.68F;
    /// An update's length while flying (an interval of 2 ticks), and the length of the first update after a drop and
    /// of every update while settling (the settle's turn re-poses the object each tick, stamping its update time).
    static constexpr float kUpdateSeconds = 1.0F / 30.0F;
    static constexpr float kShortUpdateSeconds = 1.0F / 60.0F;
    /// A contact whose normal's z is above this is the ground.
    static constexpr float kFloorNormalZ = 0.7F;
    /// How far in front of a surface a contact leaves the object.
    static constexpr float kBackOff = 0.01F;
    /// The level mesh's contact scale: a floor bounce's velocity is scaled by it (its z only when upward).
    static constexpr float kMeshContactScale = 0.98F;
    /// The physics world's settle slots: with all in use, a landing starts no settle.
    static constexpr std::size_t kSettleSlots = 64;
    /// Settle ticks (60 Hz) per step (1/30 s).
    static constexpr int kTicksPerStep = 2;

    /// What an object leaves a hand with: its type's settle axes, restitution and body.
    struct Kind {
        int axisMask = 0;
        float restitution = 0.0F;
        LooseShape shape;

        /// The kind of an object of `type`.
        [[nodiscard]] static Kind of(const ObjectType& type) {
            return Kind{.axisMask = type.axis, .restitution = type.restitution, .shape = LooseShape::of(type)};
        }
    };

    /// Object `handle` leaves a hand at `position` turned by `rotation`, moving at `velocity` and spinning at
    /// `angularVelocity` (both zero for a drop). Replaces any flight it had.
    /// @orig 0x00257f38 Human_DropHeld (unknown)
    void start(double handle, anim::Vec3 position, anim::Quat rotation, const Kind& kind, anim::Vec3 velocity = {},
               anim::Vec3 angularVelocity = {});

    /// The object is removed or picked up: it leaves, and its settle with it.
    /// @orig 0x00391c10 WorldObject_Remove (unknown)
    void remove(double handle) { m_objects.erase(handle); }

    /// One step: each object's update (fall, move, contact), then kTicksPerStep settle ticks. Objects that came to
    /// rest are reported to `rested` (may be empty) with their final state and leave.
    void step(const RayTest& ray, const std::function<void(double, const LooseObject&)>& rested = {});

    /// The object `handle` in flight; null when it is not.
    [[nodiscard]] const LooseObject* find(double handle) const;
    /// Every object in flight, by handle.
    [[nodiscard]] const std::map<double, LooseObject>& all() const { return m_objects; }
    /// How many settle slots are in use.
    [[nodiscard]] std::size_t settlesInUse() const;
    /// Forgets every object (the level is unloaded).
    void clear() { m_objects.clear(); }

  private:
    // One object's update: gravity, its move and what the move met.
    // @orig 0x00395a10 WorldObject_Integrate (unknown)
    void update(LooseObject& object, const RayTest& ray);
    // A contact at `point` (where the move's ray met the surface) with `contact.normal`: the answer of the world
    // object's contact handler, carried out.
    // @orig 0x00394050 WorldObject_OnContact (unknown)
    void contact(LooseObject& object, anim::Vec3 point, const RayContact& contact);

    std::map<double, LooseObject> m_objects;
};

} // namespace coney::world_objects
