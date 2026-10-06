// SPDX-License-Identifier: GPL-3.0-or-later
#include "world_objects/car_hits.h"

#include <array>
#include <cmath>
#include <cstddef>

#include "world_objects/cars.h"

namespace coney::world_objects {

namespace {

// One type's hit zones (docs/research/cars.md#windows): the box, the body table's five falling y thresholds and its
// part masks (band × {x ≤ −0.6, between, x > 0.6}), the cabin's half length, thresholds and masks (band × {x ≤ 0,
// x > 0}).
struct CarZones {
    float halfWidth = 0.0F;
    float halfLength = 0.0F;
    std::array<float, 5> bodyBands{};
    std::array<std::array<CarPartMask, 3>, 6> body{};
    float cabinHalfLength = 0.0F;
    std::array<float, 5> cabinBands{};
    std::array<std::array<CarPartMask, 2>, 6> cabin{};
};

// The parts `ids` as a mask.
template <std::size_t N> constexpr CarPartMask parts(const std::array<std::uint32_t, N>& ids) {
    CarPartMask mask = 0;
    for (const std::uint32_t id : ids) {
        mask |= 1U << id;
    }
    return mask;
}

// The body table's column bounds and the cabin's (`+0x54`, `+0x58`, `+0x70`).
constexpr float kBodyLeft = -0.6F;
constexpr float kBodyRight = 0.6F;
constexpr float kCabinSplit = 0.0F;
// The roof (bit 1) is never hit.
constexpr CarPartMask kRoof = 1U << 1U;
// The parts that put the aim point 1.5 m up (the windows and parts 6 and 7) and 1.0 m up (the bonnet and boot).
constexpr CarPartMask kHighParts = 0x2a80c0U;
constexpr CarPartMask kMiddleParts = 0x30U;
constexpr float kHighAim = 1.5F;
constexpr float kMiddleAim = 1.0F;
// A candidate car is less than this far outside its box, across and along.
constexpr float kCarReach = 1.0F;
// The car pass's cone either side of the search heading (3 × 54°) and its height window.
constexpr float kCarConeDegrees = 3.0F * 54.0F;
constexpr float kCarHeight = 2.0F;
// How squarely the human must face the box's nearest face (forward · outward normal at most this).
constexpr float kFacingDot = -0.7F;
// A target below the feet is aimed this far below them.
constexpr float kBelowAim = 0.25F;
constexpr float kPi = 3.14159265358979F;

// The sedan's zones (car_osedan), confirmed (code) from the type record's data.
constexpr CarZones kSedan{
    .halfWidth = 2.40F / 2.0F,
    .halfLength = 6.05F / 2.0F,
    .bodyBands = {2.716F, 1.028F, -0.034F, -1.597F, -2.778F},
    .body = {{
        {parts<4>({2, 8, 10, 26}), parts<4>({2, 4, 8, 9}), parts<4>({2, 9, 11, 27})},
        {parts<1>({10}), parts<1>({4}), parts<1>({11})},
        {parts<2>({14, 15}), parts<4>({14, 15, 16, 17}), parts<2>({16, 17})},
        {parts<2>({18, 19}), parts<4>({18, 19, 20, 21}), parts<2>({20, 21})},
        {parts<1>({12}), parts<1>({5}), parts<1>({13})},
        {parts<3>({3, 12, 28}), parts<2>({3, 5}), parts<3>({3, 13, 29})},
    }},
    .cabinHalfLength = 2.61F / 2.0F,
    .cabinBands = {1.207F, 1.007F, 0.0F, -1.007F, -1.207F},
    .cabin = {{
        {parts<1>({6}), parts<1>({6})},
        {parts<2>({6, 15}), parts<2>({6, 17})},
        {parts<1>({15}), parts<1>({17})},
        {parts<1>({19}), parts<1>({21})},
        {parts<2>({7, 19}), parts<2>({7, 21})},
        {parts<1>({7}), parts<1>({7})},
    }},
};

// A type's zones. **Coney's stand-in**: every type uses the sedan's (the others' thresholds and masks are not on the
// page yet).
const CarZones& zonesOf(const Car& /*car*/) { return kSedan; }

// The band of `y` among five falling thresholds: 0 at or above the first, 5 below the last.
std::size_t bandOf(const std::array<float, 5>& thresholds, float y) {
    std::size_t band = 0;
    while (band < thresholds.size() && y < thresholds.at(band)) {
        ++band;
    }
    return band;
}

// The car's turn as a matrix.
anim::Mat34 turnOf(const Car& car) { return anim::matrixFromQuat(anim::normalise(car.rotation)); }

// `local` in the car's frame, turned into world axes and moved to the car.
anim::Vec3 fromLocal(const Car& car, anim::Vec3 local) {
    return anim::add(car.position, anim::transformDirection(turnOf(car), local));
}

// `angle` wrapped to −π..π.
float wrapAngle(float angle) { return std::remainder(angle, 2.0F * kPi); }

} // namespace

anim::Vec3 carLocal(const Car& car, anim::Vec3 world) {
    return anim::transformDirection(anim::inverseRigid(turnOf(car)), anim::subtract(world, car.position));
}

CarPartMask carZoneParts(const Car& car, anim::Vec3 standing) {
    const CarZones& zones = zonesOf(car);
    const anim::Vec3 at = carLocal(car, standing);
    // The body table by band and column.
    const std::size_t column = at.x <= kBodyLeft ? 0 : (at.x <= kBodyRight ? 1 : 2);
    CarPartMask mask = zones.body.at(bandOf(zones.bodyBands, at.y)).at(column);
    // The cabin table, ORed in beside the cabin.
    if (std::fabs(at.y) < zones.cabinHalfLength) {
        mask |= zones.cabin.at(bandOf(zones.cabinBands, at.y)).at(at.x <= kCabinSplit ? 0 : 1);
    }
    return mask & ~kRoof;
}

CarPartMask carFacingParts(const Car& car, anim::Vec3 standing, anim::Vec3 forward) {
    const CarZones& zones = zonesOf(car);
    const anim::Vec3 at = carLocal(car, standing);
    const bool beside = std::fabs(at.x) > zones.halfWidth;
    const bool ahead = std::fabs(at.y) > zones.halfLength;
    if (!beside && !ahead) {
        return 0; // inside the footprint
    }
    // The nearest face's outward normal: ±x beside, ±y before or behind, the corner's direction diagonally.
    anim::Vec3 normal{};
    if (beside && !ahead) {
        normal = anim::Vec3{std::copysign(1.0F, at.x), 0.0F, 0.0F};
    } else if (ahead && !beside) {
        normal = anim::Vec3{0.0F, std::copysign(1.0F, at.y), 0.0F};
    } else {
        normal = anim::normalise(anim::Vec3{at.x - std::copysign(zones.halfWidth, at.x),
                                            at.y - std::copysign(zones.halfLength, at.y), 0.0F});
    }
    const anim::Vec3 face = anim::transformDirection(turnOf(car), normal);
    if ((forward.x * face.x) + (forward.y * face.y) > kFacingDot) {
        return 0;
    }
    return carZoneParts(car, standing);
}

bool carTargetable(const Car& car, anim::Vec3 feet, float heading) {
    const CarZones& zones = zonesOf(car);
    const anim::Vec3 at = carLocal(car, feet);
    const float outX = std::fabs(at.x) - zones.halfWidth;
    const float outY = std::fabs(at.y) - zones.halfLength;
    if (outX >= kCarReach || outY >= kCarReach) {
        return false;
    }
    // On the car: it must be 0-2 m below.
    if (outX <= 0.0F && outY <= 0.0F) {
        const float below = feet.z - car.position.z;
        return below >= 0.0F && below <= kCarHeight;
    }
    const float toward = std::atan2(car.position.y - feet.y, car.position.x - feet.x);
    const float headingWay = heading + (kPi / 2.0F);
    return std::fabs(wrapAngle(toward - headingWay)) <= kCarConeDegrees * kPi / 180.0F &&
           std::fabs(car.position.z - feet.z) <= kCarHeight;
}

std::optional<anim::Vec3> carAimPoint(const Car& car, anim::Vec3 feet, anim::Vec3 forward) {
    if (car.position.z < feet.z) {
        return anim::Vec3{feet.x, feet.y, feet.z - kBelowAim};
    }
    const CarPartMask hittable = carFacingParts(car, feet, forward) & ~(car.removedParts | car.removedKept);
    if (hittable == 0) {
        return std::nullopt;
    }
    const float up = (hittable & kHighParts) != 0 ? kHighAim : ((hittable & kMiddleParts) != 0 ? kMiddleAim : 0.0F);
    return anim::Vec3{feet.x + forward.x, feet.y + forward.y, feet.z + up};
}

CarPartMask carHumanHitParts(const Car& car, anim::Vec3 standing) {
    CarPartMask mask = carZoneParts(car, standing);
    // An intact window shields its door from this hit.
    for (const std::uint32_t window : {15U, 17U, 19U, 21U}) {
        const CarPartMask bit = 1U << window;
        if ((mask & bit) != 0 && (car.removedKept & bit) == 0) {
            mask &= ~(1U << (window - 1U));
        }
    }
    return mask & ~car.removedKept;
}

anim::Vec3 carStereoPosition(const Car& car) { return fromLocal(car, anim::Vec3{-0.75F, 0.25F, 0.1F}); }

anim::Vec3 carBootPosition(const Car& car) {
    // By type: car_osedan, car_coupe, car_wagon, car_copcar, car_van, car_sullycar.
    static constexpr std::array<anim::Vec3, 6> kBoot{anim::Vec3{0.0F, -2.34F, -0.01F},
                                                     anim::Vec3{0.0F, -2.34F, -0.05F},
                                                     anim::Vec3{},
                                                     anim::Vec3{0.0F, -2.26F, -0.02F},
                                                     anim::Vec3{},
                                                     anim::Vec3{0.0F, -2.34F, -0.01F}};
    return fromLocal(car, car.type && *car.type < kBoot.size() ? kBoot.at(*car.type) : anim::Vec3{});
}

} // namespace coney::world_objects
