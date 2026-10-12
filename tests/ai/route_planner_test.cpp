// SPDX-License-Identifier: GPL-3.0-or-later
// The route planner (docs/research/ai.md#path-planning): the straight line first, the ends' nodes, A* with its edge
// costs, cap and node limit, the retry and the jump detour, the route's shortcuts and the pool with its use counts;
// and the follower's waypoints (docs/research/ai.md#route-follow). Synthetic path data only.
#include "ai/route_planner.h"

#include <cmath>
#include <cstdint>
#include <expected>
#include <optional>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "ai/route_follower.h"
#include "support/path_fixtures.h"

using coney::ai::MoveFailure;
using coney::ai::RoutePlanner;
using coney::ai::SearchFailure;
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

// Why a search failed; nothing when it found a route.
std::optional<SearchFailure> failureOf(const std::expected<coney::ai::RouteSearch, SearchFailure>& found) {
    return found ? std::nullopt : std::optional<SearchFailure>(found.error());
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
        // A local: clang-tidy cannot follow a check through the outer expected.
        std::optional<coney::ai::Route> route = std::move(plan->route);
        REQUIRE(route.has_value());
        if (!route) {
            return;
        }
        held.push_back(std::move(*route));
    }
    CHECK(planner.routesInUse() == coney::ai::kRoutePoolSize);
    CHECK(planner.request({1.0F, 1.0F, 0.0F}, {1.0F, 9.0F, 0.0F}).error() == MoveFailure::NoRoute);
    held.pop_back();
    CHECK(planner.request({1.0F, 1.0F, 0.0F}, {1.0F, 9.0F, 0.0F}).has_value());
}

TEST_CASE("an edge costs its 3D length in 1/16 m, one flag's extra, the avoid bit and the routes through its node",
          "[ai][routes]") {
    const coney::world::PathMap map = corridorChain(3); // nodes 2 m apart: 32
    const RoutePlanner planner(map);
    // Unused nodes add nothing; the three extras are exclusive, 8 before 0x80 before 4.
    CHECK(planner.edgeCost(1, PathEdge{.to = 0, .flags = 1, .avoid = false}, 0xff) == 32);
    CHECK(planner.edgeCost(1, PathEdge{.to = 0, .flags = 0x8, .avoid = false}, 0xff) == 32 + 200);
    CHECK(planner.edgeCost(1, PathEdge{.to = 0, .flags = 0x80, .avoid = false}, 0xff) == 32 + 320);
    CHECK(planner.edgeCost(1, PathEdge{.to = 0, .flags = 0x4, .avoid = false}, 0xff) == 32 + 80);
    CHECK(planner.edgeCost(1, PathEdge{.to = 0, .flags = 0x8c, .avoid = false}, 0xff) == 32 + 200);
    CHECK(planner.edgeCost(1, PathEdge{.to = 0, .flags = 0x84, .avoid = false}, 0xff) == 32 + 320);
    // A charge link (0x40) with its avoid bit takes no choke extra; without the bit it does.
    CHECK(planner.edgeCost(1, PathEdge{.to = 0, .flags = 0x44, .avoid = true}, 0xff) == 32 + 1600);
    CHECK(planner.edgeCost(1, PathEdge{.to = 0, .flags = 0x44, .avoid = false}, 0xff) == 32 + 80);
    CHECK(planner.edgeCost(1, PathEdge{.to = 0, .flags = 0x4, .avoid = false}, 0x1ff) == 32); // 0x100: no extras
    CHECK(planner.edgeCost(1, PathEdge{.to = 0, .flags = 0x10, .avoid = true}, 0xff) == 32 + 1600);
    const RoutePlanner off(map, coney::ai::PlannerSettings{.extraCosts = false, .keepLeadingNodes = false});
    CHECK(off.edgeCost(1, PathEdge{.to = 0, .flags = 0x8, .avoid = false}, 0xff) == 32);

    // The distance is 3D, truncated to whole 1/16 m: sqrt(2) m is 22.6, so 22.
    coney::test::PathBuilder slope;
    const std::uint32_t area = slope.rectangle(-1.0F, 2.0F, -1.0F, 1.0F);
    slope.node(area, 0.0F, 0.0F);
    slope.node(area, 1.0F, 0.0F, 0, 1.0F);
    slope.link(0, 1);
    const coney::world::PathMap slopeMap = slope.build();
    CHECK(RoutePlanner(slopeMap).edgeCost(1, PathEdge{.to = 0, .flags = 1, .avoid = false}, 0xff) == 22);

    // The use term, 40 × uses − 8, is the expanded node's (the one whose record it is: the step enters it).
    const coney::world::PathMap u = coney::test::uCorridors();
    RoutePlanner busy(u);
    auto first = busy.request({1.0F, 1.0F, 0.0F}, {1.0F, 9.0F, 0.0F});
    REQUIRE(first.has_value());
    REQUIRE(busy.uses(1) == 1);
    REQUIRE(busy.uses(0) == 0);
    const PathEdge fromZero{.to = 0, .flags = 1, .avoid = false};
    const PathEdge fromOne{.to = 1, .flags = 1, .avoid = false};
    CHECK(busy.edgeCost(1, fromZero, 0xff) == 112 + 32); // node 1 (7 m from node 0) is used once
    CHECK(busy.edgeCost(0, fromOne, 0xff) == 112);       // node 0 is not
    auto second = busy.request({1.0F, 1.0F, 0.0F}, {1.0F, 9.0F, 0.0F});
    REQUIRE(second.has_value());
    CHECK(busy.edgeCost(1, fromZero, 0xff) == 112 + 72);
}

TEST_CASE("two routes through one node make the next search take a parallel lane", "[ai][routes]") {
    // A yard with a block in the middle: start node 0 on the left, goal node 1 on the right, the short lane over the
    // block through node 2 and the long lane under it through nodes 3 and 4, 2.25 m (36) longer.
    coney::test::PathBuilder builder;
    const std::uint32_t yard = builder.rectangle(0.0F, 20.0F, 0.0F, 10.0F);
    builder.hole(6.0F, 14.0F, 2.5F, 7.5F);
    builder.node(yard, 2.0F, 5.0F);
    builder.node(yard, 18.0F, 5.0F);
    builder.node(yard, 10.0F, 8.5F);
    builder.node(yard, 7.0F, 0.3F);
    builder.node(yard, 13.0F, 0.3F);
    builder.link(0, 2);
    builder.link(2, 1);
    builder.link(0, 3);
    builder.link(3, 4);
    builder.link(4, 1);
    const coney::world::PathMap map = builder.build();
    RoutePlanner planner(map);
    const auto searched = [&planner] {
        const auto found = planner.search(0, 1, 0xff);
        return found ? found->nodes : std::vector<std::uint32_t>{};
    };
    CHECK(searched() == std::vector<std::uint32_t>{0, 2, 1});
    auto first = planner.request({0.5F, 5.0F, 0.0F}, {19.5F, 5.0F, 0.0F});
    REQUIRE(first.has_value());
    CHECK(nodesOf(*first) == std::vector<std::uint32_t>{0, 2, 1});
    // One route through node 2 adds 32: still cheaper than the long lane.
    CHECK(searched() == std::vector<std::uint32_t>{0, 2, 1});
    auto second = planner.request({0.5F, 5.0F, 0.0F}, {19.5F, 5.0F, 0.0F});
    REQUIRE(second.has_value());
    // Two add 72: the long lane is cheaper now.
    CHECK(searched() == std::vector<std::uint32_t>{0, 3, 4, 1});
}

TEST_CASE("the search walks the edges whose flags meet the mask", "[ai][routes]") {
    const coney::world::PathMap map = corridorChain(100);
    const RoutePlanner planner(map);
    auto far = planner.search(0, 99, 1);
    REQUIRE(far.has_value());
    if (!far) {
        return;
    }
    CHECK(far.value().nodes.size() == 100);
    CHECK(far.value().nodes.front() == 0);
    CHECK(far.value().nodes.back() == 99);
    CHECK(far.value().cost == 99 * planner.edgeCost(1, PathEdge{.to = 0, .flags = 1, .avoid = false}, 1));
    CHECK(failureOf(planner.search(0, 5, 2)) == SearchFailure::NoWay); // no edge has flag 2
}

TEST_CASE("the search holds 1000 nodes open and 3000 closed and fails a route of more than 128 nodes",
          "[ai][routes]") {
    // A route holds at most 128 nodes: a corridor of 128 is found, one of 129 or 140 fails however few nodes it opens.
    const coney::world::PathMap corridor = corridorChain(140);
    const RoutePlanner planner(corridor);
    auto longest = planner.search(0, 127, 1);
    REQUIRE(longest.has_value());
    if (!longest) {
        return;
    }
    CHECK(longest.value().nodes.size() == 128);
    CHECK(failureOf(planner.search(0, 128, 1)) == SearchFailure::TooLong);
    CHECK(failureOf(planner.search(0, 139, 1)) == SearchFailure::TooLong);

    // A 30 × 20 grid of 600 nodes 1 m apart, cut by a wall at x 14.5 open only at the far end (y 19): the way round
    // opens and closes far more than 128 nodes, and the route itself is short enough.
    coney::test::PathBuilder grid;
    const std::uint32_t yard = grid.rectangle(-1.0F, 30.0F, -1.0F, 20.0F);
    constexpr std::uint32_t kWidth = 30;
    constexpr std::uint32_t kHeight = 20;
    for (std::uint32_t y = 0; y < kHeight; ++y) {
        for (std::uint32_t x = 0; x < kWidth; ++x) {
            grid.node(yard, static_cast<float>(x), static_cast<float>(y));
        }
    }
    for (std::uint32_t y = 0; y < kHeight; ++y) {
        for (std::uint32_t x = 0; x < kWidth; ++x) {
            const std::uint32_t at = (y * kWidth) + x;
            if (x + 1 < kWidth && (x != 14 || y == kHeight - 1)) {
                grid.link(at, at + 1);
            }
            if (y + 1 < kHeight) {
                grid.link(at, at + kWidth);
            }
        }
    }
    const coney::world::PathMap gridMap = grid.build();
    const RoutePlanner round(gridMap);
    // From (0, 0) to (29, 0): up the near side, through the gap at y 19 and down the far side, 29 + 19 × 2 steps.
    auto roundWall = round.search(0, kWidth - 1, 1);
    REQUIRE(roundWall.has_value());
    if (!roundWall) {
        return;
    }
    CHECK(roundWall.value().nodes.size() == 29 + (19 * 2) + 1);

    // A hub with 1000 spokes is searched from the hub (the search starts at the goal) with all 1000 open; 1001
    // overflow the open heap. Searched from a spoke, the hub is found at once.
    const auto hubWith = [](int spokes) {
        coney::test::PathBuilder builder;
        const std::uint32_t area = builder.rectangle(-20.0F, 20.0F, -20.0F, 20.0F);
        builder.node(area, 0.0F, 0.0F);
        for (int spoke = 0; spoke < spokes; ++spoke) {
            const float angle = static_cast<float>(spoke) * 0.006F;
            builder.node(area, 10.0F * std::cos(angle), 10.0F * std::sin(angle));
        }
        for (std::uint32_t spoke = 1; spoke <= static_cast<std::uint32_t>(spokes); ++spoke) {
            builder.link(0, spoke);
        }
        return builder.build();
    };
    const coney::world::PathMap fits = hubWith(1000);
    const coney::world::PathMap overflows = hubWith(1001);
    CHECK(RoutePlanner(fits).search(500, 0, 1).has_value());
    CHECK(failureOf(RoutePlanner(overflows).search(500, 0, 1)) == SearchFailure::OpenFull);
    CHECK(RoutePlanner(overflows).search(0, 500, 1).has_value());

    // The closed list: from the goal a dead-end chain whose nodes all stand on the goal (so cheaper than the dear
    // link to the start) is closed first. With the goal and the start, 2998 of them fill the 3000 closed; 2999
    // overflow it.
    const auto deadEnd = [](std::uint32_t chain) {
        coney::test::PathBuilder builder;
        const std::uint32_t area = builder.rectangle(-1.0F, 11.0F, -1.0F, 1.0F);
        builder.node(area, 0.0F, 0.0F);  // the goal
        builder.node(area, 10.0F, 0.0F); // the start
        builder.linkOneWay(0, 1, 1, true);
        for (std::uint32_t n = 0; n < chain; ++n) {
            builder.node(area, 0.0F, 0.0F);
            builder.linkOneWay(n == 0 ? 0 : n + 1, n + 2);
        }
        return builder.build();
    };
    const coney::world::PathMap justFits = deadEnd(2998);
    const coney::world::PathMap tooLong = deadEnd(2999);
    CHECK(RoutePlanner(justFits).search(1, 0, 1).has_value());
    CHECK(failureOf(RoutePlanner(tooLong).search(1, 0, 1)) == SearchFailure::ClosedFull);
}

TEST_CASE("the search's cap of 65000 is on the running cost, not on one edge", "[ai][routes]") {
    // Every link dear (the avoid bit): no single edge reaches the cap, but enough of them in a row do.
    coney::test::PathBuilder builder;
    const std::uint32_t corridor = builder.rectangle(0.0F, 100.0F, 0.0F, 2.0F);
    for (std::uint32_t n = 0; n < 50; ++n) {
        builder.node(corridor, 1.0F + 2.0F * static_cast<float>(n), 1.0F);
    }
    for (std::uint32_t n = 0; n + 1 < 50; ++n) {
        builder.link(n, n + 1, 1, true);
    }
    const coney::world::PathMap map = builder.build();
    const RoutePlanner planner(map);
    const std::uint32_t step = planner.edgeCost(1, PathEdge{.to = 0, .flags = 1, .avoid = true}, 1);
    REQUIRE(step < coney::ai::kEdgeCostCap);
    const std::uint32_t steps = (coney::ai::kEdgeCostCap - 1) / step; // the most edges under the cap
    REQUIRE(steps + 1 < 50);
    CHECK(planner.search(0, steps, 1).has_value());
    CHECK(failureOf(planner.search(0, steps + 1, 1)) == SearchFailure::NoWay);
    // A lower cap (the jump detour's) counts the same way.
    CHECK(planner.search(0, 2, 1, (2 * step) + 1).has_value());
    CHECK(failureOf(planner.search(0, 2, 1, 2 * step)) == SearchFailure::NoWay);
}

TEST_CASE("a search with a narrower mask that fails tries again with the choke, eight and jump edges; every kind is "
          "in the default mask",
          "[ai][routes]") {
    coney::test::PathBuilder builder;
    const std::uint32_t u = builder.polygon({{0.0F, 0.0F},
                                             {4.0F, 0.0F},
                                             {4.0F, 6.0F},
                                             {0.0F, 6.0F},
                                             {0.0F, 4.0F},
                                             {3.0F, 4.0F},
                                             {3.0F, 2.0F},
                                             {0.0F, 2.0F}});
    builder.node(u, 1.0F, 1.0F);
    builder.node(u, 3.5F, 1.0F);
    builder.node(u, 3.5F, 3.0F);
    builder.node(u, 3.5F, 5.0F);
    builder.node(u, 1.0F, 5.0F);
    builder.link(0, 1);
    builder.link(1, 2, coney::ai::edge_flag::kChoke);
    builder.link(2, 3);
    builder.link(3, 4);
    const coney::world::PathMap map = builder.build();
    RoutePlanner planner(map);
    CHECK_FALSE(planner.search(0, 4, 0x3).has_value());
    CHECK(planner.search(0, 4, coney::ai::edge_flag::kDefaultMask).has_value());
    auto plan = planner.request({0.5F, 1.0F, 0.0F}, {0.5F, 5.0F, 0.0F}, 0x3);
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
    const std::optional<coney::ai::Route> route = std::move(plan->route);
    REQUIRE(route.has_value());
    if (!route) {
        return;
    }
    CHECK(route->cost() == 64 + 320);
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
    if (!search) {
        return;
    }
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
    std::optional<coney::ai::Route> route = std::move(plan->route);
    REQUIRE(route.has_value());
    if (!route) {
        return;
    }
    coney::ai::RouteFollower follower(planner, std::move(*route), {1.0F, 1.0F, 0.0F}, {1.0F, 9.0F, 0.0F});
    CHECK(follower.waypoint(planner, {1.0F, 1.0F, 0.0F}) == Vec3{9.0F, 1.0F, 0.0F});
    CHECK(follower.ahead().size() == 4);
    // At node 1: on to node 2, which it reaches, and on past it to node 3 (straight up the side).
    CHECK(follower.waypoint(planner, {8.9F, 1.0F, 0.0F}) == Vec3{9.0F, 9.0F, 0.0F});
    CHECK(planner.routesInUse() == 1);
    // At node 3: only the destination is left, and the route is freed.
    CHECK(follower.waypoint(planner, {9.0F, 8.9F, 0.0F}) == Vec3{1.0F, 9.0F, 0.0F});
    CHECK(follower.onLastLeg());
    CHECK(planner.routesInUse() == 0);
}

TEST_CASE("a blocked straight line takes the route; the follower names a climb leg's kind", "[ai][routes]") {
    // One open yard cut by a fence, a hole of the yard's area across x 0.5 to 3.5 at y 3; nodes either side linked by
    // a climb.
    coney::test::PathBuilder builder;
    const std::uint32_t yard = builder.rectangle(0.0F, 4.0F, 0.0F, 6.0F);
    builder.hole(0.5F, 3.5F, 2.9F, 3.1F);
    builder.node(yard, 2.0F, 2.0F);
    builder.node(yard, 2.0F, 4.0F);
    builder.link(0, 1, coney::ai::edge_flag::kClimb);
    const coney::world::PathMap map = builder.build();
    RoutePlanner planner(map);
    CHECK(planner.lineClear({1.0F, 1.0F, 0.0F}, {3.0F, 2.5F, 0.0F}));
    CHECK(planner.lineClear({0.2F, 1.0F, 0.0F}, {0.2F, 5.0F, 0.0F}));
    CHECK_FALSE(planner.lineClear({2.0F, 1.0F, 0.0F}, {2.0F, 5.0F, 0.0F}));
    auto plan = planner.request({2.0F, 1.0F, 0.0F}, {2.0F, 5.0F, 0.0F});
    REQUIRE(plan.has_value());
    CHECK(nodesOf(*plan) == std::vector<std::uint32_t>{0, 1});
    std::optional<coney::ai::Route> route = std::move(plan->route);
    REQUIRE(route.has_value());
    if (!route) {
        return;
    }
    coney::ai::RouteFollower follower(planner, std::move(*route), {2.0F, 1.0F, 0.0F}, {2.0F, 5.0F, 0.0F});
    CHECK(follower.waypoint(planner, {2.0F, 1.0F, 0.0F}) == Vec3{2.0F, 2.0F, 0.0F});
    CHECK(follower.legKind() == 0);
    // At node 0 the next leg, over the fence, is the climb; once over it the follower moves on.
    CHECK(follower.waypoint(planner, {2.0F, 1.9F, 0.0F}) == Vec3{2.0F, 4.0F, 0.0F});
    CHECK(follower.legKind() == coney::ai::edge_flag::kClimb);
    CHECK_FALSE(follower.legAvoided());
    follower.passWaypoint(planner, {2.0F, 4.3F, 0.0F});
    CHECK(follower.onLastLeg());
}

TEST_CASE("a start in a hole plans from the hole's nearest edge or a probe; a destination in one fails",
          "[ai][routes]") {
    // A yard with an obstacle's padding, a hole across x 3 to 5, y 2 to 3; a node either side of it.
    coney::test::PathBuilder builder;
    const std::uint32_t yard = builder.rectangle(0.0F, 8.0F, 0.0F, 6.0F);
    builder.hole(3.0F, 5.0F, 2.0F, 3.0F);
    builder.node(yard, 1.0F, 1.0F);
    builder.node(yard, 1.0F, 5.0F);
    builder.link(0, 1);
    const coney::world::PathMap map = builder.build();
    RoutePlanner planner(map);
    // A point an area takes in is its own.
    CHECK(planner.navPoint({1.0F, 1.0F, 0.0F}) == Vec3{1.0F, 1.0F, 0.0F});
    CHECK_FALSE(planner.inHole({1.0F, 1.0F, 0.0F}));
    // In the hole, 0.1 m from its top edge (under kNavEdgeNear): snapped exactly onto that edge, which the yard takes
    // in; the body is not moved, but the plan starts there.
    const Vec3 inHole{4.0F, 2.9F, 0.0F};
    CHECK(planner.inHole(inHole));
    const std::optional<Vec3> snapped = planner.navPoint(inHole);
    REQUIRE(snapped.has_value());
    if (!snapped) {
        return;
    }
    CHECK(*snapped == Vec3{4.0F, 3.0F, 0.0F});
    CHECK(planner.lineClear(inHole, {4.0F, 5.0F, 0.0F}));
    CHECK(planner.request(inHole, {4.0F, 5.0F, 0.0F}).has_value());
    CHECK(planner.request(inHole, {1.0F, 1.0F, 0.0F}).has_value());
    // Whatever the edge point gives, the start is one an area takes in, on the hole's edge or a probe 1 to 1.4 m out.
    for (const Vec3 point : {Vec3{3.1F, 2.5F, 0.0F}, Vec3{4.0F, 2.1F, 0.0F}, Vec3{4.9F, 2.5F, 0.0F}}) {
        const std::optional<Vec3> start = planner.navPoint(point);
        REQUIRE(start.has_value());
        if (!start) {
            return;
        }
        CHECK(planner.areaAt(*start).has_value());
        CHECK(std::hypot(start->x - point.x, start->y - point.y) <= 1.5F);
    }
    // A destination in the hole has no fallback.
    CHECK(planner.request({4.0F, 5.0F, 0.0F}, inHole).error() == coney::ai::MoveFailure::NoRoute);
    CHECK(planner.request({1.0F, 1.0F, 0.0F}, inHole).error() == coney::ai::MoveFailure::NoRoute);
}

TEST_CASE("a jump leg's take-off is the nearest queue point on its edge and its landing the position projected across",
          "[ai][routes]") {
    // Two roofs with a 2 m gap. The take-off edge (nodes 1 and 2, pair 1) runs along the near roof's edge, the landing
    // edge (nodes 3 and 4, pair 2) along the far one's; jump links cross the gap.
    coney::test::PathBuilder builder;
    const std::uint32_t near = builder.rectangle(0.0F, 4.0F, 0.0F, 4.0F);
    const std::uint32_t far = builder.rectangle(6.0F, 10.0F, 0.0F, 4.0F);
    builder.node(near, 3.5F, 1.0F, 1);
    builder.node(near, 3.5F, 3.0F, 1);
    builder.node(far, 6.5F, 1.0F, 2);
    builder.node(far, 6.5F, 3.0F, 2);
    builder.link(0, 1);
    builder.link(2, 3);
    builder.link(0, 2, coney::ai::edge_flag::kJumpDown);
    builder.link(1, 3, coney::ai::edge_flag::kJumpDown);
    const coney::world::PathMap map = builder.build();
    CHECK(map.partnerOf(0) == std::optional<std::uint32_t>{1});
    CHECK(map.partnerOf(2) == std::optional<std::uint32_t>{3});
    CHECK(coney::ai::RouteFollower::isJumpLeg(coney::ai::edge_flag::kJumpDown));
    CHECK_FALSE(coney::ai::RouteFollower::isJumpLeg(coney::ai::edge_flag::kJumpDown | coney::ai::edge_flag::kClimb));

    RoutePlanner planner(map);
    const Vec3 start{1.0F, 2.1F, 0.0F};
    const Vec3 destination{9.0F, 2.0F, 0.0F};
    auto plan = planner.request(start, destination);
    REQUIRE(plan.has_value());
    std::optional<coney::ai::Route> route = std::move(plan->route);
    REQUIRE(route.has_value());
    if (!route) {
        return;
    }
    REQUIRE(route->nodes().size() == 2);
    coney::ai::RouteFollower follower(planner, std::move(*route), start, destination);
    // The edge is 2 m: three points from the partner to the node, (3.5, 1), (3.5, 2) and (3.5, 3); (3.5, 2) is
    // nearest. The take-off is never skipped, though the landing is in a straight line on no polygon.
    const Vec3 takeOff = follower.waypoint(planner, start);
    CHECK(takeOff == Vec3{3.5F, 2.0F, 0.0F});
    CHECK(follower.atJump());
    CHECK_FALSE(follower.onJumpLeg());
    // Reached one update's travel early at 6 m/s (0.2 m): at 0.4 m it is not yet, with the lead it is.
    CHECK(follower.waypoint(planner, {3.1F, 2.0F, 0.0F}) == takeOff);
    const Vec3 landing = follower.waypoint(planner, {3.1F, 2.0F, 0.0F}, 6.0F);
    CHECK(follower.onJumpLeg());
    // The landing: the position projected onto the far edge's line.
    CHECK(landing.x == 6.5F);
    CHECK(std::fabs(landing.y - 2.0F) < 1e-5F);
}
