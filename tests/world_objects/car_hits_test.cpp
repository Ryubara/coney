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

TEST_CASE("a player whose feet are above the car takes the cabin table in body bands 1 to 4, whatever his y",
          "[cars]") {
    // Expected: cars.md#windows, confirmed (code): Car_GetHitZone's player-above rule.
    Cars cars;
    const Car* car = cars.spawn("car_osedan", Vec3{}, Quat{}, 1);
    REQUIRE(car != nullptr);
    // Beside the front wing (body band 1), beyond the cabin's half length: the windscreen's cabin band too.
    const Vec3 above{-1.6F, 2.0F, 1.5F};
    CHECK(coney::world_objects::carZoneParts(*car, above, true) == (bit(10) | bit(6)));
    // Not a player, or not above: the body alone.
    CHECK(coney::world_objects::carZoneParts(*car, above, false) == bit(10));
    CHECK(coney::world_objects::carZoneParts(*car, Vec3{-1.6F, 2.0F, -0.5F}, true) == bit(10));
    // Body band 5 (behind the car) keeps the cabin's length test.
    CHECK(coney::world_objects::carZoneParts(*car, Vec3{-1.6F, -3.0F, 1.5F}, true) == (bit(3) | bit(12) | bit(28)));
}

TEST_CASE("a window a script removed is not struck, nor the door it no longer shields", "[cars]") {
    // Expected: cars.md#windows, confirmed (code): Car_OnHit skips the parts in +0x11f0, +0x11f4 and +0x11f8.
    Cars cars;
    REQUIRE(cars.spawn("car_osedan", Vec3{}, Quat{}, 1) != nullptr);
    // CarRemovePart on door 14 removes window 15 too (+0x11f0 only); the door is off (+0x11f8).
    cars.removePart(1, 14, true);
    CHECK(coney::world_objects::carHumanHitParts(*cars.find(1), Vec3{-1.6F, 0.5F, 0}) == 0);
    CHECK(cars.humanHit(1, Vec3{-1.6F, 0.5F, 0}, Vec3{}) == 0);
}

TEST_CASE("a hit at a front corner knocks the headlight glass off at once and sprays it", "[cars]") {
    // Expected: cars.md#type-tables, confirmed (code): hit bits 26-29 are marked removed and off, with their glass.
    using coney::world_objects::CarHitEffectKind;
    Cars cars;
    REQUIRE(cars.spawn("car_osedan", Vec3{}, Quat{}, 1) != nullptr);
    std::vector<coney::world_objects::CarHitReport> reports;
    const CarPartMask struck = cars.humanHit(1, Vec3{-1.6F, 3.5F, 0}, Vec3{-1.0F, 3.0F, 0.5F}, &reports);
    CHECK(struck == (bit(2) | bit(8) | bit(10) | bit(26)));
    const Car& car = *cars.find(1);
    CHECK((car.removedParts & bit(26)) != 0);
    CHECK((car.removedKept & bit(26)) != 0);
    const auto effects = cars.takeHitEffects();
    REQUIRE_FALSE(effects.empty());
    CHECK(effects.back().part == 26);
    CHECK(effects.back().kind == CarHitEffectKind::HeadlightGlass);
    // The lamps send no message 0x19.
    for (const coney::world_objects::CarHitReport& report : reports) {
        CHECK(report.part != 26);
    }
}

TEST_CASE("the all-broken message counts the parts of the car's type: the coupe's 1 to 17", "[cars]") {
    // Expected: cars.md#type-tables, confirmed (code): all broken is 0x3fffe for the coupe, checked after a part 1-21.
    Cars cars;
    REQUIRE(cars.spawn("car_coupe", Vec3{}, Quat{}, 1) != nullptr);
    for (std::uint32_t part = 1; part <= 17; ++part) {
        if (part != 14 && part != 15) {
            static_cast<void>(cars.damagePart(1, part, 1.0F, true));
        }
    }
    const Vec3 standing{-1.6F, 0.5F, 0};
    std::vector<coney::world_objects::CarHitReport> reports;
    CHECK(cars.humanHit(1, standing, standing, &reports) == bit(15));
    // Door 14 is the last of the coupe's parts: the hit that takes it off reports the car all broken.
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
    CHECK(reports[1].part == coney::world_objects::kCarHitAllBroken);
}
