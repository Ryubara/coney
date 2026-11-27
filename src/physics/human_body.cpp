// SPDX-License-Identifier: GPL-3.0-or-later
#include "physics/human_body.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <tuple>
#include <variant>
#include <vector>

#include "animation/anim_math.h"
#include "core/assert.h"
#include "physics/body_sweep.h"
#include "physics/contact.h"
#include "physics/physics_world.h"
#include "physics/shapes.h"

namespace coney::physics {

namespace {

using anim::Vec3;

// Human_GetCapsuleHeight (0x00219890) and Human_GetCapsuleRadius (0x00219860): the capsule per unit of scale.
constexpr float kCapsuleHeightPerScale = 2.0F;
constexpr float kCapsuleRadiusPerScale = 0.35F;
// The broad bound's least radius per unit of scale (0x3f4ccccd).
constexpr float kBroadRadiusPerScale = 0.8F;
// Moved's broad bound: an unscaled cylinder 2.0 m tall and 0.8 m round, centred 1.0 m above the translation.
constexpr float kMovedHeight = 2.0F;
constexpr float kMovedRadius = 0.8F;
constexpr float kMovedCentreRise = 1.0F;
// Body flag 0x4000: the update poses nothing (a climb sets it).
constexpr std::uint32_t kBodyFlagNoUpdate = 0x4000;
// Bone shape flags the update and Moved pose: switched on, or a strike target.
constexpr std::uint16_t kPosedBoneFlags = kShapeEnabled | kShapeStrikeTarget;
// The push sphere's overlap query: bodies with the type mask (0x2000 is never read), humans, at most 32 contacts.
constexpr std::uint32_t kPushOptions = kOverlapBodies | kOverlapTypeMask | 0x2000;
constexpr std::uint32_t kPushMaxContacts = 32;
// The push along the line between the two humans, before the weights (0x3e800000).
constexpr float kPushDistance = 0.25F;
// The type mask bit of a world object (WEAPONTARGET bodies' owners that can fly).
constexpr std::uint32_t kTypeObject = 0x8;
// The owner state bit of an airborne object, never struck.
constexpr std::uint64_t kStateAirborne = 0x4000000;
// The gather's flag mask: bodies that are a weapon target (0x8) or a melee target (0x10).
constexpr std::uint32_t kGatherFlags = 0x18;
// Candidate flags: a weapon target, and a melee or charge target.
constexpr std::uint32_t kBodyWeaponTarget = 0x8;
constexpr std::uint32_t kBodyStrikeTarget = 0x30;
// The ray's and a human target point's rise above the position (1.0).
constexpr float kRayRise = 1.0F;
// The weapon handler's answer that asks for the next shape of a body that is not a human.
constexpr std::uint32_t kAnswerNextShape = 5;
// The level sweeps' normal z limits: walls only (0xbf266666, 0x3f266666).
constexpr float kLevelMinNormalZ = -0.65F;
constexpr float kLevelMaxNormalZ = 0.65F;

// The capsule of a human body (its first shape).
// @coney-only the variant access of Coney's shape record
Capsule& capsuleOf(PhysicsWorld& world, const WorldBody& body) {
    Capsule* capsule = std::get_if<Capsule>(&world.shape(body.firstShape).posed);
    CONEY_ASSERT(capsule != nullptr);
    return *capsule;
}

// Writes the owner and shape words of a strike's contact (`+0x00`, `+0x04`, `+0x40`, `+0x44`).
// @orig 0x0033f110 Human_TestStrikes (unknown)
void markContact(const PhysicsWorld& world, Contact& contact, OwnerId own, ShapeId ownShape, OwnerId other,
                 ShapeId otherShape) {
    contact.halves[0].ownerId = own;
    contact.halves[0].owner = world.ownerPointer(own);
    contact.halves[0].shapeId = ownShape;
    contact.halves[0].shape = ownShape != kNoShape;
    contact.halves[1].ownerId = other;
    contact.halves[1].owner = world.ownerPointer(other);
    contact.halves[1].shapeId = otherShape;
    contact.halves[1].shape = otherShape != kNoShape;
}

// One strike shape's move this update: from its position at the last pose to the one now. The shape is set back to
// where it was so a sweep runs from there; restore() puts it back.
// @orig 0x0033f110 Human_TestStrikes (unknown)
struct ShapeMove {
    Vec3 now;
    Vec3 move;

    // Sets `shape` to its previous position and returns its move.
    // @orig 0x0033f110 Human_TestStrikes (unknown)
    static ShapeMove begin(ShapeRecord& shape) {
        const Vec3 now = shapePosition(shape);
        const Vec3 before = shape.previous;
        setShapePosition(shape, before);
        return ShapeMove{.now = now, .move = Vec3{now.x - before.x, now.y - before.y, now.z - before.z}};
    }
    // Puts `shape` back where it stands now.
    void restore(ShapeRecord& shape) const { setShapePosition(shape, now); }
};

// Steps 1 and 2 of the strike test for one shape: its move swept against the level in a box of its bounds at its
// previous position grown by the move; on a hit the contact's owner and shape words are zeroed. False when its type
// has no mesh sweep.
// @orig 0x0033f110 Human_TestStrikes (unknown)
bool sweepLevel(PhysicsWorld& world, BodyId id, ShapeId shapeId, std::uint32_t options, Contact& contact) {
    ShapeRecord& shape = world.shape(shapeId);
    const PhysicsWorld::MeshSweep& sweep = world.meshSweep(typeOf(shape.posed));
    // Coney's choice: an empty entry is skipped, where the strike spheres' step calls it untested (#379).
    if (!sweep) {
        return false;
    }
    const ShapeMove m = ShapeMove::begin(shape);
    Aabb box = shapeBounds(shape);
    extendByMove(box, m.move);
    const bool hit = sweep(contact, shape, m.move, box, id, options, kLevelMinNormalZ, kLevelMaxNormalZ);
    if (hit) {
        markContact(world, contact, kNoOwner, kNoShape, kNoOwner, kNoShape);
    }
    m.restore(shape);
    return hit;
}

// The sweep of `mover` by `move` against `target` through the world's sweep table; false for an empty entry.
// @orig 0x0033f110 Human_TestStrikes (unknown)
bool sweepPair(PhysicsWorld& world, Contact& contact, ShapeId mover, ShapeId target, Vec3 move) {
    const ShapeRecord& own = world.shape(mover);
    const ShapeRecord& other = world.shape(target);
    const PhysicsWorld::SweepTest& sweep = world.sweepTest(typeOf(own.posed), typeOf(other.posed));
    return sweep && sweep(contact, own.posed, other.posed, move);
}

// Step 4.3: the weapon spheres against one candidate, each from its last pose; the first hit goes to the weapon
// handler and ends the weapon spheres for this candidate (a body that is not a human answering 5 asks for its next
// shape instead).
// @orig 0x0033f110 Human_TestStrikes (unknown)
void weaponStrikes(PhysicsWorld& world, BodyId id, BodyId candidate, OwnerId handler, HumanBodyOwners& owners,
                   Contact& contact) {
    const WorldBody& other = world.body(candidate);
    const bool human = (other.typeMask & kTypeHuman) != 0;
    bool struck = false;
    for (ShapeId sphere = world.body(id).weaponChain; sphere != kNoShape && !struck;) {
        ShapeRecord& shape = world.shape(sphere);
        const ShapeMove m = ShapeMove::begin(shape);
        // A human is met by its target bone shapes, anything else by its whole shape chain.
        for (ShapeId t = human ? other.boneChain : other.firstShape; t != kNoShape; t = world.shape(t).next) {
            if (human && (world.shape(t).flags & kShapeStrikeTarget) == 0) {
                continue;
            }
            if (!sweepPair(world, contact, sphere, t, m.move)) {
                continue;
            }
            markContact(world, contact, handler, sphere, other.owner, t);
            const std::uint32_t answer = owners.weaponContact(handler, contact);
            if (!human && answer == kAnswerNextShape) {
                continue;
            }
            struck = true;
            if (other.owner != kNoOwner) {
                world.ignore(id, other.owner);
            }
            break;
        }
        m.restore(shape);
        sphere = shape.next;
    }
}

// Step 4.4: the switched-on strike shapes against one candidate: its shape chain, a human's target bone shapes, or
// his capsule alone for a capsule strike. The first hit goes to Strike_Contact and ends the strike shapes for this
// candidate.
// @orig 0x0033f110 Human_TestStrikes (unknown)
void boneStrikes(PhysicsWorld& world, BodyId id, BodyId candidate, HumanBodyOwners& owners, Contact& contact) {
    const WorldBody& me = world.body(id);
    const WorldBody& other = world.body(candidate);
    const bool human = (other.typeMask & kTypeHuman) != 0;
    const bool capsuleOnly = human && owners.hitsWithCapsule(me.owner);
    bool struck = false;
    for (ShapeId s = me.boneChain; s != kNoShape && !struck;) {
        ShapeRecord& shape = world.shape(s);
        if ((shape.flags & kShapeEnabled) == 0 || (shape.flags & kShapeFlag2) == 0) {
            s = shape.next;
            continue;
        }
        const ShapeMove m = ShapeMove::begin(shape);
        // Tries `t` and, on a hit, strikes the candidate there.
        auto tryShape = [&](ShapeId t) {
            if (!sweepPair(world, contact, s, t, m.move)) {
                return false;
            }
            markContact(world, contact, me.owner, s, other.owner, t);
            owners.strikeContact(me.owner, contact);
            if (other.owner != kNoOwner) {
                world.ignore(id, other.owner);
            }
            return true;
        };
        if (capsuleOnly) {
            struck = tryShape(other.firstShape);
        } else {
            for (ShapeId t = human ? other.boneChain : other.firstShape; t != kNoShape && !struck;
                 t = world.shape(t).next) {
                if (human && (world.shape(t).flags & kShapeStrikeTarget) == 0) {
                    continue;
                }
                struck = tryShape(t);
            }
        }
        m.restore(shape);
        s = shape.next;
    }
}

// Step 4.2: whether the line from the striker's chest to the candidate is clear: to its first shape's position, a
// human's raised 1.0 m.
// @orig 0x0033f110 Human_TestStrikes (unknown)
bool inSight(const PhysicsWorld& world, Vec3 origin, const WorldBody& candidate, const HumanBodyOwners& owners) {
    const bool human = (candidate.typeMask & kTypeHuman) != 0;
    Vec3 target = shapePosition(world.shape(candidate.firstShape));
    if (human) {
        target.z += kRayRise;
    }
    const Vec3 d{target.x - origin.x, target.y - origin.y, target.z - origin.z};
    const float length = std::sqrt(anim::dot(d, d));
    const StrikeRay ray{.origin = origin, .direction = anim::scale(d, 1.0F / length), .length = length};
    return !owners.rayBlocked(ray, human ? kStrikeRayHuman : kStrikeRayObject);
}

} // namespace

void applyHumanScale(PhysicsWorld& world, BodyId id, const HumanBodyOwners& owners) {
    WorldBody& body = world.body(id);
    const OwnerId owner = body.owner;
    Capsule& capsule = capsuleOf(world, body);
    capsule.height = kCapsuleHeightPerScale * owners.scale(owner);
    capsule.radius = kCapsuleRadiusPerScale * owners.scale(owner);
    // The bone chain in the constructor's order, with no end test: five segments, then five spheres.
    ShapeId s = body.boneChain;
    for (const BoneShapeDef& def : kBoneSegments) {
        ShapeRecord& shape = world.shape(s);
        shape.offset = anim::scale(def.offset, owners.scale(owner));
        std::get<Segment>(shape.posed).radius = def.radius * owners.scale(owner);
        shape.segmentLength = def.length * owners.scale(owner);
        s = shape.next;
    }
    for (const BoneShapeDef& def : kBoneSpheres) {
        ShapeRecord& shape = world.shape(s);
        shape.offset = anim::scale(def.offset, owners.scale(owner));
        std::get<Sphere>(shape.posed).radius = def.radius * owners.scale(owner);
        s = shape.next;
    }
    // Rebuilt at once: dirty, then the human body's update.
    body.flags |= kBodyFlagDirty;
    updateHumanBody(world, id, owners);
}

void updateHumanBody(PhysicsWorld& world, BodyId id, const HumanBodyOwners& owners) {
    WorldBody& body = world.body(id);
    std::uint32_t flags = body.flags;
    if ((flags & kBodyFlagNoUpdate) != 0) {
        return;
    }
    const ShapeOwner& me = owners.shapeOwner(body.owner);
    // 1. Dirty: the capsule alone is posed and bounds the body, which is re-sorted.
    if ((flags & kBodyFlagDirty) != 0) {
        poseShape(world.shape(body.firstShape), me);
        world.computeBounds(body.box, body.firstShape);
        world.reinsert(id);
        body.flags &= ~kBodyFlagDirty;
    }
    // 2. A weapon timer poses the weapon spheres with the held object; it or a strike bit asks for the broad bound.
    if (body.weaponTimer != 0) {
        flags |= kBodyFlagDirty;
        const OwnerId held = owners.heldObject(body.owner);
        const ShapeOwner& poser = owners.shapeOwner(held != kNoOwner ? held : body.owner);
        for (ShapeId s = body.weaponChain; s != kNoShape; s = world.shape(s).next) {
            poseShape(world.shape(s), poser);
        }
    } else if (body.strikeBits != 0) {
        flags |= kBodyFlagDirty;
    }
    // 3. The broad bound: an upright cylinder on the capsule, as wide as the strike can reach.
    if ((flags & kBodyFlagDirty) != 0) {
        const float reach = owners.reach(body.owner);
        const Capsule& capsule = capsuleOf(world, body);
        Vec3 centre = capsule.base;
        centre.z += capsule.height * 0.5F;
        float radius = owners.scale(body.owner) * kBroadRadiusPerScale;
        if (body.weaponChain == kNoShape) {
            if (radius < reach) {
                radius = reach;
            }
        } else {
            // With a weapon the first sphere's horizontal distance from the capsule's base plus its radius; the reach
            // is not used.
            const ShapeRecord& first = world.shape(body.weaponChain);
            const Vec3 at = shapePosition(first);
            const Vec3 d{at.x - capsule.base.x, at.y - capsule.base.y, 0.0F};
            const float far = std::sqrt(anim::dot(d, d)) + std::get<Sphere>(first.posed).radius;
            radius = far < radius ? radius : far;
        }
        body.broadBound = aabbFromUprightCylinder(centre, capsule.height, radius);
    }
    // 4. The switched-on and target bone shapes, then the push sphere at the human's origin.
    for (ShapeId s = body.boneChain; s != kNoShape; s = world.shape(s).next) {
        if ((world.shape(s).flags & kPosedBoneFlags) != 0) {
            poseShape(world.shape(s), me);
        }
    }
    if (body.pushSphere != kNoShape) {
        poseShape(world.shape(body.pushSphere), me);
    }
}

void humanBodyMoved(PhysicsWorld& world, BodyId id, const anim::Mat34& m) {
    world.bodyMoved(id, m);
    WorldBody& body = world.body(id);
    const Vec3 centre{m.t.x, m.t.y, m.t.z + kMovedCentreRise};
    body.broadBound = aabbFromUprightCylinder(centre, kMovedHeight, kMovedRadius);
    for (ShapeId s = body.boneChain; s != kNoShape; s = world.shape(s).next) {
        if ((world.shape(s).flags & kPosedBoneFlags) != 0) {
            setShapeMatrix(world.shape(s), m);
        }
    }
}

void setStrikeShapeEnabled(PhysicsWorld& world, BodyId id, int bone, bool on) {
    // Every event empties the struck list first, even one that changes nothing.
    world.stopIgnoring(id, kNoIgnoreSlot);
    WorldBody& body = world.body(id);
    if (bone < 0) {
        if (!on && body.strikeBits != 0) {
            body.strikeBits = 0;
            for (ShapeId s = body.boneChain; s != kNoShape; s = world.shape(s).next) {
                setEnabled(world.shape(s), false);
            }
        }
        return;
    }
    // The hardware shift reads the low five bits, so bones 0 and 32 share bit 30.
    const std::uint32_t bit = 1U << (static_cast<std::uint32_t>(bone - 2) & 31U);
    if (on == ((body.strikeBits & bit) != 0)) {
        return;
    }
    body.strikeBits = on ? (body.strikeBits | bit) : (body.strikeBits & ~bit);
    for (ShapeId s = body.boneChain; s != kNoShape; s = world.shape(s).next) {
        if (world.shape(s).bone == bone) {
            setEnabled(world.shape(s), on);
            return;
        }
    }
}

void addStrikeSphere(PhysicsWorld& world, BodyId id, ShapeId sphere) {
    WorldBody& body = world.body(id);
    world.shape(sphere).next = body.weaponChain;
    body.weaponChain = sphere;
}

void clearStrikeSpheres(PhysicsWorld& world, BodyId id) {
    WorldBody& body = world.body(id);
    while (body.weaponChain != kNoShape) {
        ShapeRecord& head = world.shape(body.weaponChain);
        const ShapeId next = head.next;
        setEnabled(head, false);
        head.next = kNoShape;
        body.weaponChain = next;
    }
}

void setPushSphere(PhysicsWorld& world, BodyId id, bool on) {
    WorldBody& body = world.body(id);
    if (!on) {
        if (body.pushSphere != kNoShape) {
            world.freeShape(body.pushSphere);
        }
        body.pushSphere = kNoShape;
    } else if (body.pushSphere == kNoShape) {
        body.pushSphere = world.allocBigSphere();
    }
}

void pushAwayHumans(PhysicsWorld& world, BodyId id, const HumanBodyOwners& owners) {
    const OwnerId owner = world.body(id).owner;
    const OwnerId target = owners.target(owner);
    if (world.body(id).pushSphere == kNoShape) {
        return;
    }
    std::vector<Contact> contacts;
    const OverlapQuery query{.body = id,
                             .shape = world.body(id).pushSphere,
                             .box = {},
                             .options = kPushOptions,
                             .typeMask = kTypeHuman,
                             .max = kPushMaxContacts};
    // From the last contact to the first.
    for (std::size_t n = world.overlapShape(contacts, query); n != 0;) {
        --n;
        const OwnerId other = contacts.at(n).halves[1].ownerId;
        if (other == target || owners.isPlayer(other)) {
            continue;
        }
        WorldBody& pushed = world.body(owners.bodyOf(other));
        if ((pushed.flags & kBodyNotPushable) != 0) {
            continue;
        }
        // Along the 3-D line from this human to the other. Coney's choice: two humans at one point get no push, as
        // the PS2's rsqrt of 0 (the largest float) times the zero line gives, where IEEE arithmetic would add NaNs.
        const Vec3 from = owners.position(owner);
        const Vec3 to = owners.position(other);
        const Vec3 d{to.x - from.x, to.y - from.y, to.z - from.z};
        const float lengthSquared = anim::dot(d, d);
        if (lengthSquared == 0.0F) {
            continue;
        }
        Vec3 push = anim::scale(anim::scale(d, 1.0F / std::sqrt(lengthSquared)), kPushDistance);
        const float share = owners.attribute(owner, kAttributeWeight) /
                            (owners.attribute(other, kAttributeWeight) * owners.attribute(other, kAttributePushWeight));
        push = anim::scale(push, share);
        pushed.pushOut = anim::add(pushed.pushOut, push);
    }
}

void testStrikes(PhysicsWorld& world, BodyId id, HumanBodyOwners& owners) {
    // 0. Nothing without a weapon timer or a strike bit.
    if (world.body(id).weaponTimer == 0 && world.body(id).strikeBits == 0) {
        return;
    }
    const OwnerId owner = world.body(id).owner;
    const OwnerId held = owners.heldObject(owner);
    const OwnerId handler = held != kNoOwner ? held : owner;
    // One contact for every try, never cleared: what a sweep does not write stays from the try before.
    Contact contact;
    // 1. The weapon spheres against the level, while an object is held; the first hit ends them.
    if (held != kNoOwner) {
        for (ShapeId s = world.body(id).weaponChain; s != kNoShape; s = world.shape(s).next) {
            if (sweepLevel(world, id, s, kWeaponLevelOptions, contact)) {
                std::ignore = owners.weaponContact(handler, contact);
                break;
            }
        }
    }
    // 2. The switched-on strike spheres against the level; the first hit ends them.
    if (world.body(id).strikeBits != 0) {
        for (ShapeId s = world.body(id).boneChain; s != kNoShape; s = world.shape(s).next) {
            const ShapeRecord& shape = world.shape(s);
            if ((shape.flags & kShapeEnabled) == 0 || (shape.flags & kShapeFlag2) == 0 ||
                typeOf(shape.posed) != ShapeType::Sphere) {
                continue;
            }
            if (sweepLevel(world, id, s, kStrikeLevelOptions, contact)) {
                owners.strikeContact(owner, contact);
                break;
            }
        }
    }
    // 3. The gather: the bodies the broad bound meets that are a weapon or melee target, its own owner's left out.
    std::vector<BodyId> candidates;
    std::ignore = world.queryBox(candidates, world.body(id).broadBound, kNoBody, owner, 0xffffffffU, kGatherFlags);
    Vec3 origin = owners.position(owner);
    origin.z += kRayRise;
    // 4. Each candidate in list order; a hit does not end the test.
    for (const BodyId c : candidates) {
        const WorldBody& other = world.body(c);
        const WorldBody& me = world.body(id);
        if (other.owner == kNoOwner || (me.group != 0 && me.group == other.group)) {
            continue;
        }
        if (world.isIgnoring(id, other.owner, owners.grabPartner(owner))) {
            continue;
        }
        if ((other.typeMask & kTypeObject) != 0 && (owners.state(other.owner) & kStateAirborne) != 0) {
            continue;
        }
        if (!inSight(world, origin, other, owners)) {
            continue;
        }
        if ((other.flags & kBodyWeaponTarget) != 0) {
            weaponStrikes(world, id, c, handler, owners, contact);
        }
        if (me.strikeBits != 0 && (other.flags & kBodyStrikeTarget) != 0 && owners.strikes(owner, other.owner)) {
            boneStrikes(world, id, c, owners, contact);
        }
    }
    // 5. The weapon timer runs down; at 0 the weapon spheres go. The body is posed again next update.
    WorldBody& body = world.body(id);
    if (body.weaponTimer != 0) {
        --body.weaponTimer;
        if (body.weaponTimer == 0) {
            clearStrikeSpheres(world, id);
        }
        body.flags |= kBodyFlagDirty;
    }
}

} // namespace coney::physics
