// SPDX-License-Identifier: GPL-3.0-or-later
#include "world_objects/pickups.h"

#include <array>
#include <cmath>
#include <cstddef>

namespace coney::world_objects {

bool pickable(std::string_view className, std::uint32_t modelHash) {
    // The model hashes a class's init treats apart (docs/research/objects.md#pickable).
    constexpr std::uint32_t kNotPickableOverhead = 0x8fc6ac30U;
    constexpr std::uint32_t kNotPickableItem = 0x2fd690d6U;
    constexpr std::uint32_t kPickableSimpleObject = 0xfcbe9fbbU;
    if (className == "melee_weapon" || className == "thrown_weapon") {
        return true;
    }
    if (className == "overhead_weapon") {
        return modelHash != kNotPickableOverhead;
    }
    if (className == "pickup_item") {
        return modelHash != kNotPickableItem;
    }
    if (className == "simple_object") {
        return modelHash == kPickableSimpleObject;
    }
    return false;
}

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

int pickupClip(int pickupAnim, float height) {
    // Each animation's low clip; its high clip is the next id (6's hat clip has none).
    constexpr std::array<int, 8> kLow{461, 461, 503, 481, 498, 463, 465, 549};
    constexpr int kHatAnim = 6;
    const int low = pickupAnim >= 0 && pickupAnim < static_cast<int>(kLow.size())
                        ? kLow.at(static_cast<std::size_t>(pickupAnim))
                        : kPickupGroundClip;
    if (pickupAnim == kHatAnim || height <= kPickupLowHeight) {
        return low;
    }
    return low + 1;
}

} // namespace coney::world_objects
