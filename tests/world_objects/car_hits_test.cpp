// SPDX-License-Identifier: GPL-3.0-or-later
// A car's windows, hit zones and stereo (docs/research/cars.md#windows): the zone tables by where the hitter stands,
// the facing test, the car pass's reach, the aim point, an intact window shielding its door, window 15 freeing the
// stereo, and the stereo's and boot's offsets.
#include "world_objects/car_hits.h"

#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "world_objects/cars.h"

using Catch::Approx;
using coney::anim::Quat;
using coney::anim::Vec3;
using coney::world_objects::Car;
using coney::world_objects::CarPartMask;
using coney::world_objects::Cars;
using coney::world_objects::StereoState;

namespace {

// The mask of one part.
constexpr CarPartMask bit(unsigned part) { return 1U << part; }

// The heading (radians) whose facing is +x: headings grow to the left from +y.
constexpr float kFacingPlusX = -1.5707963F;

} // namespace

TEST_CASE("a car's zone tables pick its parts by where the hitter stands", "[cars]") {
    Cars cars;
    const Car* car = cars.spawn("car_osedan", Vec3{}, Quat{}, 1);
    REQUIRE(car != nullptr);
    // Beside the front-left door: the door and its window, and the window from the cabin table.
    CHECK(coney::world_objects::carZoneParts(*car, Vec3{-1.6F, 0.5F, 0}) == (bit(14) | bit(15)));
    // Beside the rear-right door.
    CHECK(coney::world_objects::carZoneParts(*car, Vec3{1.6F, -0.5F, 0}) == (bit(20) | bit(21)));
    // Before the car: the bonnet's band, beyond the cabin; the roof (bit 1) never.
    CHECK(coney::world_objects::carZoneParts(*car, Vec3{0, 3.5F, 0}) == (bit(2) | bit(4) | bit(8) | bit(9)));
}

TEST_CASE("a human hits a car's parts only facing its nearest face", "[cars]") {
    Cars cars;
    const Car* car = cars.spawn("car_osedan", Vec3{}, Quat{}, 1);
    REQUIRE(car != nullptr);
    const Vec3 standing{-1.6F, 0.5F, 0};
    CHECK(coney::world_objects::carFacingParts(*car, standing, Vec3{1, 0, 0}) == (bit(14) | bit(15)));
    CHECK(coney::world_objects::carFacingParts(*car, standing, Vec3{0, 1, 0}) == 0);
    CHECK(coney::world_objects::carFacingParts(*car, Vec3{0, 0, 0}, Vec3{1, 0, 0}) == 0); // inside the box
}

TEST_CASE("the car pass takes a car within 1 m of its box and aims at a window 1.5 m up", "[cars]") {
    Cars cars;
    const Car* car = cars.spawn("car_osedan", Vec3{}, Quat{}, 1);
    REQUIRE(car != nullptr);
    CHECK(coney::world_objects::carTargetable(*car, Vec3{-1.6F, 0.5F, 0}, kFacingPlusX));
    CHECK_FALSE(coney::world_objects::carTargetable(*car, Vec3{-2.3F, 0.5F, 0}, kFacingPlusX));
    CHECK_FALSE(coney::world_objects::carTargetable(*car, Vec3{-1.6F, 0.5F, 2.5F}, kFacingPlusX));
    const std::optional<Vec3> aim = coney::world_objects::carAimPoint(*car, Vec3{-1.6F, 0.5F, 0}, Vec3{1, 0, 0});
    REQUIRE(aim.has_value());
    CHECK(aim.value_or(Vec3{}).x == Approx(-0.6F));
    CHECK(aim.value_or(Vec3{}).z == Approx(1.5F));
    // Facing away, nothing to aim at.
    CHECK_FALSE(coney::world_objects::carAimPoint(*car, Vec3{-1.6F, 0.5F, 0}, Vec3{-1, 0, 0}).has_value());
}

TEST_CASE("the first hit breaks the window, which shielded its door, and window 15 frees the stereo", "[cars]") {
    Cars cars;
    REQUIRE(cars.spawn("car_osedan", Vec3{}, Quat{}, 1) != nullptr);
    cars.spawnRadio(1);
    const Vec3 standing{-1.6F, 0.5F, 0};
    CHECK(cars.humanHit(1, standing, standing) == bit(15));
    CHECK((cars.find(1)->removedKept & bit(15)) != 0);
    CHECK(cars.find(1)->stereo == StereoState::Freed);
    // The window is gone: now the door takes a plain hit's damage.
    CHECK(cars.humanHit(1, standing, standing) == bit(14));
    CHECK(cars.find(1)->damage.at(14) == Approx(coney::world_objects::kHumanCarHitDamage));
    CHECK((cars.find(1)->removedKept & bit(14)) == 0);
    // Window 19 does not free a stereo.
    REQUIRE(cars.spawn("car_osedan", Vec3{10, 0, 0}, Quat{}, 2) != nullptr);
    cars.spawnRadio(2);
    CHECK(cars.humanHit(2, Vec3{8.4F, -0.5F, 0}, Vec3{}) == bit(19));
    CHECK(cars.find(2)->stereo == StereoState::InCar);
}

TEST_CASE("the stereo and the boot item sit at offsets turned with the car", "[cars]") {
    Cars cars;
    const float half = std::sqrt(0.5F);
    const Car* car = cars.spawn("car_osedan", Vec3{3, 4, 1}, Quat{0, 0, half, half}, 1);
    REQUIRE(car != nullptr);
    const Vec3 stereo = coney::world_objects::carLocal(*car, Cars::stereoPosition(*car));
    CHECK(stereo.x == Approx(-0.75F).margin(1e-4));
    CHECK(stereo.y == Approx(0.25F).margin(1e-4));
    CHECK(stereo.z == Approx(0.1F).margin(1e-4));
    const Vec3 boot = coney::world_objects::carLocal(*car, Cars::bootPosition(*car));
    CHECK(boot.y == Approx(-2.34F).margin(1e-4));
    REQUIRE(cars.spawn("car_van", Vec3{3, 4, 1}, Quat{}, 2) != nullptr);
    CHECK(Cars::bootPosition(*cars.find(2)) == Vec3{3, 4, 1});
}

TEST_CASE("a hit reports each part it damaged and whether it broke, and the hit that leaves every part off", "[cars]") {
    // docs/research/cars.md: message 0x19 from Car_OnHit, (car, human, part, 1 if this hit broke it), and -2 once
    // every part is off.
    Cars cars;
    REQUIRE(cars.spawn("car_osedan", Vec3{}, Quat{}, 1) != nullptr);
    const Vec3 standing{-1.6F, 0.5F, 0};
    std::vector<coney::world_objects::CarHitReport> reports;
    CHECK(cars.humanHit(1, standing, standing, &reports) == bit(15));
    REQUIRE(reports.size() == 1);
    CHECK(reports[0].part == 15);
    CHECK(reports[0].broke);
    reports.clear();
    CHECK(cars.humanHit(1, standing, standing, &reports) == bit(14));
    REQUIRE(reports.size() == 1);
    CHECK(reports[0].part == 14);
    CHECK_FALSE(reports[0].broke);
    // Every part but the door off already: the hit that takes the door off is also the last.
    for (std::uint32_t part = 1; part < coney::world_objects::kCarParts; ++part) {
        if (part != 14) {
            static_cast<void>(cars.damagePart(1, part, 1.0F, true));
        }
    }
    // The door takes a plain hit's damage until it comes off.
    constexpr int kMostHits = 20;
    for (int hit = 0; hit < kMostHits; ++hit) {
        reports.clear();
        static_cast<void>(cars.humanHit(1, standing, standing, &reports));
        if (reports.empty() || reports.front().broke) {
            break;
        }
    }
    REQUIRE(reports.size() == 2);
    CHECK(reports[0].part == 14);
    CHECK(reports[0].broke);
    CHECK(reports[1].part == coney::world_objects::kCarHitAllBroken);
    CHECK(reports[1].broke);
    // Nothing left to damage: no report.
    reports.clear();
    static_cast<void>(cars.humanHit(1, standing, standing, &reports));
    CHECK(reports.empty());
}
