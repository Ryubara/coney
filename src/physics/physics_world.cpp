// SPDX-License-Identifier: GPL-3.0-or-later
#include "physics/physics_world.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <optional>
#include <tuple>
#include <variant>
#include <vector>

#include "animation/anim_math.h"
#include "core/assert.h"
#include "physics/contact.h"
#include "physics/overlaps.h"
#include "physics/shapes.h"

namespace coney::physics {

namespace {

using anim::Vec3;

// The big spheres' radius once taken (0x40000000).
constexpr float kBigSphereRadius = 2.0F;
// The flags a pool allocation sets: enabled, 0x2 and allocated.
constexpr std::uint16_t kPoolTaken = kShapeEnabled | kShapeFlag2 | kShapeAllocated;
// The step ticks between two bound refreshes (0x78).
constexpr std::uint16_t kRefreshTicks = 120;

// Whether `node`'s bound meets `box` in y and z, touching included: the box queries' last test.
// @orig 0x0033feb0 IPhysics_QueryBox (unknown)
// @orig 0x00340120 IPhysics_QueryBoxFiltered (unknown)
bool meetsYZ(const WorldBody& node, const Aabb& box) {
    return node.box.min.y <= box.max.y && box.min.y <= node.box.max.y && node.box.min.z <= box.max.z &&
           box.min.z <= node.box.max.z;
}

// Grows `box` by its own copy moved by `d`, per axis the smallest and largest of the three.
// @orig 0x00341840 AABB_ExtendByMove (unknown)
void growByMove(Aabb& box, Vec3 d) {
    const Vec3 a{box.min.x + d.x, box.min.y + d.y, box.min.z + d.z};
    const Vec3 b{box.max.x + d.x, box.max.y + d.y, box.max.z + d.z};
    box.min = Vec3{std::min({box.min.x, a.x, b.x}), std::min({box.min.y, a.y, b.y}), std::min({box.min.z, a.z, b.z})};
    box.max = Vec3{std::max({box.max.x, a.x, b.x}), std::max({box.max.y, a.y, b.y}), std::max({box.max.z, a.z, b.z})};
}

// One entry of the overlap table: the pair test `Test` for an own shape `A` against another shape `B`.
// @coney-only the overlap table's entries, filled by the world's constructor (Unit 15, #379)
template <typename A, typename B, bool (*Test)(Contact&, const A&, const B&)>
bool pairTest(Contact& contact, const Shape& own, const Shape& other) {
    return Test(contact, std::get<A>(own), std::get<B>(other));
}

// The overlap table as the original's constructor fills it (docs/research/physics.md#unit-4): a segment and a box
// have no entry either way round.
// @coney-only the overlap table as the IPhysics constructor fills it (Unit 15, #379)
std::array<std::array<PhysicsWorld::PairTest, 5>, 5> defaultOverlaps() {
    std::array<std::array<PhysicsWorld::PairTest, 5>, 5> table{};
    auto at = [&table](ShapeType own, ShapeType other) -> PhysicsWorld::PairTest& {
        return table.at(static_cast<std::size_t>(own)).at(static_cast<std::size_t>(other));
    };
    at(ShapeType::Box, ShapeType::Box) = pairTest<Box, Box, overlapBoxBox>;
    at(ShapeType::Box, ShapeType::Sphere) = pairTest<Box, Sphere, overlapBoxSphere>;
    at(ShapeType::Box, ShapeType::Capsule) = pairTest<Box, Capsule, overlapBoxCapsule>;
    at(ShapeType::Sphere, ShapeType::Box) = pairTest<Sphere, Box, overlapSphereBox>;
    at(ShapeType::Sphere, ShapeType::Sphere) = pairTest<Sphere, Sphere, overlapSphereSphere>;
    at(ShapeType::Sphere, ShapeType::Capsule) = pairTest<Sphere, Capsule, overlapSphereCapsule>;
    at(ShapeType::Sphere, ShapeType::Segment) = pairTest<Sphere, Segment, overlapSphereSegment>;
    at(ShapeType::Capsule, ShapeType::Box) = pairTest<Capsule, Box, overlapCapsuleBox>;
    at(ShapeType::Capsule, ShapeType::Sphere) = pairTest<Capsule, Sphere, overlapCapsuleSphere>;
    at(ShapeType::Capsule, ShapeType::Capsule) = pairTest<Capsule, Capsule, overlapCapsuleCapsule>;
    at(ShapeType::Capsule, ShapeType::Segment) = pairTest<Capsule, Segment, overlapCapsuleSegment>;
    at(ShapeType::Segment, ShapeType::Sphere) = pairTest<Segment, Sphere, overlapSegmentSphere>;
    at(ShapeType::Segment, ShapeType::Capsule) = pairTest<Segment, Capsule, overlapSegmentCapsule>;
    at(ShapeType::Segment, ShapeType::Segment) = pairTest<Segment, Segment, overlapSegmentSegment>;
    return table;
}

// The index of a ShapeType in the tables.
// @coney-only table indexing
std::size_t index(ShapeType type) { return static_cast<std::size_t>(type); }

} // namespace

std::size_t shapePoolCap(ShapePool pool, EngineLimits limits) {
    if (limits == EngineLimits::Original) {
        return kOrigPoolCounts.at(static_cast<std::size_t>(pool));
    }
    return std::numeric_limits<std::size_t>::max();
}

std::size_t ignoreSlotCap(EngineLimits limits) {
    return limits == EngineLimits::Original ? kOrigIgnoreSlots : std::numeric_limits<std::size_t>::max();
}

PhysicsWorld::PhysicsWorld(EngineLimits limits) : m_limits(limits), m_overlaps(defaultOverlaps()) {
    // The constructor's tables: every pool shape built once, and the ignore slots zeroed (docs/research/
    // physics-bodies.md#u8-pools).
    for (std::size_t pool = 0; pool < kOrigPoolCounts.size(); ++pool) {
        for (std::size_t i = 0; i < kOrigPoolCounts.at(pool); ++i) {
            std::ignore = buildPoolShape(static_cast<ShapePool>(pool));
        }
    }
    m_ignoreSlots.resize(kOrigIgnoreSlots);
}

ShapeId PhysicsWorld::addShapeRecord(const ShapeRecord& record) {
    m_shapes.push_back(record);
    return static_cast<ShapeId>(m_shapes.size() - 1);
}

BodyId PhysicsWorld::createBody() {
    m_bodies.emplace_back();
    return static_cast<BodyId>(m_bodies.size() - 1);
}

BodyId PhysicsWorld::createBodyForOwner(BodyOwner* owner, std::uint32_t typeMask) {
    // Coney's choice: the scale +0x60 is 1.0 here too, where the original leaves the stack's word (nothing reads it).
    const BodyId id = createBody();
    WorldBody& b = body(id);
    b.owner = owner;
    if (owner != nullptr) {
        b.typeMask = typeMask;
    }
    return id;
}

void PhysicsWorld::setOwner(BodyId id, BodyOwner* owner, std::uint32_t typeMask) {
    WorldBody& b = body(id);
    b.owner = owner;
    if (owner != nullptr) {
        b.typeMask = typeMask;
    }
}

void PhysicsWorld::addShape(BodyId id, ShapeId shapeId) {
    WorldBody& b = body(id);
    if (b.firstShape == kNoShape) {
        b.firstShape = shapeId;
        return;
    }
    ShapeId last = b.firstShape;
    while (shape(last).next != kNoShape) {
        last = shape(last).next;
    }
    shape(last).next = shapeId;
}

void PhysicsWorld::computeBounds(Aabb& box, ShapeId shapeId) const { box = shapeBounds(shape(shapeId)); }

void PhysicsWorld::addShapeBounds(Aabb& box, ShapeId shapeId) const {
    const Aabb more = shapeBounds(shape(shapeId));
    box.min = Vec3{std::min(box.min.x, more.min.x), std::min(box.min.y, more.min.y), std::min(box.min.z, more.min.z)};
    box.max = Vec3{std::max(box.max.x, more.max.x), std::max(box.max.y, more.max.y), std::max(box.max.z, more.max.z)};
}

std::optional<UpdateSweep> PhysicsWorld::updateBody(BodyId id, const ShapeOwner& owner) {
    WorldBody& b = body(id);
    if ((b.flags & (kBodyFlagDirty | kBodyFlagAlwaysUpdate)) == 0) {
        return std::nullopt;
    }
    // 1. Pose every shape, enabled or not, and bound them all; a body always has a first shape.
    const ShapeId first = b.firstShape;
    CONEY_ASSERT(first != kNoShape);
    poseShape(shape(first), owner);
    computeBounds(b.box, first);
    for (ShapeId s = shape(first).next; s != kNoShape; s = shape(s).next) {
        poseShape(shape(s), owner);
        addShapeBounds(b.box, s);
    }
    // 2. Re-sort, and clear the dirty flag.
    reinsert(id);
    b.flags &= ~kBodyFlagDirty;
    if ((b.flags & (kBodyFlagSweepBodies | kBodyFlagAlwaysUpdate)) != (kBodyFlagSweepBodies | kBodyFlagAlwaysUpdate)) {
        return std::nullopt;
    }
    // 3. The update sweep's box: the new bound grown forward by the first shape's move. The sweep against the bodies
    // and the resolution of its contacts are Unit 11's (#377): the caller is handed the box and the move.
    const ShapeRecord& swept = shape(first);
    const Vec3 at = shapePosition(swept);
    const Vec3 move{at.x - swept.previous.x, at.y - swept.previous.y, at.z - swept.previous.z};
    Aabb box = b.box;
    growByMove(box, move);
    return UpdateSweep{.box = box, .move = move, .shape = first};
}

void PhysicsWorld::bodyMoved(BodyId id, const anim::Mat34& m) {
    WorldBody& b = body(id);
    setShapeMatrix(shape(b.firstShape), m);
    computeBounds(b.box, b.firstShape);
    reinsert(id);
}

void PhysicsWorld::insert(BodyId id, BodyId hint) {
    WorldBody& b = body(id);
    // 1. Every insert grows the bound to the body's width.
    const float width = b.box.max.x - b.box.min.x;
    if (m_bound < width) {
        m_bound = width;
    }
    // 2. The first body of an empty list.
    if (m_head == kNoBody) {
        m_head = id;
        b.prev = kNoBody;
        b.next = kNoBody;
        return;
    }
    const float x = b.box.min.x;
    const BodyId start = hint != kNoBody ? hint : m_head;
    if (x < body(start).box.min.x) {
        // 3. Backward: after the first body with a smaller min x, so before every equal one passed.
        BodyId visited = start;
        for (BodyId n = body(start).prev; n != kNoBody; n = body(n).prev) {
            WorldBody& node = body(n);
            if (node.box.min.x < x) {
                if (node.next != kNoBody) {
                    body(node.next).prev = id;
                }
                b.prev = n;
                b.next = node.next;
                node.next = id;
                return;
            }
            visited = n;
        }
        // Off the front: the new head, before the last body visited.
        b.next = visited;
        body(visited).prev = id;
        b.prev = kNoBody;
        m_head = id;
        return;
    }
    // 4. Forward: before the first body with a greater min x, so after every equal one passed and after `start`.
    BodyId last = start;
    for (BodyId n = body(start).next; n != kNoBody; n = body(n).next) {
        WorldBody& node = body(n);
        if (x < node.box.min.x) {
            if (n != m_head) {
                if (node.prev != kNoBody) {
                    body(node.prev).next = id;
                }
                b.next = n;
                b.prev = node.prev;
                node.prev = id;
                return;
            }
            m_head = id;
            b.next = n;
            b.prev = kNoBody;
            node.prev = id;
            return;
        }
        last = n;
    }
    // Off the end: the new tail.
    body(last).next = id;
    b.prev = last;
    b.next = kNoBody;
}

BodyId PhysicsWorld::remove(BodyId id) {
    WorldBody& b = body(id);
    const BodyId oldPrev = b.prev;
    if (id == m_head) {
        m_head = b.next;
        if (m_head != kNoBody) {
            body(m_head).prev = kNoBody;
        }
    } else {
        if (b.prev != kNoBody) {
            body(b.prev).next = b.next;
        }
        if (b.next != kNoBody) {
            body(b.next).prev = b.prev;
        }
    }
    b.prev = kNoBody;
    b.next = kNoBody;
    return oldPrev;
}

void PhysicsWorld::reinsert(BodyId id) { insert(id, remove(id)); }

void PhysicsWorld::addBody(BodyId id) {
    m_vector.push_back(id);
    body(id).vectorIndex = static_cast<std::int32_t>(m_vector.size() - 1);
    insert(id, kNoBody);
}

void PhysicsWorld::removeBody(BodyId id) {
    forgetIgnoredOwner(id);
    WorldBody& b = body(id);
    // Swap-remove: the last body takes the removed one's index, which the removed body keeps.
    CONEY_ASSERT(!m_vector.empty() && b.vectorIndex >= 0);
    if (m_vector.size() >= 2) {
        const BodyId last = m_vector.back();
        body(last).vectorIndex = b.vectorIndex;
        m_vector.at(static_cast<std::size_t>(b.vectorIndex)) = last;
    }
    m_vector.pop_back();
    std::ignore = remove(id);
}

template <typename Keep>
std::size_t PhysicsWorld::walkBox(std::vector<BodyId>& out, const Aabb& box, BodyId start, const Keep& keeps) const {
    // Coney's choice: `out` grows; past 512 the original's callers' lists overrun their stack (undefined behaviour).
    std::size_t count = 0;
    // A candidate the x tests let through: kept when `keeps` accepts it and it meets the box in y and z. The keep test
    // reads the node itself, which for a listed body is the body the original fetches through the vector.
    auto consider = [&](BodyId n, const WorldBody& node) {
        if (keeps(node) && meetsYZ(node, box)) {
            out.push_back(n);
            ++count;
        }
    };
    BodyId forward = m_head;
    if (start != kNoBody) {
        // 1. Backward from `start` itself: no upper x test, and a stop once the box starts further right of a body's
        // min x than the bound.
        for (BodyId n = start; n != kNoBody; n = body(n).prev) {
            const WorldBody& node = body(n);
            if (node.box.max.x < box.min.x) {
                continue;
            }
            if (box.min.x - node.box.min.x > m_bound) {
                break;
            }
            consider(n, node);
        }
        forward = body(start).next;
    }
    // 2. Forward until a body starts beyond the box.
    for (BodyId n = forward; n != kNoBody; n = body(n).next) {
        const WorldBody& node = body(n);
        if (box.max.x < node.box.min.x) {
            break;
        }
        if (node.box.max.x < box.min.x) {
            continue;
        }
        consider(n, node);
    }
    return count;
}

std::size_t PhysicsWorld::queryBox(std::vector<BodyId>& out, const Aabb& box, BodyId start, const BodyOwner* ignored,
                                   std::uint32_t typeMask, std::uint32_t flagMask) const {
    return walkBox(out, box, start, [&](const WorldBody& b) {
        return b.owner != ignored && (b.typeMask & typeMask) != 0 && (flagMask == 0 || (b.flags & flagMask) != 0);
    });
}

std::size_t PhysicsWorld::queryBoxFiltered(std::vector<BodyId>& out, BodyId query, const Aabb& box, BodyId start,
                                           const BodyFilter& filter) const {
    const BodyOwner* asker = body(query).owner;
    // The state mask is loaded as a 32-bit word and sign-extended, so its bit 31 also tests the high word.
    const auto excludedState =
        static_cast<std::uint64_t>(static_cast<std::int64_t>(static_cast<std::int32_t>(filter.excludedState)));
    return walkBox(out, box, start, [&](const WorldBody& b) {
        if (b.owner == filter.ignored || (b.typeMask & filter.typeMask) == 0) {
            return false;
        }
        if (filter.flagMask != 0 && (b.flags & filter.flagMask) == 0) {
            return false;
        }
        if ((b.flags & filter.excludedFlags) != 0) {
            return false;
        }
        // Asked of every candidate this far, whatever the mask. Coney's choice: an ownerless body, or no answer to
        // ask, has state 0 where the original calls through the null owner.
        const std::uint64_t state = (filter.stateOf && b.owner != nullptr) ? filter.stateOf(b.owner) : 0;
        if ((state & excludedState) != 0) {
            return false;
        }
        return !filter.keep || filter.keep(asker, b.owner);
    });
}

std::size_t PhysicsWorld::overlapShape(std::vector<Contact>& out, const OverlapQuery& q) {
    const ShapeRecord& own = shape(q.shape);
    // 1. The box: the given one, or the querying shape's bound in a node that starts as the zero box.
    Aabb node{};
    const Aabb* box = &node;
    if ((q.options & kOverlapGivenBox) != 0) {
        box = &q.box;
    } else {
        computeBounds(node, q.shape);
    }
    std::size_t found = 0;
    if ((q.options & kOverlapBodies) != 0) {
        // 2. The candidates, from the querying body, its owner dropped.
        const WorldBody& me = body(q.body);
        const std::uint32_t typeMask = (q.options & kOverlapTypeMask) != 0 ? q.typeMask : 0xffffffffU;
        std::vector<BodyId> candidates;
        std::ignore = queryBox(candidates, *box, q.body, me.owner, typeMask, 0);
        // 3. Each candidate's shapes through the overlap table, while the query asks for any contact. A slot a test
        // missed in is reused with what that test wrote, as the original's stack slot is.
        std::deque<Contact> slots;
        std::vector<Contact*> sorted;
        for (const BodyId c : candidates) {
            if (q.max == 0) {
                break;
            }
            const WorldBody& other = body(c);
            if ((q.options & kOverlapSkipGroup) != 0 && other.group != 0 && me.group != 0 && other.group == me.group) {
                continue;
            }
            // Coney's choice: with no answer to ask, the querying owner accepts every candidate.
            if ((q.options & kOverlapAskOwner) != 0 && q.accepts && !q.accepts(me.owner, other.owner)) {
                continue;
            }
            // Stand-in: every body answers its own chain; a human body's capsule chain is Unit 10's (#376).
            for (ShapeId s = other.firstShape; s != kNoShape; s = shape(s).next) {
                const ShapeRecord& theirs = shape(s);
                if ((q.options & kOverlapEnabledOnly) != 0 && (theirs.flags & kShapeEnabled) == 0) {
                    continue;
                }
                const PairTest& test = m_overlaps.at(index(typeOf(own.posed))).at(index(typeOf(theirs.posed)));
                if (!test) {
                    continue;
                }
                if (slots.size() <= sorted.size()) {
                    slots.emplace_back();
                }
                Contact& slot = slots.at(sorted.size());
                if (test(slot, own.posed, theirs.posed)) {
                    slot.triangle.reset();
                    insertSorted(slot, sorted, me.owner, other.owner, true);
                }
            }
        }
        // 4. The nearest `max`, in sorted order.
        found = std::min<std::size_t>(sorted.size(), q.max);
        for (std::size_t i = 0; i < found; ++i) {
            out.push_back(*sorted.at(i));
        }
    }
    // 5. The level, with the room left; it is handed the node, which is still the zero box under kOverlapGivenBox.
    const MeshOverlap& mesh = m_meshOverlaps.at(index(typeOf(own.posed)));
    if (found < q.max && (q.options & kOverlapMesh) != 0 && mesh) {
        found += mesh(q.meshLow, q.meshHigh, out, q.max - found, node, q.body);
    }
    return found;
}

void PhysicsWorld::refreshBound() {
    ++m_ticks;
    if (m_ticks < kRefreshTicks) {
        return;
    }
    m_ticks = 0;
    m_bound = 0.0F;
    for (BodyId n = m_head; n != kNoBody; n = body(n).next) {
        const float width = body(n).box.max.x - body(n).box.min.x;
        if (m_bound < width) {
            m_bound = width;
        }
    }
    // The rest of the step (the 64 settle slots and their re-poses) is Unit 15's (#379).
}

IgnoreSlotId PhysicsWorld::findFreeIgnoreSlot() {
    for (std::size_t i = 0; i < m_ignoreSlots.size(); ++i) {
        if (m_ignoreSlots[i].owner == nullptr) {
            return static_cast<IgnoreSlotId>(i);
        }
    }
    // Past the original's sixty, Coney adds a slot (ignoreSlotCap()).
    if (m_ignoreSlots.size() < ignoreSlotCap(m_limits)) {
        m_ignoreSlots.emplace_back();
        return static_cast<IgnoreSlotId>(m_ignoreSlots.size() - 1);
    }
    return kNoIgnoreSlot;
}

bool PhysicsWorld::isIgnoring(BodyId id, const BodyOwner* owner, const BodyOwner* grabPartner) const {
    return body(id).human ? humanBodyIsIgnoring(id, owner, grabPartner) : bodyIsIgnoring(id, owner);
}

void PhysicsWorld::ignore(BodyId id, const BodyOwner* owner) {
    if (body(id).human) {
        humanBodyIgnore(id, owner);
    } else {
        bodyIgnore(id, owner);
    }
}

void PhysicsWorld::stopIgnoring(BodyId id, IgnoreSlotId slot) {
    if (body(id).human) {
        humanBodyStopIgnoring(id, slot);
    } else {
        bodyStopIgnoring(id, slot);
    }
}

void PhysicsWorld::forgetIgnoredOwner(BodyId removed) {
    const BodyOwner* owner = body(removed).owner;
    for (std::size_t i = 0; i < m_ignoreSlots.size(); ++i) {
        const IgnoreSlot& slot = m_ignoreSlots[i];
        // Coney's choice: a free slot (no body) is skipped, where an ownerless body's removal would make the original
        // call through a free slot's null body.
        if (slot.owner == owner && slot.body != kNoBody) {
            stopIgnoring(slot.body, static_cast<IgnoreSlotId>(i));
        }
    }
}

void PhysicsWorld::freeShape(ShapeId id) {
    ShapeRecord& s = shape(id);
    s.flags = static_cast<std::uint16_t>(s.flags & ~kShapeAllocated);
}

ShapeId PhysicsWorld::buildPoolShape(ShapePool pool) {
    const bool sphere =
        pool == ShapePool::ObjectSphere || pool == ShapePool::BigSphere || pool == ShapePool::StrikeSphere;
    ShapeRecord record = makeShape(sphere ? ShapeType::Sphere : ShapeType::Box);
    if (pool == ShapePool::StrikeSphere) {
        // The strike spheres are switched off and given 0x2, so they start free.
        record.flags = kShapeFlag2;
    }
    const ShapeId id = addShapeRecord(record);
    m_pools.at(static_cast<std::size_t>(pool)).push_back(id);
    return id;
}

ShapeId PhysicsWorld::takeFromPool(ShapePool pool) {
    const bool strike = pool == ShapePool::StrikeSphere;
    // A strike sphere is free while switched off; the other pools' shapes while not allocated.
    auto isFree = [&](ShapeId id) { return (shape(id).flags & (strike ? kShapeEnabled : kShapeAllocated)) == 0; };
    ShapeId taken = kNoShape;
    for (const ShapeId id : m_pools.at(static_cast<std::size_t>(pool))) {
        if (isFree(id)) {
            taken = id;
            break;
        }
    }
    // Past the original's count, Coney builds one more shape (shapePoolCap()).
    if (taken == kNoShape && poolSize(pool) < shapePoolCap(pool, m_limits)) {
        taken = buildPoolShape(pool);
    }
    if (taken == kNoShape || strike) {
        return taken;
    }
    ShapeRecord& s = shape(taken);
    if (pool == ShapePool::BigSphere) {
        std::get<Sphere>(s.posed).radius = kBigSphereRadius;
    }
    s.flags = static_cast<std::uint16_t>(s.flags | kPoolTaken);
    return taken;
}

bool PhysicsWorld::bodyIsIgnoring(BodyId id, const BodyOwner* owner) const {
    if (owner == nullptr) {
        return false;
    }
    for (IgnoreSlotId s = body(id).ignoreChain; s != kNoIgnoreSlot; s = m_ignoreSlots.at(s).next) {
        if (m_ignoreSlots.at(s).owner == owner) {
            return true;
        }
    }
    return false;
}

void PhysicsWorld::bodyIgnore(BodyId id, const BodyOwner* owner) {
    if (owner == nullptr) {
        return;
    }
    const IgnoreSlotId s = findFreeIgnoreSlot();
    if (s == kNoIgnoreSlot) {
        return;
    }
    // Pushed at the front of the body's chain; ignoring an owner twice takes two slots.
    WorldBody& b = body(id);
    m_ignoreSlots.at(s) = IgnoreSlot{.body = id, .owner = owner, .next = b.ignoreChain};
    b.ignoreChain = s;
}

void PhysicsWorld::bodyStopIgnoring(BodyId id, IgnoreSlotId slot) {
    WorldBody& b = body(id);
    if (slot == kNoIgnoreSlot) {
        // Every slot of the chain, from the head.
        while (b.ignoreChain != kNoIgnoreSlot) {
            IgnoreSlot& s = m_ignoreSlots.at(b.ignoreChain);
            const IgnoreSlotId next = s.next;
            s = IgnoreSlot{};
            b.ignoreChain = next;
        }
        return;
    }
    // Unlink the slot when the chain holds it; free it whether or not it did.
    IgnoreSlot& freed = m_ignoreSlots.at(slot);
    if (b.ignoreChain == slot) {
        b.ignoreChain = freed.next;
    } else {
        for (IgnoreSlotId s = b.ignoreChain; s != kNoIgnoreSlot; s = m_ignoreSlots.at(s).next) {
            if (m_ignoreSlots.at(s).next == slot) {
                m_ignoreSlots.at(s).next = freed.next;
                break;
            }
        }
    }
    freed = IgnoreSlot{};
}

bool PhysicsWorld::humanBodyIsIgnoring(BodyId id, const BodyOwner* owner, const BodyOwner* grabPartner) const {
    if (owner == nullptr) {
        return body(id).ignoresLevel;
    }
    if (grabPartner == owner) {
        return true;
    }
    return bodyIsIgnoring(id, owner);
}

void PhysicsWorld::humanBodyIgnore(BodyId id, const BodyOwner* owner) {
    if (owner == nullptr) {
        body(id).ignoresLevel = true;
        return;
    }
    bodyIgnore(id, owner);
}

void PhysicsWorld::humanBodyStopIgnoring(BodyId id, IgnoreSlotId slot) {
    if (slot == kNoIgnoreSlot) {
        body(id).ignoresLevel = false;
    }
    bodyStopIgnoring(id, slot);
}

} // namespace coney::physics
