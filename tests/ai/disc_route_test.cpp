// SPDX-License-Identifier: GPL-3.0-or-later

// A check against the player's own disc: every level's path data decodes, its route nodes lie in the polygons that own
// them, and the route planner finds its way between seeded pairs of `level99`'s nodes. It runs only when the
// environment variable CONEY_DISC names the disc and skips otherwise; it prints counts only, never data (LEGAL.md).
// docs/research/ai.md#coney has the results.

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <format>
#include <string>
#include <utility>

#include <SDL3/SDL_stdinc.h>
#include <catch2/catch_test_macros.hpp>

#include "ai/route_planner.h"
#include "fileio/disc.h"
#include "fileio/wad.h"
#include "platform/level_file.h"
#include "platform/render_engine.h"
#include "world/level_object.h"
#include "world/path_map.h"

namespace {

// A small seeded generator for the pairs: the same pairs on every run.
struct Lcg {
    std::uint32_t state;
    std::uint32_t next(std::uint32_t bound) {
        state = state * 1664525U + 1013904223U;
        return (state >> 8U) % bound;
    }
};

} // namespace

TEST_CASE("every level's path data decodes and level99's routes are planned", "[disc][routes]") {
    const char* discPath = SDL_getenv("CONEY_DISC");
    if (discPath == nullptr || *discPath == '\0') {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    auto disc = coney::io::Disc::open(discPath);
    REQUIRE(disc.has_value());
    auto wad = coney::io::Wad::open(std::move(*disc));
    REQUIRE(wad.has_value());
    auto engine = coney::platform::RenderEngine::start(coney::platform::RenderBackend::Null, {});
    REQUIRE(engine.has_value());

    // Every level: the decode, and the owned nodes the inside test takes in.
    std::uint64_t levels = 0;
    std::uint64_t decoded = 0;
    std::uint64_t ownedNodes = 0;
    std::uint64_t nodesInside = 0;
    for (int level = 0; level < 200; ++level) {
        const std::string name = std::format("level{}", level);
        if (!wad->lookup(name + ".lev")) {
            continue;
        }
        ++levels;
        auto loaded = coney::platform::loadLevel(*wad, name, false);
        REQUIRE(loaded.has_value());
        auto map = coney::world::PathMap::decode((*loaded)->pathData);
        if (!map) {
            std::printf("  %s: %s\n", name.c_str(), map.error().message.c_str());
            continue;
        }
        ++decoded;
        for (const coney::world::PathPolygon& polygon : map->polygons()) {
            for (std::uint32_t n = polygon.firstNode; polygon.hasNodes && n < polygon.firstNode + polygon.nodeCount;
                 ++n) {
                ++ownedNodes;
                const coney::anim::Vec3 at = map->nodes()[n].position;
                nodesInside += map->inside(polygon, at.x, at.y) ? 1U : 0U;
            }
        }
    }

    // level99: routes between seeded pairs of its nodes; each failure put down to the ends' polygons being off the
    // graph, the nodes being linked only over edges a move does not ask for (flag 0x10), or not linked at all.
    auto level99 = coney::platform::loadLevel(*wad, "level99", false);
    REQUIRE(level99.has_value());
    auto map = coney::world::PathMap::decode((*level99)->pathData);
    REQUIRE(map.has_value());
    REQUIRE(!map->nodes().empty());
    coney::ai::RoutePlanner planner(*map);
    Lcg random{99};
    constexpr int kPairs = 500;
    int straight = 0;
    int routed = 0;
    int offGraph = 0;
    int otherEdges = 0;
    int unlinked = 0;
    std::uint64_t routeNodes = 0;
    const auto count = static_cast<std::uint32_t>(map->nodes().size());
    for (int pair = 0; pair < kPairs; ++pair) {
        const std::uint32_t a = random.next(count);
        const std::uint32_t b = random.next(count);
        const coney::anim::Vec3 from = map->nodes()[a].position;
        const coney::anim::Vec3 to = map->nodes()[b].position;
        auto plan = planner.request(from, to);
        // A local: clang-tidy cannot follow a check through the outer expected.
        const std::optional<coney::ai::Route> route = plan ? std::move(plan->route) : std::nullopt;
        if (route) {
            ++routed;
            routeNodes += route->nodes().size();
        } else if (plan) {
            ++straight;
        } else {
            const auto start = map->polygonAt(from.x, from.y);
            const auto end = map->polygonAt(to.x, to.y);
            if (start && end && *start != *end &&
                (map->polygons()[*start].graph == 0 || map->polygons()[*end].graph == 0)) {
                ++offGraph;
            } else if (planner.search(a, b, 0xffff)) {
                ++otherEdges;
            } else {
                ++unlinked;
            }
        }
    }
    std::printf("path data: %llu of %llu levels decoded; %llu of %llu owned route nodes inside their polygon\n",
                static_cast<unsigned long long>(decoded), static_cast<unsigned long long>(levels),
                static_cast<unsigned long long>(nodesInside), static_cast<unsigned long long>(ownedNodes));
    std::printf("level99: %zu polygons, %zu nodes, %zu edges; %d pairs: %d straight, %d routed (%llu nodes); failed: "
                "%d off the graph, %d linked only over other edges, %d not linked\n",
                map->polygons().size(), map->nodes().size(), map->edges().size(), kPairs, straight, routed,
                static_cast<unsigned long long>(routeNodes), offGraph, otherEdges, unlinked);
    CHECK(levels == 64);
    CHECK(decoded == levels);
    CHECK(nodesInside * 50 > ownedNodes * 49); // at least 98 % lie inside their owner
    CHECK(routed > 0);
    CHECK(straight + routed + offGraph + otherEdges + unlinked == kPairs);
    CHECK(planner.routesInUse() == 0);
}

TEST_CASE("level87's routes leave a hole a human stands in: checkpoint 4's start and the roof path", "[disc][routes]") {
    const char* discPath = SDL_getenv("CONEY_DISC");
    if (discPath == nullptr || *discPath == '\0') {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    auto disc = coney::io::Disc::open(discPath);
    REQUIRE(disc.has_value());
    auto wad = coney::io::Wad::open(std::move(*disc));
    REQUIRE(wad.has_value());
    auto engine = coney::platform::RenderEngine::start(coney::platform::RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    auto level = coney::platform::loadLevel(*wad, "level87", false);
    REQUIRE(level.has_value());
    auto map = coney::world::PathMap::decode((*level)->pathData);
    REQUIRE(map.has_value());
    REQUIRE((*level)->collision != nullptr);
    coney::ai::RoutePlanner planner(*map);
    planner.setGroundProbe(coney::ai::groundProbe(*(*level)->collision));

    // Checkpoint 4's start stands in a small hole of the street's area (an obstacle's padding): no area takes it in,
    // so its human plans from the hole's nearest edge (or a probe); before, every line and route from it failed, even
    // to open ground 2-10 m away.
    const coney::anim::Vec3 start{419.07F, 119.95F, 0.20F};
    CHECK_FALSE(planner.areaAt(start).has_value());
    CHECK(planner.inHole(start));
    const std::optional<coney::anim::Vec3> snapped = planner.navPoint(start);
    REQUIRE(snapped.has_value());
    CHECK(planner.areaAt(*snapped).has_value());
    std::printf("level87: checkpoint 4's start plans from %.2f m away\n",
                static_cast<double>(std::hypot(snapped->x - start.x, snapped->y - start.y)));
    // A point an area takes in is its own; a destination in the hole fails.
    const coney::anim::Vec3 open{413.5F, 122.4F, 0.20F};
    CHECK(planner.navPoint(open) == open);
    CHECK_FALSE(planner.request(open, start).has_value());

    // From the start to open ground, the tag marker, and the roof path down to the street and on to the marker.
    const coney::anim::Vec3 marker{422.6F, 79.0F, 0.3F};
    const coney::anim::Vec3 roof{470.0F, 71.0F, 6.4F};
    const coney::anim::Vec3 street{470.6F, 71.7F, 0.3F};
    const std::array<std::pair<coney::anim::Vec3, coney::anim::Vec3>, 7> legs{{{start, {425.0F, 120.0F, 0.3F}},
                                                                               {start, {410.0F, 124.0F, 0.3F}},
                                                                               {start, {417.7F, 122.6F, 0.3F}},
                                                                               {start, marker},
                                                                               {roof, street},
                                                                               {street, marker},
                                                                               {roof, marker}}};
    int planned = 0;
    for (const auto& [from, to] : legs) {
        auto plan = planner.request(from, to);
        CHECK(plan.has_value());
        planned += plan ? 1 : 0;
    }
    std::printf("level87: %d of %zu legs planned from checkpoint 4's start and the roofs\n", planned, legs.size());
    CHECK(planner.routesInUse() == 0);
}
