// SPDX-License-Identifier: GPL-3.0-or-later
#include "physics/shapes.h"

#include <array>

#include "animation/anim_math.h"

namespace coney::physics {

// Stubs for the tests-first commit: each does nothing or returns a default.

ShapeRecord makeShape(ShapeType /*type*/) { return ShapeRecord{}; }

void setEnabled(ShapeRecord& /*shape*/, bool /*on*/) {}

void setBoxSize(ShapeRecord& /*shape*/, float /*sx*/, float /*sy*/, float /*sz*/) {}

void setCapsuleSize(ShapeRecord& /*shape*/, float /*height*/, float /*radius*/) {}

void poseSphere(ShapeRecord& /*shape*/, const ShapeOwner& /*owner*/) {}

void poseBox(ShapeRecord& /*shape*/, const ShapeOwner& /*owner*/) {}

void setBoxMatrix(ShapeRecord& /*shape*/, const anim::Mat34& /*m*/) {}

std::array<anim::Vec3, 8> boxCorners(const Box& /*box*/) { return {}; }

void poseCapsule(ShapeRecord& /*shape*/, const ShapeOwner& /*owner*/) {}

void poseSegment(ShapeRecord& /*shape*/, const ShapeOwner& /*owner*/) {}

void poseShape(ShapeRecord& /*shape*/, const ShapeOwner& /*owner*/) {}

void setSphereMatrix(ShapeRecord& /*shape*/, const anim::Mat34& /*m*/) {}

void setCapsuleMatrix(ShapeRecord& /*shape*/, const anim::Mat34& /*m*/) {}

void setShapeMatrix(ShapeRecord& /*shape*/, const anim::Mat34& /*m*/) {}

anim::Vec3 shapePosition(const ShapeRecord& /*shape*/) { return {}; }

Aabb shapeBounds(const ShapeRecord& /*shape*/) { return {}; }

Aabb aabbFromSphere(anim::Vec3 /*centre*/, float /*r*/) { return {}; }

Aabb aabbFromUprightCylinder(anim::Vec3 /*centre*/, float /*height*/, float /*radius*/) { return {}; }

} // namespace coney::physics
