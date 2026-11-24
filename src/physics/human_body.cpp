// SPDX-License-Identifier: GPL-3.0-or-later
// The human body set (docs/research/physics-bodies.md#unit-10). Stubs until the port.
#include "physics/human_body.h"

namespace coney::physics {

void applyHumanScale(PhysicsWorld& /*world*/, BodyId /*id*/, const HumanBodyOwners& /*owners*/) {}
void updateHumanBody(PhysicsWorld& /*world*/, BodyId /*id*/, const HumanBodyOwners& /*owners*/) {}
void humanBodyMoved(PhysicsWorld& /*world*/, BodyId /*id*/, const anim::Mat34& /*m*/) {}
void setStrikeShapeEnabled(PhysicsWorld& /*world*/, BodyId /*id*/, int /*bone*/, bool /*on*/) {}
void addStrikeSphere(PhysicsWorld& /*world*/, BodyId /*id*/, ShapeId /*sphere*/) {}
void clearStrikeSpheres(PhysicsWorld& /*world*/, BodyId /*id*/) {}
void setPushSphere(PhysicsWorld& /*world*/, BodyId /*id*/, bool /*on*/) {}
void pushAwayHumans(PhysicsWorld& /*world*/, BodyId /*id*/, const HumanBodyOwners& /*owners*/) {}
void testStrikes(PhysicsWorld& /*world*/, BodyId /*id*/, HumanBodyOwners& /*owners*/) {}

} // namespace coney::physics
