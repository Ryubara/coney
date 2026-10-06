// SPDX-License-Identifier: GPL-3.0-or-later
// The flag network (docs/references/bindings/world.md#flagnetaddlink) and the spawn records' zones, show/hide and
// destroy (docs/research/objects.md#dynamic-objects). Synthetic handles.
#include "world_objects/flag_net.h"

#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "world_objects/spawn_records.h"

using coney::world_objects::FlagNet;
using coney::world_objects::FlagNetNode;
using coney::world_objects::SpawnRecord;
using coney::world_objects::SpawnRecords;

TEST_CASE("the flag network keeps 128 nodes and their non-nil links", "[flag_net]") {
    FlagNet net;
    REQUIRE(net.add(FlagNetNode{.flag = 10, .links = {11, 0, 12, 0}}));
    CHECK(net.neighbours(10) == std::vector<double>{11, 12});
    CHECK(net.neighbours(11).empty());
    CHECK(net.node(99) == nullptr);
    for (int i = 1; i < 128; ++i) {
        REQUIRE(net.add(FlagNetNode{.flag = 100.0 + i}));
    }
    CHECK_FALSE(net.add(FlagNetNode{.flag = 5}));
    CHECK(net.all().size() == FlagNet::kCapacity);
    net.clear();
    CHECK(net.all().empty());
}

TEST_CASE("zone 0 is on at a level's start and the others off until enabled", "[spawn_records]") {
    SpawnRecords records;
    CHECK(records.zoneEnabled(0));
    CHECK_FALSE(records.zoneEnabled(26));
    records.setZoneEnabled(26, true);
    CHECK(records.zoneEnabled(26));
    records.setZoneEnabled(26, false);
    CHECK_FALSE(records.zoneEnabled(26));
    records.setZoneEnabled(5000, true); // outside the mask: ignored
    CHECK_FALSE(records.zoneEnabled(5000));
    records.setZoneEnabled(3, true);
    records.clear();
    CHECK(records.zoneEnabled(0));
    CHECK_FALSE(records.zoneEnabled(3));
}

TEST_CASE("a destroyed record is gone for good", "[spawn_records]") {
    SpawnRecords records;
    REQUIRE(records.add(SpawnRecord{.handle = 4, .typeName = "dyn_s_box"}) != nullptr);
    REQUIRE(records.resolve(4) != nullptr);
    CHECK(records.destroy(4));
    CHECK_FALSE(records.find(4)->live);
    CHECK(records.resolve(4) == nullptr);
    CHECK_FALSE(records.destroy(4));
    CHECK_FALSE(records.destroy(9));
}
