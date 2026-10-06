// SPDX-License-Identifier: GPL-3.0-or-later
// The parked cars (docs/research/cars.md): type by name, the pool of 18, the paint's packing and reversal, removed
// parts and their links, the repair, the police car's lights and the stereo's states.
#include "world_objects/cars.h"

#include <array>
#include <cstddef>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

#include "core/name_hash.h"
#include "effects/particles.h"
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

TEST_CASE("a boot's item is released when the boot is knocked off, not when removed or blown off", "[cars]") {
    Cars cars;
    coney::world_objects::SpawnRecords records;
    double next = 100;
    cars.setObjects(&records, [&next] { return next++; });
    REQUIRE(cars.spawn("car_coupe", Vec3{10, 20, 0}, Quat{}, 1) != nullptr);
    REQUIRE(cars.spawn("car_coupe", Vec3{30, 20, 0}, Quat{}, 2) != nullptr);
    REQUIRE(cars.spawn("car_coupe", Vec3{50, 20, 0}, Quat{}, 3) != nullptr);
    // Car 1 hides a pipe: the record is pinned and moved to the boot on the third weapon hit (0.34 each).
    REQUIRE(records.add(coney::world_objects::SpawnRecord{.handle = 50, .typeName = "dyn_pipe_a"}) != nullptr);
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
