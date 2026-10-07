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
            // Each new angle lies more than the re-roll gap (60°) plus 20° from the last.
            float turn = std::fabs(mugging.targetDegrees() - lastTarget);
            turn = std::fmin(turn, 360.0F - turn);
            CHECK(turn > 80.0F);
            lastTarget = mugging.targetDegrees();
            ++moves;
        }
    }
    CHECK(result == GameResult::Succeeded);
    CHECK(update == 150); // 5000 ms of game time
    CHECK(moves == 1);    // at 2500 ms on target
}

TEST_CASE("the mugging gains nothing with the stick at 0.5 or less, and fails past 50 s off target", "[combat]") {
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
    CHECK(update == 1501);
    CHECK(mugging.offTargetMs() > 50000);
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

    // Holding one button counts its first command only; the decay then takes the meter below 0 and fails the mash.
    ButtonMash held;
    GameResult heldResult = GameResult::Running;
    int heldUpdates = 0;
    while (heldResult == GameResult::Running && heldUpdates < 100) {
        heldResult = held.update(command::kL1Held, 1.5F, tuning);
        ++heldUpdates;
    }
    CHECK(heldResult == GameResult::Failed);
    // 250 / 2 x 1.5 = 187.5, rounded to 188, then 15 an update: below 0 on the 13th decay step.
    CHECK(heldUpdates == 14);
    CHECK(held.meter() == -7);
}

TEST_CASE("the button mash waits for the first alternation and a quit command fails it", "[combat]") {
    const CombatTuning tuning;
    // No decay before the first alternation: the meter stays at 0 and the mash runs on.
    ButtonMash idle;
    for (int update = 0; update < 50; ++update) {
        CHECK(idle.update(command::kNone, 1.0F, tuning) == GameResult::Running);
    }
    CHECK(idle.meter() == 0);
    // The presses themselves do not count, only the held commands.
    CHECK(idle.update(command::kL1Pressed, 1.0F, tuning) == GameResult::Running);
    CHECK(idle.meter() == 0);
    // An alternation gains 125, then triangle quits: the meter is -1 and the mash fails.
    CHECK(idle.update(command::kL1Held, 1.0F, tuning) == GameResult::Running);
    CHECK(idle.meter() == 125);
    CHECK(idle.update(command::kTrianglePressed, 1.0F, tuning) == GameResult::Failed);
    CHECK(idle.meter() == -1);
}

TEST_CASE("the mash factor of a Warrior class byte", "[combat]") {
    CHECK(mashFactor(1) == 1.5F);
    CHECK(mashFactor(3) == 0.7F);
    CHECK(mashFactor(2) == 1.0F);
    CHECK(mashFactor(0) == 1.0F);
}

TEST_CASE("SetInterrogateParam's mugging: the off-target time adds up and the target moves with the progress",
          "[combat]") {
    // The tutorial's record: 5 s on target, a new angle every 2.5 s of it, 20 s off target, 40° tolerance, 60° gap.
    const CombatTuning tuning;
    CombatRandom random(7);
    const MuggingParams lesson{
        .requiredMs = 5000, .periodMs = 2500, .offTargetMs = 20000, .toleranceDegrees = 40.0F, .gapDegrees = 60.0F};
    MuggingGame mugging(0, random, lesson);
    // On the target at 0.8 (a partial deflection) or pointed away from it, by the update.
    const auto stickAt = [&mugging](bool on) {
        const float degrees = mugging.targetDegrees() + (on ? 30.0F : 120.0F);
        const float radians = degrees * std::numbers::pi_v<float> / 180.0F;
        return Stick{0.8F * std::sin(radians), 0.8F * std::cos(radians)};
    };
    // 1 s on (30° off the angle is inside 40°), 2 s away, 1 s on: the target has not moved (2 s of progress).
    std::uint64_t update = 0;
    const float first = mugging.targetDegrees();
    const auto run = [&](int updates, bool on) {
        GameResult result = GameResult::Running;
        for (int i = 0; i < updates && result == GameResult::Running; ++i) {
            ++update;
            result = mugging.update(msAt(update), stickAt(on), tuning, random);
        }
        return result;
    };
    CHECK(run(30, true) == GameResult::Running);
    CHECK(run(60, false) == GameResult::Running);
    CHECK(run(30, true) == GameResult::Running);
    CHECK(mugging.targetDegrees() == first);
    CHECK(mugging.offTargetMs() == 2000);
    // Another 0.5 s on target passes 2.5 s of progress: a new angle.
    CHECK(run(16, true) == GameResult::Running);
    CHECK(mugging.targetDegrees() != first);
    // 18 s more away from it passes the 20 s allowance: failed, though the progress was never reset.
    CHECK(run(600, false) == GameResult::Failed);
    CHECK(mugging.offTargetMs() > 20000);
    CHECK(mugging.offTargetMs() <= 20000 + 34);
}
