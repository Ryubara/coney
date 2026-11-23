// SPDX-License-Identifier: GPL-3.0-or-later
#include "physics/physics_world.h"

#include <cstddef>
#include <optional>
#include <vector>

namespace coney::physics {

// Stubs for the tests-first commit: each does nothing or returns a default.

std::size_t shapePoolCap(ShapePool /*pool*/, EngineLimits /*limits*/) { return 0; }

std::size_t ignoreSlotCap(EngineLimits /*limits*/) { return 0; }

PhysicsWorld::PhysicsWorld(EngineLimits limits) : m_limits(limits) {}

ShapeId PhysicsWorld::addShapeRecord(const ShapeRecord& record) {
    m_shapes.push_back(record);
    return static_cast<ShapeId>(m_shapes.size() - 1);
}

BodyId PhysicsWorld::createBody() {
    m_bodies.emplace_back();
    return static_cast<BodyId>(m_bodies.size() - 1);
}

BodyId PhysicsWorld::createBodyForOwner(BodyOwner* /*owner*/, std::uint32_t /*typeMask*/) { return createBody(); }

void PhysicsWorld::setOwner(BodyId /*id*/, BodyOwner* /*owner*/, std::uint32_t /*typeMask*/) {}

void PhysicsWorld::addShape(BodyId /*id*/, ShapeId /*shape*/) {}

void PhysicsWorld::computeBounds(Aabb& /*box*/, ShapeId /*shape*/) const {}

void PhysicsWorld::addShapeBounds(Aabb& /*box*/, ShapeId /*shape*/) const {}

std::optional<UpdateSweep> PhysicsWorld::updateBody(BodyId /*id*/, const ShapeOwner& /*owner*/) { return std::nullopt; }

void PhysicsWorld::bodyMoved(BodyId /*id*/, const anim::Mat34& /*m*/) {}

void PhysicsWorld::insert(BodyId /*id*/, BodyId /*hint*/) {}

BodyId PhysicsWorld::remove(BodyId /*id*/) { return kNoBody; }

void PhysicsWorld::reinsert(BodyId /*id*/) {}

void PhysicsWorld::addBody(BodyId /*id*/) {}

void PhysicsWorld::removeBody(BodyId /*id*/) {}

std::size_t PhysicsWorld::queryBox(std::vector<BodyId>& /*out*/, const Aabb& /*box*/, BodyId /*start*/,
                                   const BodyOwner* /*ignored*/, std::uint32_t /*typeMask*/,
                                   std::uint32_t /*flagMask*/) const {
    return 0;
}

std::size_t PhysicsWorld::queryBoxFiltered(std::vector<BodyId>& /*out*/, BodyId /*query*/, const Aabb& /*box*/,
                                           BodyId /*start*/, const BodyFilter& /*filter*/) const {
    return 0;
}

std::size_t PhysicsWorld::overlapShape(std::vector<Contact>& /*out*/, const OverlapQuery& /*q*/) { return 0; }

void PhysicsWorld::refreshBound() {}

IgnoreSlotId PhysicsWorld::findFreeIgnoreSlot() { return kNoIgnoreSlot; }

bool PhysicsWorld::isIgnoring(BodyId /*id*/, const BodyOwner* /*owner*/, const BodyOwner* /*grabPartner*/) const {
    return false;
}

void PhysicsWorld::ignore(BodyId /*id*/, const BodyOwner* /*owner*/) {}

void PhysicsWorld::stopIgnoring(BodyId /*id*/, IgnoreSlotId /*slot*/) {}

void PhysicsWorld::forgetIgnoredOwner(BodyId /*removed*/) {}

void PhysicsWorld::freeShape(ShapeId /*id*/) {}

ShapeId PhysicsWorld::takeFromPool(ShapePool /*pool*/) { return kNoShape; }

} // namespace coney::physics
