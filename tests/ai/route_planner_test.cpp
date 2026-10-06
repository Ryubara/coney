// SPDX-License-Identifier: GPL-3.0-or-later
// The route planner (docs/research/ai.md#path-planning): the straight line first, the ends' nodes, A* with its edge
// costs, cap and node limit, the retry and the jump detour, the route's shortcuts and the pool with its use counts;
// and the follower's waypoints (docs/research/ai.md#route-follow). Synthetic path data only.
#include "ai/route_planner.h"

#include <cmath>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "ai/route_follower.h"
#include "support/path_fixtures.h"

using coney::ai::MoveFailure;
using coney::ai::RoutePlanner;
using coney::anim::Vec3;
using coney::world::PathEdge;

namespace {

// The nodes of a request's route; empty when it planned none.
std::vector<std::uint32_t> nodesOf(const coney::ai::RoutePlan& plan) {
    if (!plan.route) {
        return {};
    }
    return {plan.route->nodes().begin(), plan.route->nodes().end()};
}

// A straight corridor [0, 2 × count] × [0, 2] with `count` nodes one every 2 m along it, each linked to the next.
coney::world::PathMap corridorChain(std::uint32_t count) {
    coney::test::PathBuilder builder;
    const std::uint32_t corridor = builder.rectangle(0.0F, 2.0F * static_cast<float>(count), 0.0F, 2.0F);
    for (std::uint32_t n = 0; n < count; ++n) {
        builder.node(corridor, 1.0F + 2.0F * static_cast<float>(n), 1.0F);
    }
    for (std::uint32_t n = 0; n + 1 < count; ++n) {
        builder.link(n, n + 1);
    }
    return builder.build();
}

} // namespace

TEST_CASE("a request in a straight walkable line needs no route", "[ai][routes]") {
    const coney::world::PathMap map = coney::test::uCorridors();
    RoutePlanner planner(map);
    auto plan = planner.request({1.0F, 1.0F, 0.0F}, {9.0F, 1.5F, 0.0F});
    REQUIRE(plan.has_value());
    CHECK_FALSE(plan->route.has_value());
    CHECK(planner.routesInUse() == 0);
}

TEST_CASE("a route round the U skips the nodes the ends reach in a straight line", "[ai][routes]") {
    const coney::world::PathMap map = coney::test::uCorridors();
    RoutePlanner planner(map);
    {
        auto plan = planner.request({1.0F, 1.0F, 0.0F}, {1.0F, 9.0F, 0.0F});
        REQUIRE(plan.has_value());
        // Node 0 is skipped (the start reaches node 1), node 4 dropped (node 3 reaches the destination).
        CHECK(nodesOf(*plan) == std::vector<std::uint32_t>{1, 2, 3});
        CHECK(planner.routesInUse() == 1);
        CHECK(planner.uses(2) == 1);
        CHECK(planner.uses(0) == 0);
    }
    // The route freed: its place and its uses given back.
    CHECK(planner.routesInUse() == 0);
    CHECK(planner.uses(2) == 0);
}

TEST_CASE("a request fails off the polygons, between polygons off the graph, and when the pool is full",
          "[ai][routes]") {
    const coney::world::PathMap map = coney::test::uCorridors();
    RoutePlanner planner(map);
    CHECK(planner.request({-5.0F, 1.0F, 0.0F}, {1.0F, 9.0F, 0.0F}).error() == MoveFailure::NoRoute);
    CHECK(planner.request({1.0F, 1.0F, 0.0F}, {1.0F, 15.0F, 0.0F}).error() == MoveFailure::NoRoute);
    // Just off a polygon still counts as on it.
    CHECK(planner.request({1.0F, -0.5F, 0.0F}, {1.0F, 9.0F, 0.0F}).has_value());

    coney::test::PathBuilder builder;
    const std::uint32_t left = builder.rectangle(0.0F, 2.0F, 0.0F, 2.0F, 1);
    const std::uint32_t right = builder.rectangle(4.0F, 6.0F, 0.0F, 2.0F, 0); // not on the graph
    builder.node(left, 1.0F, 1.0F);
    builder.node(right, 5.0F, 1.0F);
    builder.link(0, 1);
    const coney::world::PathMap islands = builder.build();
    RoutePlanner apart(islands);
    CHECK(apart.request({1.0F, 1.0F, 0.0F}, {5.0F, 1.0F, 0.0F}).error() == MoveFailure::NoRoute);

    std::vector<coney::ai::Route> held;
    for (std::size_t k = 0; k < coney::ai::kRoutePoolSize; ++k) {
        auto plan = planner.request({1.0F, 1.0F, 0.0F}, {1.0F, 9.0F, 0.0F});
        REQUIRE(plan.has_value());
        REQUIRE(plan->route.has_value());
        held.push_back(std::move(plan->route.value()));
    }
    CHECK(planner.routesInUse() == coney::ai::kRoutePoolSize);
    CHECK(planner.request({1.0F, 1.0F, 0.0F}, {1.0F, 9.0F, 0.0F}).error() == MoveFailure::NoRoute);
    held.pop_back();
    CHECK(planner.request({1.0F, 1.0F, 0.0F}, {1.0F, 9.0F, 0.0F}).has_value());
}

TEST_CASE("an edge costs its length in 1/16 m, its flag's extra, the avoid bit and the routes through its node",
          "[ai][routes]") {
    const coney::world::PathMap map = corridorChain(3); // nodes 2 m apart: 32
    const RoutePlanner planner(map);
    CHECK(planner.edgeCost(0, PathEdge{.to = 1, .flags = 1, .avoid = false}, 1) == 32 - 8);
    CHECK(planner.edgeCost(0, PathEdge{.to = 1, .flags = 0x8, .avoid = false}, 0x8) == 32 - 8 + 200);
    CHECK(planner.edgeCost(0, PathEdge{.to = 1, .flags = 0x80, .avoid = false}, 0x80) == 32 - 8 + 320);
    CHECK(planner.edgeCost(0, PathEdge{.to = 1, .flags = 0x4, .avoid = false}, 0x4) == 32 - 8 + 80);
    CHECK(planner.edgeCost(0, PathEdge{.to = 1, .flags = 0x4, .avoid = false}, 0x104) ==
          32 - 8); // 0x100 turns the extras off
    CHECK(planner.edgeCost(0, PathEdge{.to = 1, .flags = 0x10, .avoid = true}, 0x10) == 32 - 8 + 1600);
    const RoutePlanner off(map, coney::ai::PlannerSettings{.extraCosts = false, .keepLeadingNodes = false});
    CHECK(off.edgeCost(0, PathEdge{.to = 1, .flags = 0x8, .avoid = false}, 0x8) == 32 - 8);

    // Each route through a node makes the edges into it dearer by 40, spreading AIs over parallel routes.
    const coney::world::PathMap u = coney::test::uCorridors();
    RoutePlanner busy(u);
    const PathEdge intoSide{.to = 2, .flags = 1, .avoid = false};
    const std::uint32_t idle = busy.edgeCost(1, intoSide, 1);
    auto first = busy.request({1.0F, 1.0F, 0.0F}, {1.0F, 9.0F, 0.0F});
    auto second = busy.request({1.0F, 1.0F, 0.0F}, {1.0F, 9.0F, 0.0F});
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    CHECK(busy.uses(2) == 2);
    CHECK(busy.edgeCost(1, intoSide, 1) == idle + 80);
}

TEST_CASE("the search walks the edges whose flags meet the mask and gives up when its open list outgrows 128",
          "[ai][routes]") {
    const coney::world::PathMap map = corridorChain(200);
    const RoutePlanner planner(map);
    auto far = planner.search(0, 150, 1);
    REQUIRE(far.has_value());
    CHECK(far.value().nodes.size() == 151);
    CHECK(far.value().nodes.front() == 0);
    CHECK(far.value().nodes.back() == 150);
    CHECK(far.value().cost == 150 * (32 - 8));
    CHECK_FALSE(planner.search(0, 5, 2).has_value());     // no edge has flag 2
    CHECK_FALSE(planner.search(0, 5, 1, 50).has_value()); // dearer than the cap

    // A hub with 130 spokes: expanding it would put 130 nodes on the open list.
    coney::test::PathBuilder builder;
    const std::uint32_t area = builder.rectangle(-20.0F, 20.0F, -20.0F, 20.0F);
    builder.node(area, 0.0F, 0.0F);
    for (int spoke = 0; spoke < 130; ++spoke) {
        const float angle = static_cast<float>(spoke) * 0.048F;
        builder.node(area, 10.0F * std::cos(angle), 10.0F * std::sin(angle));
    }
    for (std::uint32_t spoke = 1; spoke <= 130; ++spoke) {
        builder.link(0, spoke);
    }
    const coney::world::PathMap hub = builder.build();
    const RoutePlanner wide(hub);
    CHECK(wide.search(0, 1, 1).has_value());       // searched from the spoke: the hub is found at once
    CHECK_FALSE(wide.search(1, 0, 1).has_value()); // searched from the hub: its 130 spokes overflow the list
}

TEST_CASE("a search that fails tries again with the choke, eight and jump edges", "[ai][routes]") {
    coney::test::PathBuilder builder;
    const std::uint32_t left = builder.rectangle(0.0F, 4.0F, 0.0F, 2.0F);
    const std::uint32_t up = builder.rectangle(3.0F, 4.0F, 0.0F, 6.0F);
    const std::uint32_t top = builder.rectangle(0.0F, 4.0F, 4.0F, 6.0F);
    builder.node(left, 1.0F, 1.0F);
    builder.node(left, 3.5F, 1.0F);
    builder.node(up, 3.5F, 3.0F);
    builder.node(top, 3.5F, 5.0F);
    builder.node(top, 1.0F, 5.0F);
    builder.link(0, 1);
    builder.link(1, 2, coney::ai::edge_flag::kChoke);
    builder.link(2, 3);
    builder.link(3, 4);
    const coney::world::PathMap map = builder.build();
    RoutePlanner planner(map);
    CHECK_FALSE(planner.search(0, 4, coney::ai::edge_flag::kDefaultMask).has_value());
    auto plan = planner.request({0.5F, 1.0F, 0.0F}, {0.5F, 5.0F, 0.0F});
    REQUIRE(plan.has_value());
    CHECK(nodesOf(*plan) == std::vector<std::uint32_t>{1, 2, 3});
}

TEST_CASE("a route over a jump edge is kept when no way without one is cheaper", "[ai][routes]") {
    coney::test::PathBuilder builder;
    const std::uint32_t low = builder.rectangle(0.0F, 2.0F, 0.0F, 2.0F);
    const std::uint32_t high = builder.rectangle(0.0F, 2.0F, 4.0F, 6.0F);
    builder.node(low, 1.0F, 1.0F);
    builder.node(high, 1.0F, 5.0F);
    builder.link(0, 1, coney::ai::edge_flag::kJump);
    const coney::world::PathMap map = builder.build();
    RoutePlanner planner(map);
    auto plan = planner.request({1.0F, 0.5F, 0.0F}, {1.0F, 5.5F, 0.0F});
    REQUIRE(plan.has_value());
    CHECK(nodesOf(*plan) == std::vector<std::uint32_t>{0, 1});
    CHECK(plan->route.value().cost() == 64 - 8 + 320);
}

TEST_CASE("a route shortcuts to a node up to four ahead that links back", "[ai][routes]") {
    // A zigzag where node 4 links straight back to node 0, but no straight line skips it.
    coney::test::PathBuilder builder;
    const std::uint32_t lane = builder.rectangle(0.0F, 20.0F, 0.0F, 1.0F);
    const std::uint32_t far = builder.rectangle(19.0F, 20.0F, 0.0F, 10.0F);
    for (int n = 0; n < 5; ++n) {
        builder.node(lane, 1.0F + 4.0F * static_cast<float>(n), 0.5F);
    }
    builder.node(far, 19.5F, 9.0F);
    for (std::uint32_t n = 0; n < 4; ++n) {
        builder.link(n, n + 1);
    }
    builder.linkOneWay(4, 0, 1, true); // dear: the search takes the chain
    builder.link(4, 5);
    const coney::world::PathMap map = builder.build();
    RoutePlanner planner(map, coney::ai::PlannerSettings{.extraCosts = true, .keepLeadingNodes = true});
    auto search = planner.search(0, 5, 1);
    REQUIRE(search.has_value());
    CHECK(search.value().nodes.size() == 6);
    auto plan = planner.request({0.5F, 0.5F, 0.0F}, {19.5F, 9.5F, 0.0F});
    REQUIRE(plan.has_value());
    // Kept leading node 0, then straight to 4 (it links back to 0); node 5 dropped (4 reaches the destination? no:
    // the lane's corner is in the way), so 5 stays.
    CHECK(nodesOf(*plan) == std::vector<std::uint32_t>{0, 4, 5});
}

TEST_CASE("the follower moves on at each waypoint and skips the ones it reaches in a straight line", "[ai][routes]") {
    const coney::world::PathMap map = coney::test::uCorridors();
    RoutePlanner planner(map);
    auto plan = planner.request({1.0F, 1.0F, 0.0F}, {1.0F, 9.0F, 0.0F});
    REQUIRE(plan.has_value());
    REQUIRE(plan->route.has_value());
    coney::ai::RouteFollower follower(map, std::move(plan->route.value()), {1.0F, 1.0F, 0.0F}, {1.0F, 9.0F, 0.0F});
    CHECK(follower.waypoint(map, {1.0F, 1.0F, 0.0F}) == Vec3{9.0F, 1.0F, 0.0F});
    CHECK(follower.ahead().size() == 4);
    // At node 1: on to node 2, which it reaches, and on past it to node 3 (straight up the side).
    CHECK(follower.waypoint(map, {8.9F, 1.0F, 0.0F}) == Vec3{9.0F, 9.0F, 0.0F});
    CHECK(planner.routesInUse() == 1);
    // At node 3: only the destination is left, and the route is freed.
    CHECK(follower.waypoint(map, {9.0F, 8.9F, 0.0F}) == Vec3{1.0F, 9.0F, 0.0F});
    CHECK(follower.onLastLeg());
    CHECK(planner.routesInUse() == 0);
}
