// SPDX-License-Identifier: GPL-3.0-or-later
// The Missions page: the story's missions in order, each a page of checkpoints that start the level through the
// session's services. No game, no disc.
#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "debug/debug_session.h"
#include "debug/menu_model.h"
#include "debug/story_missions.h"

using coney::debug::DebugServices;
using coney::debug::DebugSession;
using coney::debug::TunableRegistry;

TEST_CASE("the story missions are 18, in order, each with at least one checkpoint", "[debug]") {
    const auto& missions = coney::debug::storyMissions();
    REQUIRE(missions.size() == 18);
    for (std::size_t i = 0; i < missions.size(); ++i) {
        CHECK(missions[i].number == static_cast<int>(i) + 1);
        CHECK(missions[i].checkpoints >= 1);
        CHECK_FALSE(missions[i].title.empty());
    }
    CHECK(missions.front().level == "level99");
    CHECK(missions.back().level == "level84");
}

TEST_CASE("the Missions page lists the missions and jumps to a checkpoint through the loader", "[debug]") {
    TunableRegistry tunables;
    std::vector<std::pair<std::string, int>> started;
    DebugServices services;
    services.loadLevelAt = [&started](std::string_view name, int checkpoint) {
        started.emplace_back(std::string(name), checkpoint);
        return true;
    };
    DebugSession session(tunables, services, nullptr);
    const auto page = session.model().openPage("Missions");
    REQUIRE(page != nullptr);
    REQUIRE(page->items().size() == 18);
    CHECK(page->items().front().label == "1. New Blood");
    CHECK(page->items().front().detail == "level99, 3 cp");
    // The tenth mission's page has its six checkpoints.
    const coney::debug::MenuItem& tenth = page->items()[9];
    REQUIRE(tenth.open);
    const auto sub = tenth.open();
    const coney::debug::MenuItem* second = sub->find("Checkpoint 2");
    REQUIRE(second != nullptr);
    CHECK(sub->find("Checkpoint 6") != nullptr);
    CHECK(sub->find("Checkpoint 7") == nullptr);
    second->run();
    REQUIRE(started.size() == 1);
    CHECK(started.front() == std::pair<std::string, int>{"level93", 2});
}

TEST_CASE("the Missions page says so when the run cannot start a mission", "[debug]") {
    TunableRegistry tunables;
    DebugSession session(tunables, DebugServices{}, nullptr);
    const auto page = session.model().openPage("Missions");
    REQUIRE(page != nullptr);
    const auto sub = page->items().front().open();
    sub->find("Checkpoint 1")->run();
    CHECK(session.log().last(1).front() == "missions: cannot start a mission in this run");
}
