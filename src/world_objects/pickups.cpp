// SPDX-License-Identifier: GPL-3.0-or-later
#include "world_objects/pickups.h"

#include <cmath>

namespace coney::world_objects {

bool pickableClass(std::string_view className) { return className == "pickup_item"; }

std::optional<std::size_t> searchPickup(anim::Vec3 feet, anim::Vec3 facing, std::span<const PickupCandidate> candidates,
                                        const SightBlocked& blocked) {
    const anim::Vec3 behind{feet.x - facing.x * kPickupBehind, feet.y - facing.y * kPickupBehind, feet.z};
    std::optional<std::size_t> best;
    int bestScore = 0;
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        const PickupCandidate& candidate = candidates[i];
        const anim::Vec3 at = candidate.position;
        if (!candidate.pickable || std::hypot(at.x - feet.x, at.y - feet.y) > kPickupReach) {
            continue;
        }
        // In sight from the waist's height, or else from above the head.
        if (blocked && blocked(anim::Vec3{feet.x, feet.y, feet.z + kPickupSightLow}, at) &&
            blocked(anim::Vec3{feet.x, feet.y, feet.z + kPickupSightHigh}, at)) {
            continue;
        }
        const float dx = at.x - behind.x;
        const float dy = at.y - behind.y;
        const float length = std::hypot(dx, dy);
        const float ahead = length > 1e-6F ? (dx * facing.x + dy * facing.y) / length : 1.0F;
        int score = 1;
        if (ahead >= kPickupAhead) {
            score = 3;
        } else if (ahead >= 0.0F) {
            score = 2;
        }
        if (score > bestScore) {
            best = i;
            bestScore = score;
        }
    }
    return best;
}

int pickupClip(float height) { return height <= kPickupLowHeight ? kPickupLowClip : kPickupHighClip; }

} // namespace coney::world_objects
