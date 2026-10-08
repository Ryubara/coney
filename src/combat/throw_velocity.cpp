// SPDX-License-Identifier: GPL-3.0-or-later
#include "combat/throw_velocity.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "world_objects/object_types.h"

namespace coney::combat {

namespace {

// The weight's scale into the factor, and the factor of a knife.
constexpr float kWeightScale = 0.05F;
// A direction more sideways than forward is clamped to 45° off forward: its x and y become ±/+ this.
constexpr float kHalfRoot2 = 0.70710678F;

// `v` turned by `angle` radians about the axis through the origin along the unit vector `axis` (Rodrigues).
anim::Vec3 turnAbout(anim::Vec3 v, anim::Vec3 axis, float angle) {
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    const anim::Vec3 k = anim::cross(axis, v);
    const float along = anim::dot(axis, v) * (1.0F - c);
    return anim::add(anim::add(anim::scale(v, c), anim::scale(k, s)), anim::scale(axis, along));
}

} // namespace

float throwWeightFactor(int objectKind, int weight, float throwerScale) {
    float w = objectKind == world_objects::kObjectKindKnife ? 1.0F : kWeightScale * static_cast<float>(weight);
    if (w <= 0.0F) {
        w = 1.0F;
    }
    if (throwerScale > kThrowBigScale) {
        w *= 0.5F;
    }
    return w;
}

anim::Vec3 throwVelocity(const ThrowAim& aim) {
    const float w = aim.weightFactor > 0.0F ? aim.weightFactor : 1.0F;
    const float speed = kThrowSpeed / w;
    if (aim.toTarget) {
        const float d = anim::length(*aim.toTarget);
        anim::Vec3 u = d > 0.0F ? anim::scale(*aim.toTarget, 1.0F / d) : anim::Vec3{0.0F, 1.0F, 0.0F};
        // More sideways than forward: 45° off forward on that side, the height kept.
        if (std::fabs(u.x) > std::fabs(u.y)) {
            u.x = std::copysign(kHalfRoot2, u.x);
            u.y = kHalfRoot2;
        }
        if (std::fabs(u.z) < kThrowSteepZ) {
            anim::Vec3 v = anim::scale(u, speed);
            // The lift that brings it back down to the target's height after the flight time d / speed.
            v.z += 0.5F * kThrowGravity * d / speed;
            if (aim.spread) {
                const float share = std::clamp(d / kThrowSpreadDistance, kThrowSpreadLeast, 1.0F);
                const float most = share * kThrowSpreadDegrees * aim.spreadShare * std::numbers::pi_v<float> / 180.0F;
                v = turnAbout(v, anim::Vec3{0.0F, 0.0F, 1.0F}, most * aim.spread->x);
                const float flat = std::hypot(v.x, v.y);
                if (flat > 0.0F) {
                    v = turnAbout(v, anim::Vec3{v.y / flat, -v.x / flat, 0.0F}, most * aim.spread->y);
                }
            }
            return v;
        }
    }
    return anim::scale(anim::normalise(anim::Vec3{0.0F, 1.0F, kThrowDefaultLift}),
                       aim.fastDefault ? kFastThrowSpeed / w : speed);
}

anim::Vec3 aimedThrowVelocity(float weightFactor, float pitch) {
    const float w = weightFactor > 0.0F ? weightFactor : 1.0F;
    return anim::scale(anim::Vec3{0.0F, std::cos(pitch), std::sin(pitch)}, kAimedThrowSpeed / w);
}

} // namespace coney::combat
