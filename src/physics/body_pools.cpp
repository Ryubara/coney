// SPDX-License-Identifier: GPL-3.0-or-later
// The physics world's body pools (docs/research/physics-bodies.md#unit-9): the human body sets, the punch bag's set,
// the world objects', cars' and glass panes' bodies, how each kind is created and freed, and the human list.
#include <cstddef>
#include <cstdint>
#include <limits>
#include <tuple>
#include <variant>
#include <vector>

#include "core/assert.h"
#include "physics/human_body.h"
#include "physics/physics_world.h"
#include "physics/shapes.h"

namespace coney::physics {

namespace {

// A human body after its constructor: the body's flags with the six sweep bits (`0x8000043f`).
constexpr std::uint32_t kHumanConstructedFlags = kBodyFlagDirty | kBodyFlagWriteVelocity | 0x3fU;
// The flags each create writes (docs/research/physics-bodies.md#u9-create).
constexpr std::uint32_t kHumanFlags = 0x8002243fU;
constexpr std::uint32_t kPunchBagFlags = 0x8000243fU;
constexpr std::uint32_t kObjectFlags = 0x80000500U;
constexpr std::uint32_t kCarFlags = 0x80000000U;
constexpr std::uint32_t kPaneFlags = 0x8000007aU;
// The capsule every human set is built with: height 2.0, radius 0.35 (0x40000000, 0x3eb33333).
constexpr float kSetCapsuleHeight = 2.0F;
constexpr float kSetCapsuleRadius = 0.35F;
// The punch bag's one segment: bone 3, radius 0.35, length 1.5 (0x3fc00000), a strike target only.
constexpr int kPunchBagBone = 3;
constexpr float kPunchBagRadius = 0.35F;
constexpr float kPunchBagLength = 1.5F;
// A pane's box depth (0x3f000000), its full size before PhysicsBox_SetSize halves it.
constexpr float kPaneDepth = 0.5F;
// The flags a pane's box gains when taken: enabled and allocated.
constexpr std::uint16_t kPaneBoxTaken = kShapeEnabled | kShapeAllocated;

// The index of a body pool in the tables.
// @coney-only table indexing
std::size_t index(BodyPool pool) { return static_cast<std::size_t>(pool); }

} // namespace

std::size_t bodyPoolCap(BodyPool pool, EngineLimits limits) {
    if (limits == EngineLimits::Original || pool == BodyPool::PunchBag) {
        return kOrigBodyCounts.at(index(pool));
    }
    return std::numeric_limits<std::size_t>::max();
}

BodyId PhysicsWorld::constructHumanBody() {
    const BodyId id = createBody();
    WorldBody& b = body(id);
    b.human = true;
    b.flags = kHumanConstructedFlags;
    return id;
}

void PhysicsWorld::addBoneShape(BodyId id, ShapeId shapeId) {
    WorldBody& b = body(id);
    if (b.boneChain == kNoShape) {
        b.boneChain = shapeId;
        return;
    }
    ShapeId last = b.boneChain;
    while (shape(last).next != kNoShape) {
        last = shape(last).next;
    }
    shape(last).next = shapeId;
}

BodyId PhysicsWorld::buildHumanSet() {
    const BodyId id = constructHumanBody();
    // The capsule, its flags the constructor's 0x1.
    ShapeRecord capsule = makeShape(ShapeType::Capsule);
    setCapsuleSize(capsule, kSetCapsuleHeight, kSetCapsuleRadius);
    body(id).firstShape = addShapeRecord(capsule);
    // Five segments then five spheres, each 0x2 on top of 0x1, and 0x4 for the spine and the head.
    auto addBone = [&](ShapeType type, const BoneShapeDef& def, bool target) {
        ShapeRecord shape = makeShape(type);
        shape.bone = static_cast<std::int8_t>(def.bone);
        shape.offset = def.offset;
        if (type == ShapeType::Segment) {
            std::get<Segment>(shape.posed).radius = def.radius;
            shape.segmentLength = def.length;
        } else {
            std::get<Sphere>(shape.posed).radius = def.radius;
        }
        shape.flags = static_cast<std::uint16_t>(kShapeEnabled | kShapeFlag2 | (target ? kShapeStrikeTarget : 0U));
        addBoneShape(id, addShapeRecord(shape));
    };
    for (std::size_t i = 0; i < kBoneSegments.size(); ++i) {
        addBone(ShapeType::Segment, kBoneSegments.at(i), i == 0);
    }
    for (std::size_t i = 0; i < kBoneSpheres.size(); ++i) {
        addBone(ShapeType::Sphere, kBoneSpheres.at(i), i == 0);
    }
    m_bodyPools.at(index(BodyPool::Human)).push_back(id);
    return id;
}

BodyId PhysicsWorld::buildPunchBagSet() {
    const BodyId id = constructHumanBody();
    ShapeRecord capsule = makeShape(ShapeType::Capsule);
    setCapsuleSize(capsule, kSetCapsuleHeight, kSetCapsuleRadius);
    body(id).firstShape = addShapeRecord(capsule);
    ShapeRecord segment = makeShape(ShapeType::Segment);
    std::get<Segment>(segment.posed).radius = kPunchBagRadius;
    segment.segmentLength = kPunchBagLength;
    segment.bone = kPunchBagBone;
    segment.flags = kShapeStrikeTarget; // set outright: not enabled, a strike target, not a striker
    body(id).boneChain = addShapeRecord(segment);
    m_bodyPools.at(index(BodyPool::PunchBag)).push_back(id);
    return id;
}

BodyId PhysicsWorld::buildPoolBody(BodyPool pool) {
    const BodyId id = createBody();
    if (pool == BodyPool::Pane) {
        // The panes' boxes advance with their bodies: box i goes with body i.
        m_paneBoxes.push_back(addShapeRecord(makeShape(ShapeType::Box)));
    }
    m_bodyPools.at(index(pool)).push_back(id);
    return id;
}

std::size_t PhysicsWorld::findFreeBody(BodyPool pool) {
    std::vector<BodyId>& bodies = m_bodyPools.at(index(pool));
    for (std::size_t i = 0; i < bodies.size(); ++i) {
        if (body(bodies[i]).owner == kNoOwner) {
            return i;
        }
    }
    // Coney's choice, a lifted limit: past the original's count, one more body (bodyPoolCap()).
    if (bodies.size() < bodyPoolCap(pool, m_limits)) {
        if (pool == BodyPool::Human) {
            std::ignore = buildHumanSet();
        } else {
            std::ignore = buildPoolBody(pool);
        }
        return bodies.size() - 1;
    }
    return std::numeric_limits<std::size_t>::max();
}

BodyId PhysicsWorld::createHumanBody(OwnerId owner, std::uint32_t typeMask, const HumanBodyOwners& owners) {
    const std::size_t slot = findFreeBody(BodyPool::Human);
    if (slot == std::numeric_limits<std::size_t>::max()) {
        return kNoBody;
    }
    const BodyId id = m_bodyPools.at(index(BodyPool::Human)).at(slot);
    setOwner(id, owner, typeMask);
    WorldBody& b = body(id);
    b.flags = kHumanFlags;
    b.scale = anim::Vec3{1.0F, 1.0F, 1.0F};
    b.group = 0;
    b.stuck = 0;
    b.strikeBits = 0;
    b.weaponChain = kNoShape; // the chain is dropped, not cleared: the free cleared it
    b.weaponTimer = 0;
    // The capsule on without 0x2, every bone shape off.
    ShapeRecord& capsule = shape(b.firstShape);
    setEnabled(capsule, true);
    capsule.flags = static_cast<std::uint16_t>(capsule.flags & ~kShapeFlag2);
    for (ShapeId s = b.boneChain; s != kNoShape; s = shape(s).next) {
        setEnabled(shape(s), false);
    }
    addHumanBody(id);
    addBody(id);
    applyHumanScale(*this, id, owners);
    return id;
}

BodyId PhysicsWorld::createPunchBagBody(OwnerId owner, std::uint32_t typeMask) {
    // The one set, taken whether or not it is free.
    const BodyId id = m_bodyPools.at(index(BodyPool::PunchBag)).front();
    setOwner(id, owner, typeMask);
    WorldBody& b = body(id);
    b.scale = anim::Vec3{1.0F, 1.0F, 1.0F};
    b.flags = kPunchBagFlags;
    b.group = 0;
    b.stuck = 0;
    b.strikeBits = 0;
    b.weaponChain = kNoShape;
    b.weaponTimer = 0;
    ShapeRecord& capsule = shape(b.firstShape);
    setEnabled(capsule, true);
    capsule.flags = static_cast<std::uint16_t>(capsule.flags & ~kShapeFlag2);
    setEnabled(shape(b.boneChain), false);
    addHumanBody(id);
    addBody(id);
    return id;
}

BodyId PhysicsWorld::createObjectBody(OwnerId owner, std::uint32_t typeMask) {
    const std::size_t slot = findFreeBody(BodyPool::Object);
    if (slot == std::numeric_limits<std::size_t>::max()) {
        return kNoBody;
    }
    const BodyId id = m_bodyPools.at(index(BodyPool::Object)).at(slot);
    setOwner(id, owner, typeMask);
    WorldBody& b = body(id);
    b.flags = kObjectFlags;
    b.group = 0;
    b.stuck = 0;
    b.scale = anim::Vec3{1.0F, 1.0F, 1.0F};
    addBody(id);
    return id;
}

BodyId PhysicsWorld::createCarBody(OwnerId owner, std::uint32_t typeMask) {
    const std::size_t slot = findFreeBody(BodyPool::Car);
    if (slot == std::numeric_limits<std::size_t>::max()) {
        return kNoBody;
    }
    const BodyId id = m_bodyPools.at(index(BodyPool::Car)).at(slot);
    setOwner(id, owner, typeMask);
    WorldBody& b = body(id);
    b.flags = kCarFlags;
    b.group = 0;
    b.stuck = 0;
    addBody(id);
    return id;
}

BodyId PhysicsWorld::createBoxBody(OwnerId owner, std::uint32_t typeMask, float width, float height) {
    const std::size_t slot = findFreeBody(BodyPool::Pane);
    if (slot == std::numeric_limits<std::size_t>::max()) {
        return kNoBody;
    }
    const BodyId id = m_bodyPools.at(index(BodyPool::Pane)).at(slot);
    const ShapeId boxId = m_paneBoxes.at(slot);
    setOwner(id, owner, typeMask);
    WorldBody& b = body(id);
    b.flags = kPaneFlags;
    b.group = 0;
    b.stuck = 0;
    ShapeRecord& box = shape(boxId);
    setBoxSize(box, width, kPaneDepth, height);
    box.flags = static_cast<std::uint16_t>(box.flags | kPaneBoxTaken);
    b.firstShape = boxId;
    addBody(id);
    return id;
}

void PhysicsWorld::freeHumanBody(BodyId id) {
    removeHumanBody(id);
    removeBody(id);
    setOwner(id, kNoOwner, 0);
    setPushSphere(*this, id, false);
}

void PhysicsWorld::freeObjectBody(BodyId id) {
    removeBody(id);
    WorldBody& b = body(id);
    if (b.firstShape != kNoShape) {
        ShapeRecord& s = shape(b.firstShape);
        s.flags = static_cast<std::uint16_t>(s.flags & ~kShapeAllocated);
    }
    stopIgnoring(id, kNoIgnoreSlot);
    setOwner(id, kNoOwner, 0);
    b.firstShape = kNoShape;
}

void PhysicsWorld::freeCarBody(BodyId id) {
    removeBody(id);
    WorldBody& b = body(id);
    // Each shape unchained in turn: its next cleared, its allocated bit cleared.
    while (b.firstShape != kNoShape) {
        ShapeRecord& s = shape(b.firstShape);
        const ShapeId next = s.next;
        s.next = kNoShape;
        s.flags = static_cast<std::uint16_t>(s.flags & ~kShapeAllocated);
        b.firstShape = next;
    }
    stopIgnoring(id, kNoIgnoreSlot);
    setOwner(id, kNoOwner, 0);
}

void PhysicsWorld::freeBoxBody(BodyId id) {
    removeBody(id);
    // No StopIgnoring, and the box stays the body's first shape: the slot's next create sets it again.
    const WorldBody& b = body(id);
    if (b.firstShape != kNoShape) {
        ShapeRecord& s = shape(b.firstShape);
        s.flags = static_cast<std::uint16_t>(s.flags & ~kShapeAllocated);
    }
    setOwner(id, kNoOwner, 0);
}

void PhysicsWorld::addHumanBody(BodyId id) {
    WorldBody& b = body(id);
    if (m_humanHead == kNoBody) {
        m_humanHead = id;
        b.humanPrev = kNoBody;
        b.humanNext = kNoBody;
        return;
    }
    BodyId last = m_humanHead;
    while (body(last).humanNext != kNoBody) {
        last = body(last).humanNext;
    }
    body(last).humanNext = id;
    b.humanPrev = last;
    b.humanNext = kNoBody;
}

void PhysicsWorld::removeHumanBody(BodyId id) {
    stopIgnoring(id, kNoIgnoreSlot);
    WorldBody& b = body(id);
    if (id == m_humanHead) {
        m_humanHead = b.humanNext;
        if (m_humanHead != kNoBody) {
            body(m_humanHead).humanPrev = kNoBody;
        }
    } else {
        if (b.humanPrev != kNoBody) {
            body(b.humanPrev).humanNext = b.humanNext;
        }
        if (b.humanNext != kNoBody) {
            body(b.humanNext).humanPrev = b.humanPrev;
        }
    }
    clearStrikeSpheres(*this, id);
    b.humanPrev = kNoBody;
    b.humanNext = kNoBody;
}

} // namespace coney::physics
