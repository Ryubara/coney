// SPDX-License-Identifier: GPL-3.0-or-later
#include "human/human_contacts.h"

#include <algorithm>
#include <cmath>

namespace coney::human {

namespace {

// Below this the axes stand on each other, or the move is nothing.
constexpr float kTiny = 1e-6F;
// Half of the move into the one met goes into his push-out (code 4).
constexpr float kPushShare = 0.5F;

} // namespace

std::optional<CapsuleContact> sweepCapsules(const UprightCapsule& mover, anim::Vec3 move, const UprightCapsule& other) {
    // The heights must overlap somewhere along the move.
    const float moverLow = mover.feet.z + std::min(0.0F, move.z);
    const float moverHigh = mover.feet.z + mover.top + std::max(0.0F, move.z);
    if (moverHigh < other.feet.z || moverLow > other.feet.z + other.top) {
        return std::nullopt;
    }
    // Across the ground: the mover's axis from the other's, and the move.
    const float px = mover.feet.x - other.feet.x;
    const float py = mover.feet.y - other.feet.y;
    const float mx = move.x;
    const float my = move.y;
    const float reach = mover.radius + other.radius;
    const float startSq = px * px + py * py;
    const float going = px * mx + py * my; // below 0: the move closes in
    if (startSq <= reach * reach) {
        // Already within reach: a move that goes in is met at once.
        const float start = std::sqrt(startSq);
        if (start < kTiny || going >= 0.0F) {
            return std::nullopt;
        }
        return CapsuleContact{.fraction = 0.0F, .normal = anim::Vec3{px / start, py / start, 0.0F}};
    }
    // |p + t m| = reach: the first root from 0 to 1.
    const float a = mx * mx + my * my;
    if (a < kTiny * kTiny || going >= 0.0F) {
        return std::nullopt;
    }
    const float c = startSq - reach * reach;
    const float disc = going * going - a * c;
    if (disc < 0.0F) {
        return std::nullopt;
    }
    const float t = (-going - std::sqrt(disc)) / a;
    if (t < 0.0F || t > 1.0F) {
        return std::nullopt;
    }
    const float nx = px + t * mx;
    const float ny = py + t * my;
    const float length = std::hypot(nx, ny);
    if (length < kTiny) {
        return std::nullopt;
    }
    return CapsuleContact{.fraction = t, .normal = anim::Vec3{nx / length, ny / length, 0.0F}};
}

anim::Vec3 contactPush(anim::Vec3 move, anim::Vec3 normal, float pushWeight) {
    const float into = anim::dot(move, normal);
    if (into >= 0.0F || pushWeight <= 0.0F) {
        return anim::Vec3{};
    }
    // (d · n) n is the part of the move into the one met; it points away from the mover, so he is pushed on.
    return anim::scale(normal, kPushShare * into / pushWeight);
}

} // namespace coney::human
