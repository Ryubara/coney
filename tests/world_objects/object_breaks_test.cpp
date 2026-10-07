// SPDX-License-Identifier: GPL-3.0-or-later
// BreakObjectsInRadius' destroy message and the molotov breaking itself
// (docs/research/objects.md#break-objects-in-radius, docs/research/script-types.md#molotov).
#include "world_objects/object_breaks.h"

#include <cstddef>
#include <string>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "world_objects/spawn_records.h"

using coney::anim::Vec3;
using coney::world_objects::sendDestroyInRadius;
using coney::world_objects::SpawnRecord;
using coney::world_objects::SpawnRecords;
using coney::world_objects::stepSelfBreaks;

namespace {

// A record of `type` at (x, 0, 1), live or not.
SpawnRecord record(double handle, std::string type, float x, bool live) {
    SpawnRecord made;
    made.handle = handle;
    made.typeName = std::move(type);
    made.position = {x, 0.0F, 1.0F};
    made.live = live;
    return made;
}

constexpr float kStep = 1.0F / 30.0F;

} // namespace

TEST_CASE("a molotov told to break breaks itself 22 ticks later, its flash 0.7 m up", "[world_objects][molotov]") {
    SpawnRecords records;
    records.add(record(1.0, "dyn_molotv", 0.0F, false)); // the centre: told although not live
    records.add(record(2.0, "dyn_molotv", 5.0F, true));  // out of the radius
    CHECK(sendDestroyInRadius(records, Vec3{0.0F, 0.0F, 1.0F}, 0.5F, 1.0) == 1);
    for (int step = 0; step < 10; ++step) {
        CHECK(stepSelfBreaks(records, kStep).empty());
    }
    const auto flashes = stepSelfBreaks(records, kStep); // the 11th step: 22 ticks
    REQUIRE(flashes.size() == 1);
    CHECK_THAT(flashes[0].z, Catch::Matchers::WithinAbs(1.7, 1e-5));
    CHECK(records.find(1.0)->removed);
    CHECK_FALSE(records.find(2.0)->removed);
    CHECK(stepSelfBreaks(records, kStep).empty());
}

TEST_CASE("the destroy message reaches only live objects and the centre; other types do not break",
          "[world_objects][molotov]") {
    SpawnRecords records;
    records.add(record(1.0, "dyn_box", 0.0F, true));
    records.add(record(2.0, "dyn_molotv", 0.2F, false)); // neither live nor the centre
    CHECK(sendDestroyInRadius(records, Vec3{0.0F, 0.0F, 1.0F}, 0.5F, 1.0) == 1);
    for (int step = 0; step < 30; ++step) {
        CHECK(stepSelfBreaks(records, kStep).empty());
    }
    CHECK_FALSE(records.find(1.0)->removed);
}
