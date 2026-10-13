// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/fight_trace.h"

#include <cstddef>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "ai/brain.h"
#include "ai/gangs.h"
#include "support/ai_fixtures.h"

// The all-humans trace (--trace-humans): one row per human per update, the same columns for the player's gang and the
// others. Synthetic humans on a floor; game time is the steps run (1/30 s each).
// Research: docs/research/feel.md#method

namespace {

// The fields of one CSV line.
std::vector<std::string> fieldsOf(std::string_view line) {
    std::vector<std::string> fields;
    std::string field;
    for (const char c : line) {
        if (c == ',' || c == '\n') {
            fields.push_back(field);
            field.clear();
        } else {
            field += c;
        }
    }
    if (!field.empty()) {
        fields.push_back(field);
    }
    return fields;
}

// The lines of `text`.
std::vector<std::string> linesOf(const std::string& text) {
    std::vector<std::string> lines;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        lines.push_back(line + "\n");
    }
    return lines;
}

// The index of the column called `name` in the header.
std::size_t columnOf(std::string_view name) {
    const std::vector<std::string> names = fieldsOf(coney::ai::FightTracer::header());
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (names[i] == name) {
            return i;
        }
    }
    FAIL("no column " << name);
    return 0;
}

} // namespace

TEST_CASE("the all-humans trace header shares the player trace's names for the same quantities", "[ai][trace]") {
    const std::vector<std::string> names = fieldsOf(coney::ai::FightTracer::header());
    REQUIRE(!names.empty());
    CHECK(names.front() == "step");
    for (const char* shared :
         {"x", "y", "z", "heading", "speed", "vz", "gait", "clip", "command", "health", "stamina", "power"}) {
        std::size_t found = 0;
        for (const std::string& name : names) {
            found += name == shared ? 1 : 0;
        }
        INFO(shared);
        CHECK(found == 1);
    }
}

TEST_CASE("every human gets one row per update with as many fields as the header has columns", "[ai][trace]") {
    coney::test::AiScene scene;
    scene.add(coney::anim::Vec3{44.0F, 40.0F, 0.0F}, 90.0F);
    scene.add(coney::anim::Vec3{36.0F, 40.0F, 0.0F}, 270.0F);
    coney::ai::FightTracer tracer;
    const std::size_t columns = fieldsOf(coney::ai::FightTracer::header()).size();
    for (int step = 1; step <= 3; ++step) {
        scene.run(1);
        const std::vector<std::string> lines = linesOf(tracer.rows(static_cast<std::uint64_t>(step), scene.brains));
        REQUIRE(lines.size() == 3);
        for (std::size_t human = 0; human < lines.size(); ++human) {
            const std::vector<std::string> fields = fieldsOf(lines[human]);
            REQUIRE(fields.size() == columns);
            CHECK(fields[columnOf("step")] == std::to_string(step));
            CHECK(fields[columnOf("human")] == std::to_string(human));
        }
    }
}

TEST_CASE("the player's row says player, the others' do not, and the positions are the humans' feet", "[ai][trace]") {
    coney::test::AiScene scene;
    scene.add(coney::anim::Vec3{44.0F, 41.0F, 0.0F}, 90.0F);
    coney::ai::FightTracer tracer;
    scene.run(1);
    const std::vector<std::string> lines = linesOf(tracer.rows(1, scene.brains));
    REQUIRE(lines.size() == 2);
    const std::vector<std::string> player = fieldsOf(lines[0]);
    const std::vector<std::string> other = fieldsOf(lines[1]);
    CHECK(player[columnOf("player")] == "0"); // player 1
    CHECK(other[columnOf("player")] == "-1");
    CHECK(std::stof(other[columnOf("x")]) == 44.0F);
    CHECK(std::stof(other[columnOf("y")]) == 41.0F);
    CHECK(std::stoi(other[columnOf("health")]) > 0);
    CHECK(other[columnOf("down")] == "0");
    CHECK(other[columnOf("blocking")] == "0");
}

TEST_CASE("a fight shows in the goal stack, the target and its distance, and the attackers' slots", "[ai][trace]") {
    coney::test::AiScene scene;
    coney::ai::Brain& a = scene.add(coney::anim::Vec3{30.0F, 30.0F, 0.0F}, 0.0F);
    coney::ai::Brain& b = scene.add(coney::anim::Vec3{30.0F, 33.0F, 0.0F}, 180.0F);
    const int gangA = scene.brains.gangs().create(19, "a");
    const int gangB = scene.brains.gangs().create(19, "b");
    scene.brains.gangs().addMember(gangA, a);
    scene.brains.gangs().addMember(gangB, b);
    scene.brains.gangs().makeEnemies(gangA, gangB);
    a.startFight(b);
    scene.run(2);
    coney::ai::FightTracer tracer;
    const std::vector<std::string> lines = linesOf(tracer.rows(1, scene.brains));
    REQUIRE(lines.size() == 3);
    const std::vector<std::string> row = fieldsOf(lines[1]);
    CHECK(row[columnOf("gang")] == std::to_string(gangA));
    CHECK(std::stoi(row[columnOf("goals")]) >= 1);
    CHECK(row[columnOf("goal")] != "-1");
    CHECK(row[columnOf("goal0")] != "-1");
    CHECK(row[columnOf("goal5")] == "-1"); // the stack is not six deep
    CHECK(row[columnOf("target")] == "2"); // b is the third human
    CHECK(std::stof(row[columnOf("target_dist")]) > 0.0F);
    CHECK(row[columnOf("slot_held")] == "1");
    const std::vector<std::string> victim = fieldsOf(lines[2]);
    CHECK(victim[columnOf("slots")] == "1");
    CHECK(row[columnOf("enemies")] != "0");
}

TEST_CASE("a human with no goals and no target traces -1 for them", "[ai][trace]") {
    coney::test::AiScene scene;
    scene.add(coney::anim::Vec3{30.0F, 30.0F, 0.0F}, 0.0F);
    scene.run(1);
    coney::ai::FightTracer tracer;
    const std::vector<std::string> lines = linesOf(tracer.rows(1, scene.brains));
    const std::vector<std::string> row = fieldsOf(lines[1]);
    CHECK(row[columnOf("goal")] == "-1");
    CHECK(row[columnOf("goals")] == "0");
    CHECK(row[columnOf("target")] == "-1");
    CHECK(std::stof(row[columnOf("target_dist")]) == -1.0F);
    CHECK(row[columnOf("action_kind")] == "none");
    CHECK(row[columnOf("goal0")] == "-1");
    CHECK(row[columnOf("react")] == "-1");
    CHECK(row[columnOf("lock")] == "-1");
}

TEST_CASE("a human keeps its id when another leaves the scene", "[ai][trace]") {
    coney::test::AiScene scene;
    scene.add(coney::anim::Vec3{30.0F, 30.0F, 0.0F}, 0.0F);
    coney::ai::Brain& last = scene.add(coney::anim::Vec3{50.0F, 30.0F, 0.0F}, 0.0F);
    coney::ai::FightTracer tracer;
    scene.run(1);
    (void)tracer.rows(1, scene.brains);
    const coney::human::Human& middle = scene.brains.at(1).human();
    scene.step.remove(middle);
    scene.brains.remove(middle);
    scene.run(1);
    const std::vector<std::string> lines = linesOf(tracer.rows(2, scene.brains));
    REQUIRE(lines.size() == 2);
    CHECK(fieldsOf(lines[1])[columnOf("human")] == "2");
    CHECK(last.human().position().x == 50.0F);
}
