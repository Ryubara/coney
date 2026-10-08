// SPDX-License-Identifier: GPL-3.0-or-later
#include "world_objects/pickups.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace coney::world_objects {

bool neverGlints(std::uint32_t modelHash) {
    constexpr std::array<std::uint32_t, 7> kHoboFood{0x8733e003U, 0x1e3ab1b9U, 0x693d812fU, 0xe09f6c15U,
                                                     0xc57ab00aU, 0x7056d232U, 0xb239f45dU};
    return std::ranges::find(kHoboFood, modelHash) != kHoboFood.end();
}

bool inSight(anim::Vec3 feet, anim::Vec3 at, const SightBlocked& blocked) {
    // From the waist's height, or else from above the head.
    return !blocked || !blocked(anim::Vec3{feet.x, feet.y, feet.z + kPickupSightLow}, at) ||
           !blocked(anim::Vec3{feet.x, feet.y, feet.z + kPickupSightHigh}, at);
}

PileTake pileTake(int objectKind, const std::function<int(int)>& pick) {
    // The pile types by their object type (TYPE_BRICKPILE ... TYPE_BOLTCUTTERBOX).
    constexpr int kBrickPile = 17;
    constexpr int kChunkPile = 18;
    constexpr int kBeerPile = 19;
    constexpr int kBaseballPile = 20;
    constexpr int kLiquorPile = 21;
    constexpr int kMolotovPile = 22;
    constexpr int kPoolBallPile = 23;
    constexpr int kOilPile = 41;
    constexpr int kSprayPile = 44;
    constexpr int kDonutPile = 46;
    constexpr int kBeerPileHeavy = 47;
    constexpr int kBoltCutterBox = 49;
    constexpr std::array<std::string_view, 3> kLiquor{"dyn_bottle_a", "dyn_bottle_b", "dyn_bottle_c"};
    constexpr int kLiquorKinds = 3;
    const auto draw = [&pick](int below) { return pick ? std::clamp(pick(below), 0, below - 1) : 0; };
    switch (objectKind) {
    case kBrickPile:
        return {.object = "dyn_brick", .cue = 25};
    case kChunkPile:
        return {.object = "dyn_cueball", .cue = 26};
    case kBeerPile:
        return {.object = "dyn_beerbottle", .cue = 27};
    case kBaseballPile:
        return {.object = "dyn_baseball", .cue = 28};
    case kLiquorPile:
        return {.object = kLiquor.at(static_cast<std::size_t>(draw(kLiquorKinds))), .cue = 29};
    case kMolotovPile:
        return {.object = "dyn_molotv", .cue = 30};
    case kPoolBallPile:
        // A number is drawn and never used: the draw still takes its place in the sequence, which one draw advances
        // the same whatever its range (not on the page), so any range keeps the game's sequence.
        static_cast<void>(draw(1));
        return {.object = "dyn_poolball08_", .cue = 31};
    case kOilPile:
        return {.object = "dyn_oilcan", .cue = -1};
    case kBeerPileHeavy:
        return {.object = "dyn_beerbottleheavy", .cue = 27};
    case kBoltCutterBox:
        return {.object = "dyn_boltcutter_b", .cue = -1};
    case kSprayPile:
    case kDonutPile:
        return {};
    default:
        return {.object = {}, .cue = -1, .itself = true};
    }
}

bool pickable(std::string_view className, std::uint32_t modelHash) {
    // The model hashes a class's init treats apart (docs/research/objects.md#pickable).
    constexpr std::uint32_t kNotPickableOverhead = 0x8fc6ac30U;
    constexpr std::uint32_t kNotPickableItem = 0x2fd690d6U;
    constexpr std::uint32_t kPickableSimpleObject = 0xfcbe9fbbU;
    if (className == "melee_weapon" || className == "thrown_weapon" || className == kDynPileClass) {
        return true;
    }
    if (className == "overhead_weapon") {
        return modelHash != kNotPickableOverhead;
    }
    if (className == kPickupItemClass) {
        return modelHash != kNotPickableItem;
    }
    // A cash register's init sets the flag too (docs/research/script-types.md#dyn-cashreg).
    if (className == "dyn_cashreg") {
        return true;
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
        if (!inSight(feet, at, blocked)) {
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
