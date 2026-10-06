// SPDX-License-Identifier: GPL-3.0-or-later
// The doors' and panes' changes to the navigation links (docs/research/objects.md#nav-links): retagging and opening a
// door number's links, the polygon flag 8 that goes with an open link, and the nearest link by position.
#include "world_objects/nav_links.h"

#include <optional>

#include <catch2/catch_test_macros.hpp>

#include "support/object_fixtures.h"
#include "world/path_map.h"

using coney::world_objects::FoundLink;
using coney::world_objects::NavLinks;

TEST_CASE("a door number's links are retagged, opened and closed together", "[world_objects][nav]") {
    coney::world::PathMap paths = coney::test::objectPaths();
    NavLinks links(&paths);
    links.setKindByNumber(coney::test::kTestDoorNumber, coney::world_objects::link_kind::kBreakable);
    CHECK(paths.edges()[0].flags == 0x40);
    CHECK(paths.edges()[1].flags == 0x40);
    CHECK(paths.edges()[2].flags == 0x4); // another number's (none) is left alone

    links.closeByNumber(coney::test::kTestDoorNumber);
    CHECK(paths.edges()[0].avoid);
    CHECK(paths.edges()[1].avoid);
    CHECK((paths.polygons()[0].flags & coney::world::kPathPolygonExcluded) == 0);

    // Opening clears the avoid bit and sets flag 8 on each link's starting polygon.
    links.openByNumber(coney::test::kTestDoorNumber);
    CHECK_FALSE(paths.edges()[0].avoid);
    CHECK_FALSE(paths.edges()[1].avoid);
    CHECK((paths.polygons()[0].flags & coney::world::kPathPolygonExcluded) != 0);
    CHECK((paths.polygons()[1].flags & coney::world::kPathPolygonExcluded) != 0);
    CHECK((paths.polygons()[2].flags & coney::world::kPathPolygonExcluded) == 0);
}

TEST_CASE("the nearest link of a kind within 5 m comes with its reverse", "[world_objects][nav]") {
    coney::world::PathMap paths = coney::test::objectPaths();
    const NavLinks links(&paths);
    const std::optional<FoundLink> nearest = links.findNearest({4.0F, 2.5F, 0.0F}, 0x10);
    REQUIRE(nearest.has_value());
    const FoundLink door = nearest.value_or(FoundLink{});
    CHECK(door.link == 0);
    CHECK(door.back == 1);
    CHECK_FALSE(links.findNearest({4.0F, 2.5F, 0.0F}, 0x40).has_value());  // no link of that kind
    CHECK_FALSE(links.findNearest({4.0F, 30.0F, 0.0F}, 0x10).has_value()); // too far
}

TEST_CASE("door links by position: disable, enable and convert a jump", "[world_objects][nav]") {
    coney::world::PathMap paths = coney::test::objectPaths();
    NavLinks links(&paths);
    links.disableNear({4.0F, 2.0F, 0.0F});
    CHECK(paths.edges()[0].avoid);
    CHECK(paths.edges()[1].avoid);
    links.enableNear({4.0F, 2.0F, 0.0F});
    CHECK_FALSE(paths.edges()[0].avoid);
    links.convertJumpToDoor({4.0F, 12.0F, 0.0F});
    CHECK(paths.edges()[2].flags == 0x10);
    CHECK(paths.edges()[3].flags == 0x10);
}

TEST_CASE("links over no path map change nothing", "[world_objects][nav]") {
    NavLinks links(nullptr);
    links.openByNumber(1);
    links.disableNear({});
    CHECK_FALSE(links.findNearest({}, 0xffff).has_value());
    CHECK_FALSE(links.polygonAt({}).has_value());
}
