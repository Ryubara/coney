// SPDX-License-Identifier: GPL-3.0-or-later
// The physics world's body pools (docs/research/physics-bodies.md#unit-9). Stubs until the port.
#include <cstddef>
#include <cstdint>
#include <limits>

#include "physics/human_body.h"
#include "physics/physics_world.h"

namespace coney::physics {

std::size_t bodyPoolCap(BodyPool /*pool*/, EngineLimits /*limits*/) { return 0; }
BodyId PhysicsWorld::constructHumanBody() { return kNoBody; }
void PhysicsWorld::addBoneShape(BodyId /*id*/, ShapeId /*shape*/) {}
BodyId PhysicsWorld::buildHumanSet() { return kNoBody; }
BodyId PhysicsWorld::buildPunchBagSet() { return kNoBody; }
BodyId PhysicsWorld::buildPoolBody(BodyPool /*pool*/) { return kNoBody; }
std::size_t PhysicsWorld::findFreeBody(BodyPool /*pool*/) { return std::numeric_limits<std::size_t>::max(); }
BodyId PhysicsWorld::createHumanBody(OwnerId /*owner*/, std::uint32_t /*typeMask*/, const HumanBodyOwners& /*owners*/) {
    return kNoBody;
}
BodyId PhysicsWorld::createPunchBagBody(OwnerId /*owner*/, std::uint32_t /*typeMask*/) { return kNoBody; }
BodyId PhysicsWorld::createObjectBody(OwnerId /*owner*/, std::uint32_t /*typeMask*/) { return kNoBody; }
BodyId PhysicsWorld::createCarBody(OwnerId /*owner*/, std::uint32_t /*typeMask*/) { return kNoBody; }
BodyId PhysicsWorld::createBoxBody(OwnerId /*owner*/, std::uint32_t /*typeMask*/, float /*width*/, float /*height*/) {
    return kNoBody;
}
void PhysicsWorld::freeHumanBody(BodyId /*id*/) {}
void PhysicsWorld::freeObjectBody(BodyId /*id*/) {}
void PhysicsWorld::freeCarBody(BodyId /*id*/) {}
void PhysicsWorld::freeBoxBody(BodyId /*id*/) {}
void PhysicsWorld::addHumanBody(BodyId /*id*/) {}
void PhysicsWorld::removeHumanBody(BodyId /*id*/) {}

} // namespace coney::physics
