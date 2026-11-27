// SPDX-License-Identifier: GPL-3.0-or-later
// The parked cars (docs/research/cars.md): type by name, the pool of 18, the paint's packing and reversal, removed
// parts and their links, the repair, the police car's lights and the stereo's states.
#include "world_objects/cars.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

#include "core/name_hash.h"
#include "effects/particles.h"
#include "world_objects/car_types.h"
#include "world_objects/spawn_records.h"

using coney::anim::Quat;
using coney::anim::Vec3;
using coney::world_objects::Car;
using coney::world_objects::CarPaint;
using coney::world_objects::Cars;
using coney::world_objects::StereoState;

TEST_CASE("a car takes the index of its type name; an unknown name has none", "[cars]") {
    Cars cars;
    const Car* van = cars.spawn("car_van", Vec3{1, 2, 3}, Quat{0, 0, 1, 0}, 10);
    REQUIRE(van != nullptr);
    CHECK(van->type == 4);
    CHECK(van->nameHash == coney::crc32(std::string_view("car_van")));
    CHECK(van->position == Vec3{1, 2, 3});
    const Car* odd = cars.spawn("car_osedan01", Vec3{}, Quat{}, 11);
    REQUIRE(odd != nullptr);
    CHECK_FALSE(odd->type.has_value());
    CHECK(cars.find(10) != nullptr);
    CHECK(cars.find(12) == nullptr);
    CHECK(cars.find(0) == nullptr);
}

TEST_CASE("the pool holds 18 cars", "[cars]") {
    Cars cars;
    for (std::size_t i = 0; i < Cars::kPool; ++i) {
        REQUIRE(cars.spawn("car_coupe", Vec3{}, Quat{}, static_cast<double>(i + 1)) != nullptr);
    }
    CHECK(cars.spawn("car_coupe", Vec3{}, Quat{}, 99) == nullptr);
}

TEST_CASE("CarSetColor's components are stored in order and read back reversed", "[cars]") {
    CHECK(coney::world_objects::packCarColour({1.0F, 0.0F, 0.5F, 1.0F}) == 0xFF7F00FFU);
    CHECK(coney::world_objects::paintOf(0xFF7F00FFU) == CarPaint{255, 127, 0, 255});
    Cars cars;
    REQUIRE(cars.spawn("car_osedan", Vec3{}, Quat{}, 5) != nullptr);
    cars.find(5)->dirty = false;
    cars.setColour(5, {1.0F, 0.2F, 0.4F, 0.6F});
    const Car& car = *cars.find(5);
    CHECK(car.painted);
    CHECK(car.dirty);
    CHECK(car.paint[0] == car.paint[1]);
    CHECK(coney::world_objects::paintOf(car.paint[0]) == CarPaint{153, 102, 51, 255});
    cars.setColour(77, {0, 0, 0, 0}); // not a car: ignored
}

TEST_CASE("removing a door removes its window; the repair puts every part back", "[cars]") {
    Cars cars;
    REQUIRE(cars.spawn("car_wagon", Vec3{}, Quat{}, 3) != nullptr);
    cars.removePart(3, 18, true);
    CHECK(cars.find(3)->removedParts == ((1U << 18) | (1U << 19)));
    CHECK(cars.find(3)->removedKept == (1U << 18));
    cars.removePart(3, 18, false);
    CHECK(cars.find(3)->removedParts == (1U << 19));
    cars.removePart(3, 5, true);
    cars.find(3)->openParts = 1U << 4;
    cars.repair(3);
    CHECK(cars.find(3)->removedParts == 0);
    CHECK(cars.find(3)->openParts == 0);
    CHECK(coney::world_objects::linkedCarPart(14) == 15U);
    CHECK_FALSE(coney::world_objects::linkedCarPart(22).has_value());
}

TEST_CASE("the police car brings its lights", "[cars]") {
    coney::effects::ParticleSystems particles;
    Cars cars;
    cars.setParticles(&particles);
    REQUIRE(cars.spawn("car_copcar", Vec3{5, 5, 0}, Quat{}, 8) != nullptr);
    CHECK(cars.find(8)->lights);
    REQUIRE(particles.systems().size() == 1);
    CHECK(particles.systems().front().type->name == "part_copcar_lights");
    REQUIRE(cars.spawn("car_van", Vec3{}, Quat{}, 9) != nullptr);
    CHECK(particles.systems().size() == 1);
}

TEST_CASE("a stereo is put in, freed by a broken window, then taken once", "[cars]") {
    Cars cars;
    REQUIRE(cars.spawn("car_coupe", Vec3{0, 0, 0}, Quat{}, 4) != nullptr);
    CHECK_FALSE(cars.freeStereo(4));
    cars.spawnRadio(4);
    CHECK(cars.find(4)->stereo == StereoState::InCar);
    CHECK_FALSE(cars.takeStereo(4));
    CHECK(cars.freeStereo(4));
    CHECK(cars.takeStereo(4));
    CHECK(cars.find(4)->stereo == StereoState::Taken);
    CHECK_FALSE(cars.takeStereo(4));
    CHECK(Cars::stereoPosition(*cars.find(4)).z > 0.0F);
}

TEST_CASE("a hit sends sparks off a solid part and each part's first-hit effect once", "[cars]") {
    // Expected: cars.md#hit-effects, confirmed (code)
    using coney::world_objects::CarHitEffectKind;
    Cars cars;
    REQUIRE(cars.spawn("car_osedan", Vec3{}, Quat{}, 4) != nullptr);
    // Standing at the front-left door: the first square breaks window 15 only, a small shatter on the atomic's frame
    // (its first row, negated), and no sparks (glass only).
    const Vec3 standing{-1.6F, 0.5F, 0};
    const Vec3 point{-0.6F, 0.5F, 1.5F};
    REQUIRE(cars.humanHit(4, standing, point) == (1U << 15U));
    auto effects = cars.takeHitEffects();
    REQUIRE(effects.size() == 1);
    CHECK(effects[0].kind == CarHitEffectKind::SmallShatter);
    CHECK(effects[0].part == 15);
    CHECK(effects[0].fromAtomic);
    CHECK(effects[0].atomic == 40);
    CHECK(effects[0].frameRow == 0);
    CHECK(effects[0].rowSign == -1.0F);
    CHECK(cars.takeHitEffects().empty());
    // The door next: sparks at the point and the door's own first hit (kind 0 too); a second hit sparks only.
    REQUIRE(cars.humanHit(4, standing, point) == (1U << 14U));
    effects = cars.takeHitEffects();
    REQUIRE(effects.size() == 2);
    CHECK(effects[0].kind == CarHitEffectKind::Sparks);
    CHECK(effects[0].position.z == 1.5F);
    CHECK(effects[1].kind == CarHitEffectKind::Sparks);
    CHECK(effects[1].part == 14);
    REQUIRE(cars.humanHit(4, standing, point) == (1U << 14U));
    CHECK(cars.takeHitEffects().size() == 1);
    // The bonnet's first hit is steam at its point in the car's frame, upward.
    const auto steam = coney::world_objects::carBreakWindowEffect(*cars.find(4), 4, point);
    CHECK(steam.kind == CarHitEffectKind::Steam);
    CHECK(std::fabs(steam.position.y - 2.741F) < 1e-4F);
    CHECK(steam.direction.z == 1.0F);
    // The right windows burst along the first row, the front and rear glass along the second.
    CHECK(coney::world_objects::carBreakWindowEffect(*cars.find(4), 17, point).rowSign == 1.0F);
    CHECK(coney::world_objects::carBreakWindowEffect(*cars.find(4), 7, point).kind == CarHitEffectKind::LargeShatter);
    CHECK(coney::world_objects::carBreakWindowEffect(*cars.find(4), 7, point).frameRow == 1);
    CHECK(coney::world_objects::carBreakWindowEffect(*cars.find(4), 7, point).rowSign == -1.0F);
    // The explosion sends nothing.
    CHECK(cars.explode(4));
    CHECK(cars.takeHitEffects().empty());
}

TEST_CASE("a stereo is drawn in its car until a theft takes it", "[cars]") {
    Cars cars;
    REQUIRE(cars.spawn("car_osedan", Vec3{10, 20, 1}, Quat{}, 4) != nullptr);
    REQUIRE(cars.spawn("car_osedan", Vec3{0, 0, 1}, Quat{}, 5) != nullptr);
    CHECK(coney::world_objects::stereoDraws(cars).empty());
    cars.spawnRadio(4);
    auto draws = coney::world_objects::stereoDraws(cars);
    REQUIRE(draws.size() == 1);
    CHECK(draws[0].modelHash == coney::crc32("dyn_carstereo"));
    CHECK(draws[0].handle == 4.25);
    // The car's position + (-0.75, 0.25, 0.1), unturned.
    CHECK(std::fabs(draws[0].position.x - 9.25F) < 1e-4F);
    CHECK(std::fabs(draws[0].position.y - 20.25F) < 1e-4F);
    CHECK(std::fabs(draws[0].position.z - 1.1F) < 1e-4F);
    CHECK(cars.freeStereo(4));
    CHECK(coney::world_objects::stereoDraws(cars).size() == 1);
    CHECK(cars.takeStereo(4));
    CHECK(coney::world_objects::stereoDraws(cars).empty());
}

TEST_CASE("a boot's item is released when the boot is knocked off, not when removed or blown off", "[cars]") {
    Cars cars;
    coney::world_objects::SpawnRecords records;
    double next = 100;
    cars.setObjects(&records, [&next] { return next++; });
    REQUIRE(cars.spawn("car_coupe", Vec3{10, 20, 0}, Quat{}, 1) != nullptr);
    REQUIRE(cars.spawn("car_coupe", Vec3{30, 20, 0}, Quat{}, 2) != nullptr);
    REQUIRE(cars.spawn("car_coupe", Vec3{50, 20, 0}, Quat{}, 3) != nullptr);
    // Car 1 hides a pipe: the record is pinned and moved to the boot on the third weapon hit (0.34 each).
    REQUIRE(records.add(coney::world_objects::SpawnRecord{.handle = 50,
                                                          .typeName = "dyn_pipe_a",
                                                          .shownMessage = std::nullopt,
                                                          .model = std::nullopt,
                                                          .physicsBody = std::nullopt,
                                                          .breakIn = std::nullopt}) != nullptr);
    cars.placeInTrunk(1, 50, 0);
    CHECK(records.find(50)->pinned);
    CHECK(cars.find(1)->trunkLoaded);
    CHECK_FALSE(cars.damagePart(1, coney::world_objects::kBootPart, 0.34F, false));
    CHECK_FALSE(cars.damagePart(1, coney::world_objects::kBootPart, 0.34F, false));
    CHECK(cars.damagePart(1, coney::world_objects::kBootPart, 0.34F, false));
    CHECK_FALSE(cars.find(1)->trunkLoaded);
    const Vec3 boot = Cars::bootPosition(*cars.find(1));
    CHECK(records.find(50)->position == std::array<float, 3>{boot.x, boot.y, boot.z});
    // Off already: no more damage.
    CHECK_FALSE(cars.damagePart(1, coney::world_objects::kBootPart, 1.0F, false));
    // Car 2 hides $5: knocked off, a dyn_money record holding it appears at the boot.
    cars.placeInTrunk(2, 0, 5);
    CHECK(cars.damagePart(2, coney::world_objects::kBootPart, 0.0F, false) == false);
    CHECK(cars.damagePart(2, coney::world_objects::kBootPart, 1.0F, false));
    REQUIRE(records.find(100) != nullptr);
    CHECK(records.find(100)->typeName == coney::world_objects::kMoneyPickup);
    CHECK(records.find(100)->money == 5);
    // Car 3's $5 is lost when the car blows up (instant), and CarRemovePart releases nothing.
    cars.placeInTrunk(3, 0, 5);
    cars.removePart(3, coney::world_objects::kBootPart, true);
    CHECK(cars.find(3)->trunkLoaded);
    CHECK_FALSE(cars.damagePart(3, coney::world_objects::kBootPart, 1.0F, false)); // gone already: hits skip it
    cars.placeInTrunk(2, 0, 5);
    CHECK(cars.damagePart(2, 4, 0.0F, true));
    CHECK(records.all().size() == 2);
}

TEST_CASE("an explosion knocks every part off once and loses a loaded boot's item", "[cars]") {
    // Expected: cars.md#explode, confirmed (code): parts 1-29, never the body.
    Cars cars;
    coney::world_objects::SpawnRecords records;
    double next = 100;
    cars.setObjects(&records, [&next] { return next++; });
    REQUIRE(cars.spawn("car_coupe", Vec3{10, 20, 0}, Quat{}, 1) != nullptr);
    cars.placeInTrunk(1, 0, 5);
    CHECK(cars.explode(1));
    const coney::world_objects::Car& car = *cars.find(1);
    CHECK(car.exploded);
    CHECK(car.removedKept == 0x3ffffffeU);
    // Blown off, the boot's money is lost: no pickup is made.
    CHECK(records.all().empty());
    // Once only; an unknown car does nothing.
    CHECK_FALSE(cars.explode(1));
    CHECK_FALSE(cars.explode(9));
}

namespace {

// A spawn record of a pipe named `handle`: the object a test hides in a boot.
coney::world_objects::SpawnRecord pipeRecord(double handle) {
    return coney::world_objects::SpawnRecord{.handle = handle,
                                             .typeName = "dyn_pipe_a",
                                             .shownMessage = std::nullopt,
                                             .model = std::nullopt,
                                             .physicsBody = std::nullopt,
                                             .breakIn = std::nullopt};
}

// The mask of one part.
constexpr std::uint32_t partBit(std::uint32_t part) { return 1U << part; }

} // namespace

TEST_CASE("the repair clears the damage, the kept and first-hit bits and the exploded flag", "[cars]") {
    // Expected: cars.md#parts, confirmed (code): Car_ResetParts clears every part record and the masks.
    Cars cars;
    REQUIRE(cars.spawn("car_osedan", Vec3{}, Quat{}, 1) != nullptr);
    static_cast<void>(cars.damagePart(1, 10, 0.4F, false));
    CHECK(cars.explode(1));
    cars.repair(1);
    const Car& car = *cars.find(1);
    CHECK(car.removedParts == 0);
    CHECK(car.removedKept == 0);
    CHECK(car.firstHits == 0);
    CHECK_FALSE(car.exploded);
    CHECK(car.damage.at(10) == 0.0F);
    // Good as new, it can blow up again.
    CHECK(cars.explode(1));
}

TEST_CASE("dollars put in a boot after an object leave the object, which the release still moves", "[cars]") {
    // Expected: cars.md#parts, confirmed (code): Car_SetTrunkItemKind sets only the dollars and the loaded flag.
    Cars cars;
    coney::world_objects::SpawnRecords records;
    double next = 100;
    cars.setObjects(&records, [&next] { return next++; });
    REQUIRE(cars.spawn("car_osedan", Vec3{10, 20, 0}, Quat{}, 1) != nullptr);
    REQUIRE(records.add(pipeRecord(50)) != nullptr);
    cars.placeInTrunk(1, 50, 0);
    cars.placeInTrunk(1, 0, 7);
    CHECK(cars.find(1)->trunkObject == 50);
    CHECK(cars.find(1)->trunkMoney == 7);
    CHECK(cars.damagePart(1, coney::world_objects::kBootPart, 1.0F, false));
    // The object is preferred: it goes to the boot and no money pickup is made.
    const Vec3 boot = Cars::bootPosition(*cars.find(1));
    CHECK(records.find(50)->position == std::array<float, 3>{boot.x, boot.y, boot.z});
    CHECK(records.find(100) == nullptr);
}

TEST_CASE("an object put in a boot after dollars keeps the dollars", "[cars]") {
    // Expected: cars.md#parts, confirmed (code): Car_SetTrunkObject pins and stores the object and sets loaded only.
    Cars cars;
    coney::world_objects::SpawnRecords records;
    cars.setObjects(&records, [] { return 100.0; });
    REQUIRE(cars.spawn("car_osedan", Vec3{}, Quat{}, 1) != nullptr);
    REQUIRE(records.add(pipeRecord(50)) != nullptr);
    cars.placeInTrunk(1, 0, 7);
    cars.placeInTrunk(1, 50, 0);
    CHECK(cars.find(1)->trunkObject == 50);
    CHECK(cars.find(1)->trunkMoney == 7);
    CHECK(records.find(50)->pinned);
}

TEST_CASE("CarPlaceInTrunkOnDetach with a handle that names no object loads nothing", "[cars]") {
    // Expected: cars.md#parts, confirmed (code): with item kind 0 the call acts only when the object resolves.
    Cars cars;
    coney::world_objects::SpawnRecords records;
    cars.setObjects(&records, [] { return 100.0; });
    REQUIRE(cars.spawn("car_osedan", Vec3{}, Quat{}, 1) != nullptr);
    cars.placeInTrunk(1, 50, 0);
    CHECK_FALSE(cars.find(1)->trunkLoaded);
    CHECK(cars.find(1)->trunkObject == 0);
}

TEST_CASE("the boot's release spends its dollars, and forgets an object that is gone", "[cars]") {
    // Expected: cars.md#parts, confirmed (code): Car_ReleaseTrunkItem clears the dollars once the pickup is made, and
    // the object's handle when it no longer names an object.
    Cars cars;
    coney::world_objects::SpawnRecords records;
    double next = 100;
    cars.setObjects(&records, [&next] { return next++; });
    REQUIRE(cars.spawn("car_osedan", Vec3{}, Quat{}, 1) != nullptr);
    REQUIRE(cars.spawn("car_osedan", Vec3{20, 0, 0}, Quat{}, 2) != nullptr);
    cars.placeInTrunk(1, 0, 7);
    CHECK(cars.damagePart(1, coney::world_objects::kBootPart, 1.0F, false));
    REQUIRE(records.find(100) != nullptr);
    CHECK(cars.find(1)->trunkMoney == 0);
    // Car 2's object is destroyed before the boot comes off.
    REQUIRE(records.add(pipeRecord(50)) != nullptr);
    cars.placeInTrunk(2, 50, 0);
    CHECK(records.destroy(50));
    CHECK(cars.damagePart(2, coney::world_objects::kBootPart, 1.0F, false));
    CHECK(cars.find(2)->trunkObject == 0);
    CHECK_FALSE(cars.find(2)->trunkLoaded);
}

TEST_CASE("a door knocked off by damage takes its window off with it; blown off, it does not", "[cars]") {
    // Expected: cars.md#drawn, confirmed (code): Car_AddPartDamage gives a door's window full damage on a non-instant
    // call.
    Cars cars;
    REQUIRE(cars.spawn("car_osedan", Vec3{}, Quat{}, 1) != nullptr);
    cars.spawnRadio(1);
    CHECK(cars.damagePart(1, 14, 1.0F, false));
    const Car& car = *cars.find(1);
    CHECK((car.removedKept & partBit(15)) != 0);
    CHECK(car.damage.at(15) == 1.0F);
    // Window 15 coming off this way frees the stereo too.
    CHECK(car.stereo == StereoState::Freed);
    REQUIRE(cars.spawn("car_osedan", Vec3{10, 0, 0}, Quat{}, 2) != nullptr);
    CHECK(cars.damagePart(2, 18, 1.0F, true));
    CHECK((cars.find(2)->removedKept & partBit(19)) == 0);
}

TEST_CASE("the parts their record marks come off at the first damage: the roof, glass 6 and 7, lamps 8 and 9",
          "[cars]") {
    // Expected: cars.md#type-tables, confirmed (code): the part record's byte +0x01 bit 0x10; on the van also the boot
    // and side panels 12 and 13.
    Cars cars;
    REQUIRE(cars.spawn("car_osedan", Vec3{}, Quat{}, 1) != nullptr);
    for (const std::uint32_t part : {1U, 6U, 7U, 8U, 9U}) {
        CHECK(cars.damagePart(1, part, 0.115F, false));
    }
    // A bumper takes the damage as it comes.
    CHECK_FALSE(cars.damagePart(1, 2, 0.115F, false));
    REQUIRE(cars.spawn("car_van", Vec3{}, Quat{}, 2) != nullptr);
    CHECK(cars.damagePart(2, 5, 0.115F, false));
    CHECK(cars.damagePart(2, 12, 0.115F, false));
    CHECK_FALSE(cars.damagePart(2, 10, 0.115F, false));
}

TEST_CASE("the roof coming off ends the police car's lights", "[cars]") {
    // Expected: cars.md#hit-volumes, confirmed (code): Car_AddPartDamage ends +0x1210 when part 1 comes off.
    coney::effects::ParticleSystems particles;
    Cars cars;
    cars.setParticles(&particles);
    REQUIRE(cars.spawn("car_copcar", Vec3{5, 5, 0}, Quat{}, 8) != nullptr);
    REQUIRE(particles.systems().size() == 1);
    CHECK(cars.damagePart(8, 1, 0.34F, false));
    CHECK_FALSE(cars.find(8)->lights);
    CHECK(particles.systems().empty());
}

TEST_CASE("an explosion marks as hit only the parts not off yet, removes the lamps and destroys the stereo", "[cars]") {
    // Expected: cars.md#explode, confirmed (code)
    Cars cars;
    REQUIRE(cars.spawn("car_osedan", Vec3{}, Quat{}, 1) != nullptr);
    cars.spawnRadio(1);
    cars.removePart(1, coney::world_objects::kBootPart, true);
    CHECK(cars.explode(1));
    const Car& car = *cars.find(1);
    CHECK((car.firstHits & partBit(coney::world_objects::kBootPart)) == 0);
    CHECK((car.firstHits & partBit(0)) == 0);
    CHECK((car.firstHits & partBit(29)) != 0);
    // The lamps' bits 26-29 go straight into the removed parts.
    CHECK((car.removedParts & 0x3c000000U) == 0x3c000000U);
    CHECK(car.damage.at(0) == 0.0F);
    CHECK(car.stereo == StereoState::Destroyed);
}

TEST_CASE("a quiet explosion hides the parts a blast would throw", "[cars]") {
    // Expected: cars.md#drawn, confirmed (code): the parts of record flag 0x04 go into +0x11f0 when quiet.
    Cars cars;
    REQUIRE(cars.spawn("car_osedan", Vec3{}, Quat{}, 1) != nullptr);
    CHECK(cars.explode(1, true));
    constexpr std::uint32_t kThrown =
        partBit(4) | partBit(5) | partBit(14) | partBit(16) | partBit(18) | partBit(20) | (0xfU << 22U);
    CHECK(cars.find(1)->removedParts == (kThrown | 0x3c000000U));
}
