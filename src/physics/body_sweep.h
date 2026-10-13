// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

#include "animation/anim_math.h"
#include "physics/contact.h"
#include "physics/mesh_sweep.h"
#include "raycast/collision_mesh.h"

// Moving a body: the physics world's sweep of up to three passes over the contacts the world reports, and the owner's
// answer to each contact (slide, bounce, stop, push) applied as the original applies it. The body is advanced along a
// contact's normal to 0.01 m short of it, then the slid move is swept again in full from there.
// Research: docs/research/physics-bodies.md#unit-3

namespace coney::physics {

struct Body;

/// Body flags (`+0x40`) the sweep reads.
inline constexpr std::uint32_t kBodySweepBodies = 0x1;      ///< Sweep against the other bodies too.
inline constexpr std::uint32_t kBodyNotPushable = 0x200;    ///< A code 4 contact only slides along it.
inline constexpr std::uint32_t kBodyWriteVelocity = 0x400;  ///< Write the slid velocity back (a player, objects).
inline constexpr std::uint32_t kBodyPushUndivided = 0x1000; ///< The push-out is not divided by the push weight.
inline constexpr std::uint32_t kBodyNoCornerSlide = 0x8000; ///< Never add a corner slide.
inline constexpr std::uint32_t kBodyDirty = 0x80000000;     ///< Moved: re-pose and re-sort on the next update.

/// The type mask (`+0x44`) bit of a human.
inline constexpr std::uint32_t kTypeHuman = 0x40;

/// Answer bits of a contact handler: the code in the low 16 bits, then what to do after it.
inline constexpr std::uint32_t kAnswerCodeMask = 0xffff;
inline constexpr std::uint32_t kAnswerStop = 0x10000;  ///< End the walk; the sweep moves the rest (return 0).
inline constexpr std::uint32_t kAnswerScale = 0x20000; ///< Scale the velocity by the body's contact scale.
inline constexpr std::uint32_t kAnswerAbort = 0x40000; ///< Abort the sweep (return −1).

/// The codes a contact handler answers.
inline constexpr std::uint32_t kCodeSlide = 1;  ///< Advance, then slide `V` and `v` along the normal.
inline constexpr std::uint32_t kCodeBounce = 2; ///< Advance, then bounce both by the owner's restitution.
inline constexpr std::uint32_t kCodeStop = 3;   ///< Advance, then stop both.
inline constexpr std::uint32_t kCodePush = 4;   ///< Push the other body by half the move into it, then slide.

/// Owner attributes (owner vtable `+0xd4`).
inline constexpr int kAttributeWeight = 1;
inline constexpr int kAttributePushWeight = 7;
inline constexpr int kAttributeRestitution = 8;

/// What a body's owner answers the sweep (the original's owner vtable slots,
/// docs/research/physics-bodies.md#u3-records).
class BodyOwner {
  public:
    virtual ~BodyOwner() = default;
    BodyOwner() = default;
    BodyOwner(const BodyOwner&) = delete;
    BodyOwner& operator=(const BodyOwner&) = delete;
    BodyOwner(BodyOwner&&) = delete;
    BodyOwner& operator=(BodyOwner&&) = delete;

    /// `+0x24`: the type mask (kTypeHuman for a human).
    [[nodiscard]] virtual std::uint32_t typeMask() const = 0;
    /// `+0x54`: the state's airborne bit (`0x4000000`).
    [[nodiscard]] virtual bool airborne() const = 0;
    /// `+0x9c`: the position the owner copied at the start of its update.
    [[nodiscard]] virtual anim::Vec3 cachedPosition() const = 0;
    /// `+0xac`: the position now.
    [[nodiscard]] virtual anim::Vec3 position() const = 0;
    /// `+0xb4`: moves the owner (its rotation unchanged); the shapes follow it.
    virtual void setPosition(anim::Vec3 position) = 0;
    /// `+0x74`: sets the owner's velocity.
    virtual void setVelocity(anim::Vec3 velocity) = 0;
    /// `+0xd4`: an attribute (kAttributeWeight, kAttributePushWeight, kAttributeRestitution).
    [[nodiscard]] virtual float attribute(int which) const = 0;
    /// `+0xf4`: the owner's body, or null when it has none.
    [[nodiscard]] virtual Body* body() = 0;
    /// `+0xfc`: the contact handler's answer: a code and kAnswer* bits.
    [[nodiscard]] virtual std::uint32_t onContact(const Contact& contact) = 0;
    /// `+0x11c`: called once after a sweep that was not aborted.
    virtual void afterMove() {}
    /// `Human_IsInPairOrBlock` (`0x00227fd8`): a human in a grab, hold, mug, tackle, throw or block takes no corner
    /// slide. False for anything else.
    [[nodiscard]] virtual bool inPairOrBlock() const { return false; }
    /// The airborne human's landing segment (`Human_LandingTest`), asked in each pass while the body's type mask has
    /// kTypeHuman and the owner is airborne: a contact flagged kContactLanding, or nothing.
    [[nodiscard]] virtual std::optional<Contact> landing(anim::Vec3 /*move*/) { return std::nullopt; }
    /// The body slot `+0x54` "you were pushed", called on the other body's owner of a code 4 contact after `push` was
    /// added to its body's push-out; `move` is the pusher's pass move. Nothing by default.
    virtual void onPushed(const Contact& /*contact*/, anim::Vec3 /*push*/, anim::Vec3 /*move*/) {}
};

/// The kinds of shape a body sweeps with (shape `+0x30`).
enum class ShapeType : std::uint8_t {
    Sphere = 2,  ///< PhysicsMesh_SweepSphere.
    Capsule = 3, ///< PhysicsMesh_SweepCapsule: the walking body's one sphere.
};

/// A body's first shape as its sweep uses it.
struct BodyShape {
    ShapeType type = ShapeType::Capsule;
    float radius = 0.0F;         ///< Sphere: its radius. Capsule: the walking sphere's (shape × body `+0x60` × scale).
    bool enabled = true;         ///< Shape flags `+0x32` bit 0; a disabled shape moves freely.
    float footGap = 0.05F;       ///< Capsule: the sphere's centre is `radius + footGap` above the position.
    float minWallHeight = 0.25F; ///< Capsule: the low and thin rules.
};

/// Which contact scale the body answers (body vtable `+0x48`).
enum class ContactScale : std::uint8_t {
    Generic, ///< 1.0 against a body, 0.98 against the level.
    Human,   ///< 0.25 while the owner is airborne, else 1.0.
};

/// A physics body (docs/research/physics.md#bodies), the fields the sweep reads and writes.
struct Body {
    BodyOwner* owner = nullptr; ///< `+0x34`.
    std::uint32_t flags = 0;    ///< `+0x40`: kBody* bits.
    std::uint32_t typeMask = 0; ///< `+0x44`: the owner's type mask.
    anim::Vec3 pushOut;         ///< `+0x50`: the pending push-out, used once by the next sweep.
    int stuck = 0;              ///< `+0x70`: updates in a row the sweep could not move the body.
    BodyShape shape;            ///< The first shape.
    ContactScale scaleKind = ContactScale::Generic;
};

/// The contacts of one pass: a pool whose entries do not move as it grows, and the list sorted by fraction.
struct ContactList {
    std::deque<Contact> pool;
    std::vector<Contact*> sorted;

    /// Empties both for the next pass.
    void clear();
};

/// What the sweep asks of the world it moves through.
class SweepWorld {
  public:
    virtual ~SweepWorld() = default;
    SweepWorld() = default;
    SweepWorld(const SweepWorld&) = delete;
    SweepWorld& operator=(const SweepWorld&) = delete;
    SweepWorld(SweepWorld&&) = delete;
    SweepWorld& operator=(SweepWorld&&) = delete;

    /// The pass's contacts with the level, inserted into `list` (IPhysics_SweepBodyVsMesh).
    virtual void sweepMesh(const Box& box, Body& body, anim::Vec3 move, ContactList& list) = 0;
    /// The pass's contacts with the other bodies, for a body with kBodySweepBodies (Unit 11); none by default.
    virtual void sweepBodies(const Box& /*box*/, Body& /*body*/, anim::Vec3 /*move*/, ContactList& /*list*/) {}
    /// Level triangle `index`'s flags now (a contact's triangle may have been disabled since it was found).
    [[nodiscard]] virtual std::uint16_t triangleFlags(std::uint32_t index) const = 0;
};

/// The level's collision mesh as the sweep's world: each pass inserts the shape's nearest mesh contact, then an
/// airborne human's landing contact.
class MeshWorld final : public SweepWorld {
  public:
    /// A world over `mesh`, querying with `scratch` (kept by the caller so its memory is reused between sweeps); both
    /// must outlive it.
    MeshWorld(const raycast::CollisionMesh& mesh, MeshScratch& scratch) : m_mesh(mesh), m_scratch(scratch) {}

    /// The shape's mesh sweep (the capsule's or the sphere's, with one contact asked for, option 4 for a human and the
    /// −0.65 minimum `n.z`); only the first contact it writes is inserted. Then, for a human body whose owner is
    /// airborne, the owner's landing contact, flagged kContactLanding.
    /// @orig 0x0033d340 IPhysics_SweepBodyVsMesh (unknown)
    /// @orig 0x0033d2d8 IPhysics_SweepShapeVsMesh (unknown)
    void sweepMesh(const Box& box, Body& body, anim::Vec3 move, ContactList& list) override;
    [[nodiscard]] std::uint16_t triangleFlags(std::uint32_t index) const override;

  private:
    const raycast::CollisionMesh& m_mesh;
    MeshScratch& m_scratch;
    std::vector<Contact> m_found;
};

/// The box a body's sweep queries: its shape's bounds at its start position. **Coney stand-in**: a capsule body's box
/// is its walking sphere's, not the capsule shape's own bounds (Unit 6).
[[nodiscard]] Box shapeBox(const Body& body, anim::Vec3 position);

/// Moves `body` by its owner's velocity `velocity` over `dt` against `world`: the pending push-out joins the move
/// (over `dt` and, unless kBodyPushUndivided, the owner's push weight) and is cleared; up to three passes each sweep
/// the whole current move from where the body is and resolve the contacts; then the rest of the move is made, or the
/// body is stuck (horizontal velocity zeroed and written, the counter up) when three passes all stopped. Returns the
/// displacement from the owner's cached position, or nothing when the move was too small to make or a contact aborted
/// it (the owner's after-move call is then not made either).
/// @orig 0x0033e278 PhysicsBody_Sweep (unknown)
std::optional<anim::Vec3> sweepBody(float dt, SweepWorld& world, Body& body, anim::Vec3 velocity);

/// The state one sweep keeps across its passes.
struct MoveState {
    anim::Vec3 move;         ///< `V`: the move velocity, slid, bounced and given corner slides.
    anim::Vec3 velocity;     ///< `v`: the real velocity, slid and bounced like `V`, written back to the owner.
    std::uint32_t flags = 0; ///< kMove* bits.
    float scale = 0.0F;      ///< The largest contact scale met (with kMoveScale).
};

/// MoveState flags.
inline constexpr std::uint32_t kMoveSlideStored = 0x1; ///< A slide was stored in the push-out this move.
inline constexpr std::uint32_t kMoveWriteBack = 0x2;   ///< Write `v` back to the owner.
inline constexpr std::uint32_t kMoveScale = 0x4;       ///< A contact scale applies.

/// Walks the pass's contacts `sorted` (nearest first) for a body that moves by `pass` this pass: skips disabled
/// triangles and owners without bodies, asks the owner, applies the first non-zero code and the tail, and returns
/// 1 (stopped: slide and try again), 0 (done: move the rest) or −1 (abort).
/// @orig 0x0033d9d8 PhysicsBody_ResolveContacts (unknown)
int resolveContacts(const SweepWorld& world, Body& body, const std::vector<Contact*>& sorted, anim::Vec3 pass,
                    MoveState& state);

/// The advance as a stand-alone: `d = (move × fraction) · n`; when `d < −0.01`, `point += n × (d + 0.01)`.
/// @orig 0x0033d7b8 PhysicsVec_BackOffPlane (unknown)
void backOffPlane(anim::Vec3& point, anim::Vec3 move, anim::Vec3 normal, float fraction);

/// Removes from `v` its part into the unit `n`, only when it goes in (`v · n < 0`).
/// @orig 0x0033d870 Vec_RemoveNormalPart (unknown)
void removeNormalPart(anim::Vec3& v, anim::Vec3 n);

/// Bounces `v` off the unit `n` with restitution `e`: going in it is reflected (`v −= n d (1 + e)`); going out its
/// normal speed is still reduced to `1 − e` of itself.
/// @orig 0x0033d8f0 PhysicsVec_Bounce (unknown)
void bounce(anim::Vec3& v, anim::Vec3 n, float e);

/// Scales `box` about its centre by `s` per axis (nothing when all three are exactly 1); a negative scale still gives a
/// valid box.
/// @orig 0x003414c0 AABB_ScaleAboutCentre (unknown)
void scaleAboutCentre(Box& box, anim::Vec3 s);

/// Grows `box` to cover itself moved by `d`.
/// @orig 0x00341840 AABB_ExtendByMove (unknown)
void extendByMove(Box& box, anim::Vec3 d);

/// The contact scale of a body: Generic answers 1.0 against a body and 0.98 against the level; Human 0.25 while its
/// owner is airborne, else 1.0.
/// @orig 0x00341e18 PhysicsBody_GetContactScale (unknown)
/// @orig 0x00342288 PhysicsHumanBody_GetContactScale (unknown)
[[nodiscard]] float contactScale(const Body& body, const Contact& contact);

/// The airborne human's landing segment, from the higher of `upper` and the moved feet (`feet + move`, raised by
/// `raise` first) to the lower, cast against every enabled triangle of `mesh`: a contact with the hit's normal and
/// triangle when the normal's z is above 0.65, its fraction measured on the segment beyond `reach` (1.0 m) from the
/// origin, 0 when the hit is nearer. Nothing otherwise.
/// @orig 0x0023e408 Human_LandingTest (unknown)
[[nodiscard]] std::optional<Contact> landingContact(const raycast::CollisionMesh& mesh, anim::Vec3 feet,
                                                    anim::Vec3 upper, anim::Vec3 move, float raise = 0.0F,
                                                    float reach = 1.0F);

} // namespace coney::physics
