// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>

#include "animation/anim_math.h"
#include "physics/contact.h"
#include "physics/physics_world.h"
#include "physics/shapes.h"

// The human body set (docs/research/physics-bodies.md#unit-10): what a human body adds to a body of the physics world.
// Its scale step, its update with the broad bound a strike gathers from, Moved, the strike-shape switch, the weapon
// strike spheres, the push sphere and the push it gives other humans, and the strike test that sweeps the switched-on
// shapes against the level and against every body the broad bound meets. Owners are named by id; what the body set
// asks of an owner goes through HumanBodyOwners.
// Research: docs/research/physics-bodies.md#unit-10

namespace coney::physics {

/// A ray of the strike test's line of sight: from `origin` along the unit `direction` for `length`.
struct StrikeRay {
    anim::Vec3 origin;
    anim::Vec3 direction;
    float length = 0.0F;
};

/// The strike test's ray options (`WorldManager_RayCast`'s last argument): 0x48 for a candidate that is not a human,
/// 0x8 for a human.
inline constexpr std::uint32_t kStrikeRayHuman = 0x8;
inline constexpr std::uint32_t kStrikeRayObject = 0x48;

/// The mesh sweep options of the strike test's level steps: a weapon sphere's (walls only, triangles with bit 3
/// skipped), and a strike sphere's (the same, one-sided faces kept).
inline constexpr std::uint32_t kWeaponLevelOptions = 0x800c;
inline constexpr std::uint32_t kStrikeLevelOptions = 0x804c;

/// One bone shape of a human body set as the world's constructor builds it, before the body scale: its bone, offset
/// on the bone, radius and (a segment) length.
struct BoneShapeDef {
    int bone = -1;
    anim::Vec3 offset;
    float radius = 0.0F;
    float length = 0.0F;
};

/// The five bone segments in chain order: spine, both forearms, both shins (offsets `0x00512810`, radii
/// `0x005128b0`, lengths `0x005128e0`; docs/research/physics-bodies.md#u9-constructed). The spine is a strike target.
inline constexpr std::array<BoneShapeDef, 5> kBoneSegments{{
    {.bone = 3, .offset = {-0.07F, 0.0F, 0.05F}, .radius = 0.18F, .length = 0.38F},
    {.bone = 24, .offset = {}, .radius = 0.07F, .length = 0.2F},
    {.bone = 18, .offset = {}, .radius = 0.07F, .length = 0.2F},
    {.bone = 32, .offset = {}, .radius = 0.1F, .length = 0.4F},
    {.bone = 29, .offset = {}, .radius = 0.1F, .length = 0.4F},
}};

/// The five bone spheres in chain order after the segments: head, both hands, both feet (offsets `0x00512860`, radii
/// `0x005128c8`). The head is a strike target.
inline constexpr std::array<BoneShapeDef, 5> kBoneSpheres{{
    {.bone = 6, .offset = {0.05F, 0.0F, 0.03F}, .radius = 0.15F, .length = 0.0F},
    {.bone = 25, .offset = {0.08F, 0.0F, 0.0F}, .radius = 0.09F, .length = 0.0F},
    {.bone = 19, .offset = {0.08F, 0.0F, 0.0F}, .radius = 0.09F, .length = 0.0F},
    {.bone = 33, .offset = {}, .radius = 0.15F, .length = 0.0F},
    {.bone = 30, .offset = {}, .radius = 0.15F, .length = 0.0F},
}};

/// What the human body set asks of owners, by id: the owner vtable slots and the human functions its code calls
/// (docs/research/physics-bodies.md#unit-10). The game mode answers for its humans and objects.
class HumanBodyOwners {
  public:
    virtual ~HumanBodyOwners() = default;
    HumanBodyOwners() = default;
    HumanBodyOwners(const HumanBodyOwners&) = delete;
    HumanBodyOwners& operator=(const HumanBodyOwners&) = delete;
    HumanBodyOwners(HumanBodyOwners&&) = delete;
    HumanBodyOwners& operator=(HumanBodyOwners&&) = delete;

    /// What a shape's pose reads of `owner` (the owner handed to the shape vtable's pose slot).
    [[nodiscard]] virtual const ShapeOwner& shapeOwner(OwnerId owner) const = 0;
    /// `Human_GetScale` (`0x0021d020`): the human's `+0x65c`, or 1.0 while in a scene state with it under 1.1.
    [[nodiscard]] virtual float scale(OwnerId owner) const = 0;
    /// The reach of the anim the human plays (`AttackTable_GetReach` `0x002544a0` with `Human_GetAnimId`), or 0 when
    /// the owner is not a human, has no anim record or no Anim Range List.
    [[nodiscard]] virtual float reach(OwnerId owner) const = 0;
    /// `Human_GetHeldObject` (`0x00226ff0`), or kNoOwner.
    [[nodiscard]] virtual OwnerId heldObject(OwnerId owner) const = 0;
    /// `Human_GetTarget` (`0x00226e60`), or kNoOwner.
    [[nodiscard]] virtual OwnerId target(OwnerId owner) const = 0;
    /// Whether the human is a player (its byte `+0x1b0` is not −1).
    [[nodiscard]] virtual bool isPlayer(OwnerId owner) const = 0;
    /// The owner's body (owner vtable `+0xf0`).
    [[nodiscard]] virtual BodyId bodyOf(OwnerId owner) const = 0;
    /// The owner's authoritative position (the transforms at `0x00714b00`, slot `+0x92`).
    [[nodiscard]] virtual anim::Vec3 position(OwnerId owner) const = 0;
    /// An attribute (owner vtable `+0xd0`): kAttributeWeight (1) or kAttributePushWeight (7).
    [[nodiscard]] virtual float attribute(OwnerId owner, int which) const = 0;
    /// The owner's state flags (owner vtable `+0x50`).
    [[nodiscard]] virtual std::uint64_t state(OwnerId owner) const = 0;
    /// The other human of the owner's grab (`Human_GetGrabOther` `0x002269d8`), or kNoOwner.
    [[nodiscard]] virtual OwnerId grabPartner(OwnerId owner) const = 0;
    /// `Anim_HitsWithCapsule(owner, Human_GetAnimId(owner))` (`0x00101b88`): its clip has flag `0x10000`.
    [[nodiscard]] virtual bool hitsWithCapsule(OwnerId owner) const = 0;
    /// Owner vtable `+0x108`: whether the striker `striker` strikes `other`'s body at all.
    [[nodiscard]] virtual bool strikes(OwnerId striker, OwnerId other) const = 0;
    /// `Strike_Contact` (owner vtable `+0x100`): `striker`'s strike shape met something.
    virtual void strikeContact(OwnerId striker, const Contact& contact) = 0;
    /// The held object's contact handler (owner vtable `+0xf8`) with a weapon sphere's contact; 5 asks for the next
    /// shape of a body that is not a human.
    virtual std::uint32_t weaponContact(OwnerId handler, const Contact& contact) = 0;
    /// `WorldManager_RayCast` (`0x0040db88`) with the material exclusion list at `0x005771c8`: whether the ray hits.
    [[nodiscard]] virtual bool rayBlocked(const StrikeRay& ray, std::uint32_t options) const = 0;
};

/// The human body's scale step: the capsule sized by the owner's scale (height 2 × s, radius 0.35 × s), the five
/// bone segments' offsets, radii and lengths and the five bone spheres' offsets and radii scaled from the
/// constructor's tables, then the body marked dirty and updated at once.
/// @orig 0x00341f28 PhysicsBody_ApplyHumanScale (unknown)
void applyHumanScale(PhysicsWorld& world, BodyId id, const HumanBodyOwners& owners);

/// The human body's update: nothing while flag 0x4000 is set; when dirty the capsule is posed, bounds the body alone
/// and the body is re-sorted; the weapon spheres are posed with the held object (or the owner); when dirty, a weapon
/// timer runs or a strike bit is set, the broad bound is rebuilt; then the switched-on or target bone shapes and the
/// push sphere are posed.
/// @orig 0x003422d0 PhysicsHumanBody_Update (unknown)
void updateHumanBody(PhysicsWorld& world, BodyId id, const HumanBodyOwners& owners);

/// The human body set to a new pose `m`: the body's Moved, the broad bound a 0.8 m by 2.0 m upright cylinder on the
/// translation (not scaled), and every switched-on or target bone sphere's centre set to the translation (segments
/// do nothing). No previous position is reset.
/// @orig 0x00342828 PhysicsHumanBody_Moved (unknown)
void humanBodyMoved(PhysicsWorld& world, BodyId id, const anim::Mat34& m);

/// A clip event's strike-shape switch for bone `bone`: always first empties the struck list; `bone` < 0 and off
/// clears every strike bit and switches every bone shape off; otherwise bit `1 << ((bone − 2) & 31)` changes, and the
/// first bone shape of that bone with it. Bones 0 and 32 share a bit.
/// @orig 0x003428d0 PhysBody_SetShapeEnabled (unknown)
void setStrikeShapeEnabled(PhysicsWorld& world, BodyId id, int bone, bool on);

/// Pushes a weapon strike sphere at the head of the body's weapon chain.
/// @orig 0x00342158 PhysicsHumanBody_AddStrikeSphere (unknown)
void addStrikeSphere(PhysicsWorld& world, BodyId id, ShapeId sphere);

/// Pops every weapon strike sphere off the chain, switching each off.
/// @orig 0x00342168 PhysicsHumanBody_ClearStrikeSpheres (unknown)
void clearStrikeSpheres(PhysicsWorld& world, BodyId id);

/// Switches the push sphere: on takes a big sphere when none is held (none when all are taken); off gives it back.
/// @orig 0x003420d0 PhysicsHumanBody_SetPushSphere (unknown)
void setPushSphere(PhysicsWorld& world, BodyId id, bool on);

/// The push sphere's push: every human whose capsule the push sphere overlaps (at most 32, walked from the last
/// contact) gets `0.25 × w(this) / (w(other) × k(other))` along the 3-D line from this human added to its body's
/// push-out, except the target, a player and a body with flag 0x200.
/// @orig 0x003425c0 PhysicsHumanBody_PushAwayHumans (unknown)
void pushAwayHumans(PhysicsWorld& world, BodyId id, const HumanBodyOwners& owners);

/// The strike test, after the move: the weapon spheres and then the strike spheres against the level, then every body
/// the broad bound gathers that is in line of sight, struck once per update by the weapon spheres and once by the
/// strike shapes, each hit going to the owner and onto the struck list; then the weapon timer runs down.
/// @orig 0x0033f110 Human_TestStrikes (unknown)
void testStrikes(PhysicsWorld& world, BodyId id, HumanBodyOwners& owners);

} // namespace coney::physics
