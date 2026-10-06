// SPDX-License-Identifier: GPL-3.0-or-later
// The triangle pick-up search and its clip (docs/research/combat.md#breakables), and a level's placed objects file
// (docs/research/objects.md#objs-file) read into spawn records.
#include "world_objects/pickups.h"

#include <array>
#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "animation/anim_math.h"
#include "world_objects/placed_objects_file.h"
#include "world_objects/spawn_records.h"

using coney::anim::Vec3;
namespace wo = coney::world_objects;

namespace {

constexpr Vec3 kFeet{10.0F, 10.0F, 0.0F};
constexpr Vec3 kFacingY{0.0F, 1.0F, 0.0F};

// A pickable candidate at `position` with `handle`.
wo::PickupCandidate item(double handle, Vec3 position) {
    return wo::PickupCandidate{.handle = handle, .position = position, .pickable = true};
}

} // namespace

TEST_CASE("only pickup items are picked up by triangle", "[pickups]") {
    CHECK(wo::pickableClass("pickup_item"));
    CHECK_FALSE(wo::pickableClass("powerup_item"));
    CHECK_FALSE(wo::pickableClass("simple_object"));
}

TEST_CASE("the search prefers an item ahead, then beside, then behind; the first among equals", "[pickups]") {
    const std::vector<wo::PickupCandidate> candidates{
        item(1, Vec3{10.0F, 9.0F, 0.5F}),  // behind: 1
        item(2, Vec3{11.0F, 10.0F, 0.5F}), // beside, a little ahead of the point behind the feet: 2
        item(3, Vec3{10.2F, 11.0F, 0.5F}), // ahead: 3
        item(4, Vec3{9.8F, 11.0F, 0.5F}),  // ahead too, but after 3
    };
    CHECK(wo::searchPickup(kFeet, kFacingY, candidates, {}) == std::optional<std::size_t>(2));
    CHECK(wo::searchPickup(kFeet, kFacingY, std::span(candidates).first(2), {}) == std::optional<std::size_t>(1));
    CHECK(wo::searchPickup(kFeet, kFacingY, std::span(candidates).first(1), {}) == std::optional<std::size_t>(0));
}

TEST_CASE("the search skips what is out of reach, not pickable or out of sight from both heights", "[pickups]") {
    std::vector<wo::PickupCandidate> candidates{item(1, Vec3{10.0F, 11.6F, 0.5F})};
    CHECK_FALSE(wo::searchPickup(kFeet, kFacingY, candidates, {}).has_value());
    candidates = {item(1, Vec3{10.0F, 11.4F, 0.5F})};
    candidates[0].pickable = false;
    CHECK_FALSE(wo::searchPickup(kFeet, kFacingY, candidates, {}).has_value());
    candidates[0].pickable = true;
    // Blocked from the waist only: the ray from above the head sees it.
    std::vector<float> heights;
    const wo::SightBlocked waistBlocked = [&heights](Vec3 from, Vec3 /*to*/) {
        heights.push_back(from.z);
        return from.z < 1.5F;
    };
    CHECK(wo::searchPickup(kFeet, kFacingY, candidates, waistBlocked) == std::optional<std::size_t>(0));
    CHECK(heights == std::vector<float>{wo::kPickupSightLow, wo::kPickupSightHigh});
    const wo::SightBlocked allBlocked = [](Vec3, Vec3) { return true; };
    CHECK_FALSE(wo::searchPickup(kFeet, kFacingY, candidates, allBlocked).has_value());
}

TEST_CASE("an item up to 0.8 m above the feet is picked up low, higher high", "[pickups]") {
    CHECK(wo::pickupClip(0.3F) == wo::kPickupLowClip);
    CHECK(wo::pickupClip(0.8F) == wo::kPickupLowClip);
    CHECK(wo::pickupClip(1.54F) == wo::kPickupHighClip);
}

TEST_CASE("a placed objects file reads into records, emitters left out", "[pickups][placed_objects]") {
    const std::string_view text = "3\r\n"
                                  "dyn_watch {1.5, 2.25, -0.5}, {0, 0, 0.7071, 0.7071}, -1, 26, 4, ffffffff, nil )\r\n"
                                  "part_smoke {0, 0, 0}, {0, 0, 0, 1}, -1, 0, 0, 80ff00ff, nil )\r\n"
                                  "dyn_ring {3, 4, 5}, {0, 0, 0, 1}, -1, 0, 0, 7f7f7f7f, flag_a )\r\n";
    const auto objects = wo::parsePlacedObjects(text);
    REQUIRE(objects.has_value());
    REQUIRE(objects->size() == 3);
    const wo::PlacedObject& watch = (*objects)[0];
    CHECK(watch.name == "dyn_watch");
    CHECK(watch.position == std::array<float, 3>{1.5F, 2.25F, -0.5F});
    CHECK(watch.rotation[3] == 0.7071F);
    CHECK(watch.zone == 26);
    CHECK(watch.flags == 4);
    CHECK(watch.tint == 0xffffffffU);
    CHECK(watch.flagName.empty());
    CHECK((*objects)[1].emitter());
    CHECK((*objects)[2].flagName == "flag_a");
    CHECK((*objects)[2].tint == 0x7f7f7f7fU);

    wo::SpawnRecords records;
    double next = 100;
    CHECK(wo::addPlacedObjects(*objects, records, [&next] { return next++; }) == 2);
    REQUIRE(records.all().size() == 2);
    CHECK(records.all()[0].handle == 100);
    CHECK(records.all()[0].typeName == "dyn_watch");
    CHECK(records.all()[0].zone == 26);
    CHECK(records.all()[1].handle == 101);
    CHECK(records.all()[1].flagName == "flag_a");
}

TEST_CASE("a placed objects file with a short line or a bad count does not read", "[pickups][placed_objects]") {
    CHECK_FALSE(wo::parsePlacedObjects("x\n").has_value());
    CHECK_FALSE(wo::parsePlacedObjects("2\ndyn_a {0, 0, 0}, {0, 0, 0, 1}, -1, 0, 0, ff, nil )\n").has_value());
    CHECK_FALSE(wo::parsePlacedObjects("1\ndyn_a {0, 0, 0}, {0, 0, 0, 1}, -1, 0, 0, nil )\n").has_value());
    CHECK_FALSE(wo::parsePlacedObjects("1\ndyn_a {0, 0, z}, {0, 0, 0, 1}, -1, 0, 0, ff, nil )\n").has_value());
}
