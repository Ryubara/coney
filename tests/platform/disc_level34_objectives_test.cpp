// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc that `level34` (mission 4, the riot) checkpoint 3 ends when its three
// objectives are done (docs/research/scripting.md#level34, item 10 of #level34-fixes): three car stereos stolen, $350
// of store loot taken and the riot meter at 100 %, after which the scripts run `P1.HeadToGate` and call
// `SetCheckPoint(4)`. The level plays in the game session as `--play-level level34 --checkpoint 3` plays it
// (support/disc_session.h); player 1 is put beside each thing in turn (the debug teleport stands for the walk) and the
// pad does the rest: square, triangle and the stick, as a player does. The other gangs' brains are suspended
// (`BrSuspend`) so that their attacks do not cut a theft or a pick-up short. It runs only when the environment
// variable CONEY_DISC names the disc and skips otherwise; it prints counts only (LEGAL.md).

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <format>
#include <numbers>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "debug/play_controls.h"
#include "fileio/wad.h"
#include "platform/game_session.h"
#include "platform/play_level_mode.h"
#include "scripting/script_system.h"
#include "support/disc_play_fixtures.h"
#include "support/disc_session.h"
#include "support/live_pad.h"
#include "warriors/created_humans.h"
#include "world_objects/cars.h"
#include "world_objects/object_types.h"
#include "world_objects/spawn_records.h"

namespace {

constexpr float kPi = std::numbers::pi_v<float>;

// The frame of play at which the opening scene is skipped, and the one the first action waits for.
constexpr std::uint64_t kSkipAt = 100;
constexpr std::uint64_t kSettleAt = 400;

// The loot objective's target in dollars and the riot meter's in points (docs/research/scripting.md#level34).
constexpr int kLootTarget = 350;

// One run of checkpoint 3 with the script trace kept.
struct Riot {
    coney::test::LivePad& pad;
    coney::test::DiscSession& run;
    std::vector<std::string>& trace;
    std::set<double> suspended{};

    // Runs to the pad's frame `frame`, suspending each human not in player 1's gang as he appears.
    void runTo(std::uint64_t frame) {
        while (run.playFrames() < frame) {
            run.run(1);
            suspendOthers();
        }
    }
    // Plays `script` from two frames on (its frames counted from there) for `frames` frames.
    void play(std::string_view script, std::uint64_t frames) {
        const std::uint64_t from = pad.nextFrame() + 2;
        std::string shifted;
        std::size_t start = 0;
        while (start < script.size()) {
            const std::size_t end = script.find('\n', start);
            const std::string_view line = script.substr(start, end - start);
            const std::size_t space = line.find(' ');
            shifted +=
                std::format("{} {}\n", from + std::stoull(std::string(line.substr(0, space))), line.substr(space + 1));
            start = end == std::string_view::npos ? script.size() : end + 1;
        }
        pad.play(shifted);
        runTo(from + frames);
    }
    // Puts player 1's feet at (x, y) on the ground he stands on, facing `heading` degrees.
    void place(float x, float y, float heading) {
        coney::platform::PlayLevelMode* mode = run.session().play();
        REQUIRE(mode != nullptr);
        mode->teleport(
            coney::debug::Place{.name = "objective", .feet = {x, y, mode->playerFeet().z}, .headingDegrees = heading});
    }
    // Puts player 1 `distance` metres from (x, y) in the direction `degrees` (from +x), facing the point.
    void faceFrom(float x, float y, float degrees, float distance) {
        const float dx = std::cos(degrees * kPi / 180.0F);
        const float dy = std::sin(degrees * kPi / 180.0F);
        // Heading 0 faces +y and turns counter-clockwise, so facing (-dx, -dy) is atan2(dx, -dy).
        place(x + (dx * distance), y + (dy * distance), std::atan2(dx, -dy) * 180.0F / kPi);
    }
    // How many trace lines start with `text`.
    [[nodiscard]] std::size_t count(std::string_view text) const {
        std::size_t n = 0;
        for (const std::string& line : trace) {
            n += line.starts_with(text) ? 1 : 0;
        }
        return n;
    }
    // Player 1's money.
    [[nodiscard]] int money() { return run.session().flow().state().player.inventory.count(0, 2); }

  private:
    // BrSuspend on every human created so far who is not in player 1's gang and not suspended yet.
    void suspendOthers() {
        const coney::CreatedHumans& humans = run.session().flow().humans();
        const coney::HumanCreation* player = humans.player(1);
        if (player == nullptr) {
            return;
        }
        for (const coney::HumanCreation& human : humans.all()) {
            if (human.handle != player->handle && (human.gang != player->gang || human.gang < 0) &&
                suspended.insert(human.handle).second) {
                const std::array<coney::script::Value, 2> args{coney::script::Value(human.handle),
                                                               coney::script::Value(true)};
                (void)run.session().flow().scripts().call("BrSuspend", args);
            }
        }
    }
};

// A stereo theft as a player does it, frames from 0: square at the car (its window breaks), triangle (the theft
// starts), then the left stick turned anticlockwise at 0.9, 30 degrees an update, for 400 updates
// (docs/research/combat-targets.md#stereo-theft: four stages of three turns).
std::string stereoTheft() {
    std::string script = "40 tap square\n120 tap triangle\n";
    constexpr int kTurnUpdates = 400;
    constexpr float kDeflection = 90.0F;
    for (int i = 0; i < kTurnUpdates; ++i) {
        const float angle = (kPi / 2.0F) + (static_cast<float>(i) * kPi / 6.0F);
        script += std::format("{} stick left {} {}\n", 150 + i, static_cast<int>(kDeflection * std::cos(angle)),
                              static_cast<int>(kDeflection * std::sin(angle)));
    }
    script += std::format("{} stick left 0 0\n", 150 + kTurnUpdates);
    return script;
}

// Square `taps` times, 30 frames apart.
std::string squares(int taps) {
    std::string script;
    for (int i = 0; i < taps; ++i) {
        script += std::format("{} tap square\n", 5 + (i * 30));
    }
    return script;
}

} // namespace

TEST_CASE("the disc's level34 checkpoint 3: the stereos, the loot and the riot meter lead to the gate",
          "[disc][story]") {
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    coney::test::LivePad pad;
    pad.play(std::format("{} tap cross\n", kSkipAt));
    coney::test::DiscSession run(*wad, pad);
    coney::script::ScriptSystem& scripts = run.session().flow().scripts();
    std::vector<std::string> trace;
    run.session().startAtLevel("level34", 3, [&scripts, &trace] {
        scripts.traceCalls([&trace](std::string_view line) { trace.emplace_back(line); });
    });
    REQUIRE(run.runUntilPlay());
    Riot riot{.pad = pad, .run = run, .trace = trace};
    riot.runTo(kSettleAt);
    coney::platform::PlayLevelMode* play = run.session().play();
    REQUIRE(play != nullptr);
    REQUIRE(play->cars() != nullptr);
    // P1.HeadToGate is a Lua call from F.CheckObjectives, so the trace (which logs the engine's calls into the
    // scripts and the scripts' calls to bindings) shows it by the SetCheckPoint(4) it makes.
    const auto gateReached = [&riot] { return riot.count("SetCheckPoint(4)") > 0; };
    REQUIRE_FALSE(gateReached());

    // The stereos first, before the riot meter's blows can wreck their cars: beside each of the three cars
    // CarSpawnRadio gave one, at the places the checkpoint 2 radio test steals them from.
    struct Spot {
        float x, y, heading;
    };
    for (const Spot spot :
         {Spot{-82.4F, -156.8F, 90.0F}, Spot{-114.5F, -180.8F, 180.0F}, Spot{-59.76F, -252.3F, 264.0F}}) {
        riot.place(spot.x, spot.y, spot.heading);
        riot.play(stereoTheft(), 600);
    }
    const std::size_t stereos = riot.count("> F.InventoryPickup(11)");

    // The loot: each store item (a TYPE_SPECIAL record still there) in turn, from whichever side and distance lets
    // triangle take it, until the money taken reaches the target.
    const int startMoney = riot.money();
    const coney::world_objects::ObjectTypes& types = run.session().flow().objectTypes();
    std::vector<coney::world_objects::SpawnRecord> loot;
    for (const coney::world_objects::SpawnRecord& record : run.session().flow().spawnRecords().all()) {
        const coney::world_objects::ObjectType* type = types.find(record.typeName);
        if (!record.removed && !record.hidden && type != nullptr &&
            type->objectKind == coney::world_objects::kObjectKindSpecial && type->value > 0) {
            loot.push_back(record);
        }
    }
    std::size_t taken = 0;
    for (const coney::world_objects::SpawnRecord& item : loot) {
        if (riot.money() - startMoney >= kLootTarget) {
            break;
        }
        bool got = false;
        for (const float distance : {0.8F, 0.5F, 1.1F}) {
            for (const float degrees : {0.0F, 90.0F, 180.0F, 270.0F}) {
                const int before = riot.money();
                riot.faceFrom(item.position[0], item.position[1], degrees, distance);
                riot.play("3 tap triangle\n", 60);
                if (riot.money() > before) {
                    ++taken;
                    got = true;
                    break;
                }
            }
            if (got) {
                break;
            }
        }
    }
    const int looted = riot.money() - startMoney;

    // The riot meter: square at every car from each side, then at the street props inside vLevelVandal (passes over
    // them again while the meter is short), until the scripts move on to checkpoint 4.
    std::size_t carsHit = 0;
    for (const coney::world_objects::Car& car : play->cars()->all()) {
        if (gateReached()) {
            break;
        }
        ++carsHit;
        for (const float degrees : {0.0F, 90.0F, 180.0F, 270.0F}) {
            riot.faceFrom(car.position.x, car.position.y, degrees, 1.6F);
            riot.play(squares(6), 200);
        }
    }
    constexpr int kPropPasses = 3;
    std::size_t propsHit = 0;
    for (int pass = 0; pass < kPropPasses && !gateReached(); ++pass) {
        for (const coney::world_objects::SpawnRecord& record : run.session().flow().spawnRecords().all()) {
            if (gateReached()) {
                break;
            }
            const coney::world_objects::ObjectType* type = types.find(record.typeName);
            if (record.removed || record.hidden || type == nullptr ||
                type->objectKind == coney::world_objects::kObjectKindSpecial || record.position[2] < -1.0F ||
                record.position[0] < -142.9F || record.position[0] > -4.0F || record.position[1] < -300.1F ||
                record.position[1] > -20.2F) {
                continue;
            }
            ++propsHit;
            riot.faceFrom(record.position[0], record.position[1], 0.0F, 1.0F);
            riot.play(squares(2), 70);
        }
    }
    riot.play("0 stick left 0 0\n", 120);
    std::printf("  level34 checkpoint 3: %zu stereos, $%d of loot from %zu items, %zu cars and %zu props hit, %zu car "
                "messages 25, %zu vandal messages, SetCheckPoint(4) %zu\n",
                stereos, looted, taken, carsHit, propsHit, riot.count("> F.CarDamage"), riot.count("> F.Vandalize"),
                riot.count("SetCheckPoint(4)"));
    CHECK(scripts.errors() == 0);
    CHECK(stereos == 3);
    CHECK(looted >= kLootTarget);
    // Each objective saves its target as its Lua float when done (F.SaveData): radios 3, loot 350, riot 200.
    CHECK(riot.count("SetLUASaveDataFloat(3, 3)") == 1);
    CHECK(riot.count("SetLUASaveDataFloat(4, 350)") == 1);
    CHECK(riot.count("SetLUASaveDataFloat(2, 200)") == 1);
    CHECK(riot.count("SetCheckPoint(4)") == 1);
}
