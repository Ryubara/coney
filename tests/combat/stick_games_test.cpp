// SPDX-License-Identifier: GPL-3.0-or-later
#include "combat/stick_games.h"

#include <cmath>
#include <cstdint>
#include <format>
#include <numbers>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "combat/combat_script.h"
#include "combat/commands.h"

using namespace coney::combat;

namespace {

// Game time in whole milliseconds after `update` fixed steps of 1/30 s.
std::uint64_t msAt(std::uint64_t update) { return update * 1000 / 30; }

// An input script turning the left stick at `percent` by `stepDegrees` every update from frame 0 for `frames`
// frames (positive anticlockwise), as the research's stereo theft script does with 30° steps.
std::string rotationScript(int percent, float stepDegrees, int frames) {
    std::string script;
    for (int frame = 0; frame < frames; ++frame) {
        const float radians = static_cast<float>(frame) * stepDegrees * std::numbers::pi_v<float> / 180.0F;
        const auto x = static_cast<int>(std::lround(static_cast<float>(percent) * std::cos(radians)));
        const auto y = static_cast<int>(std::lround(static_cast<float>(percent) * std::sin(radians)));
        script += std::format("{} stick left {} {}\n", frame, x, y);
    }
    return script;
}

// Plays a rotation script through the stereo theft with `commands` (none when empty); returns the update it ended on,
// or -1, and the result.
std::pair<int, GameResult> playTheft(const std::string& script, int frames, float stageTurns, int failAt = -1) {
    const CombatTuning tuning;
    StereoTheft theft(0, stageTurns);
    const auto pad = coney::test::playScript(script, static_cast<std::uint64_t>(frames));
    for (int update = 0; update < frames; ++update) {
        const auto& frame = pad[static_cast<std::size_t>(update)];
        const CommandId command = update == failAt ? command::kR1Pressed : command::kNone;
        const GameResult result =
            theft.update(msAt(static_cast<std::uint64_t>(update)), command, Stick{frame.leftX, frame.leftY}, tuning);
        if (result != GameResult::Running) {
            return {update, result};
        }
    }
    return {-1, GameResult::Running};
}

} // namespace

TEST_CASE("the mugging succeeds after 5 s with the stick held on its moving target", "[combat]") {
    const CombatTuning tuning;
    CombatRandom random(42);
    MuggingGame mugging(0, random);
    float lastTarget = mugging.targetDegrees();
    int moves = 0;
    GameResult result = GameResult::Running;
    std::uint64_t update = 0;
    while (result == GameResult::Running && update < 1000) {
        ++update;
        // A partial deflection (0.7) pointed 20° off the target: inside the 50° tolerance.
        const float radians = (mugging.targetDegrees() + 20.0F) * std::numbers::pi_v<float> / 180.0F;
        result =
            mugging.update(msAt(update), Stick{0.7F * std::sin(radians), 0.7F * std::cos(radians)}, tuning, random);
        if (mugging.targetDegrees() != lastTarget) {
            // Each move turns the target by at least the tolerance plus 20°.
            float turn = std::fabs(mugging.targetDegrees() - lastTarget);
            turn = std::fmin(turn, 360.0F - turn);
            CHECK(turn >= 70.0F - 0.01F);
            lastTarget = mugging.targetDegrees();
            ++moves;
        }
    }
    CHECK(result == GameResult::Succeeded);
    CHECK(update == 150); // 5000 ms of game time
    CHECK(moves == 1);    // at 2500 ms
}

TEST_CASE("the mugging gains nothing off target or with the stick at 0.5 or less, and fails at 50 s", "[combat]") {
    const CombatTuning tuning;
    CombatRandom random(3);
    MuggingGame mugging(0, random);
    GameResult result = GameResult::Running;
    std::uint64_t update = 0;
    int onTarget = 0;
    while (result == GameResult::Running && update < 3000) {
        ++update;
        // On the target's angle but at 0.45, below the 0.5 the mugging needs.
        const float radians = mugging.targetDegrees() * std::numbers::pi_v<float> / 180.0F;
        result =
            mugging.update(msAt(update), Stick{0.45F * std::sin(radians), 0.45F * std::cos(radians)}, tuning, random);
        onTarget += mugging.onTarget() ? 1 : 0;
    }
    CHECK(result == GameResult::Failed);
    CHECK(update == 1500);
    CHECK(onTarget == 0);
    CHECK(mugging.progressMs() == 0);
}

TEST_CASE("the stereo theft succeeds after 4 stages of anticlockwise turns at full stick", "[combat]") {
    // 30° steps, 12 an turn: a stage of 3 turns is 36 updates, then a 250 ms pause (8 updates) before the next.
    const auto [ended, result] = playTheft(rotationScript(100, 30.0F, 400), 400, stereoStageTurns(2));
    CHECK(result == GameResult::Succeeded);
    CHECK(ended >= 4 * 36);
    CHECK(ended <= (4 * 36) + (4 * 9) + 4);

    // With one turn a stage (any other Warrior byte), it is a third as long.
    const auto [shortEnded, shortResult] = playTheft(rotationScript(100, 30.0F, 400), 400, stereoStageTurns(0));
    CHECK(shortResult == GameResult::Succeeded);
    CHECK(shortEnded < ended / 2);
}

TEST_CASE("the stereo theft ignores clockwise turns, big steps and a stick at 0.8 or less", "[combat]") {
    CHECK(playTheft(rotationScript(100, -30.0F, 300), 300, 1.0F).second == GameResult::Running);
    CHECK(playTheft(rotationScript(100, 100.0F, 300), 300, 1.0F).second == GameResult::Running);
    CHECK(playTheft(rotationScript(75, 30.0F, 300), 300, 1.0F).second == GameResult::Running);
}

TEST_CASE("any command fails the stereo theft", "[combat]") {
    const auto [ended, result] = playTheft(rotationScript(100, 30.0F, 100), 100, 3.0F, 40);
    CHECK(result == GameResult::Failed);
    CHECK(ended == 40);
}

TEST_CASE("the button mash fills on alternating L1 and R1 and loses 15 an update", "[combat]") {
    // L1 and R1 alternated every 3 updates through the command matcher, as a player mashes.
    std::string script;
    for (int frame = 0; frame < 200; frame += 6) {
        script += std::format("{} press l1\n{} release l1\n{} press r1\n{} release r1\n", frame, frame + 3, frame + 3,
                              frame + 6);
    }
    const CombatTuning tuning;
    const CommandTables tables = CommandTables::street();
    const auto pad = coney::test::playScript(script, 200);

    // Played with the Warrior factor `factor`; returns the update it completed on, or -1.
    auto mashWith = [&](float factor) {
        CommandMatcher matcher;
        ButtonMash mash;
        for (std::size_t update = 0; update < pad.size(); ++update) {
            const CommandId command = matcher.update(pad[update].buttons, tables, tuning.historyHoldSamples);
            if (mash.update(command, factor, tuning) == GameResult::Succeeded) {
                return static_cast<int>(update);
            }
        }
        return -1;
    };
    const int fast = mashWith(1.5F);
    const int slow = mashWith(0.7F);
    CHECK(fast > 0);
    CHECK(slow > fast);

    // Holding one button fills nothing past its first press.
    ButtonMash held;
    for (int update = 0; update < 100; ++update) {
        CHECK(held.update(command::kL1Held, 1.5F, tuning) == GameResult::Running);
    }
    CHECK(held.meter() == 0);
}
